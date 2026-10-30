#include "EchoPluginGraphics.h"
#include "GpuTextureLayout_p.h"
#include "EchoPluginContext.h"
#include "EchoPluginOperation.h"
#include <cmath>
#include <QSet>
namespace EchoMusic
{
GpuSubmission::~GpuSubmission()
{
    if (budget) {
        budget->submissions.release(bytes);
        --budget->pendingSubmissions;
        if (retired)
            retired();
    }
}
namespace
{
bool integral(const QVariant &v, int lo, int hi)
{
    bool ok = false;
    const double n = v.toDouble(&ok);
    return ok && v.metaType().id() != QMetaType::Bool && std::isfinite(n) && n == std::floor(n) &&
           n >= lo && n <= hi;
}
int pixelBytes(const QVariantMap &d)
{
    const auto f = d.value("format").toString();
    return f == "rgba32f" ? 16 : f == "rgba16f" ? 8 : 4;
}
} // namespace
bool EchoPluginGraphics::prepareSubmission(const QVariantMap &input, QVariantMap &output,
                                           QString &error)
{
    auto reject = [&](const QString &message) {
        error = message;
        return false;
    };
    if (!active() || input.size() != 1 || !input.contains("passes"))
        return reject("Invalid GPU command list or retired owner");
    const auto passes = input.value("passes").toList();
    if (passes.isEmpty() || passes.size() > 32)
        return reject("A submission requires 1..32 commands");
    QVariantList normalized;
    qint64 uploads = 0;
    QSet<QString> consumedUniforms;
    for (const auto &value : passes) {
        auto p = value.toMap();
        const auto kind = p.value("type", "render").toString();
        if (kind == "render" || kind == "compute") {
            if (!validateFrame({{"passes", QVariantList{p}}}, error))
                return false;
            for (const auto &value : p.value("bindings").toList()) {
                const auto b = value.toMap();
                if (b.value("kind") == "uniform")
                    consumedUniforms.insert(b.value("resource").toString());
            }
            normalized.append(p);
            continue;
        }
        QStringList allowed{"type"};
        if (kind == "copy-texture")
            allowed += QStringList{
                "source",          "destination",       "sourceLevel", "destinationLevel",
                "sourceOrigin",    "destinationOrigin", "size",        "sourceLayer",
                "destinationLayer"};
        else if (kind == "copy-buffer")
            allowed +=
                QStringList{"source", "destination", "sourceOffset", "destinationOffset", "size"};
        else if (kind == "upload-texture")
            allowed += QStringList{"destination", "level", "origin", "size",
                                   "bytesPerRow", "data",  "layer"};
        else if (kind == "upload-buffer")
            allowed += QStringList{"destination", "offset", "data"};
        else if (kind == "generate-mips")
            allowed += QStringList{"texture"};
        else
            return reject("Unknown GPU command");
        for (auto it = p.begin(); it != p.end(); ++it)
            if (!allowed.contains(it.key()))
                return reject("Unknown GPU command field");
        const auto targetId =
            p.value(kind == "generate-mips" ? "texture" : "destination").toString();
        auto *dst = m_resources.value(targetId);
        auto *src = m_resources.value(p.value("source").toString());
        if (!dst || !dst->active() || (kind.startsWith("copy-") && (!src || src == dst)))
            return reject("Transfer requires distinct live resources from this scope");
        const auto &dd = dst->m_description.definition;
        if (kind == "generate-mips") {
            if (dst->kind() != "texture" || !dd.value("mipmaps").toBool())
                return reject("Mipmap generation requires a mipmapped texture");
        } else if (kind.endsWith("buffer")) {
            if (dst->kind() != "buffer" || (src && src->kind() != "buffer"))
                return reject("Invalid transfer buffer");
            if (kind == "copy-buffer") {
                const auto &sd = src->m_description.definition;
                if (!dd.value("usage").toStringList().contains("storage") ||
                    !sd.value("usage").toStringList().contains("storage"))
                    return reject("GPU buffer copies require storage usage on both buffers");
                for (const auto &key : {"sourceOffset", "destinationOffset", "size"}) {
                    const auto v = p.value(key, key == QString("size") ? QVariant() : QVariant(0));
                    if (!integral(v, key == QString("size") ? 4 : 0, 16 * 1024 * 1024) ||
                        v.toInt() % 4)
                        return reject("Buffer copy ranges must be positive and four-byte aligned");
                    p[key] = v.toInt();
                }
                if (p.value("sourceOffset").toLongLong() + p.value("size").toInt() >
                        sd.value("size").toInt() ||
                    p.value("destinationOffset").toLongLong() + p.value("size").toInt() >
                        dd.value("size").toInt())
                    return reject("Buffer copy exceeds resource range");
            } else {
                if (dd.value("usage").toStringList().contains("uniform") &&
                    consumedUniforms.contains(targetId))
                    return reject("Upload uniforms before their first read in a submission");
                const auto offset = p.value("offset", 0);
                if (!integral(offset, 0, dd.value("size").toInt()) || offset.toInt() % 4)
                    return reject("Invalid buffer upload offset");
                m_error.clear();
                const auto data =
                    bytes(m_scope.engine().toScriptValue(p.value("data")),
                          dd.value("format").toString(), dd.value("size").toInt() - offset.toInt());
                if (!m_error.isEmpty() || data.isEmpty() || data.size() % 4)
                    return reject(m_error.isEmpty() ? "Buffer upload requires aligned bytes"
                                                    : m_error);
                p["data"] = data;
                p["offset"] = offset.toInt();
                uploads += data.size();
            }
        } else {
            if (dst->kind() != "texture" || (src && src->kind() != "texture"))
                return reject("Invalid transfer texture");
            const auto &sd = src ? src->m_description.definition : dd;
            if (src && sd.value("format") != dd.value("format"))
                return reject("Texture copy formats must match");
            const auto mipSize = [&](const QVariantMap &d, const QString &key, QSize &size) {
                const auto level = p.value(key, 0);
                if (!integral(level, 0, 12) || (level.toInt() && !d.value("mipmaps").toBool()))
                    return false;
                const int maximum = int(std::floor(
                    std::log2(qMax(d.value("width").toInt(), d.value("height").toInt()))));
                if (level.toInt() > maximum)
                    return false;
                size = {qMax(1, d.value("width").toInt() >> level.toInt()),
                        qMax(1, d.value("height").toInt() >> level.toInt())};
                p[key] = level.toInt();
                return true;
            };
            QSize ds, ss;
            if (!mipSize(dd, src ? "destinationLevel" : "level", ds) ||
                (src && !mipSize(sd, "sourceLevel", ss)))
                return reject("Invalid texture mip level");
            for (const auto &key :
                 src ? QStringList{"sourceLayer", "destinationLayer"} : QStringList{"layer"}) {
                const auto &d = key == "sourceLayer" ? sd : dd;
                if (!integral(p.value(key, 0), 0,
                              gpuTextureSlices(d, p.value(key == "sourceLayer" ? "sourceLevel"
                                                          : src                ? "destinationLevel"
                                                                               : "level")
                                                      .toInt()) -
                                  1))
                    return reject("Invalid texture transfer face, layer or volume slice");
                p[key] = p.value(key, 0).toInt();
            }
            const auto extent = p.value("size").toList();
            if (extent.size() != 2 || !integral(extent[0], 1, 4096) ||
                !integral(extent[1], 1, 4096))
                return reject("Invalid transfer extent");
            for (const auto &key :
                 src ? QStringList{"sourceOrigin", "destinationOrigin"} : QStringList{"origin"}) {
                const auto origin = p.value(key, QVariantList{0, 0}).toList();
                const auto size = key == "sourceOrigin" ? ss : ds;
                if (origin.size() != 2 || !integral(origin[0], 0, 4096) ||
                    !integral(origin[1], 0, 4096) ||
                    origin[0].toInt() + extent[0].toInt() > size.width() ||
                    origin[1].toInt() + extent[1].toInt() > size.height())
                    return reject("Texture transfer exceeds subresource range");
                p[key] = origin;
            }
            if (!src) {
                const int row = extent[0].toInt() * pixelBytes(dd);
                const auto stride = p.value("bytesPerRow", row);
                if (!integral(stride, row, 65536) || stride.toInt() % pixelBytes(dd))
                    return reject("Invalid texture row stride");
                const int size = stride.toInt() * extent[1].toInt();
                if (size > 16 * 1024 * 1024)
                    return reject("Texture upload exceeds 16 MiB");
                const auto f = dd.value("format").toString();
                m_error.clear();
                const auto data = bytes(m_scope.engine().toScriptValue(p.value("data")),
                                        f == "rgba8"     ? "uint8"
                                        : f == "rgba16f" ? "float16"
                                                         : "float32",
                                        size);
                if (!m_error.isEmpty() || data.size() != size)
                    return reject("Texture upload byte length does not match row stride/extent");
                p["data"] = data;
                p["bytesPerRow"] = stride.toInt();
                uploads += data.size();
            }
        }
        if (uploads > 32 * 1024 * 1024)
            return reject("Submission upload budget exceeded");
        normalized.append(p);
    }
    output = {{"passes", normalized}};
    return true;
}
} // namespace EchoMusic
