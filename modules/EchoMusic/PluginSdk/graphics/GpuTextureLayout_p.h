#pragma once
#include <QVariantMap>
namespace EchoMusic
{
// QRhi encodes a cube face or 3D z-slice in the same layer field as an array.
// Only a volume's slice count shrinks at successive mip levels.
inline int gpuTextureSlices(const QVariantMap &definition, int level = 0)
{
    if (level < 0 || level > 12)
        return 0;
    const auto dimension = definition.value("dimension", "2d").toString();
    if (dimension == "cube")
        return 6;
    if (dimension == "3d")
        return qMax(1, definition.value("depth").toInt() >> level);
    return definition.value("layers", 1).toInt();
}
} // namespace EchoMusic
