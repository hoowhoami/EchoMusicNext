#include "GpuVertexLayout_p.h"
#include "GpuRhi_p.h"
#include <rhi/qshaderdescription.h>
#include <QSet>
#include <cmath>
namespace EchoMusic
{
GpuVertexFormat gpuVertexFormat(const QString &name)
{
    using A = QRhiVertexInputAttribute;
    struct Family
    {
        const char *name;
        int unit, category;
        bool half;
        A::Format formats[4];
    };
    static const Family families[]{
        {"float", 4, 0, false, {A::Float, A::Float2, A::Float3, A::Float4}},
        {"half", 2, 0, true, {A::Half, A::Half2, A::Half3, A::Half4}},
        {"uint", 4, 1, false, {A::UInt, A::UInt2, A::UInt3, A::UInt4}},
        {"int", 4, 2, false, {A::SInt, A::SInt2, A::SInt3, A::SInt4}},
        {"uint16", 2, 1, false, {A::UShort, A::UShort2, A::UShort3, A::UShort4}},
        {"int16", 2, 2, false, {A::SShort, A::SShort2, A::SShort3, A::SShort4}}};
    for (const auto &f : families)
        for (int n = 1; n <= 4; ++n) {
            const auto suffix = n == 1                           ? QString()
                                : QString(f.name).endsWith("16") ? "x" + QString::number(n)
                                                                 : QString::number(n);
            if (name == QString(f.name) + suffix)
                return {f.formats[n - 1], f.unit * (f.unit == 2 && n == 3 ? 4 : n),
                        f.unit,           n,
                        f.category,       f.half};
        }
    if (name == "unorm8")
        return {A::UNormByte, 1, 1, 1};
    if (name == "unorm8x2")
        return {A::UNormByte2, 2, 1, 2};
    if (name == "unorm8x4")
        return {A::UNormByte4, 4, 1, 4};
    return {};
}
namespace
{
bool fields(const QVariantMap &m, const QStringList &allowed)
{
    for (auto i = m.begin(); i != m.end(); ++i)
        if (!allowed.contains(i.key()))
            return false;
    return true;
}
bool number(const QVariant &v, int low, int high)
{
    bool ok = false;
    double n = v.toDouble(&ok);
    return ok && v.metaType().id() != QMetaType::Bool && std::isfinite(n) && n == std::floor(n) &&
           n >= low && n <= high;
}
} // namespace
bool gpuNormalizeVertexLayout(QVariantMap &layout, QString &error)
{
    auto reject = [&](QString message) {
        error = std::move(message);
        return false;
    };
    if (!fields(layout, {"stride", "bindings", "attributes"}) ||
        layout.contains("stride") == layout.contains("bindings"))
        return reject("Vertex layout requires stride or bindings");
    auto bindings = layout.value("bindings").toList();
    if (layout.contains("stride"))
        bindings = {QVariantMap{{"stride", layout.value("stride")}}};
    if (bindings.isEmpty() || bindings.size() > 16)
        return reject("Vertex layout requires 1..16 streams");
    for (auto &value : bindings) {
        auto b = value.toMap();
        if (!fields(b, {"stride", "stepMode", "stepRate"}) || !number(b.value("stride"), 4, 2048) ||
            b.value("stride").toInt() % 4 ||
            !QStringList{"vertex", "instance"}.contains(b.value("stepMode", "vertex").toString()) ||
            !number(b.value("stepRate", 1), 1, 1000000) ||
            (b.value("stepMode", "vertex") == "vertex" && b.value("stepRate", 1).toInt() != 1))
            return reject("Invalid vertex stream stride, classification or step rate");
        b["stepMode"] = b.value("stepMode", "vertex");
        b["stepRate"] = b.value("stepRate", 1);
        value = b;
    }
    auto attributes = layout.value("attributes").toList();
    QSet<int> locations, used;
    if (attributes.isEmpty() || attributes.size() > 16)
        return reject("Vertex layout requires 1..16 attributes");
    for (auto &value : attributes) {
        auto a = value.toMap();
        const auto f = gpuVertexFormat(a.value("format").toString());
        if (!fields(a, {"binding", "location", "offset", "format", "matrixSlice"}) || !f.bytes ||
            !number(a.value("binding", 0), 0, int(bindings.size()) - 1) ||
            !number(a.value("location"), 0, 15) ||
            locations.contains(a.value("location").toInt()) ||
            !number(a.value("offset"), 0, 2048) || a.value("offset").toInt() % f.alignment ||
            a.value("offset").toInt() + f.bytes >
                bindings[a.value("binding", 0).toInt()].toMap().value("stride").toInt() ||
            !number(a.value("matrixSlice", -1), -1, 3) ||
            a.value("matrixSlice", -1).toInt() > a.value("location").toInt())
            return reject(
                "Invalid vertex attribute binding, format, location, matrix slice or byte range");
        a["binding"] = a.value("binding", 0);
        a["matrixSlice"] = a.value("matrixSlice", -1);
        locations.insert(a.value("location").toInt());
        used.insert(a.value("binding").toInt());
        value = a;
    }
    if (used.size() != bindings.size())
        return reject("Every vertex stream must supply an attribute");
    layout = {{"bindings", bindings}, {"attributes", attributes}};
    return true;
}
bool GpuRenderState::vertexLayout(const QVariantMap &layout, const QShader &shader,
                                  QRhiVertexInputLayout &native)
{
    const auto bindings = layout.value("bindings").toList();
    if (layout.isEmpty()) {
        if (shader.description().inputVariables().isEmpty())
            return true;
        error = "Shader requires a vertex input layout";
        return false;
    }
    QVector<QRhiVertexInputBinding> streams;
    for (const auto &v : bindings) {
        const auto b = v.toMap();
        const bool instance = b.value("stepMode") == "instance";
        if ((instance && !rhi->isFeatureSupported(QRhi::Instancing)) ||
            (b.value("stepRate").toInt() != 1 &&
             !rhi->isFeatureSupported(QRhi::CustomInstanceStepRate))) {
            error = "Instance attributes or custom instance step rate unsupported by device";
            return false;
        }
        streams.append(QRhiVertexInputBinding(b.value("stride").toUInt(),
                                              instance ? QRhiVertexInputBinding::PerInstance
                                                       : QRhiVertexInputBinding::PerVertex,
                                              b.value("stepRate").toUInt()));
    }
    const auto definitions = layout.value("attributes").toList();
    if (definitions.size() > rhi->resourceLimit(QRhi::MaxVertexInputs)) {
        error = "Vertex inputs exceed device limits";
        return false;
    }
    QMap<int, QVariantMap> attrs;
    QVector<QRhiVertexInputAttribute> attributes;
    for (const auto &v : definitions) {
        const auto a = v.toMap();
        const auto f = gpuVertexFormat(a.value("format").toString());
        if ((f.category && !rhi->isFeatureSupported(QRhi::IntAttributes)) ||
            (f.half && !rhi->isFeatureSupported(QRhi::HalfAttributes))) {
            error = "Integer or half attributes unsupported by device";
            return false;
        }
        attrs.insert(a.value("location").toInt(), a);
        attributes.append({a.value("binding").toInt(), a.value("location").toInt(), f.native,
                           a.value("offset").toUInt(), a.value("matrixSlice").toInt()});
    }
    for (const auto &input : shader.description().inputVariables()) {
        int columns = 1, rows = 0, category = 0;
        using D = QShaderDescription;
        switch (input.type) {
        case D::Float:
            rows = 1;
            break;
        case D::Vec2:
            rows = 2;
            break;
        case D::Vec3:
            rows = 3;
            break;
        case D::Vec4:
            rows = 4;
            break;
        case D::Int:
            rows = 1;
            category = 2;
            break;
        case D::Int2:
            rows = 2;
            category = 2;
            break;
        case D::Int3:
            rows = 3;
            category = 2;
            break;
        case D::Int4:
            rows = 4;
            category = 2;
            break;
        case D::Uint:
            rows = 1;
            category = 1;
            break;
        case D::Uint2:
            rows = 2;
            category = 1;
            break;
        case D::Uint3:
            rows = 3;
            category = 1;
            break;
        case D::Uint4:
            rows = 4;
            category = 1;
            break;
        case D::Mat2:
            columns = rows = 2;
            break;
        case D::Mat3:
            columns = rows = 3;
            break;
        case D::Mat4:
            columns = rows = 4;
            break;
        case D::Mat2x3:
            columns = 2;
            rows = 3;
            break;
        case D::Mat2x4:
            columns = 2;
            rows = 4;
            break;
        case D::Mat3x2:
            columns = 3;
            rows = 2;
            break;
        case D::Mat3x4:
            columns = 3;
            rows = 4;
            break;
        case D::Mat4x2:
            columns = 4;
            rows = 2;
            break;
        case D::Mat4x3:
            columns = 4;
            rows = 3;
            break;
        default:
            break;
        }
        if (!rows || !input.arrayDims.isEmpty()) {
            error = "Unsupported vertex input shape";
            return false;
        }
        for (int c = 0; c < columns; ++c) {
            const auto a = attrs.value(input.location + c);
            const auto f = gpuVertexFormat(a.value("format").toString());
            if (!f.bytes || f.category != category ||
                (columns > 1 && (f.components != rows || a.value("matrixSlice").toInt() != c)) ||
                (columns == 1 && a.value("matrixSlice").toInt() != -1)) {
                error =
                    "Vertex input reflection differs from attribute scalar type or matrix columns";
                return false;
            }
        }
    }
    native.setBindings(streams.cbegin(), streams.cend());
    native.setAttributes(attributes.cbegin(), attributes.cend());
    return true;
}
bool GpuRenderState::vertexInputs(const QVariantMap &draw, const QVariantMap &layout,
                                  QVector<QRhiCommandBuffer::VertexInput> &inputs)
{
    const auto streams = gpuDrawStreams(draw), bindings = layout.value("bindings").toList();
    if (streams.size() != bindings.size()) {
        error = "Draw vertex stream count differs from pipeline";
        return false;
    }
    for (int n = 0; n < streams.size(); ++n) {
        const auto stream = streams[n].toMap(), b = bindings[n].toMap();
        const auto found = resources.find(stream.value("buffer").toString());
        if (found == resources.end() || !found->second->buffer ||
            !found->second->buffer->usage().testFlag(QRhiBuffer::VertexBuffer)) {
            error = "Draw requires live vertex buffers";
            return false;
        }
        qint64 records = 1;
        if (!draw.contains("indirect")) {
            if (b.value("stepMode") == "instance")
                // The base offsets records after applying the instance divisor.
                records =
                    draw.value("firstInstance", 0).toLongLong() +
                    (draw.value("instances", 1).toLongLong() - 1) / b.value("stepRate").toInt() + 1;
            else if (!draw.contains("indexBuffer"))
                records = qint64(draw.value("first", 0).toInt()) + draw.value("count").toInt();
        }
        int tail = 0;
        for (const auto &value : layout.value("attributes").toList()) {
            const auto a = value.toMap();
            if (a.value("binding").toInt() == n)
                tail = qMax(tail, a.value("offset").toInt() +
                                      gpuVertexFormat(a.value("format").toString()).bytes);
        }
        const auto offset = stream.value("offset", 0).toUInt();
        if (quint64(offset) + quint64(records - 1) * b.value("stride").toUInt() + tail >
            quint64(found->second->buffer->size())) {
            error = "Vertex or instance stream range exceeds buffer";
            return false;
        }
        inputs.append({found->second->buffer.get(), offset});
    }
    return true;
}
} // namespace EchoMusic
