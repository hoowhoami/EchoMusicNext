#include "GpuRhi_p.h"
#include <rhi/qshader.h>
#include <rhi/qshaderdescription.h>
#include <QSet>
namespace EchoMusic
{
namespace
{
bool reflectionValid(const QShader &shader, const QVariantList &bindings,
                     const std::map<QString, NativeGpuResource> &resources, QString &error)
{
    const auto find = [&](int binding) {
        for (const auto &value : bindings)
            if (value.toMap().value("binding").toInt() == binding)
                return value.toMap();
        return QVariantMap{};
    };
    const auto d = shader.description();
    if (!d.pushConstantBlocks().isEmpty() || !d.separateImages().isEmpty() ||
        !d.separateSamplers().isEmpty()) {
        error = "Use uniform blocks and combined texture samplers in portable QSB shaders";
        return false;
    }
    for (const auto &block : d.uniformBlocks()) {
        const auto b = find(block.binding);
        const auto id = b.value("resource").toString();
        const auto r = resources.find(id);
        if (block.descriptorSet > 0 || b.value("kind").toString() != "uniform" ||
            (id == "frame" ? block.size > 96
                           : r == resources.end() || !r->second.buffer ||
                                 !r->second.buffer->usage().testFlag(QRhiBuffer::UniformBuffer) ||
                                 quint64(r->second.buffer->size()) < quint64(block.size))) {
            error = "Shader uniform block is missing, too small or incompatible";
            return false;
        }
    }
    for (const auto &block : d.storageBlocks()) {
        const auto b = find(block.binding);
        const auto r = resources.find(b.value("resource").toString());
        if (block.descriptorSet > 0 || b.value("kind").toString() != "storage-buffer" ||
            r == resources.end() || !r->second.buffer ||
            !r->second.buffer->usage().testFlag(QRhiBuffer::StorageBuffer) ||
            quint64(r->second.buffer->size()) < quint64(block.knownSize)) {
            error = "Shader storage block is missing or incompatible";
            return false;
        }
    }
    for (const auto &image : d.combinedImageSamplers())
        if (image.descriptorSet > 0 || image.type != QShaderDescription::Sampler2D ||
            !image.arrayDims.isEmpty() ||
            find(image.binding).value("kind").toString() != "texture") {
            error = "Shader sampler binding is missing or incompatible";
            return false;
        }
    for (const auto &image : d.storageImages()) {
        const auto binding = find(image.binding);
        const auto resource = resources.find(binding.value("resource").toString());
        const auto format =
            image.imageFormat == QShaderDescription::ImageFormatRgba16f   ? QRhiTexture::RGBA16F
            : image.imageFormat == QShaderDescription::ImageFormatRgba32f ? QRhiTexture::RGBA32F
            : image.imageFormat == QShaderDescription::ImageFormatR32f    ? QRhiTexture::R32F
            : image.imageFormat == QShaderDescription::ImageFormatRgba8
                ? QRhiTexture::RGBA8
                : QRhiTexture::UnknownFormat;
        if (image.descriptorSet > 0 || image.type != QShaderDescription::Image2D ||
            !image.arrayDims.isEmpty() || binding.value("kind").toString() != "storage-image" ||
            resource == resources.end() || !resource->second.texture ||
            format == QRhiTexture::UnknownFormat || resource->second.texture->format() != format ||
            (!image.imageFlags.testFlag(QShaderDescription::ReadOnlyImage) &&
             binding.value("access", "read") == "read")) {
            error = "Shader image binding is missing or incompatible";
            return false;
        }
    }
    return true;
}
} // namespace
bool GpuRenderState::buildPasses(const QVariantMap &frame, QRhiRenderTarget *output)
{
    ++pipelineBuilds;
    passes.clear();
    for (const auto &value : frame.value("passes").toList()) {
        NativeGpuPass pass;
        pass.definition = value.toMap();
        const auto &p = pass.definition;
        const auto pipeline = resources.find(p.value("pipeline").toString());
        if (pipeline == resources.end()) {
            error = "Pipeline has retired";
            return false;
        }
        const auto &d = pipeline->second.definition;
        const bool compute = p.value("type", "render") == "compute";
        if (compute && !rhi->isFeatureSupported(QRhi::Compute)) {
            error = "Compute shaders are unavailable on this backend";
            return false;
        }
        const auto bindings = p.value("bindings").toList();
        const auto vertex =
            compute ? QShader() : QShader::fromSerialized(d.value("vertexBytes").toByteArray());
        const auto fragment =
            compute ? QShader() : QShader::fromSerialized(d.value("fragmentBytes").toByteArray());
        const auto computeShader =
            compute ? QShader::fromSerialized(d.value("computeBytes").toByteArray()) : QShader();
        if (compute) {
            if (!reflectionValid(computeShader, bindings, resources, error))
                return false;
            const auto local = computeShader.description().computeShaderLocalSize();
            const auto totalLocal = qint64(local[0]) * local[1] * local[2];
            if (local[0] < 1 || local[1] < 1 || local[2] < 1 ||
                totalLocal > rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup)) {
                error = "Compute local size exceeds device limits";
                return false;
            }
            const auto groups = p.value("groups").toList();
            if (totalLocal * groups[0].toLongLong() * groups[1].toInt() * groups[2].toInt() >
                64000000) {
                error = "Compute invocation budget exceeded";
                return false;
            }
            for (int i = 0; i < 3; ++i)
                if (groups[i].toInt() > rhi->resourceLimit(QRhi::MaxThreadGroupsPerDimension)) {
                    error = "Compute dispatch exceeds device limits";
                    return false;
                }
        } else if (!reflectionValid(vertex, bindings, resources, error) ||
                   !reflectionValid(fragment, bindings, resources, error))
            return false;
        QVector<QRhiShaderResourceBinding> nativeBindings;
        const auto stages =
            compute
                ? QRhiShaderResourceBinding::StageFlags(QRhiShaderResourceBinding::ComputeStage)
                : QRhiShaderResourceBinding::StageFlags(QRhiShaderResourceBinding::VertexStage |
                                                        QRhiShaderResourceBinding::FragmentStage);
        QSet<QString> writable, readable;
        for (const auto &item : bindings) {
            const auto b = item.toMap();
            const int binding = b.value("binding").toInt();
            const auto id = b.value("resource").toString();
            const auto kind = b.value("kind").toString();
            if (id == "frame" && kind == "uniform") {
                nativeBindings.append(
                    QRhiShaderResourceBinding::uniformBuffer(binding, stages, frameUniform.get()));
                continue;
            }
            const auto found = resources.find(id);
            if (found == resources.end()) {
                error = "Binding resource has retired";
                return false;
            }
            const auto &r = found->second;
            const auto access = b.value("access", "read").toString();
            if (kind == "uniform" && r.buffer &&
                r.buffer->usage().testFlag(QRhiBuffer::UniformBuffer)) {
                nativeBindings.append(
                    QRhiShaderResourceBinding::uniformBuffer(binding, stages, r.buffer.get()));
                readable.insert(id);
            } else if (kind == "texture" && r.texture &&
                       r.definition.value("usage").toStringList().contains("sampled")) {
                const auto sampler = resources.find(b.value("sampler").toString());
                if (sampler == resources.end() || !sampler->second.sampler) {
                    error = "Sampled texture requires a live sampler";
                    return false;
                }
                nativeBindings.append(QRhiShaderResourceBinding::sampledTexture(
                    binding, stages, r.texture.get(), sampler->second.sampler.get()));
                readable.insert(id);
            } else if (kind == "storage-buffer" && r.buffer &&
                       r.buffer->usage().testFlag(QRhiBuffer::StorageBuffer)) {
                if (access == "read")
                    nativeBindings.append(
                        QRhiShaderResourceBinding::bufferLoad(binding, stages, r.buffer.get()));
                else if (access == "write")
                    nativeBindings.append(
                        QRhiShaderResourceBinding::bufferStore(binding, stages, r.buffer.get()));
                else if (access == "read-write")
                    nativeBindings.append(QRhiShaderResourceBinding::bufferLoadStore(
                        binding, stages, r.buffer.get()));
                else {
                    error = "Invalid storage buffer access";
                    return false;
                }
                if (access == "read")
                    readable.insert(id);
                else
                    writable.insert(id);
            } else if (kind == "storage-image" && r.texture &&
                       r.texture->flags().testFlag(QRhiTexture::UsedWithLoadStore)) {
                if (access == "read")
                    nativeBindings.append(
                        QRhiShaderResourceBinding::imageLoad(binding, stages, r.texture.get(), 0));
                else if (access == "write")
                    nativeBindings.append(
                        QRhiShaderResourceBinding::imageStore(binding, stages, r.texture.get(), 0));
                else if (access == "read-write")
                    nativeBindings.append(QRhiShaderResourceBinding::imageLoadStore(
                        binding, stages, r.texture.get(), 0));
                else {
                    error = "Invalid storage image access";
                    return false;
                }
                if (access == "read")
                    readable.insert(id);
                else
                    writable.insert(id);
            } else {
                error = "Binding kind is incompatible with resource usage";
                return false;
            }
        }
        for (const auto &id : writable)
            if (readable.contains(id)) {
                error = "Conflicting resource usages in one pass";
                return false;
            }
        pass.bindings.reset(rhi->newShaderResourceBindings());
        pass.bindings->setBindings(nativeBindings.cbegin(), nativeBindings.cend());
        if (!pass.bindings->create()) {
            error = "Cannot create shader resource bindings";
            return false;
        }
        if (compute) {
            pass.compute.reset(rhi->newComputePipeline());
            pass.compute->setShaderStage({QRhiShaderStage::Compute, computeShader});
            pass.compute->setShaderResourceBindings(pass.bindings.get());
            if (!pass.compute->create()) {
                error = "Compute pipeline compilation failed";
                return false;
            }
        } else {
            auto *target = output;
            const auto id = p.value("target", "surface").toString();
            const bool load = p.value("loadOp", "clear") == "load";
            if (id != "surface" || load) {
                QRhiTexture *texture = workingTexture.get();
                QRhiTexture *depth = workingDepth.get();
                bool newDepth = false;
                if (id != "surface") {
                    const auto found = resources.find(id);
                    if (found == resources.end() || !found->second.texture ||
                        readable.contains(id) || writable.contains(id)) {
                        error = "Render target is missing or used as shader input in the same pass";
                        return false;
                    }
                    texture = found->second.texture.get();
                    depth = nullptr;
                    if (d.value("depthTest").toBool() || d.value("depthWrite").toBool()) {
                        auto &attachment = found->second.depth;
                        if (!attachment) {
                            attachment.reset(rhi->newTexture(QRhiTexture::D32F,
                                                             texture->pixelSize(), 1,
                                                             QRhiTexture::RenderTarget));
                            if (!attachment->create()) {
                                error = "Cannot create offscreen depth attachment";
                                return false;
                            }
                            newDepth = true;
                        }
                        depth = attachment.get();
                    }
                }
                QRhiColorAttachment color(texture);
                if (id == "surface" && workingMultisample) {
                    color = QRhiColorAttachment(workingMultisample.get());
                    color.setResolveTexture(workingTexture.get());
                }
                QRhiTextureRenderTargetDescription description{color};
                description.setDepthTexture(depth);
                QRhiTextureRenderTarget::Flags flags;
                if (load) {
                    flags |= QRhiTextureRenderTarget::PreserveColorContents;
                    if (depth && !newDepth)
                        flags |= QRhiTextureRenderTarget::PreserveDepthStencilContents;
                }
                pass.target.reset(rhi->newTextureRenderTarget(description, flags));
                pass.descriptor.reset(pass.target->newCompatibleRenderPassDescriptor());
                pass.target->setRenderPassDescriptor(pass.descriptor.get());
                if (!pass.target->create()) {
                    error = "Cannot create render target";
                    return false;
                }
                target = pass.target.get();
            }
            pass.graphics.reset(rhi->newGraphicsPipeline());
            auto *ps = pass.graphics.get();
            ps->setShaderStages(
                {{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
            const auto topology = d.value("topology", "triangles").toString();
            ps->setTopology(topology == "lines"            ? QRhiGraphicsPipeline::Lines
                            : topology == "line-strip"     ? QRhiGraphicsPipeline::LineStrip
                            : topology == "triangle-strip" ? QRhiGraphicsPipeline::TriangleStrip
                            : topology == "points"         ? QRhiGraphicsPipeline::Points
                                                           : QRhiGraphicsPipeline::Triangles);
            QRhiGraphicsPipeline::TargetBlend blend;
            const auto blending = d.value("blend", "alpha").toString();
            blend.enable = blending != "none";
            if (blending == "add")
                blend.dstColor = blend.dstAlpha = QRhiGraphicsPipeline::One;
            ps->setTargetBlends({blend});
            ps->setSampleCount(target->sampleCount());
            QRhiVertexInputLayout layout;
            const auto vertexLayout = d.value("vertexLayout").toMap();
            if (!vertexLayout.isEmpty()) {
                const int stride = vertexLayout.value("stride").toInt();
                if (stride < 4 || stride > 2048) {
                    error = "Invalid vertex stride";
                    return false;
                }
                layout.setBindings({QRhiVertexInputBinding(quint32(stride))});
                QVector<QRhiVertexInputAttribute> attributes;
                const auto definitions = vertexLayout.value("attributes").toList();
                if (definitions.isEmpty() || definitions.size() > 16) {
                    error = "Invalid vertex attributes";
                    return false;
                }
                QSet<int> locations;
                for (const auto &v : definitions) {
                    const auto a = v.toMap();
                    const int location = a.value("location").toInt(),
                              offset = a.value("offset").toInt();
                    const auto format = a.value("format").toString();
                    const int components = format == "float"    ? 1
                                           : format == "float2" ? 2
                                           : format == "float3" ? 3
                                           : format == "float4" ? 4
                                                                : 0;
                    if (!components || location < 0 || location > 15 ||
                        locations.contains(location) || offset < 0 || offset % 4 ||
                        offset + components * 4 > stride) {
                        error = "Invalid vertex attribute format or range";
                        return false;
                    }
                    locations.insert(location);
                    attributes.append({0, location,
                                       components == 1   ? QRhiVertexInputAttribute::Float
                                       : components == 2 ? QRhiVertexInputAttribute::Float2
                                       : components == 3 ? QRhiVertexInputAttribute::Float3
                                                         : QRhiVertexInputAttribute::Float4,
                                       quint32(offset)});
                }
                layout.setAttributes(attributes.cbegin(), attributes.cend());
            } else if (!vertex.description().inputVariables().isEmpty()) {
                error = "Shader requires a vertex input layout";
                return false;
            }
            ps->setVertexInputLayout(layout);
            ps->setCullMode(d.value("cull") == "back"    ? QRhiGraphicsPipeline::Back
                            : d.value("cull") == "front" ? QRhiGraphicsPipeline::Front
                                                         : QRhiGraphicsPipeline::None);
            ps->setDepthTest(d.value("depthTest").toBool() || d.value("depthWrite").toBool());
            ps->setDepthOp(d.value("depthTest").toBool() ? QRhiGraphicsPipeline::Less
                                                         : QRhiGraphicsPipeline::Always);
            ps->setDepthWrite(d.value("depthWrite", d.value("depthTest", false)).toBool());
            ps->setShaderResourceBindings(pass.bindings.get());
            ps->setRenderPassDescriptor(target->renderPassDescriptor());
            if (!ps->create()) {
                error = "Graphics pipeline compilation failed";
                return false;
            }
        }
        passes.push_back(std::move(pass));
    }
    objects = int(resources.size()) + int(passes.size()) + bool(frameUniform);
    return true;
}
} // namespace EchoMusic
