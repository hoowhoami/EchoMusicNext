#include "EchoPluginGraphics.h"
#include "EchoGpuSurface.h"
#include "graphics/GpuRhi_p.h"
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
    if (format == "uint8")
        return 1;
    if (format == "uint16" || format == "float16")
        return 2;
    if (format == "float32" || format == "uint32")
        return 4;
    return 0;
}
bool integer(const QVariant &value, int minimum, int maximum)
{
    bool ok = false;
    const double n = value.toDouble(&ok);
    return ok && std::isfinite(n) && n == std::floor(n) && n >= minimum && n <= maximum;
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
    if (d.kind == "buffer")
        return d.definition.value("size").toLongLong();
    if (d.kind == "texture") {
        const auto format = d.definition.value("format", "rgba8").toString();
        return d.definition.value("width").toLongLong() *
               d.definition.value("height").toLongLong() *
               (format == "rgba32f"   ? 16
                : format == "rgba16f" ? 8
                                      : 4);
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
    auto *activation = scope.activationContext();
    m_memory = activation == &scope ? std::make_shared<GpuMemoryBudget>()
                                    : activation->graphics()->memoryBudget();
    if (activation != &scope)
        connect(activation->graphics(), &EchoPluginGraphics::changed, this,
                &EchoPluginGraphics::changed);
    connect(&scope, &EchoPluginContext::sessionInvalidated, this, [this] {
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
    values["limits"] = QVariantMap{{"logicalBytes", budget},
                                   {"nativeBytes", m_memory->native.limit},
                                   {"renderTargetBytes", m_memory->targets.limit},
                                   {"readbackBytes", m_memory->readbacks.limit},
                                   {"resources", 256},
                                   {"surfaces", 16},
                                   {"textureExtent", 4096},
                                   {"surfacePixels", 8 * 1024 * 1024},
                                   {"passes", 32},
                                   {"drawVertices", 1000000},
                                   {"computeInvocations", 64000000}};
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
        if (!item.isNumber() || !std::isfinite(n) ||
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
        if (format == "float32") {
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
EchoGpuResource *EchoPluginGraphics::create(QString kind, QVariantMap definition, QByteArray data)
{
    if (!active()) {
        fail("Graphics capability is missing or its scope has retired");
        return nullptr;
    }
    GpuResourceDescription d{kind, std::move(definition), std::move(data), 1};
    auto *root = m_scope.activationContext()->graphics();
    const auto used = root == this ? m_bytes : root->m_bytes;
    if (m_memory->objects >= 256 || cost(d) > budget - used) {
        fail("Plugin GPU resource budget exceeded (64 MiB)");
        return nullptr;
    }
    d.allocationBytes = cost(d);
    auto *resource =
        new EchoGpuResource(*this,
                            m_scope.id() + ":" + m_scope.activationGeneration() + ":" +
                                m_scope.contextId() + ".gpu." + QString::number(++m_sequence),
                            std::move(d));
    ++m_memory->objects;
    QObject::connect(resource, &QObject::destroyed, [memory = m_memory] { --memory->objects; });
    QQmlEngine::setObjectOwnership(resource, QQmlEngine::CppOwnership);
    m_resources.insert(resource->id(), resource);
    m_bytes += cost(resource->m_description);
    if (root != this)
        root->m_bytes += cost(resource->m_description);
    m_error.clear();
    emit changed();
    emit resourcesChanged();
    return resource;
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
        if (!QStringList{"uniform", "vertex", "index", "storage"}.contains(u)) {
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
    if (!keys(d, {"width", "height", "format", "usage"})) {
        fail("Unknown texture field");
        return nullptr;
    }
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
    d["format"] = f;
    d["usage"] = usage;
    const auto size = int(cost({"texture", d, {}, 1}));
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
    if (!keys(d, {"minFilter", "magFilter", "u", "v"})) {
        fail("Unknown sampler field");
        return nullptr;
    }
    for (const auto &key : {"minFilter", "magFilter"})
        if (!QStringList{"nearest", "linear"}.contains(d.value(key, "linear").toString())) {
            fail("Invalid sampler filter");
            return nullptr;
        }
    for (const auto &key : {"u", "v"})
        if (!QStringList{"clamp", "repeat", "mirror"}.contains(d.value(key, "clamp").toString())) {
            fail("Invalid sampler addressing");
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
                  "depthWrite", "vertexLayout"}) ||
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
    auto &d = resource->m_description;
    const auto f = d.definition.value("format").toString();
    m_error.clear();
    const auto value = bytes(data,
                             f == "rgba8"     ? "uint8"
                             : f == "rgba16f" ? "float16"
                                              : "float32",
                             int(cost(d)));
    if (!m_error.isEmpty() || value.size() != cost(d))
        return fail("Texture upload must cover the complete extent");
    d.data = value;
    ++d.revision;
    emit resourcesChanged();
    return true;
}
void EchoPluginGraphics::retire(EchoGpuResource &resource)
{
    resource.m_active = false;
    m_resources.remove(resource.id());
    const auto size = cost(resource.m_description);
    m_bytes -= size;
    auto *root = m_scope.activationContext()->graphics();
    if (root != this)
        root->m_bytes -= size;
    resource.m_description.data.clear();
    resource.m_description.definition.clear();
    emit resource.changed();
    emit resourcesChanged();
    resource.deleteLater();
}
QHash<QString, GpuResourceDescription> EchoPluginGraphics::descriptions() const
{
    QHash<QString, GpuResourceDescription> result;
    if (active())
        for (auto *r : m_resources)
            if (r->active())
                result.insert(r->id(), r->m_description);
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
                                                        "clear", "draws", "loadOp"})) {
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
        const auto target = pass.value("target", "surface").toString();
        if (kind == "render" && target != "surface") {
            const auto *r = m_resources.value(target);
            if (!r || r->kind() != "texture" ||
                !r->m_description.definition.value("usage").toStringList().contains(
                    "render-target")) {
                error = "Invalid render target";
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
            if (!keys(b, {"binding", "kind", "resource", "sampler", "access"}) ||
                !QStringList{"uniform", "texture", "storage-buffer", "storage-image"}.contains(
                    b.value("kind").toString()) ||
                !QStringList{"read", "write", "read-write"}.contains(
                    b.value("access", "read").toString())) {
                error = "Invalid binding fields";
                return false;
            }
            const auto index = b.value("binding");
            const auto id = b.value("resource").toString();
            if (!integer(index, 0, 31) || indices.contains(index.toInt()) ||
                (id != "frame" && !m_resources.contains(id))) {
                error = "Invalid shader binding or resource owner";
                return false;
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
            const auto draws = pass.value("draws").toList();
            if (draws.isEmpty() || draws.size() > 256) {
                error = "A render pass requires 1..256 draws";
                return false;
            }
            for (const auto &v : draws) {
                const auto d = v.toMap();
                if (!keys(d, {"count", "first", "instances", "vertexBuffer", "vertexOffset",
                              "indexBuffer", "indexFormat"}) ||
                    !integer(d.value("first", 0), 0, 1000000) ||
                    !integer(d.value("vertexOffset", 0), 0, 16 * 1024 * 1024) ||
                    !QStringList{"uint16", "uint32"}.contains(
                        d.value("indexFormat", "uint16").toString()) ||
                    !integer(d.value("count"), 1, 1000000) ||
                    !integer(d.value("instances", 1), 1, 1000000) ||
                    qint64(d.value("count").toInt()) * d.value("instances", 1).toInt() > 1000000) {
                    error = "Draw exceeds vertex budget";
                    return false;
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
