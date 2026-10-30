#include "GpuRhi_p.h"
#include "GpuVertexLayout_p.h"
#include <rhi/qshader.h>
#include <rhi/qshaderdescription.h>
#include <QSet>
namespace EchoMusic
{
namespace
{
QShaderDescription::VariableType textureType(const NativeGpuResource &resource, bool storage)
{
    const auto dimension = resource.definition.value("dimension").toString();
    if (dimension == "2d-array")
        return storage ? QShaderDescription::Image2DArray : QShaderDescription::Sampler2DArray;
    if (dimension == "cube")
        return storage ? QShaderDescription::ImageCube : QShaderDescription::SamplerCube;
    if (dimension == "3d")
        return storage ? QShaderDescription::Image3D : QShaderDescription::Sampler3D;
    return storage ? QShaderDescription::Image2D : QShaderDescription::Sampler2D;
}
bool reflectionValid(const QShader &shader, const QVariantList &bindings,
                     const std::map<QString, std::shared_ptr<NativeGpuResource>> &resources,
                     QString &error)
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
                           : r == resources.end() || !r->second->buffer ||
                                 !r->second->buffer->usage().testFlag(QRhiBuffer::UniformBuffer) ||
                                 quint64(b.value("size", r->second->buffer->size() -
                                                             b.value("offset", 0).toInt())
                                             .toUInt()) < quint64(block.size))) {
            error = "Shader uniform block is missing, too small or incompatible";
            return false;
        }
    }
    for (const auto &block : d.storageBlocks()) {
        const auto b = find(block.binding);
        const auto r = resources.find(b.value("resource").toString());
        if (block.descriptorSet > 0 || b.value("kind").toString() != "storage-buffer" ||
            r == resources.end() || !r->second->buffer ||
            !r->second->buffer->usage().testFlag(QRhiBuffer::StorageBuffer) ||
            quint64(r->second->buffer->size()) < quint64(block.knownSize)) {
            error = "Shader storage block is missing or incompatible";
            return false;
        }
    }
    for (const auto &image : d.combinedImageSamplers()) {
        const auto binding = find(image.binding);
        const auto resource = resources.find(binding.value("resource").toString());
        if (image.descriptorSet > 0 ||
            image.type != (resource == resources.end() ? QShaderDescription::Unknown
                                                       : textureType(*resource->second, false)) ||
            !image.arrayDims.isEmpty() || binding.value("kind").toString() != "texture" ||
            resource == resources.end() || !resource->second->texture) {
            error = "Shader sampler dimension or binding is incompatible";
            return false;
        }
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
        if (image.descriptorSet > 0 ||
            image.type != (resource == resources.end() ? QShaderDescription::Unknown
                                                       : textureType(*resource->second, true)) ||
            !image.arrayDims.isEmpty() || binding.value("kind").toString() != "storage-image" ||
            resource == resources.end() || !resource->second->texture ||
            format == QRhiTexture::UnknownFormat || resource->second->texture->format() != format ||
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
    const auto incoming = frame.value("passes").toList();
    const auto topology = [](QVariantMap definition) {
        bool indirect = false;
        for (const auto &value : definition.value("draws").toList())
            indirect |= value.toMap().contains("indirect");
        definition["usesIndirect"] = indirect;
        for (const auto &key : {"draws", "groups", "clear", "viewport", "scissor"})
            definition.remove(key);
        return definition;
    };
    bool reuse = passes.size() == size_t(incoming.size());
    for (int i = 0; reuse && i < incoming.size(); ++i)
        reuse = topology(passes[size_t(i)].definition) == topology(incoming[i].toMap());
    if (reuse) {
        for (int i = 0; i < incoming.size(); ++i)
            passes[size_t(i)].definition = incoming[i].toMap();
        return true;
    }
    ++pipelineBuilds;
    passes.clear();
    for (const auto &value : frame.value("passes").toList()) {
        NativeGpuPass pass;
        pass.definition = value.toMap();
        const auto &p = pass.definition;
        const auto command = p.value("type", "render").toString();
        if (command != "render" && command != "compute") {
            if (!buildTransfer(pass))
                return false;
            passes.push_back(std::move(pass));
            continue;
        }
        const auto pipeline = resources.find(p.value("pipeline").toString());
        if (pipeline == resources.end()) {
            error = "Pipeline has retired";
            return false;
        }
        const auto &d = pipeline->second->definition;
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
            const auto &r = *found->second;
            const auto access = b.value("access", "read").toString();
            if (kind == "uniform" && r.buffer &&
                r.buffer->usage().testFlag(QRhiBuffer::UniformBuffer)) {
                const auto offset = b.value("offset", 0).toUInt();
                const auto size = b.value("size", r.buffer->size() - int(offset)).toUInt();
                if (size > quint32(rhi->resourceLimit(QRhi::MaxUniformBufferRange)) ||
                    offset % rhi->ubufAlignment() ||
                    quint64(offset) + size > quint64(r.buffer->size())) {
                    error = "Uniform offset is not device aligned or exceeds buffer";
                    return false;
                }
                nativeBindings.append(QRhiShaderResourceBinding::uniformBuffer(
                    binding, stages, r.buffer.get(), offset, size));
                readable.insert(r.identity);
            } else if (kind == "texture" && r.texture &&
                       r.definition.value("usage").toStringList().contains("sampled")) {
                const auto sampler = resources.find(b.value("sampler").toString());
                if (sampler == resources.end() || !sampler->second->sampler) {
                    error = "Sampled texture requires a live sampler";
                    return false;
                }
                nativeBindings.append(QRhiShaderResourceBinding::sampledTexture(
                    binding, stages, r.texture.get(), sampler->second->sampler.get()));
                readable.insert(r.identity);
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
                    readable.insert(r.identity);
                else
                    writable.insert(r.identity);
            } else if (kind == "storage-image" && r.texture &&
                       r.texture->flags().testFlag(QRhiTexture::UsedWithLoadStore)) {
                if (access == "read")
                    nativeBindings.append(QRhiShaderResourceBinding::imageLoad(
                        binding, stages, r.texture.get(), b.value("level", 0).toInt()));
                else if (access == "write")
                    nativeBindings.append(QRhiShaderResourceBinding::imageStore(
                        binding, stages, r.texture.get(), b.value("level", 0).toInt()));
                else if (access == "read-write")
                    nativeBindings.append(QRhiShaderResourceBinding::imageLoadStore(
                        binding, stages, r.texture.get(), b.value("level", 0).toInt()));
                else {
                    error = "Invalid storage image access";
                    return false;
                }
                if (access == "read")
                    readable.insert(r.identity);
                else
                    writable.insert(r.identity);
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
            const auto views = gpuColorTargets(p);
            const int attachmentCount = qMax(1, int(views.size()));
            if (attachmentCount > rhi->resourceLimit(QRhi::MaxColorAttachments)) {
                error = "Color attachment count exceeds device limits";
                return false;
            }
            for (const auto &variable : fragment.description().outputVariables())
                if (variable.location < 0 || variable.location >= attachmentCount ||
                    !variable.arrayDims.isEmpty()) {
                    error = "Fragment output location is outside the color target layout";
                    return false;
                }
            const bool load = p.value("loadOp", "clear") == "load";
            if (!views.isEmpty() || load) {
                QVector<QRhiColorAttachment> colors;
                QRhiTexture *depth = workingDepth.get();
                bool newDepth = false;
                if (views.isEmpty()) {
                    QRhiColorAttachment color(workingMultisample ? workingMultisample.get()
                                                                 : workingTexture.get());
                    if (workingMultisample)
                        color.setResolveTexture(workingTexture.get());
                    colors.append(color);
                } else {
                    depth = nullptr;
                    QSize extent;
                    QSet<NativeGpuResource *> attachments;
                    for (const auto &value : views) {
                        const auto view = value.toMap();
                        const auto id = view.value("texture").toString();
                        const auto found = resources.find(id);
                        if (found == resources.end() || !found->second->texture ||
                            readable.contains(found->second->identity) ||
                            writable.contains(found->second->identity)) {
                            error =
                                "Color target is missing or aliases a shader input in this pass";
                            return false;
                        }
                        if (attachments.contains(found->second.get())) {
                            error = "Color targets alias the same shared allocation";
                            return false;
                        }
                        attachments.insert(found->second.get());
                        auto *texture = found->second->texture.get();
                        const int level = view.value("level", 0).toInt();
                        if (texture->flags().testFlag(QRhiTexture::ThreeDimensional) &&
                            !rhi->isFeatureSupported(QRhi::RenderTo3DTextureSlice)) {
                            error = "Rendering to 3D texture slices is unsupported by this device";
                            return false;
                        }
                        if (level && !rhi->isFeatureSupported(QRhi::RenderToNonBaseMipLevel)) {
                            error = "Rendering to a non-base mip is unsupported by this device";
                            return false;
                        }
                        const QSize size(qMax(1, texture->pixelSize().width() >> level),
                                         qMax(1, texture->pixelSize().height() >> level));
                        if (!extent.isEmpty() && size != extent) {
                            error = "Color target sizes differ";
                            return false;
                        }
                        extent = size;
                        QRhiColorAttachment color(texture);
                        color.setLevel(level);
                        color.setLayer(view.value("layer", 0).toInt());
                        colors.append(color);
                    }
                    if (d.value("depthTest").toBool() || d.value("depthWrite").toBool()) {
                        const auto first = views.first().toMap();
                        auto &attachment = resources.at(first.value("texture").toString())
                                               ->depths[gpuDepthKey(first)];
                        if (!attachment) {
                            if (!resources.at(first.value("texture").toString())
                                     ->reserve(qint64(extent.width()) * extent.height() * 4)) {
                                error = "Shared depth allocation exceeds GPU budget";
                                return false;
                            }
                            attachment.reset(rhi->newTexture(QRhiTexture::D32F, extent, 1,
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
                QRhiTextureRenderTargetDescription description;
                description.setColorAttachments(colors.cbegin(), colors.cend());
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
            bool indirect = false;
            for (const auto &value : p.value("draws").toList()) {
                const auto draw = value.toMap();
                indirect |= draw.contains("indirect");
                for (const auto &id :
                     {draw.value("vertexBuffer").toString(), draw.value("indexBuffer").toString(),
                      draw.value("indirect").toMap().value("buffer").toString()})
                    if (!id.isEmpty() && resources.contains(id) &&
                        writable.contains(resources.at(id)->identity)) {
                        error = "Draw buffers cannot be shader-writable in the same render pass";
                        return false;
                    }
            }
            if (indirect && !rhi->isFeatureSupported(QRhi::DrawIndirect)) {
                error = "Indirect draws are unsupported by this device";
                return false;
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
            auto blending = d.value("blends").toList();
            if (blending.isEmpty())
                for (int i = 0; i < attachmentCount; ++i)
                    blending.append(d.value("blend", "alpha"));
            if (blending.size() != attachmentCount) {
                error = "Pipeline blend count differs from color target count";
                return false;
            }
            QSet<QString> modes;
            for (const auto &value : blending)
                modes.insert(value.toString());
            if (modes.size() > 1 && !rhi->isFeatureSupported(QRhi::PerRenderTargetBlending)) {
                error = "Independent color attachment blending is unsupported by this device";
                return false;
            }
            QVector<QRhiGraphicsPipeline::TargetBlend> blends;
            for (const auto &value : blending) {
                QRhiGraphicsPipeline::TargetBlend blend;
                const auto mode = value.toString();
                blend.enable = mode != "none";
                if (mode == "add")
                    blend.dstColor = blend.dstAlpha = QRhiGraphicsPipeline::One;
                blends.append(blend);
            }
            ps->setTargetBlends(blends.cbegin(), blends.cend());
            ps->setSampleCount(target->sampleCount());
            QRhiVertexInputLayout layout;
            if (!vertexLayout(d.value("vertexLayout").toMap(), vertex, layout))
                return false;
            ps->setVertexInputLayout(layout);
            auto flags = QRhiGraphicsPipeline::Flags(QRhiGraphicsPipeline::UsesScissor);
            if (indirect)
                flags |= QRhiGraphicsPipeline::UsesIndirectDraws;
            ps->setFlags(flags);
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
