// SPDX-License-Identifier: AGPL-3.0-only
#include "NativeGraph.h"
#include "ModelSnapshot.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <optional>
#include <atomic>
#include <webgpu/wgpu.h>
#include <cmath>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace AudioAnalysisTool
{
namespace
{
WGPUStringView view(const QByteArray &text)
{
    return {text.constData(), size_t(text.size())};
}
QString text(WGPUStringView value)
{
    return QString::fromUtf8(value.data, qsizetype(value.length));
}
[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}
quint64 integer(const QJsonValue &value, quint64 maximum = 1024 * 1024 * 1024)
{
    const double number = value.toDouble(-1);
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > double(maximum))
        fail(QStringLiteral("invalid graph integer/budget"));
    return quint64(number);
}
template <class T> const T &at(const std::vector<T> &values, const QJsonValue &index)
{
    const auto id = integer(index, 65535);
    if (id >= values.size())
        fail(QStringLiteral("graph handle is outside its table"));
    return values[size_t(id)];
}
struct Device
{
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    std::mutex mutex;
    QString error;
    ~Device()
    {
        if (queue)
            wgpuQueueRelease(queue);
        if (device) {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        if (adapter)
            wgpuAdapterRelease(adapter);
        if (instance)
            wgpuInstanceRelease(instance);
    }
    void check()
    {
        const std::lock_guard lock(mutex);
        if (!error.isEmpty())
            fail(error);
    }
};
QString backendName(WGPUBackendType type)
{
    switch (type) {
    case WGPUBackendType_Metal:
        return QStringLiteral("Metal");
    case WGPUBackendType_Vulkan:
        return QStringLiteral("Vulkan");
    case WGPUBackendType_D3D12:
        return QStringLiteral("D3D12");
    default:
        return QStringLiteral("other-native-gpu");
    }
}
void open(Device &state, ComputeResult &result)
{
    WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    state.instance = wgpuCreateInstance(&instanceDesc);
    if (!state.instance)
        fail(QStringLiteral("Unavailable: native WebGPU instance"));
    WGPUInstanceEnumerateAdapterOptions options{};
#if defined(Q_OS_MACOS)
    options.backends = WGPUInstanceBackend_Metal;
#else
    options.backends = WGPUInstanceBackend_Primary;
#endif
    const auto count = wgpuInstanceEnumerateAdapters(state.instance, &options, nullptr);
    if (count > 32)
        fail(QStringLiteral("adapter count budget exceeded"));
    std::vector<WGPUAdapter> adapters(count);
    wgpuInstanceEnumerateAdapters(state.instance, &options, adapters.data());
    for (auto adapter : adapters) {
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        wgpuAdapterGetInfo(adapter, &info);
        // gpuOnly must never select a software/CPU implementation.
        if (!state.adapter && info.adapterType != WGPUAdapterType_CPU &&
            info.adapterType != WGPUAdapterType_Unknown) {
            state.adapter = adapter;
            result.backend = backendName(info.backendType);
            result.adapter = text(info.device);
        } else
            wgpuAdapterRelease(adapter);
        wgpuAdapterInfoFreeMembers(info);
    }
    if (!state.adapter)
        fail(QStringLiteral("Unavailable: no hardware native GPU adapter"));
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo.userdata1 = &state;
    descriptor.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const *, WGPUErrorType, WGPUStringView message, void *data, void *) {
            auto &owner = *static_cast<Device *>(data);
            const std::lock_guard lock(owner.mutex);
            owner.error = text(message);
        };
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.userdata1 = &state;
    descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const *, WGPUDeviceLostReason reason,
                                                    WGPUStringView message, void *data, void *) {
        if (reason == WGPUDeviceLostReason_Destroyed)
            return;
        auto &owner = *static_cast<Device *>(data);
        const std::lock_guard lock(owner.mutex);
        owner.error = QStringLiteral("device lost: ") + text(message);
    };
    std::promise<WGPUDevice> created;
    WGPURequestDeviceCallbackInfo callback{};
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = &created;
    callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView,
                           void *data, void *) {
        static_cast<std::promise<WGPUDevice> *>(data)->set_value(
            status == WGPURequestDeviceStatus_Success ? device : nullptr);
    };
    wgpuAdapterRequestDevice(state.adapter, &descriptor, callback);
    // All blocking GPU calls are confined to the killable, non-GUI helper process.
    state.device = created.get_future().get();
    if (!state.device)
        fail(QStringLiteral("Unavailable: native GPU device creation failed"));
    state.queue = wgpuDeviceGetQueue(state.device);
}
} // namespace

