#include "CloudUploadFile.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
namespace EchoMusic::CloudUploadFile
{
Part readPart(const Snapshot &snapshot, int part, const std::atomic_bool &canceled)
{
    if (canceled.load())
        return {};
    if (part < 1 || part > 100 || snapshot.size <= 0 || snapshot.size > 100 * PartBytes)
        return {{}, QStringLiteral("上传文件大小或分片无效")};
    QFile file(snapshot.path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, QStringLiteral("无法读取上传文件")};
    const auto unchanged = [&] {
        const QFileInfo opened(file), path(snapshot.path);
        return opened.isFile() && path.isFile() && opened.size() == snapshot.size &&
               path.size() == snapshot.size &&
               opened.lastModified().toMSecsSinceEpoch() == snapshot.modified &&
               path.lastModified().toMSecsSinceEpoch() == snapshot.modified;
    };
    if (!unchanged())
        return {{}, QStringLiteral("上传文件已变化，请重新选择")};
    const auto offset = qint64(part - 1) * PartBytes;
    if (offset >= snapshot.size || !file.seek(offset))
        return {{}, QStringLiteral("无效的上传分片")};
    const auto expected = qMin(PartBytes, snapshot.size - offset);
    const auto bytes = file.read(expected);
    if (canceled.load())
        return {};
    if (bytes.size() != expected || !unchanged())
        return {{}, QStringLiteral("上传文件已变化或读取失败，请重新选择")};
    return {bytes.toBase64(), {}};
}
} // namespace EchoMusic::CloudUploadFile
