#include "StorageFence.h"
#include "ThemeImageStore.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>

namespace EchoMusic
{
namespace
{
QColor average(const QImage &input)
{
    const auto image = input.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGBA8888);
    double red = 0, green = 0, blue = 0, weight = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const auto *pixel = line + x * 4;
            const auto alpha = pixel[3] / 255.0;
            red += pixel[0] * alpha;
            green += pixel[1] * alpha;
            blue += pixel[2] * alpha;
            weight += alpha;
        }
    }
    return weight ? QColor(qRound(red / weight), qRound(green / weight), qRound(blue / weight))
                  : QColor("#6b9bd1");
}
} // namespace
bool ThemeImageStore::validId(const QString &id)
{
    static const QRegularExpression pattern("^[a-f0-9]{64}\\.png$");
    return pattern.match(id).hasMatch();
}
ThemeImageResult ThemeImageStore::normalize(const QString &source, const QString &directory)
{
    const QFileInfo file(source);
    if (!file.isFile() || file.size() <= 0 || file.size() > 20 * 1024 * 1024)
        return {{}, QObject::tr("请选择不超过 20 MB 的图片"), {}, {}};
    QImageReader reader(source);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "webp")
        return {{}, QObject::tr("仅支持 JPG、PNG、WebP 图片"), {}, {}};
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 100000000)
        return {{}, QObject::tr("图片尺寸无效或超过 1 亿像素，请先缩小图片"), {}, {}};
    if (std::max(size.width(), size.height()) > 2560)
        reader.setScaledSize(size.scaled(2560, 2560, Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull())
        return {{}, QObject::tr("图片无法解码，请重新导出为 JPG、PNG 或 WebP"), {}, {}};
    if (std::max(image.width(), image.height()) > 2560)
        image = image.scaled(2560, 2560, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // The reference normalizes through a browser sRGB canvas. Keep the managed
    // asset in that display space and 8-bit RGBA, including tagged photographs.
    image = image.colorSpace().isValid()
                ? image.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)
                : image.convertToFormat(QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace::SRgb);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || bytes.size() > 32 * 1024 * 1024)
        return {{}, QObject::tr("图片处理失败，请重新选择"), {}, {}};
    const auto id =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
        ".png";
    if (!QDir().mkpath(directory))
        return {{}, QObject::tr("无法创建背景图片目录"), {}, {}};
    QSaveFile target(QDir(directory).filePath(id));
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() ||
        !target.commit())
        return {{}, QObject::tr("背景图片保存失败"), {}, {}};
    return {id, {}, image.size(), average(image)};
}
QColor ThemeImageStore::sample(const QString &directory, const QString &id, const QRectF &crop)
{
    if (!validId(id))
        return {};
    QImageReader reader(QDir(directory).filePath(id));
    if (!crop.isEmpty())
        reader.setClipRect(crop.toAlignedRect());
    const auto image = reader.read();
    return image.isNull() ? QColor() : average(image);
}
void ThemeImageStore::clean(const QString &directory, const QStringList &retained)
{
    const auto storage = StorageFence::write(directory);
    if (!storage)
        return;
    QDir folder(directory);
    // Runs only with no imports pending, on the same serialized image worker.
    // These private directories can remain only after process termination.
    for (const auto &name : folder.entryList(
             {".import-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks))
        QDir(folder.filePath(name)).removeRecursively();
    for (const auto &id : folder.entryList({"*.png"}, QDir::Files))
        if (validId(id) && !retained.contains(id))
            folder.remove(id);
}
} // namespace EchoMusic
