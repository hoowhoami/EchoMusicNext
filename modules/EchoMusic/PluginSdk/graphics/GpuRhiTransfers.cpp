#include "GpuRhi_p.h"
#include <QFile>
#include <rhi/qshader.h>
namespace EchoMusic
{
bool GpuRenderState::buildTransfer(NativeGpuPass &pass)
{
    const auto type = pass.definition.value("type").toString();
    if (type != "copy-buffer" && type != "upload-buffer")
        return true;
    const auto dst = resources.find(pass.definition.value("destination").toString());
    if (dst == resources.end() || !dst->second.buffer) {
        error = "Transfer buffer retired";
        return false;
    }
    if (type == "upload-buffer") {
        if (dst->second.buffer->type() == QRhiBuffer::Dynamic ||
            !rhi->isFeatureSupported(QRhi::Compute))
            return true;
        const auto data = pass.definition.value("data").toByteArray();
        pass.stagingBuffer.reset(
            rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, int(data.size())));
        if (!pass.stagingBuffer->create()) {
            error = "Cannot allocate GPU upload staging";
            return false;
        }
        pass.definition["sourceOffset"] = 0;
        pass.definition["destinationOffset"] = pass.definition.value("offset", 0);
        pass.definition["size"] = data.size();
        return buildBufferCopy(pass, pass.stagingBuffer.get(), dst->second.buffer.get());
    }
    const auto src = resources.find(pass.definition.value("source").toString());
    if (src == resources.end() || !src->second.buffer) {
        error = "Copy source retired";
        return false;
    }
    return buildBufferCopy(pass, src->second.buffer.get(), dst->second.buffer.get());
}
bool GpuRenderState::buildBufferCopy(NativeGpuPass &pass, QRhiBuffer *source,
                                     QRhiBuffer *destination)
{
    if (!rhi->isFeatureSupported(QRhi::Compute)) {
        error = "GPU buffer copy requires compute support";
        return false;
    }
    pass.transferUniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    if (!pass.transferUniform->create()) {
        error = "Cannot create copy uniforms";
        return false;
    }
    const auto stage = QRhiShaderResourceBinding::ComputeStage;
    pass.bindings.reset(rhi->newShaderResourceBindings());
    pass.bindings->setBindings(
        {QRhiShaderResourceBinding::bufferLoad(0, stage, source),
         QRhiShaderResourceBinding::bufferStore(1, stage, destination),
         QRhiShaderResourceBinding::uniformBuffer(2, stage, pass.transferUniform.get())});
    if (!pass.bindings->create()) {
        error = "Cannot bind copy resources";
        return false;
    }
    if (!copyPipeline) {
        QFile file(":/echo/graphics/graphics/shaders/copy-buffer.comp.qsb");
        const auto shader =
            file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
        if (!shader.isValid()) {
            error = "GPU copy shader unavailable";
            return false;
        }
        copySource.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, 4));
        copyDestination.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, 4));
        copyUniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
        if (!copySource->create() || !copyDestination->create() || !copyUniform->create()) {
            error = "Cannot create copy layout buffers";
            return false;
        }
        copyLayout.reset(rhi->newShaderResourceBindings());
        copyLayout->setBindings(
            {QRhiShaderResourceBinding::bufferLoad(0, stage, copySource.get()),
             QRhiShaderResourceBinding::bufferStore(1, stage, copyDestination.get()),
             QRhiShaderResourceBinding::uniformBuffer(2, stage, copyUniform.get())});
        if (!copyLayout->create()) {
            error = "Cannot create copy pipeline layout";
            return false;
        }
        auto pipeline = std::unique_ptr<QRhiComputePipeline>(rhi->newComputePipeline());
        pipeline->setShaderStage({QRhiShaderStage::Compute, shader});
        pipeline->setShaderResourceBindings(copyLayout.get());
        if (!pipeline->create()) {
            error = "Cannot create GPU buffer copy pipeline";
            return false;
        }
        copyPipeline = std::move(pipeline);
        ++copyPipelineBuilds;
    }
    return true;
}
bool GpuRenderState::transfer(QRhiCommandBuffer *cb, NativeGpuPass &pass)
{
    const auto &p = pass.definition;
    const auto type = p.value("type").toString();
    const auto dst =
        resources.find(p.value(type == "generate-mips" ? "texture" : "destination").toString());
    const auto src = resources.find(p.value("source").toString());
    if (dst == resources.end() || (type.startsWith("copy-") && src == resources.end())) {
        error = "GPU transfer resource retired";
        return false;
    }
    auto *updates = rhi->nextResourceUpdateBatch();
    if (type == "copy-buffer" || (type == "upload-buffer" && pass.transferUniform)) {
        if (pass.stagingBuffer)
            updates->uploadStaticBuffer(pass.stagingBuffer.get(), p.value("data").toByteArray());
        copyBuffer(cb, pass, updates);
        return true;
    }
    if (type == "upload-buffer") {
        auto *buffer = dst->second.buffer.get();
        if (buffer->type() == QRhiBuffer::Dynamic)
            updates->updateDynamicBuffer(buffer, p.value("offset").toUInt(),
                                         p.value("data").toByteArray());
        else
            updates->uploadStaticBuffer(buffer, p.value("offset").toUInt(),
                                        p.value("data").toByteArray());
    } else if (type == "upload-texture") {
        const auto size = p.value("size").toList(), origin = p.value("origin").toList();
        QRhiTextureSubresourceUploadDescription upload(p.value("data").toByteArray());
        upload.setSourceSize({size[0].toInt(), size[1].toInt()});
        upload.setDataStride(p.value("bytesPerRow").toUInt());
        upload.setDestinationTopLeft({origin[0].toInt(), origin[1].toInt()});
        updates->uploadTexture(dst->second.texture.get(),
                               QRhiTextureUploadDescription{{p.value("layer", 0).toInt(),
                                                             p.value("level").toInt(), upload}});
    } else if (type == "copy-texture") {
        QRhiTextureCopyDescription copy;
        const auto extent = p.value("size").toList(), a = p.value("sourceOrigin").toList(),
                   b = p.value("destinationOrigin").toList();
        copy.setPixelSize({extent[0].toInt(), extent[1].toInt()});
        copy.setSourceTopLeft({a[0].toInt(), a[1].toInt()});
        copy.setDestinationTopLeft({b[0].toInt(), b[1].toInt()});
        copy.setSourceLayer(p.value("sourceLayer", 0).toInt());
        copy.setDestinationLayer(p.value("destinationLayer", 0).toInt());
        copy.setSourceLevel(p.value("sourceLevel").toInt());
        copy.setDestinationLevel(p.value("destinationLevel").toInt());
        updates->copyTexture(dst->second.texture.get(), src->second.texture.get(), copy);
    } else if (type == "generate-mips")
        updates->generateMips(dst->second.texture.get());
    cb->resourceUpdate(updates);
    return true;
}
void GpuRenderState::copyBuffer(QRhiCommandBuffer *cb, NativeGpuPass &pass,
                                QRhiResourceUpdateBatch *updates)
{
    const auto &p = pass.definition;
    const auto groups = (quint32(p.value("size").toInt() / 4) + 63) / 64;
    const auto x = qMin(groups, quint32(rhi->resourceLimit(QRhi::MaxThreadGroupsPerDimension)));
    const quint32 values[]{quint32(p.value("sourceOffset").toInt() / 4),
                           quint32(p.value("destinationOffset").toInt() / 4),
                           quint32(p.value("size").toInt() / 4), x * 64};
    updates->updateDynamicBuffer(pass.transferUniform.get(), 0, sizeof(values), values);
    cb->beginComputePass(updates);
    cb->setComputePipeline(copyPipeline.get());
    cb->setShaderResources(pass.bindings.get());
    cb->dispatch(int(x), int((groups + x - 1) / x), 1);
    cb->endComputePass();
}
void GpuRenderState::pumpReadbacks(QRhiCommandBuffer *cb)
{
    for (auto &read : reads) {
        if (!read->gpuSnapshot || !read->fenceReady || read->bufferReadScheduled || read->complete)
            continue;
        read->bufferReadScheduled = true;
        auto *updates = rhi->nextResourceUpdateBatch();
        updates->readBackBuffer(read->gpuSnapshot.get(), 0, quint32(read->gpuSnapshot->size()),
                                &read->result);
        cb->resourceUpdate(updates);
    }
}
void GpuRenderState::finishSubmission(QRhiCommandBuffer *cb, QRhiTexture *surface)
{
    auto submission = std::exchange(snapshot.submission, {});
    if (!submission)
        return;
    if (!error.isEmpty() || !surface) {
        bridge->complete(submission->operation, {},
                         error.isEmpty() ? "Submission surface lost" : error);
        return;
    }
    if (!budget || !budget->readbacks.reserve(32)) {
        bridge->complete(submission->operation, {}, "GPU completion budget exceeded");
        return;
    }
    auto read = std::make_shared<PendingGpuReadback>();
    read->budget = budget;
    read->bytes = 32;
    read->submission = submission;
    read->operation = submission->operation;
    const auto weak = std::weak_ptr<PendingGpuReadback>(read);
    const QVariantMap result{{"serial", QVariant::fromValue(submission->serial)},
                             {"generation", QVariant::fromValue(generation)},
                             {"frame", QVariant::fromValue(frames + 1)},
                             {"backend", rhi->backendName()}};
    read->result.completed = [weak, sink = bridge, result] {
        if (auto read = weak.lock()) {
            read->complete = true;
            sink->complete(read->operation, result);
            read->result.data.clear();
        }
    };
    reads.push_back(read);
    QRhiReadbackDescription description(surface);
    description.setRect({0, 0, 1, 1});
    auto *updates = rhi->nextResourceUpdateBatch();
    updates->readBackTexture(description, &read->result);
    cb->resourceUpdate(updates);
}
} // namespace EchoMusic
