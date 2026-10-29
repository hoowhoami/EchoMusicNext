#include <QGuiApplication>
#include <QColorSpace>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QImageWriter>
#include <QtEndian>
#include "AssetSvgValidation.h"
#include "echo_ffi.h"
#include <QCryptographicHash>
#include <cstdio>

namespace
{
QJsonObject error(const QString &message)
{
    return {{"version", 1}, {"error", message}};
}
class BoundedOutput final : public QIODevice
{
public:
    BoundedOutput(const QString &path, qint64 maximum) : file(path), limit(maximum)
    {
        if (limit > 0 && file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            open(QIODevice::WriteOnly);
    }
    ~BoundedOutput() override
    {
        file.close();
        if (!kept)
            file.remove();
    }
    bool isSequential() const override
    {
        return true;
    }
    qint64 bytes = 0;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    bool kept = false;

protected:
    qint64 readData(char *, qint64) override
    {
        return -1;
    }
    qint64 writeData(const char *data, qint64 length) override
    {
        if (length < 0 || length > limit - bytes)
            return -1;
        const auto count = file.write(data, length);
        if (count > 0) {
            hash.addData(QByteArrayView(data, count));
            bytes += count;
        }
        return count;
    }

private:
    QFile file;
    qint64 limit;
};
QJsonObject svg(const QByteArray &source, const QString &mode, const QString &output, int width,
                int height)
{
    QImage image;
    QString problem;
    if (!EchoMusic::AssetSvgValidation::render(source, image, problem))
        return error(problem);
    const auto original = image.size();
    QJsonObject result{{"version", 1},
                       {"extension", "svg"},
                       {"mimeType", "image/svg+xml"},
                       {"width", image.width()},
                       {"height", image.height()},
                       {"sourceWidth", image.width()},
                       {"sourceHeight", image.height()},
                       {"animated", false},
                       {"frameCount", 1},
                       {"durationMs", 0}};
    if (mode == "sample") {
        if (width < 1 || width > 256 || height < 1 || height > 256)
            return error("Sample dimensions must be 1..256");
        image = image.convertToFormat(QImage::Format_RGBA8888)
                    .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        BoundedOutput file(output, width * height * 4);
        if (!file.isOpen())
            return error("Cannot write SVG sample");
        for (int y = 0; y < height; ++y)
            if (file.write(reinterpret_cast<const char *>(image.constScanLine(y)), width * 4) !=
                width * 4)
                return error("Cannot write SVG sample");
        file.kept = true;
        result["width"] = width;
        result["height"] = height;
        return result;
    }
    const bool sequence = mode == "frames" || mode == "frames-cover";
    if (sequence) {
        if (width < 1 || width > 4096 || height < 1 || height > 4096 || !QDir().mkpath(output))
            return error("Animation dimensions must be 1..4096");
        auto fitted = original.scaled(QSize(width, height), mode == "frames-cover"
                                                                ? Qt::KeepAspectRatioByExpanding
                                                                : Qt::KeepAspectRatio);
        if (fitted.width() > original.width() || fitted.height() > original.height())
            fitted = original;
        image = image.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    const auto path = sequence ? QDir(output).filePath("0.png") : output;
    const qint64 budget = sequence ? 64 * 1024 * 1024 : width;
    if (budget <= 0 || budget > 64 * 1024 * 1024)
        return error("SVG PNG derivative has no remaining byte budget");
    BoundedOutput file(path, budget);
    QImageWriter writer(&file, "png");
    if (!file.isOpen() || !writer.write(image))
        return error("SVG PNG derivative exceeds its byte budget or cannot be encoded");
    file.kept = true;
    if (sequence) {
        result["width"] = image.width();
        result["height"] = image.height();
        result["delays"] = QJsonArray{1};
        result["loopCount"] = 0;
        result["durationMs"] = 1;
        result["encodedBytes"] = file.bytes;
    } else {
        result["renderBytes"] = file.bytes;
        result["renderSha256"] = QString::fromLatin1(file.hash.result().toHex());
    }
    return result;
}
QJsonObject inspect(const QStringList &arguments)
{
    if (arguments.size() != 6)
        return error("Invalid worker request");
    const auto mode = arguments[1], source = arguments[2], output = arguments[3];
    const int width = arguments[4].toInt(), height = arguments[5].toInt();
    if (mode == "archive") {
        char message[1024]{};
        if (!echo_asset_archive(source.toUtf8().constData(), output.toUtf8().constData(),
                                reinterpret_cast<uint8_t *>(message), sizeof(message)))
            return error(QString::fromUtf8(message));
        QFile metadata(output);
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 2 * 1024 * 1024)
            return error("Invalid archive metadata result");
        const auto bytes = metadata.read(2 * 1024 * 1024 + 1);
        return {{"version", 1},
                {"metadataBytes", bytes.size()},
                {"metadataSha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    }
    const QFileInfo info(source);
    const bool font = mode == "font";
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 ||
        info.size() > (font ? 16 : 8) * 1024 * 1024)
        return error("Asset file exceeds its byte budget");
    if (font) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly))
            return error("Cannot read font");
        const auto bytes = file.read(16 * 1024 * 1024 + 1);
        const auto signature = bytes.first(4);
        if (signature != QByteArray::fromHex("00010000") && signature != "OTTO" &&
            signature != "ttcf" && signature != "true")
            return error("Only TrueType, OpenType and font collections are supported");
        const int id = QFontDatabase::addApplicationFontFromData(bytes);
        if (id < 0)
            return error("Font cannot be decoded");
        const auto families = QFontDatabase::applicationFontFamilies(id);
        QFontDatabase::removeApplicationFont(id);
        if (families.isEmpty() || families.size() > 32)
            return error("Font families exceed the supported budget");
        return {{"version", 1},
                {"extension", signature == "OTTO"   ? "otf"
                              : signature == "ttcf" ? "ttc"
                                                    : "ttf"},
                {"mimeType", signature == "OTTO" ? "font/otf" : "font/ttf"},
                {"families", QJsonArray::fromStringList(families)}};
    }
    if (mode != "image" && mode != "sample" && mode != "frames" && mode != "frames-cover")
        return error("Unknown asset request");
    QFile possibleXml(source);
    if (!possibleXml.open(QIODevice::ReadOnly))
        return error("Cannot read image");
    auto header = possibleXml.read(4096);
    if (header.startsWith(QByteArray::fromHex("efbbbf")))
        header.remove(0, 3);
    while (header.trimmed().isEmpty() && !possibleXml.atEnd())
        header += possibleXml.read(4096);
    if (header.trimmed().startsWith('<') || header.startsWith(QByteArray::fromHex("fffe")) ||
        header.startsWith(QByteArray::fromHex("feff")) ||
        header.startsWith(QByteArray::fromHex("0000feff")) ||
        header.startsWith(QByteArray::fromHex("003c003f")) ||
        header.startsWith(QByteArray::fromHex("3c003f00"))) {
        possibleXml.seek(0);
        return svg(possibleXml.read(8 * 1024 * 1024 + 1), mode, output, width, height);
    }
    const bool sequence = mode == "frames" || mode == "frames-cover";
    if (sequence &&
        (width < 1 || height < 1 || width > 4096 || height > 4096 || !QDir().mkpath(output)))
        return error("Animation dimensions must be 1..4096");
    QImageReader reader(source);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "gif" && format != "webp")
        return error("Only PNG, JPEG, GIF and WebP are supported");
    // Some codecs return a last frame for a truncated stream. Retaining the
    // original animation requires a complete container, not just that frame.
    QFile container(source);
    if (!container.open(QIODevice::ReadOnly))
        return error("Cannot verify image container");
    if (format == "gif") {
        if (!container.seek(container.size() - 1) || container.read(1) != QByteArray(1, '\x3b'))
            return error("GIF animation is missing its trailer");
    } else if (format == "webp") {
        const auto header = container.read(12);
        if (header.size() != 12 || header.first(4) != "RIFF" || header.mid(8, 4) != "WEBP" ||
            quint64(qFromLittleEndian<quint32>(header.constData() + 4)) + 8 != quint64(info.size()))
            return error("WebP animation container is incomplete");
    }
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096 ||
        qint64(size.width()) * size.height() > 16 * 1024 * 1024)
        return error("Image exceeds 4096 pixels per edge or 16M pixels");
    const int expected = reader.imageCount();
    if (expected > 256)
        return error("Animation exceeds 256 frames");
    QImage first;
    qint64 pixels = 0, duration = 0;
    qint64 encodedBytes = 0;
    QJsonArray delays;
    QSize frameSize;
    const int loops = reader.loopCount();
    int frames = 0;
    while (reader.canRead()) {
        auto image = reader.read();
        if (image.isNull())
            return error("Image or animation frame cannot be decoded");
        pixels += qint64(image.width()) * image.height();
        if (++frames > 256 || pixels > 64 * 1024 * 1024 || image.width() > 4096 ||
            image.height() > 4096)
            return error("Animation exceeds its frame or pixel budget");
        duration += qMax(0, reader.nextImageDelay());
        if (frames == 1)
            first = image;
        if (sequence) {
            auto frame =
                image.colorSpace().isValid()
                    ? image.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)
                    : image.convertToFormat(QImage::Format_RGBA8888);
            auto fitted = frame.size().scaled(
                QSize(width, height),
                mode == "frames-cover" ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            if (fitted.width() > frame.width() || fitted.height() > frame.height())
                fitted = frame.size();
            frame = frame.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (!frameSize.isValid())
                frameSize = frame.size();
            if (frame.size() != frameSize)
                return error("Animation canvas changed between frames");
            const auto path = QDir(output).filePath(QString::number(frames - 1) + ".png");
            QByteArray encoded;
            QBuffer buffer(&encoded);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "png");
            if (!writer.write(frame))
                return error("Cannot encode animation frame");
            encodedBytes += encoded.size();
            if (encodedBytes > 64 * 1024 * 1024)
                return error("Animation PNG cache exceeds 64 MiB");
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                file.write(encoded) != encoded.size())
                return error("Cannot write animation frame");
            // GIF's codec supplies its specified browser-compatible minimum delay.
            delays.append(qMax(1, reader.nextImageDelay()));
        }
        if (mode == "sample" || !reader.supportsAnimation())
            break;
    }
    if (first.isNull() || ((mode == "image" || sequence) && expected > 0 && frames != expected))
        return error("Animation ended before all frames could be decoded");
    QJsonObject result{{"version", 1},
                       {"extension", format == "jpeg" ? "jpg" : QString::fromLatin1(format)},
                       {"mimeType", "image/" + QString::fromLatin1(format)},
                       {"width", first.width()},
                       {"height", first.height()},
                       {"sourceWidth", first.width()},
                       {"sourceHeight", first.height()},
                       {"animated", frames > 1},
                       {"frameCount", frames},
                       {"durationMs", duration}};
    if (sequence) {
        result["width"] = frameSize.width();
        result["height"] = frameSize.height();
        result["delays"] = delays;
        result["loopCount"] = loops;
        result["encodedBytes"] = encodedBytes;
    }
    if (mode == "sample") {
        if (width < 1 || width > 256 || height < 1 || height > 256)
            return error("Sample dimensions must be 1..256");
        auto image = first.colorSpace().isValid()
                         ? first.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)
                         : first.convertToFormat(QImage::Format_RGBA8888);
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly))
            return error("Cannot write sampled pixels");
        for (int y = 0; y < image.height(); ++y)
            if (file.write(reinterpret_cast<const char *>(image.constScanLine(y)), width * 4) !=
                width * 4)
                return error("Cannot write sampled pixels");
        result["width"] = width;
        result["height"] = height;
    }
    return result;
}
} // namespace

int main(int argc, char **argv)
{
    // Qt caches the environment override before consulting setAllocationLimit.
    // Fix it before application/plugin startup so inherited 0 cannot disable it.
    qputenv("QT_IMAGEIO_MAXALLOC", "128");
    QGuiApplication app(argc, argv);
    // The allocation limit is process-global in Qt and never changed in the host.
    QImageReader::setAllocationLimit(128);
    const auto bytes = QJsonDocument(inspect(app.arguments())).toJson(QJsonDocument::Compact);
    return std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout) == size_t(bytes.size())
               ? 0
               : 3;
}
