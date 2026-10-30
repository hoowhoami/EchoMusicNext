#pragma once
#include <QVariantMap>
#include <rhi/qrhi.h>
namespace EchoMusic
{
struct GpuVertexFormat
{
    QRhiVertexInputAttribute::Format native = QRhiVertexInputAttribute::Float;
    int bytes = 0, alignment = 4, components = 0, category = 0;
    bool half = false;
};
GpuVertexFormat gpuVertexFormat(const QString &name);
bool gpuNormalizeVertexLayout(QVariantMap &layout, QString &error);
inline QVariantList gpuDrawStreams(const QVariantMap &draw)
{
    if (draw.contains("vertexBuffers"))
        return draw.value("vertexBuffers").toList();
    if (draw.contains("vertexBuffer"))
        return {QVariantMap{{"buffer", draw.value("vertexBuffer")},
                            {"offset", draw.value("vertexOffset", 0)}}};
    return {};
}
} // namespace EchoMusic
