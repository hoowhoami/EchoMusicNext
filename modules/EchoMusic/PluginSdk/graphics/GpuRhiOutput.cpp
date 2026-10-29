#include "GpuRhi_p.h"
#include <QFile>
#include <rhi/qshader.h>
namespace EchoMusic
{
namespace
{
QShader baked(const QString &name)
{
    QFile f(":/echo/graphics/graphics/shaders/" + name + ".qsb");
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}
} // namespace
bool GpuRenderState::prepareOutput(QRhiRenderTarget *output)
{
    const auto size = output->pixelSize();
    const auto format = output->renderPassDescriptor()->serializedFormat();
    bool needsDepth = false;
    for (const auto &value : snapshot.frame.value("passes").toList()) {
        const auto pass = value.toMap();
        if (pass.value("type") != "render" || pass.value("target", "surface") != "surface")
            continue;
        const auto pipeline =
            snapshot.resources.value(pass.value("pipeline").toString()).definition;
        needsDepth |= pipeline.value("depthTest").toBool() || pipeline.value("depthWrite").toBool();
    }
    if (workingTexture && workingTexture->pixelSize() == size && outputPipeline &&
        outputFormat == format && workingWithDepth == needsDepth)
        return true;
    passes.clear();
    outputPipeline.reset();
    outputDescriptor.reset();
    outputBindings.reset();
    outputSampler.reset();
    workingTarget.reset();
    workingDescriptor.reset();
    workingDepth.reset();
    workingMultisample.reset();
    workingTexture.reset();
    frameRevision = 0;
    const auto workFormat =
        rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget)
            ? QRhiTexture::RGBA16F
            : QRhiTexture::RGBA8;
    workingTexture.reset(rhi->newTexture(
        workFormat, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    workingWithDepth = needsDepth;
    if (needsDepth)
        workingDepth.reset(rhi->newTexture(QRhiTexture::D32F, size, output->sampleCount(),
                                           QRhiTexture::RenderTarget));
    if (!workingTexture->create() || (workingDepth && !workingDepth->create())) {
        error = "Cannot allocate linear render target";
        return false;
    }
    QRhiColorAttachment color(workingTexture.get());
    if (output->sampleCount() > 1) {
        workingMultisample.reset(
            rhi->newTexture(workFormat, size, output->sampleCount(), QRhiTexture::RenderTarget));
        if (!workingMultisample->create()) {
            error = "Cannot allocate multisample working buffer";
            return false;
        }
        color = QRhiColorAttachment(workingMultisample.get());
        color.setResolveTexture(workingTexture.get());
    }
    QRhiTextureRenderTargetDescription desc(color);
    desc.setDepthTexture(workingDepth.get());
    workingTarget.reset(rhi->newTextureRenderTarget(desc));
    workingDescriptor.reset(workingTarget->newCompatibleRenderPassDescriptor());
    workingTarget->setRenderPassDescriptor(workingDescriptor.get());
    if (!workingTarget->create()) {
        error = "Cannot create linear render target";
        return false;
    }
    workingNeedsClear = true;
    outputSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!outputSampler->create()) {
        error = "Cannot create output sampler";
        return false;
    }
    outputBindings.reset(rhi->newShaderResourceBindings());
    outputBindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
             frameUniform.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                   workingTexture.get(), outputSampler.get())});
    if (!outputBindings->create()) {
        error = "Cannot create output bindings";
        return false;
    }
    outputPipeline.reset(rhi->newGraphicsPipeline());
    outputPipeline->setShaderStages({{QRhiShaderStage::Vertex, baked("composite.vert")},
                                     {QRhiShaderStage::Fragment, baked("composite.frag")}});
    outputDescriptor.reset(output->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
    outputPipeline->setShaderResourceBindings(outputBindings.get());
    outputPipeline->setRenderPassDescriptor(outputDescriptor.get());
    outputPipeline->setSampleCount(output->sampleCount());
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    outputPipeline->setTargetBlends({blend});
    if (!outputPipeline->create()) {
        error = "Cannot create HDR/SDR output pipeline";
        return false;
    }
    return true;
}
void GpuRenderState::composite(QRhiCommandBuffer *cb, QRhiRenderTarget *output)
{
    cb->beginPass(output, Qt::transparent, {1., 0});
    cb->setGraphicsPipeline(outputPipeline.get());
    cb->setShaderResources(outputBindings.get());
    cb->setViewport(
        {0, 0, float(output->pixelSize().width()), float(output->pixelSize().height())});
    cb->draw(3);
    cb->endPass();
}
} // namespace EchoMusic
