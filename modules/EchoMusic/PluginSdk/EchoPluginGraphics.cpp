#include "EchoPluginGraphics.h"
#include "EchoGpuSurface.h"
#include "graphics/GpuRhi_p.h"
#include "graphics/GpuTextureLayout_p.h"
#include "graphics/GpuVertexLayout_p.h"
#include <QQuickWindow>
#include "EchoPluginContext.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <qfloat16.h>
#include <QSet>
#include <rhi/qshader.h>
#include <cmath>
#include <cstring>
namespace EchoMusic
{
namespace
{
constexpr qint64 budget = 64ll * 1024 * 1024;
int elementSize(const QString &format)
{
    if (format == "uint8" || format == "int8")
        return 1;
    if (format == "uint16" || format == "int16" || format == "float16")
        return 2;
    if (format == "float32" || format == "uint32" || format == "int32")
        return 4;
    return 0;
}
bool integer(const QVariant &value, int minimum, int maximum)
{
    bool ok = false;
    const double n = value.toDouble(&ok);
    return ok && value.metaType().id() != QMetaType::Bool && std::isfinite(n) &&
           n == std::floor(n) && n >= minimum && n <= maximum;
}
bool keys(const QVariantMap &map, const QStringList &allowed)
{
    for (auto i = map.begin(); i != map.end(); ++i)
        if (!allowed.contains(i.key()))
            return false;
    return true;
}
qint64 cost(const GpuResourceDescription &d)
{
    if (d.imported || d.textureSource)
        return 0;
    if (d.kind == "buffer")
        return d.definition.value("size").toLongLong();
    if (d.kind == "texture") {
        const auto format = d.definition.value("format", "rgba8").toString();
        qint64 pixels = 0;
        auto w = d.definition.value("width").toInt(), h = d.definition.value("height").toInt();
        int level = 0;
        do {
            pixels += qint64(w) * h * gpuTextureSlices(d.definition, level++);
            if (!d.definition.value("mipmaps").toBool() || (w == 1 && h == 1))
                break;
            w = qMax(1, w / 2);
            h = qMax(1, h / 2);
        } while (true);
        return pixels * (format == "rgba32f" ? 16 : format == "rgba16f" ? 8 : 4);
    }
    return 4096 + d.definition.value("vertexBytes").toByteArray().size() +
           d.definition.value("fragmentBytes").toByteArray().size() +
           d.definition.value("computeBytes").toByteArray().size();
}
} // namespace
EchoGpuResource::EchoGpuResource(EchoPluginGraphics &owner, QString id,
                                 GpuResourceDescription description)
    : QObject(&owner), m_owner(owner), m_id(std::move(id)), m_description(std::move(description))
{
}
void EchoGpuResource::dispose()
{
    if (m_active)
        m_owner.retire(*this);
}
EchoPluginGraphics::EchoPluginGraphics(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    m_atlasDomain = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *activation = scope.activationContext();
    m_memory = activation == &scope ? std::make_shared<GpuMemoryBudget>()
                                    : activation->graphics()->memoryBudget();
    if (activation != &scope)
        connect(activation->graphics(), &EchoPluginGraphics::changed, this,
                &EchoPluginGraphics::changed);
    connect(&scope, &EchoPluginContext::sessionInvalidated, this, [this] {
        m_atlasDomain = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shutdown();
        // The activation facade survives account changes; presentation scopes do
        // not. New account resources must never reuse the retired handles.
        if (m_scope.active()) {
            m_closed = false;
            m_capabilities.clear();
            m_error.clear();
            emit changed();
        }
    });
}
bool EchoPluginGraphics::active() const
{
    return !m_closed && m_scope.active() && m_scope.package().capabilities.contains("graphics");
}
bool EchoPluginGraphics::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}
QVariantMap EchoPluginGraphics::capabilities() const
{
    auto *root = m_scope.activationContext()->graphics();
    const auto &reported =
        m_capabilities.isEmpty() && root != this ? root->m_capabilities : m_capabilities;
    QVariantMap values{{"available", false},    {"reason", "No active GPU surface"},
                       {"compute", false},      {"hdr", false},
                       {"hdrRequested", false}, {"localHdrSupported", false},
                       {"dynamicRange", "sdr"}};
    for (auto i = reported.begin(); i != reported.end(); ++i)
        values.insert(i.key(), i.value());
    // graphicsApi() describes the requested RHI API; a software scene-graph
    // adaptation selected via QT_QUICK_BACKEND can still leave it at Metal/D3D.
    if (QQuickWindow::graphicsApi() == QSGRendererInterface::Software ||
        qEnvironmentVariable("QT_QUICK_BACKEND").compare("software", Qt::CaseInsensitive) == 0) {
        values["available"] = false;
        values["state"] = "unavailable";
        values["backend"] = "Software";
        values["compute"] = false;
        values["hdr"] = false;
        values["localHdrSupported"] = false;
        values["dynamicRange"] = "sdr";
        values["reason"] = "Qt software renderer has no RHI";
    }
    if (!active()) {
        values["available"] = false;
        values["state"] = "suspended";
        values["reason"] = "Plugin graphics scope is inactive";
    }
    values["limits"] = QVariantMap{{"logicalBytes", budget},
                                   {"nativeBytes", m_memory->native.limit},
                                   {"renderTargetBytes", m_memory->targets.limit},
                                   {"readbackBytes", m_memory->readbacks.limit},
                                   {"resources", 256},
                                   {"surfaces", 16},
                                   {"textureExtent", 4096},
                                   {"textureArrayLayers", 256},
                                   {"textureVolumeExtent", 2048},
                                   {"surfacePixels", 8 * 1024 * 1024},
                                   {"passes", 32},
                                   {"drawVertices", 1000000},
                                   {"vertexStreams", 16},
                                   {"vertexAttributes", 16},
                                   {"indirectCommands", 256},
                                   {"computeInvocations", 64000000},
                                   {"submissionBytes", m_memory->submissions.limit},
                                   {"pendingSubmissions", 32},
                                   {"surfaceSubmissions", 8}};
    values["pendingSubmissions"] = m_memory->pendingSubmissions.load();
    values["submissionBytes"] = m_memory->submissions.used.load();
    values["logicalResources"] = m_resources.size();
    values["logicalBytes"] = m_bytes;
    return values;
}
void EchoPluginGraphics::report(const QVariantMap &values)
{
    if (m_closed)
        return;
    m_capabilities = values;
    QPointer<EchoPluginGraphics> root = m_scope.activationContext()->graphics();
    if (root != this) {
        root->m_capabilities = values;
    }
    const bool child = root != this;
    emit changed();
    if (child && root)
        emit root->changed();
}
QByteArray EchoPluginGraphics::bytes(const QJSValue &value, const QString &format, int maximum)
{
    if (value.isUndefined() || value.isNull())
        return {};
    const auto variant = value.toVariant();
    if (variant.metaType().id() == QMetaType::QByteArray) {
        const auto result = variant.toByteArray();
        if (result.size() > maximum) {
            fail("GPU data exceeds its resource range");
            return {};
        }
        return result;
    }
    const auto length = value.property("length");
    const auto stride = elementSize(format);
    if (!length.isNumber() || !stride || length.toNumber() < 0 ||
        length.toNumber() > maximum / stride || length.toNumber() > 65536 ||
        length.toNumber() != std::floor(length.toNumber())) {
        fail("GPU data must be a bounded numeric array or ArrayBuffer");
        return {};
    }
    QByteArray result(int(length.toNumber()) * stride, Qt::Uninitialized);
    for (int i = 0; i < length.toInt(); ++i) {
        const auto item = value.property(quint32(i));
        const double n = item.toNumber();
        const double signedMinimum = format == "int8"    ? -128.
                                     : format == "int16" ? -32768.
                                                         : -2147483648.;
        const double signedMaximum = format == "int8"    ? 127.
                                     : format == "int16" ? 32767.
                                                         : 2147483647.;
        if ((format.startsWith("int") &&
             (n < signedMinimum || n > signedMaximum || n != std::floor(n))) ||
            !item.isNumber() || !std::isfinite(n) ||
            (format == "float16" && std::abs(n) > 65504.) ||
            (format == "float32" && std::abs(n) > 3.402823466e38) ||
            (format.startsWith("uint") && (n < 0 || n != std::floor(n) ||
                                           n > (format == "uint8"    ? 255.
                                                : format == "uint16" ? 65535.
                                                                     : 4294967295.)))) {
            fail("GPU data contains an invalid numeric component");
            return {};
        }
        char *out = result.data() + i * stride;
        if (format == "int32") {
            const qint32 v = qint32(n);
            std::memcpy(out, &v, 4);
        } else if (format == "int16") {
            const qint16 v = qint16(n);
            std::memcpy(out, &v, 2);
        } else if (format == "int8") {
            const qint8 v = qint8(n);
            std::memcpy(out, &v, 1);
        } else if (format == "float32") {
            const float v = float(n);
            std::memcpy(out, &v, 4);
        } else if (format == "float16") {
            const qfloat16 v{float(n)};
            std::memcpy(out, &v, 2);
        } else if (format == "uint32") {
            const quint32 v = quint32(n);
            std::memcpy(out, &v, 4);
        } else if (format == "uint16") {
            const quint16 v = quint16(n);
            std::memcpy(out, &v, 2);
        } else
            *out = char(quint8(n));
    }
    return result;
}
QByteArray EchoPluginGraphics::shader(const QString &relative, int stage)
{
    if (relative.isEmpty() || !relative.endsWith(".qsb") || QDir::isAbsolutePath(relative) ||
        relative.contains('\\') || relative.contains(':')) {
        fail("Shaders must be package-relative .qsb assets");
        return {};
    }
    for (const auto &part : relative.split('/'))
        if (part.isEmpty() || part == "." || part == "..") {
            fail("Invalid shader path");
            return {};
        }
    const auto root = QFileInfo(m_scope.package().root).canonicalFilePath();
    const QFileInfo info(root + '/' + relative);
    const auto canonical = info.canonicalFilePath();
    if (!canonical.startsWith(root + '/') || !info.isFile() || info.isSymLink() ||
        info.size() > 1024 * 1024) {
        fail("Shader is outside its package or exceeds 1 MiB");
        return {};
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly)) {
        fail("Cannot read shader");
        return {};
    }
    const auto result = file.readAll();
    const auto qshader = QShader::fromSerialized(result);
    if (!qshader.isValid() || int(qshader.stage()) != stage) {
        fail("Invalid QSB shader or wrong shader stage");
        return {};
    }
    return result;
}
EchoGpuResource *
EchoPluginGraphics::create(QString kind, QVariantMap definition, QByteArray data,
                           EchoGpuResource *source, bool readOnly,
                           std::shared_ptr<GpuResourceDescription::TextureSource> textureSource)
{
    if (!active()) {
        fail("Graphics capability is missing or its scope has retired");
        return nullptr;
    }
    GpuResourceDescription d;
    d.kind = std::move(kind);
    d.definition = std::move(definition);
    d.data = std::move(data);
    d.imported = source != nullptr;
    d.readOnly = readOnly;
    d.textureSource = std::move(textureSource);
    if (source) {
        d.shared = source->m_description.shared;
        d.lease = std::make_shared<std::atomic<bool>>(true);
    } else if (!d.textureSource &&
               (d.kind == "texture" ||
                (d.kind == "buffer" &&
                 !d.definition.value("usage").toStringList().contains("uniform"))))
        d.shared = std::make_shared<GpuSharedResourceState>();
    auto *root = m_scope.activationContext()->graphics();
    const auto used = root == this ? m_bytes : root->m_bytes;
    if (m_memory->objects >= 256 || cost(d) > budget - used) {
        fail("Plugin GPU resource budget exceeded (64 MiB)");
        return nullptr;
    }
    d.allocationBytes = source ? source->m_description.allocationBytes : cost(d);
    auto *resource =
        new EchoGpuResource(*this,
                            m_scope.id() + ":" + m_scope.activationGeneration() + ":" +
                                m_scope.contextId() + ".gpu." + QString::number(++m_sequence),
                            std::move(d));
    if (source) {
        resource->m_description.data.clear();
        resource->m_source = source;
        resource->m_imported = true;
        resource->m_exportEpoch = source->m_exportEpoch;
        connect(source, &EchoGpuResource::changed, resource, [resource] {
            if (!resource->active())
                resource->dispose();
            else
                emit resource->changed();
        });
        connect(source, &QObject::destroyed, resource, [resource] { resource->dispose(); });
        connect(&source->m_owner, &EchoPluginGraphics::resourcesChanged, resource,
                [this] { emit resourcesChanged(); });
    }
    connect(this, &EchoPluginGraphics::changed, resource, &EchoGpuResource::changed);
    ++m_memory->objects;
    QObject::connect(resource, &QObject::destroyed, [memory = m_memory] { --memory->objects; });
    QQmlEngine::setObjectOwnership(resource, QQmlEngine::CppOwnership);
    m_resources.insert(resource->id(), resource);
    m_bytes += cost(resource->m_description);
    if (root != this)
        root->m_bytes += cost(resource->m_description);
    m_error.clear();
    QPointer<EchoPluginGraphics> alive(this);
    QPointer<EchoGpuResource> retained(resource);
    emit changed();
    if (!alive || !retained)
        return nullptr;
    emit resourcesChanged();
    return retained;
}
EchoGpuResource *EchoPluginGraphics::createBuffer(const QVariantMap &input, const QJSValue &data)
{
    if (!active()) {
        fail("Graphics capability is missing or scope is closed");
        return nullptr;
    }
    auto d = input;
    if (!keys(d, {"size", "usage", "format"})) {
        fail("Unknown buffer field");
        return nullptr;
    }
    const auto usage = d.value("usage").toStringList();
    if (!integer(d.value("size"), 4, 16 * 1024 * 1024) || d.value("size").toInt() % 4 ||
        usage.isEmpty()) {
        fail("Buffer size must be aligned, positive and <=16 MiB");
        return nullptr;
    }
    for (const auto &u : usage)
        if (!QStringList{"uniform", "vertex", "index", "storage", "indirect"}.contains(u)) {
            fail("Unknown buffer usage");
            return nullptr;
        }
    if (usage.contains("uniform") && usage.size() != 1) {
        fail("Uniform buffers cannot have other usages");
        return nullptr;
    }
    const auto format = d.value("format", "float32").toString();
    if (!elementSize(format)) {
        fail("Invalid buffer data format");
        return nullptr;
    }
    d["format"] = format;
    d["usage"] = usage;
    m_error.clear();
    auto initial = bytes(data, format, d.value("size").toInt());
    if (!m_error.isEmpty())
        return nullptr;
    initial.resize(d.value("size").toInt(), 0);
    return create("buffer", d, std::move(initial));
}
EchoGpuResource *EchoPluginGraphics::createTexture(const QVariantMap &input, const QJSValue &data)
{
    if (!active()) {
        fail("Graphics capability is missing or scope is closed");
        return nullptr;
    }
    auto d = input;
    if ((d.contains("mipmaps") && d.value("mipmaps").metaType().id() != QMetaType::Bool)) {
        fail("mipmaps must be boolean");
        return nullptr;
    }
    if (!keys(d,
              {"width", "height", "format", "usage", "mipmaps", "dimension", "layers", "depth"})) {
        fail("Unknown texture field");
        return nullptr;
    }
    const auto dimension = d.value("dimension", "2d").toString();
    if (!QStringList{"2d", "2d-array", "cube", "3d"}.contains(dimension) ||
        (dimension != "2d-array" && d.contains("layers")) ||
        (dimension == "2d-array" && !integer(d.value("layers"), 1, 256)) ||
        (dimension != "3d" && d.contains("depth")) ||
        (dimension == "3d" &&
         (!integer(d.value("depth"), 1, 2048) || !integer(d.value("width"), 1, 2048) ||
          !integer(d.value("height"), 1, 2048))) ||
        (dimension == "cube" && d.value("width").toInt() != d.value("height").toInt())) {
        fail("Invalid texture dimension, layer/depth extent or non-square cube face");
        return nullptr;
    }
    d["dimension"] = dimension;
    d["layers"] = dimension == "2d-array" ? d.value("layers").toInt() : 1;
    const auto f = d.value("format", "rgba8").toString();
    const auto usage = d.value("usage", QStringList{"sampled"}).toStringList();
    if (!integer(d.value("width"), 1, 4096) || !integer(d.value("height"), 1, 4096) ||
        !QStringList{"rgba8", "rgba16f", "rgba32f", "r32f"}.contains(f) || usage.isEmpty()) {
        fail("Invalid texture size, format or usage");
        return nullptr;
    }
    for (const auto &u : usage)
        if (!QStringList{"sampled", "render-target", "storage", "readback"}.contains(u)) {
            fail("Unknown texture usage");
            return nullptr;
        }
    if (dimension == "cube" && usage.contains("storage")) {
        fail("Cube faces use render-target or transfers; portable storage images use 2d, 2d-array "
             "or 3d");
        return nullptr;
    }
    d["format"] = f;
    d["usage"] = usage;
    const auto size = qint64(d.value("width").toInt()) * d.value("height").toInt() *
                      gpuTextureSlices(d) *
                      (f == "rgba32f"   ? 16
                       : f == "rgba16f" ? 8
                                        : 4);
    if (size > budget) {
        fail("Texture exceeds GPU budget");
        return nullptr;
    }
    m_error.clear();
    auto initial = bytes(data,
                         f == "rgba8"     ? "uint8"
                         : f == "rgba16f" ? "float16"
                                          : "float32",
                         size);
    if (!m_error.isEmpty())
        return nullptr;
    if (!initial.isEmpty() && initial.size() != size) {
        fail("Texture upload size does not match its extent");
        return nullptr;
    }
    if (initial.isEmpty())
        initial.resize(size, 0);
    return create("texture", d, std::move(initial));
}
EchoGpuResource *EchoPluginGraphics::createSampler(const QVariantMap &d)
{
    if (!active()) {
        fail("Graphics capability is missing or scope is closed");
        return nullptr;
    }
    if (!keys(d, {"minFilter", "magFilter", "mipFilter", "u", "v", "w"})) {
        fail("Unknown sampler field");
        return nullptr;
    }
    for (const auto &key : {"minFilter", "magFilter"})
        if (!QStringList{"nearest", "linear"}.contains(d.value(key, "linear").toString())) {
            fail("Invalid sampler filter");
            return nullptr;
        }
    for (const auto &key : {"u", "v", "w"})
        if (!QStringList{"clamp", "repeat", "mirror"}.contains(d.value(key, "clamp").toString())) {
            fail("Invalid sampler addressing");
            return nullptr;
        }
    if (!QStringList{"none", "nearest", "linear"}.contains(
            d.value("mipFilter", "none").toString())) {
        fail("Invalid mip filter");
        return nullptr;
    }
    return create("sampler", d);
}
EchoGpuResource *EchoPluginGraphics::createRenderPipeline(const QVariantMap &input)
{
    if (!active()) {
        fail("Graphics capability is missing or scope is closed");
        return nullptr;
    }
    auto d = input;
    if (!keys(d, {"vertexShader", "fragmentShader", "topology", "blend", "cull", "depthTest",
                  "depthWrite", "vertexLayout", "blends"}) ||
        !QStringList{"none", "back", "front"}.contains(d.value("cull", "none").toString())) {
        fail("Invalid render pipeline fields");
        return nullptr;
    }
    d["vertexBytes"] = shader(d.value("vertexShader").toString(), QShader::VertexStage);
    if (d.value("vertexBytes").toByteArray().isEmpty())
        return nullptr;
    d["fragmentBytes"] = shader(d.value("fragmentShader").toString(), QShader::FragmentStage);
    if (d.value("fragmentBytes").toByteArray().isEmpty())
        return nullptr;
    if (!QStringList{"triangles", "triangle-strip", "lines", "line-strip", "points"}.contains(
            d.value("topology", "triangles").toString()) ||
        !QStringList{"none", "alpha", "add"}.contains(d.value("blend", "alpha").toString())) {
        fail("Invalid pipeline topology or blending");
        return nullptr;
    }
    if (d.contains("blends")) {
        const auto blends = d.value("blends").toList();
        if (blends.isEmpty() || blends.size() > 8) {
            fail("Pipeline blends requires 1..8 attachment modes");
            return nullptr;
        }
        for (const auto &blend : blends)
            if (!QStringList{"none", "alpha", "add"}.contains(blend.toString())) {
                fail("Invalid attachment blend mode");
                return nullptr;
            }
    }
    if (d.contains("vertexLayout")) {
        auto layout = d.value("vertexLayout").toMap();
        QString error;
        if (!gpuNormalizeVertexLayout(layout, error)) {
            fail(error);
            return nullptr;
        }
        d["vertexLayout"] = layout;
    }
    return create("render-pipeline", d);
}
EchoGpuResource *EchoPluginGraphics::createComputePipeline(const QVariantMap &input)
{
    if (!active()) {
        fail("Graphics capability is missing or scope is closed");
        return nullptr;
    }
    auto d = input;
    if (!keys(d, {"computeShader"})) {
        fail("Unknown compute pipeline field");
        return nullptr;
    }
    d["computeBytes"] = shader(d.value("computeShader").toString(), QShader::ComputeStage);
    if (d.value("computeBytes").toByteArray().isEmpty())
        return nullptr;
    return create("compute-pipeline", d);
}
bool EchoPluginGraphics::writeBuffer(EchoGpuResource *resource, const QJSValue &data, int offset)
{
    if (!active() || !resource || &resource->m_owner != this || !resource->active() ||
        resource->kind() != "buffer" || offset < 0 || offset % 4)
        return fail("Invalid or retired GPU buffer");
    if (resource->m_imported) {
        if (resource->readOnly())
            return fail("Shared resource lease is read-only");
        return resource->m_source->m_owner.writeBuffer(resource->m_source, data, offset);
    }
    auto &d = resource->m_description;
    const auto size = d.definition.value("size").toInt();
    if (offset > size)
        return fail("Buffer write is outside its range");
    m_error.clear();
    const auto value = bytes(data, d.definition.value("format").toString(), size - offset);
    if (!m_error.isEmpty())
        return false;
    if (!value.isEmpty())
        std::memcpy(d.data.data() + offset, value.constData(), size_t(value.size()));
    ++d.revision;
    emit resourcesChanged();
    return true;
}
bool EchoPluginGraphics::writeTexture(EchoGpuResource *resource, const QJSValue &data)
{
    if (!active() || !resource || &resource->m_owner != this || !resource->active() ||
        resource->kind() != "texture")
        return fail("Invalid or retired GPU texture");
    if (resource->m_imported) {
        if (resource->readOnly())
            return fail("Shared resource lease is read-only");
        return resource->m_source->m_owner.writeTexture(resource->m_source, data);
    }
    auto &d = resource->m_description;
    const auto f = d.definition.value("format").toString();
    m_error.clear();
    const auto value =
        bytes(data,
              f == "rgba8"     ? "uint8"
              : f == "rgba16f" ? "float16"
                               : "float32",
              d.definition.value("width").toInt() * d.definition.value("height").toInt() *
                  gpuTextureSlices(d.definition) *
                  (f == "rgba32f"   ? 16
                   : f == "rgba16f" ? 8
                                    : 4));
    if (!m_error.isEmpty() || value.size() != d.data.size())
        return fail("Texture upload must cover the complete extent");
    d.data = value;
    ++d.revision;
    emit resourcesChanged();
    return true;
}
void EchoPluginGraphics::retire(EchoGpuResource &resource)
{
    if (!resource.m_active)
        return;
    resource.m_active = false;
    if (resource.m_description.textureSource) {
        resource.m_description.textureSource->active = false;
        resource.m_description.textureSource->ready = false;
    }
    m_resources.remove(resource.id());
    if (!resource.m_imported) {
        revokeSharing(&resource);
        if (resource.m_description.shared)
            resource.m_description.shared->active = false;
    }
    if (resource.m_description.lease)
        *resource.m_description.lease = false;
    const auto surfaces = m_surfaces;
    for (auto surface : surfaces)
        if (surface)
            surface->retireResource(resource.id());
    const auto size = cost(resource.m_description);
    m_bytes -= size;
    auto *root = m_scope.activationContext()->graphics();
    if (root != this)
        root->m_bytes -= size;
    resource.m_description.data.clear();
    resource.m_description.definition.clear();
    emit resource.changed();
    emit changed();
    emit resourcesChanged();
    resource.deleteLater();
}
QHash<QString, GpuResourceDescription> EchoPluginGraphics::descriptions() const
{
    QHash<QString, GpuResourceDescription> result;
    if (active())
        for (auto *r : m_resources)
            if (r->active()) {
                auto d = r->m_description;
                if (r->m_imported && r->m_source) {
                    d.data = r->m_source->m_description.data;
                    d.revision = r->m_source->m_description.revision;
                }
                result.insert(r->id(), std::move(d));
            }
    return result;
}
bool EchoPluginGraphics::validateFrame(const QVariantMap &frame, QString &error) const
{
    if (!active()) {
        error = "Graphics scope has retired";
        return false;
    }
    if (!keys(frame, {"passes"})) {
        error = "Unknown GPU frame field";
        return false;
    }
    const auto passes = frame.value("passes").toList();
    if (passes.isEmpty() || passes.size() > 32) {
        error = "A GPU frame requires 1..32 passes";
        return false;
    }
    for (const auto &entry : passes) {
        const auto pass = entry.toMap();
        const auto kind = pass.value("type", "render").toString();
        if (!keys(pass, kind == "compute" ? QStringList{"type", "pipeline", "bindings", "groups"}
                                          : QStringList{"type", "pipeline", "bindings", "target",
                                                        "clear", "draws", "loadOp", "viewport",
                                                        "scissor", "colorTargets"})) {
            error = "Unknown pass field";
            return false;
        }
        if (!QStringList{"clear", "load"}.contains(pass.value("loadOp", "clear").toString())) {
            error = "Invalid render load operation";
            return false;
        }
        if (pass.contains("clear")) {
            const auto color = pass.value("clear").toList();
            if (color.size() != 4) {
                error = "Clear color requires four finite components";
                return false;
            }
            for (int i = 0; i < 4; ++i) {
                bool ok = false;
                const auto component = color[i].toDouble(&ok);
                if (!ok || !std::isfinite(component) || component < 0 ||
                    component > (i == 3 ? 1 : 64)) {
                    error = "Invalid linear clear color";
                    return false;
                }
            }
        }
        const auto pipeline = m_resources.value(pass.value("pipeline").toString());
        if (!pipeline ||
            pipeline->kind() != (kind == "compute" ? "compute-pipeline" : "render-pipeline") ||
            (kind != "render" && kind != "compute")) {
            error = "Invalid pass pipeline or resource owner";
            return false;
        }
        if (kind == "render") {
            if (pass.contains("target") && pass.contains("colorTargets")) {
                error = "Use target or colorTargets, not both";
                return false;
            }
            auto targets = pass.value("colorTargets").toList();
            if (pass.contains("colorTargets") && (targets.isEmpty() || targets.size() > 8)) {
                error = "Render pass requires 1..8 color targets";
                return false;
            }
            if (!pass.contains("colorTargets") && pass.value("target", "surface") != "surface")
                targets.append(QVariantMap{{"texture", pass.value("target")}});
            QSet<QString> textures;
            QSet<GpuSharedResourceState *> allocations;
            QSize extent;
            for (const auto &value : targets) {
                const auto view = value.toMap();
                const auto id = view.value("texture").toString();
                const auto *r = m_resources.value(id);
                if (!keys(view, {"texture", "level", "layer"}) || !r || r->kind() != "texture" ||
                    r->readOnly() ||
                    !r->m_description.definition.value("usage").toStringList().contains(
                        "render-target") ||
                    textures.contains(id) ||
                    (r && r->m_description.shared &&
                     allocations.contains(r->m_description.shared.get()))) {
                    error = "Color targets require distinct live render-target textures";
                    return false;
                }
                const auto d = r->m_description.definition;
                const int maxLevel =
                    d.value("mipmaps").toBool()
                        ? int(std::floor(
                              std::log2(qMax(d.value("width").toInt(), d.value("height").toInt()))))
                        : 0;
                if (!integer(view.value("level", 0), 0, maxLevel) ||
                    !integer(view.value("layer", 0), 0,
                             gpuTextureSlices(d, view.value("level", 0).toInt()) - 1)) {
                    error = "Invalid color target mip level or slice";
                    return false;
                }
                const int level = view.value("level", 0).toInt();
                const QSize size(qMax(1, d.value("width").toInt() >> level),
                                 qMax(1, d.value("height").toInt() >> level));
                if (!extent.isEmpty() && extent != size) {
                    error = "Color target mip extents must match";
                    return false;
                }
                textures.insert(id);
                if (r->m_description.shared)
                    allocations.insert(r->m_description.shared.get());
                extent = size;
            }
            const auto blends = pipeline->m_description.definition.value("blends").toList();
            if (!blends.isEmpty() && blends.size() != qMax(1, int(targets.size()))) {
                error = "Pipeline attachment blends must match color target count";
                return false;
            }
        }
        const auto bindings = pass.value("bindings").toList();
        if (bindings.size() > 32) {
            error = "Too many shader bindings";
            return false;
        }
        QSet<int> indices;
        for (const auto &v : bindings) {
            const auto b = v.toMap();
            if (!keys(b, {"binding", "kind", "resource", "sampler", "access", "offset", "size",
                          "level"}) ||
                !QStringList{"uniform", "texture", "storage-buffer", "storage-image"}.contains(
                    b.value("kind").toString()) ||
                !QStringList{"read", "write", "read-write"}.contains(
                    b.value("access", "read").toString())) {
                error = "Invalid binding fields";
                return false;
            }
            const auto *boundResource = m_resources.value(b.value("resource").toString());
            if (boundResource && boundResource->readOnly() &&
                b.value("kind").toString().startsWith("storage-") &&
                b.value("access", "read") != "read") {
                error = "Shared resource lease is read-only";
                return false;
            }
            const auto index = b.value("binding");
            const auto id = b.value("resource").toString();
            if (!integer(index, 0, 31) || indices.contains(index.toInt()) ||
                (id != "frame" && !m_resources.contains(id))) {
                error = "Invalid shader binding or resource owner";
                return false;
            }
            if (b.contains("offset") || b.contains("size")) {
                auto *r = m_resources.value(id);
                if (b.value("kind") != "uniform" || !r || r->kind() != "buffer" ||
                    !integer(b.value("offset", 0), 0,
                             r->m_description.definition.value("size").toInt()) ||
                    !integer(b.value("size", r->m_description.definition.value("size").toInt() -
                                                 b.value("offset", 0).toInt()),
                             1, r->m_description.definition.value("size").toInt()) ||
                    b.value("offset", 0).toLongLong() +
                            b.value("size", r->m_description.definition.value("size").toInt() -
                                                b.value("offset", 0).toInt())
                                .toLongLong() >
                        r->m_description.definition.value("size").toInt()) {
                    error = "Invalid uniform binding range";
                    return false;
                }
            }
            if (b.value("kind") == "storage-buffer") {
                const auto *resource = m_resources.value(id);
                if (!resource || resource->kind() != "buffer" ||
                    !resource->m_description.definition.value("usage").toStringList().contains(
                        "storage")) {
                    error = "Storage binding requires declared storage usage";
                    return false;
                }
            }
            if (b.contains("level")) {
                auto *r = m_resources.value(id);
                if (b.value("kind") != "storage-image" || !r || r->kind() != "texture") {
                    error = "Only storage-image bindings accept a mip level";
                    return false;
                }
                const auto d = r->m_description.definition;
                const int maxLevel =
                    d.value("mipmaps").toBool()
                        ? int(std::floor(
                              std::log2(qMax(d.value("width").toInt(), d.value("height").toInt()))))
                        : 0;
                if (!integer(b.value("level"), 0, maxLevel)) {
                    error = "Invalid storage image mip level";
                    return false;
                }
            }
            indices.insert(index.toInt());
            if (b.contains("sampler") &&
                (!m_resources.contains(b.value("sampler").toString()) ||
                 m_resources.value(b.value("sampler").toString())->kind() != "sampler")) {
                error = "Invalid sampler owner";
                return false;
            }
        }
        if (kind == "compute") {
            const auto groups = pass.value("groups").toList();
            if (groups.size() != 3 || !integer(groups[0], 1, 65535) ||
                !integer(groups[1], 1, 65535) || !integer(groups[2], 1, 65535) ||
                qint64(groups[0].toInt()) * groups[1].toInt() * groups[2].toInt() > 1000000) {
                error = "Invalid compute dispatch size";
                return false;
            }
        } else {
            for (const auto &key : {"viewport", "scissor"}) {
                if (!pass.contains(key))
                    continue;
                const auto rectangle = pass.value(key).toList();
                if (rectangle.size() != 4) {
                    error = "Viewport/scissor requires four integers";
                    return false;
                }
                for (int i = 0; i < 4; ++i)
                    if (!integer(rectangle[i], i < 2 ? 0 : 1, 4096)) {
                        error = "Invalid viewport/scissor range";
                        return false;
                    }
            }
            const auto draws = pass.value("draws").toList();
            if (draws.isEmpty() || draws.size() > 256) {
                error = "A render pass requires 1..256 draws";
                return false;
            }
            int commands = 0;
            for (const auto &v : draws) {
                const auto d = v.toMap();
                const bool indirect = d.contains("indirect");
                if (!keys(d, {"count", "first", "instances", "vertexBuffer", "vertexOffset",
                              "indexBuffer", "indexFormat", "baseVertex", "firstInstance",
                              "indirect", "vertexBuffers"}) ||
                    !integer(d.value("first", 0), 0, 1000000) ||
                    !integer(d.value("baseVertex", 0), -1000000, 1000000) ||
                    !integer(d.value("firstInstance", 0), 0, 1000000) ||
                    !integer(d.value("vertexOffset", 0), 0, 16 * 1024 * 1024) ||
                    !QStringList{"uint16", "uint32"}.contains(
                        d.value("indexFormat", "uint16").toString()) ||
                    (!indirect &&
                     (!integer(d.value("count"), 1, 1000000) ||
                      !integer(d.value("instances", 1), 1, 1000000) ||
                      qint64(d.value("count").toInt()) * d.value("instances", 1).toInt() >
                          1000000))) {
                    error = "Draw exceeds vertex budget";
                    return false;
                }
                if (indirect) {
                    for (const auto &key :
                         {"count", "first", "instances", "baseVertex", "firstInstance"})
                        if (d.contains(key)) {
                            error = "Indirect draws take their arguments only from the buffer";
                            return false;
                        }
                    const auto args = d.value("indirect").toMap();
                    const auto *r = m_resources.value(args.value("buffer").toString());
                    const int size = d.contains("indexBuffer") ? 20 : 16;
                    if (!keys(args, {"buffer", "offset", "drawCount", "stride"}) || !r ||
                        r->kind() != "buffer" ||
                        !r->m_description.definition.value("usage").toStringList().contains(
                            "indirect") ||
                        !integer(args.value("offset", 0), 0, 16 * 1024 * 1024) ||
                        args.value("offset", 0).toInt() % 4 ||
                        !integer(args.value("drawCount", 1), 1, 256) ||
                        !integer(args.value("stride", size), size, 16 * 1024 * 1024) ||
                        args.value("stride", size).toInt() % 4 ||
                        args.value("offset", 0).toLongLong() +
                                qint64(args.value("drawCount", 1).toInt() - 1) *
                                    args.value("stride", size).toInt() +
                                size >
                            r->m_description.definition.value("size").toInt()) {
                        error = "Invalid indirect buffer owner, usage, alignment or range";
                        return false;
                    }
                    commands += args.value("drawCount", 1).toInt();
                } else
                    ++commands;
                if (commands > 256) {
                    error = "Render pass exceeds its 256 draw command budget";
                    return false;
                }
                if (d.contains("vertexBuffers") &&
                    (d.contains("vertexBuffer") || d.contains("vertexOffset"))) {
                    error = "Use vertexBuffers or the single-buffer fields";
                    return false;
                }
                const auto streams = gpuDrawStreams(d);
                if (streams.size() != pipeline->m_description.definition.value("vertexLayout")
                                          .toMap()
                                          .value("bindings")
                                          .toList()
                                          .size()) {
                    error = "Draw stream count differs from pipeline";
                    return false;
                }
                for (const auto &value : streams) {
                    const auto b = value.toMap();
                    const auto *r = m_resources.value(b.value("buffer").toString());
                    if (!keys(b, {"buffer", "offset"}) || !r || r->kind() != "buffer" ||
                        !r->m_description.definition.value("usage").toStringList().contains(
                            "vertex") ||
                        !integer(b.value("offset", 0), 0,
                                 r->m_description.definition.value("size").toInt()) ||
                        b.value("offset", 0).toInt() % 4) {
                        error = "Invalid vertex stream owner, usage or offset";
                        return false;
                    }
                }
                for (const auto &key : {"vertexBuffer", "indexBuffer"})
                    if (d.contains(key)) {
                        auto *r = m_resources.value(d.value(key).toString());
                        if (!r || r->kind() != "buffer") {
                            error = "Invalid draw buffer owner";
                            return false;
                        }
                    }
            }
        }
    }
    return true;
}
bool EchoPluginGraphics::attach(EchoGpuSurface *surface)
{
    m_surfaces.removeIf([](auto s) { return !s; });
    if (m_memory->surfaces >= 16)
        return fail("Plugin GPU surface limit exceeded");
    ++m_memory->surfaces;
    m_surfaces.append(surface);
    return true;
}
void EchoPluginGraphics::detach(EchoGpuSurface *surface)
{
    if (m_surfaces.removeAll(surface))
        --m_memory->surfaces;
    emit drainedChanged();
}
void EchoPluginGraphics::retainState(const std::shared_ptr<GpuRenderState> &state)
{
    m_retiredStates.removeIf([](const auto &s) { return s->objects == 0; });
    if (state && state->objects > 0 && !m_retiredStates.contains(state))
        m_retiredStates.append(state);
}
bool EchoPluginGraphics::drained() const
{
    for (const auto &state : m_retiredStates)
        if (state->objects > 0)
            return false;
    for (auto s : m_surfaces)
        if (s && !s->drained())
            return false;
    return true;
}
void EchoPluginGraphics::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    const auto surfaces = m_surfaces;
    for (auto s : surfaces)
        if (s)
            s->retire();
    const auto resources = m_resources;
    for (auto *r : resources)
        r->dispose();
    emit changed();
    if (drained())
        emit drainedChanged();
}
} // namespace EchoMusic

namespace EchoMusic
{
QString EchoPluginGraphics::atlasDomain() const
{
    auto *root = m_scope.activationContext();
    return root == &m_scope ? m_atlasDomain : root->graphics()->atlasDomain();
}
} // namespace EchoMusic