ComputeResult probeNativeGpu()
{
    ComputeResult result;
    try {
        Device state;
        open(state, result);
        state.check();
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

QJsonObject makeSourceGraphSnapshot(const QString &, const QByteArray &, const QString &, int,
                                    QString *);
namespace
{
constexpr qint64 BufferBudget = 1024LL * 1024 * 1024;
struct Buffer
{
    WGPUBuffer handle = nullptr;
    quint64 size = 0, usage = 0;
    bool modelUploaded = false;
    ComputeStatistics *stats;
    ~Buffer()
    {
        if (!handle)
            return;
        wgpuBufferDestroy(handle);
        wgpuBufferRelease(handle);
        stats->liveBufferBytes -= qint64(size);
        --stats->liveBuffers;
    }
};
struct Pipeline
{
    WGPUComputePipeline handle = nullptr;
    QByteArray code, entry;
    ComputeStatistics *stats;
    ~Pipeline()
    {
        if (handle) {
            wgpuComputePipelineRelease(handle);
            --stats->livePipelines;
        }
    }
};
struct Group
{
    WGPUBindGroup handle = nullptr;
    ComputeStatistics *stats;
    ~Group()
    {
        if (handle) {
            wgpuBindGroupRelease(handle);
            --stats->liveBindGroups;
        }
    }
};
struct Graph
{
    QJsonObject definition;
    quint64 used = 0;
    bool ready = false;
    std::vector<std::shared_ptr<Buffer>> buffers;
    std::vector<std::shared_ptr<Pipeline>> pipelines;
    std::vector<std::unique_ptr<Group>> groups;
};
struct ModelWrite
{
    int buffer;
    QJsonObject upload;
    quint64 size, usage;
};
std::optional<ModelWrite> modelWrite(const QJsonObject &graph)
{
    std::optional<ModelWrite> found;
    const auto buffers = graph.value("buffers").toArray();
    for (const auto &value : graph.value("writes").toArray()) {
        const auto object = value.toObject();
        if (!object.contains("modelOffset"))
            continue;
        if (found)
            fail("ModelInvalid: one immutable packed weight upload required");
        const int id = int(integer(object.value("buffer"), 8191));
        if (id >= buffers.size())
            fail("ModelInvalid: weight buffer index");
        const auto descriptor = buffers[id].toObject();
        found = ModelWrite{id, object, integer(descriptor.value("size"), 256 * 1024 * 1024),
                           integer(descriptor.value("usage"), 1023)};
    }
    return found;
}
QJsonObject objectSchema(const QJsonValue &value, const QStringList &allowed,
                         const QStringList &required)
{
    if (!value.isObject())
        fail("GraphInvalid: expected descriptor object");
    const auto object = value.toObject();
    for (auto i = object.begin(); i != object.end(); ++i)
        if (!allowed.contains(i.key()))
            fail("GraphInvalid: unsupported descriptor field " + i.key());
    for (const auto &key : required)
        if (!object.contains(key))
            fail("GraphInvalid: missing descriptor field " + key);
    return object;
}
QJsonArray arraySchema(const QJsonValue &value, bool optional = false)
{
    if (optional && value.isUndefined())
        return {};
    if (!value.isArray())
        fail("GraphInvalid: expected descriptor array");
    return value.toArray();
}
void validateSchema(const QJsonObject &graph)
{
    objectSchema(graph,
                 {"model", "frames", "buffers", "pipelines", "groups", "batches", "writes",
                  "outputs", "totalBytes"},
                 {"buffers", "batches", "writes", "outputs"});
    if (graph.contains("model") && !graph.value("model").isString())
        fail("GraphInvalid: model name");
    const auto buffers = arraySchema(graph.value("buffers"));
    qint64 total = 0;
    for (const auto &value : buffers) {
        const auto object = objectSchema(value, {"size", "usage"}, {"size", "usage"});
        total += qint64(integer(object.value("size"), 256 * 1024 * 1024));
    }
    if (graph.contains("totalBytes") && integer(graph.value("totalBytes")) != quint64(total))
        fail("GraphInvalid: inconsistent buffer byte sum");
    for (const auto &value : arraySchema(graph.value("pipelines"), true)) {
        const auto object = objectSchema(value, {"code", "entryPoint"}, {"code", "entryPoint"});
        if (!object.value("code").isString() || !object.value("entryPoint").isString())
            fail("GraphInvalid: shader descriptor");
    }
    for (const auto &value : arraySchema(graph.value("groups"), true)) {
        const auto group =
            objectSchema(value, {"pipeline", "index", "entries"}, {"pipeline", "index", "entries"});
        for (const auto &item : arraySchema(group.value("entries")))
            objectSchema(item, {"binding", "buffer", "offset", "size"},
                         {"binding", "buffer", "offset", "size"});
    }
    for (const auto &value : arraySchema(graph.value("writes"))) {
        if (!value.isObject())
            fail("GraphInvalid: upload descriptor");
        const auto object = value.toObject();
        if (object.contains("modelOffset"))
            objectSchema(object, {"buffer", "offset", "size", "modelOffset"},
                         {"buffer", "offset", "size", "modelOffset"});
        else if (object.contains("input")) {
            objectSchema(object, {"buffer", "offset", "size", "input", "inputOffset"},
                         {"buffer", "offset", "size", "input", "inputOffset"});
            if (!object.value("input").isString())
                fail("GraphInvalid: input name");
        } else {
            objectSchema(object, {"buffer", "offset", "size", "data"},
                         {"buffer", "offset", "size", "data"});
            if (!object.value("data").isString())
                fail("GraphInvalid: static upload encoding");
        }
    }
    for (const auto &batch : arraySchema(graph.value("batches")))
        for (const auto &value : arraySchema(batch)) {
            if (!value.isObject())
                fail("GraphInvalid: command descriptor");
            const auto object = value.toObject();
            if (object.value("kind") == "dispatch") {
                objectSchema(object, {"kind", "pipeline", "groups", "count"},
                             {"kind", "pipeline", "groups", "count"});
                arraySchema(object.value("groups"));
                arraySchema(object.value("count"));
            } else if (object.value("kind") == "copy")
                objectSchema(object, {"kind", "from", "fromOffset", "to", "toOffset", "size"},
                             {"kind", "from", "fromOffset", "to", "toOffset", "size"});
            else
                fail("GraphInvalid: command kind");
        }
    for (const auto &output : arraySchema(graph.value("outputs")))
        objectSchema(output, {"buffer", "size"}, {"buffer", "size"});
}
QString graphKey(const QJsonObject &graph)
{
    return graph.value("model").toString() + ':' +
           QString::number(integer(
               graph.value("frames").isUndefined() ? QJsonValue(0) : graph.value("frames"), 60000));
}
void canceled(const std::function<bool()> &check)
{
    if (check && check())
        fail("Canceled: native graph retired");
}
struct Encoder
{
    WGPUCommandEncoder handle;
    ~Encoder()
    {
        if (handle)
            wgpuCommandEncoderRelease(handle);
    }
};
struct Pass
{
    WGPUComputePassEncoder handle;
    ~Pass()
    {
        if (handle) {
            wgpuComputePassEncoderEnd(handle);
            wgpuComputePassEncoderRelease(handle);
        }
    }
};
} // namespace
struct ComputeSession::Impl
{
    ComputeStatistics stats;
    QString model, file, error;
    QByteArray snapshot;
    std::unique_ptr<Device> device;
    ComputeResult hardware;
    std::vector<std::unique_ptr<Graph>> graphs;
    std::vector<std::shared_ptr<Pipeline>> pipelines;
    std::shared_ptr<Buffer> weights;
    QJsonObject weightUpload;
    quint64 serial = 0;
    bool closed = false;
    qint64 budget;
    Impl(QString model, QString file, qint64 budget)
        : model(model == "percussive" ? "beat_this" : std::move(model)), file(std::move(file)),
          budget(budget)
    {
        if (budget < 4 || budget > BufferBudget) {
            error = "InvalidArgument: bounded GPU cache budget";
            closed = true;
        }
    }
    ~Impl()
    {
        close();
    }
    void close()
    {
        if (device && device->device)
            wgpuDevicePoll(device->device, true, nullptr);
        graphs.clear();
        weights.reset();
        pipelines.clear();
        snapshot.clear();
        stats.residentGraphs = 0;
        if (device) {
            if (device->device)
                ++stats.deviceReleases;
            device.reset();
        }
        closed = true;
    }
    void ensureSnapshot()
    {
        if (!snapshot.isEmpty())
            return;
        QString failure;
        snapshot = readModelSnapshot(model, file, &failure);
        if (!failure.isEmpty())
            fail("ModelInvalid: upload " + failure);
        ++stats.modelVerifications;
    }
    void ensureDevice()
    {
        if (device) {
            device->check();
            return;
        }
        device = std::make_unique<Device>();
        open(*device, hardware);
        ++stats.deviceCreations;
        device->check();
    }
    void ensureModel(const QJsonObject &graph)
    {
        const auto name = graph.value("model").toString();
        if (!model.isEmpty() && name != model && !(name == "percussive" && model == "beat_this"))
            fail("ModelInvalid: graph does not belong to this fixed model session");
    }
    void evict(Graph *keep)
    {
        auto oldest = graphs.end();
        for (auto it = graphs.begin(); it != graphs.end(); ++it)
            if (it->get() != keep && (oldest == graphs.end() || (*it)->used < (*oldest)->used))
                oldest = it;
        if (oldest == graphs.end())
            fail("GPU buffer budget exceeded");
        if ((*oldest)->ready)
            --stats.residentGraphs;
        graphs.erase(oldest);
        pipelines.erase(std::remove_if(pipelines.begin(), pipelines.end(),
                                       [](const auto &value) { return value.use_count() == 1; }),
                        pipelines.end());
    }
    Graph &entry(const QJsonObject &definition)
    {
        const auto key = graphKey(definition);
        for (auto &graph : graphs)
            if (graphKey(graph->definition) == key) {
                if (graph->definition != definition)
                    fail("GraphChanged: cached model/shape descriptor differs");
                graph->used = ++serial;
                return *graph;
            }
        while (graphs.size() >= 4)
            evict(nullptr);
        auto graph = std::make_unique<Graph>();
        graph->definition = definition;
        graph->used = ++serial;
        graphs.push_back(std::move(graph));
        return *graphs.back();
    }
    std::shared_ptr<Buffer> buffer(quint64 size, quint64 usage)
    {
        auto owned = std::make_shared<Buffer>();
        owned->stats = &stats;
        owned->size = size;
        owned->usage = usage;
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = size;
        descriptor.usage = usage;
        owned->handle = wgpuDeviceCreateBuffer(device->device, &descriptor);
        if (!owned->handle)
            fail("GPU buffer allocation failed");
        ++stats.liveBuffers;
        stats.liveBufferBytes += qint64(size);
        stats.peakBufferBytes = std::max(stats.peakBufferBytes, stats.liveBufferBytes);
        device->check();
        return owned;
    }
    std::shared_ptr<Pipeline> pipeline(const QJsonObject &descriptor, Graph *keep)
    {
        const auto code = descriptor.value("code").toString().toUtf8(),
                   entry = descriptor.value("entryPoint").toString().toUtf8();
        if (code.isEmpty() || code.size() > 1024 * 1024 || entry.isEmpty() || entry.size() > 128)
            fail("shader size/entry budget exceeded");
        qint64 bytes = code.size();
        for (const auto &value : pipelines) {
            if (value->code == code && value->entry == entry)
                return value;
            bytes += value->code.size();
        }
        while (pipelines.size() >= 1024 || bytes > 16 * 1024 * 1024) {
            if (graphs.size() <= 1)
                fail("pipeline/shader budget exceeded");
            evict(keep);
            bytes = code.size();
            for (const auto &value : pipelines)
                bytes += value->code.size();
        }
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = view(code);
        WGPUShaderModuleDescriptor shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        shaderDesc.nextInChain = &wgsl.chain;
        auto shader = wgpuDeviceCreateShaderModule(device->device, &shaderDesc);
        if (!shader)
            fail("GPU shader module creation failed");
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.compute.module = shader;
        desc.compute.entryPoint = view(entry);
        auto handle = wgpuDeviceCreateComputePipeline(device->device, &desc);
        wgpuShaderModuleRelease(shader);
        if (!handle)
            fail("GPU compute pipeline creation failed");
        auto owned = std::make_shared<Pipeline>();
        owned->stats = &stats;
        owned->handle = handle;
        owned->code = code;
        owned->entry = entry;
        ++stats.pipelineCreations;
        ++stats.livePipelines;
        pipelines.push_back(owned);
        device->check();
        return owned;
    }
    void upload(Graph &graph, const QJsonObject &object, const QMap<QString, QByteArray> &inputs)
    {
        auto target = at(graph.buffers, object.value("buffer"));
        const auto offset = integer(object.value("offset")),
                   size = integer(object.value("size"), 256 * 1024 * 1024);
        if (offset > target->size || size > target->size - offset || offset % 4 || size % 4)
            fail("write exceeds/alignment of buffer");
        if (target == weights && !object.contains("modelOffset"))
            fail("ModelInvalid: only verified model bytes may initialize resident weights");
        QByteArray bytes;
        if (object.contains("modelOffset")) {
            ensureSnapshot();
            const auto start = integer(object.value("modelOffset"));
            if (start > quint64(snapshot.size()) || size > quint64(snapshot.size()) - start)
                fail("ModelInvalid: upload snapshot range");
            bytes =
                QByteArray::fromRawData(snapshot.constData() + qsizetype(start), qsizetype(size));
            stats.modelUploadBytes += size;
        } else if (object.contains("input")) {
            const auto &input = inputs[object.value("input").toString()];
            const auto start = integer(object.value("inputOffset"));
            if (start > quint64(input.size()) || size > quint64(input.size()) - start)
                fail("input byte range invalid");
            bytes = QByteArray::fromRawData(input.constData() + qsizetype(start), qsizetype(size));
            stats.inputUploadBytes += size;
        } else {
            const auto decoded =
                QByteArray::fromBase64Encoding(object.value("data").toString().toLatin1(),
                                               QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded)
                fail("GraphInvalid: static upload base64");
            bytes = decoded.decoded;
        }
        if (quint64(bytes.size()) != size)
            fail("upload byte count mismatch");
        wgpuQueueWriteBuffer(device->queue, target->handle, offset, bytes.constData(),
                             size_t(size));
        if (!object.contains("input"))
            stats.immutableUploadBytes += size;
        device->check();
        if (object.contains("modelOffset"))
            target->modelUploaded = true;
    }
    void prepare(Graph &graph, const std::function<bool()> &cancel)
    {
        const auto descriptors = graph.definition.value("buffers").toArray();
        if (descriptors.isEmpty() || descriptors.size() > 8192)
            fail("invalid buffer table");
        const auto weight = modelWrite(graph.definition);
        if (weight)
            ensureSnapshot();
        qint64 need = 0;
        for (qsizetype i = 0; i < descriptors.size(); ++i) {
            const auto object = descriptors[i].toObject();
            const auto size = integer(object.value("size"), 256 * 1024 * 1024);
            if (size < 4 || size % 4)
                fail("invalid buffer size/alignment");
            integer(object.value("usage"), 1023);
            if (weight && i == weight->buffer && weights) {
                if (weights->size != weight->size || weights->usage != weight->usage ||
                    weightUpload != weight->upload)
                    fail("ModelInvalid: resident weight descriptor changed");
            } else
                need += qint64(size);
        }
        if (need > budget)
            fail("GPU buffer budget exceeded");
        while (stats.liveBufferBytes > budget - need) {
            if (graphs.size() > 1)
                evict(&graph);
            else if (!weight && weights && weights.use_count() == 1)
                weights.reset();
            else
                fail("GPU buffer budget exceeded");
        }
        ensureDevice();
        for (qsizetype i = 0; i < descriptors.size(); ++i) {
            canceled(cancel);
            const auto object = descriptors[i].toObject();
            if (weight && i == weight->buffer && weights)
                graph.buffers.push_back(weights);
            else {
                auto owned = buffer(integer(object.value("size"), 256 * 1024 * 1024),
                                    integer(object.value("usage"), 1023));
                if (weight && i == weight->buffer) {
                    weights = owned;
                    weightUpload = weight->upload;
                }
                graph.buffers.push_back(std::move(owned));
            }
        }
        const auto pipelineDescriptors = graph.definition.value("pipelines").toArray();
        if (pipelineDescriptors.size() > 1024)
            fail("pipeline budget exceeded");
        for (const auto &value : pipelineDescriptors) {
            canceled(cancel);
            graph.pipelines.push_back(pipeline(value.toObject(), &graph));
        }
        const auto groups = graph.definition.value("groups").toArray();
        if (groups.size() > 8192)
            fail("bind group budget exceeded");
        for (const auto &value : groups) {
            canceled(cancel);
            const auto object = value.toObject();
            const auto sources = object.value("entries").toArray();
            if (sources.size() > 32)
                fail("binding count exceeded");
            std::vector<WGPUBindGroupEntry> entries;
            for (const auto &item : sources) {
                const auto source = item.toObject();
                auto owned = at(graph.buffers, source.value("buffer"));
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = uint32_t(integer(source.value("binding"), 31));
                entry.buffer = owned->handle;
                entry.offset = integer(source.value("offset"));
                entry.size = integer(source.value("size"));
                if (entry.offset > owned->size || entry.size > owned->size - entry.offset)
                    fail("binding exceeds its buffer");
                entries.push_back(entry);
            }
            auto layout = wgpuComputePipelineGetBindGroupLayout(
                at(graph.pipelines, object.value("pipeline"))->handle,
                uint32_t(integer(object.value("index"), 3)));
            WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            descriptor.layout = layout;
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.data();
            auto group = wgpuDeviceCreateBindGroup(device->device, &descriptor);
            wgpuBindGroupLayoutRelease(layout);
            if (!group)
                fail("GPU bind group creation failed");
            auto owned = std::make_unique<Group>();
            owned->stats = &stats;
            owned->handle = group;
            ++stats.liveBindGroups;
            graph.groups.push_back(std::move(owned));
            device->check();
        }
        const auto writes = graph.definition.value("writes").toArray();
        if (writes.size() > 16384)
            fail("upload count budget exceeded");
        for (const auto &value : writes) {
            canceled(cancel);
            const auto object = value.toObject();
            if (object.contains("input"))
                continue;
            // Shared model bytes were uploaded by the first prepared graph only.
            if (object.contains("modelOffset") && weights->modelUploaded)
                continue;
            upload(graph, object, {});
        }
        graph.ready = true;
        ++stats.graphPreparations;
        ++stats.residentGraphs;
    }
    QByteArray run(Graph &graph, const QMap<QString, QByteArray> &inputs,
                   const std::function<bool()> &cancel)
    {
        for (const auto &value : graph.definition.value("writes").toArray()) {
            canceled(cancel);
            if (value.toObject().contains("input"))
                upload(graph, value.toObject(), inputs);
        }
        const auto batches = graph.definition.value("batches").toArray();
        if (batches.size() > 8192)
            fail("GPU batch budget exceeded");
        qsizetype commandCount = 0;
        for (const auto &batch : batches) {
            canceled(cancel);
            Encoder encoder{wgpuDeviceCreateCommandEncoder(device->device, nullptr)};
            if (!encoder.handle)
                fail("GPU command encoder creation failed");
            for (const auto &command : batch.toArray()) {
                canceled(cancel);
                if (++commandCount > 262144)
                    fail("GPU command count budget exceeded");
                const auto object = command.toObject();
                if (object.value("kind") == "dispatch") {
                    Pass pass{wgpuCommandEncoderBeginComputePass(encoder.handle, nullptr)};
                    if (!pass.handle)
                        fail("GPU compute pass creation failed");
                    wgpuComputePassEncoderSetPipeline(
                        pass.handle, at(graph.pipelines, object.value("pipeline"))->handle);
                    const auto groupIds = object.value("groups").toArray();
                    if (groupIds.size() > 4)
                        fail("GPU bind group count exceeded");
                    for (qsizetype i = 0; i < groupIds.size(); ++i)
                        wgpuComputePassEncoderSetBindGroup(pass.handle, uint32_t(i),
                                                           at(graph.groups, groupIds[i])->handle, 0,
                                                           nullptr);
                    const auto count = object.value("count").toArray();
                    if (count.size() != 3)
                        fail("invalid dispatch dimensions");
                    wgpuComputePassEncoderDispatchWorkgroups(
                        pass.handle, uint32_t(integer(count[0], 65535)),
                        uint32_t(integer(count[1], 65535)), uint32_t(integer(count[2], 65535)));
                } else if (object.value("kind") == "copy") {
                    auto from = at(graph.buffers, object.value("from")),
                         to = at(graph.buffers, object.value("to"));
                    const auto fromOffset = integer(object.value("fromOffset")),
                               toOffset = integer(object.value("toOffset")),
                               size = integer(object.value("size"));
                    if (to == weights || fromOffset > from->size ||
                        size > from->size - fromOffset || toOffset > to->size ||
                        size > to->size - toOffset || fromOffset % 4 || toOffset % 4 || size % 4)
                        fail("invalid copy buffer range or immutable target");
                    wgpuCommandEncoderCopyBufferToBuffer(encoder.handle, from->handle, fromOffset,
                                                         to->handle, toOffset, size);
                } else
                    fail("unknown graph command");
            }
            auto commands = wgpuCommandEncoderFinish(encoder.handle, nullptr);
            if (!commands)
                fail("GPU command buffer finish failed");
            wgpuQueueSubmit(device->queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
            wgpuDevicePoll(device->device, true, nullptr);
            device->check();
        }
        canceled(cancel);
        const auto outputs = graph.definition.value("outputs").toArray();
        if (outputs.size() != 1)
            fail("one bounded final output required");
        const auto output = outputs[0].toObject();
        auto buffer = at(graph.buffers, output.value("buffer"));
        const auto size = integer(output.value("size"), 32 * 1024 * 1024);
        if (size < 4 || size > buffer->size || size % 4)
            fail("invalid output byte range");
        std::atomic_bool mapped{false};
        WGPUBufferMapCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &mapped;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *data, void *) {
            static_cast<std::atomic_bool *>(data)->store(status == WGPUMapAsyncStatus_Success);
        };
        wgpuBufferMapAsync(buffer->handle, WGPUMapMode_Read, 0, size_t(size), callback);
        wgpuDevicePoll(device->device, true, nullptr);
        device->check();
        if (!mapped.load())
            fail("GPU output map failed");
        QByteArray data(static_cast<const char *>(
                            wgpuBufferGetConstMappedRange(buffer->handle, 0, size_t(size))),
                        qsizetype(size));
        wgpuBufferUnmap(buffer->handle);
        canceled(cancel);
        return data;
    }
};
ComputeSession::ComputeSession(QString model, QString file, qint64 budget)
    : m_impl(std::make_unique<Impl>(std::move(model), std::move(file), budget))
{
}
ComputeSession::~ComputeSession() = default;
void ComputeSession::close()
{
    m_impl->close();
}
ComputeStatistics ComputeSession::statistics() const
{
    return m_impl->stats;
}
QJsonObject ComputeSession::sourceGraph(const QString &runtime, const QString &model, int frames,
                                        QString *error)
{
    try {
        if (m_impl->closed)
            fail("Closed: native graph session ended");
        if (model != m_impl->model && !(model == "percussive" && m_impl->model == "beat_this"))
            fail("ModelInvalid: source graph model");
        for (auto &graph : m_impl->graphs)
            if (graph->definition.value("model") == model &&
                graph->definition.value("frames") == frames) {
                graph->used = ++m_impl->serial;
                error->clear();
                return graph->definition;
            }
        m_impl->ensureSnapshot();
        auto graph = makeSourceGraphSnapshot(runtime, m_impl->snapshot, model, frames, error);
        if (!error->isEmpty())
            fail(*error);
        m_impl->entry(graph);
        return graph;
    } catch (const std::exception &failure) {
        *error = QString::fromUtf8(failure.what());
        m_impl->error = *error;
        m_impl->close();
        return {};
    }
}
ComputeResult ComputeSession::execute(const QJsonObject &definition,
                                      const QMap<QString, QByteArray> &inputs,
                                      const std::function<bool()> &cancel)
{
    ComputeResult result;
    try {
        if (m_impl->closed)
            fail(m_impl->error.isEmpty() ? "Closed: native graph session ended" : m_impl->error);
        canceled(cancel);
        m_impl->ensureModel(definition);
        if (QJsonDocument(definition).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024)
            fail("QuotaExceeded: graph protocol");
        validateSchema(definition);
        auto &graph = m_impl->entry(definition);
        if (!graph.ready)
            m_impl->prepare(graph, cancel);
        result.data = m_impl->run(graph, inputs, cancel);
        ++m_impl->stats.executions;
        result.backend = m_impl->hardware.backend;
        result.adapter = m_impl->hardware.adapter;
        result.allocatedBytes = m_impl->stats.liveBufferBytes;
    } catch (const std::exception &failure) {
        result.error = QString::fromUtf8(failure.what());
        m_impl->error = result.error;
        m_impl->close();
        result.data.clear();
    }
    return result;
}
ComputeResult executeGraph(const QJsonObject &graph, const QString &modelFile,
                           const QMap<QString, QByteArray> &inputs)
{
    ComputeSession session(graph.value("model").toString(), modelFile);
    return session.execute(graph, inputs);
}
} // namespace AudioAnalysisTool
