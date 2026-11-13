#include "EchoPluginClipboard.h"
#include "EchoPluginContext.h"
#include <QBuffer>
#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTimer>
#include <QtConcurrent>
namespace EchoMusic
{
namespace
{
constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxImagePixels = 16 * 1024 * 1024;
constexpr int kMaxImageEdge = 4096;

struct DecodedImage
{
    QImage image;
    QString errorCode;
    QString errorMessage;
};

DecodedImage decodeImage(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {{},
                QStringLiteral("Unavailable"),
                QStringLiteral("Clipboard image buffer is unavailable")};
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    // QImageReader's allocation limit is process-global, so this request relies on
    // its own edge/pixel budget instead of changing state for concurrent decodes.
    // Read the declared size before decoding: a ~200 KiB PNG can still describe
    // an 8000x8000 image that expands to hundreds of MiB.
    const auto size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return {{},
                QStringLiteral("InvalidArgument"),
                QStringLiteral("Clipboard image data is not a supported image")};
    if (size.width() > kMaxImageEdge || size.height() > kMaxImageEdge ||
        qint64(size.width()) * size.height() > kMaxImagePixels)
        return {{},
                QStringLiteral("LimitExceeded"),
                QStringLiteral("Clipboard image exceeds 4096 pixels per edge or 16M pixels")};
    const auto image = reader.read();
    if (image.isNull())
        return {{},
                QStringLiteral("InvalidArgument"),
                QStringLiteral("Clipboard image data is not a supported image")};
    // A decoder that ignored the declared header size must not escape the budget.
    if (image.width() > kMaxImageEdge || image.height() > kMaxImageEdge ||
        qint64(image.width()) * image.height() > kMaxImagePixels)
        return {{},
                QStringLiteral("LimitExceeded"),
                QStringLiteral("Clipboard image exceeds 4096 pixels per edge or 16M pixels")};
    return {image, {}, {}};
}
} // namespace
EchoPluginClipboard::EchoPluginClipboard(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
}
EchoPluginOperation *EchoPluginClipboard::write(const QString &text)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (text.toUtf8().size() > 1024 * 1024) {
        op->fail("LimitExceeded", "Clipboard text exceeds 1 MiB");
        return op;
    }
    op->start();
    QTimer::singleShot(0, op, [op, text] {
        if (op->finished())
            return;
        auto *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            op->fail("Unavailable", "System clipboard is unavailable");
            return;
        }
        clipboard->setText(text, QClipboard::Clipboard);
        op->succeed();
    });
    return op;
}
EchoPluginOperation *EchoPluginClipboard::writeImage(const QByteArray &bytes)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (bytes.isEmpty()) {
        op->fail("InvalidArgument", "Clipboard image data is empty");
        return op;
    }
    if (bytes.size() > kMaxImageBytes) {
        op->fail("LimitExceeded", "Clipboard image exceeds 8 MiB");
        return op;
    }
    op->start();
    // Decode on the worker pool, never in the GUI callback. The watcher is parented
    // to the operation so a canceled/deactivated scope drops the delivery entirely;
    // cancellation is also re-checked before the clipboard handoff.
    auto *watcher = new QFutureWatcher<DecodedImage>(op);
    QObject::connect(watcher, &QFutureWatcher<DecodedImage>::finished, op, [op, watcher] {
        const auto decoded = watcher->result();
        watcher->deleteLater();
        if (op->finished())
            return;
        if (!decoded.errorCode.isEmpty()) {
            op->fail(decoded.errorCode, decoded.errorMessage);
            return;
        }
        auto *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            op->fail("Unavailable", "System clipboard is unavailable");
            return;
        }
        clipboard->setImage(decoded.image, QClipboard::Clipboard);
        op->succeed();
    });
    watcher->setFuture(QtConcurrent::run([bytes] { return decodeImage(bytes); }));
    return op;
}
} // namespace EchoMusic
