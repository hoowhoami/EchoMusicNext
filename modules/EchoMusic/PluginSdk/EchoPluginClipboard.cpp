#include "EchoPluginClipboard.h"
#include "EchoPluginContext.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QTimer>
namespace EchoMusic
{
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
    if (bytes.isEmpty() || bytes.size() > 8 * 1024 * 1024) {
        op->fail("LimitExceeded", "Clipboard image exceeds 8 MiB");
        return op;
    }
    // Decode on the GUI thread only for the delivery step; the encoding itself is
    // produced by the plugin (Canvas/ImageCapture) and never re-encoded here.
    op->start();
    QTimer::singleShot(0, op, [op, bytes] {
        if (op->finished())
            return;
        QImage image;
        if (!image.loadFromData(bytes) || image.isNull()) {
            op->fail("InvalidArgument", "Clipboard image data is not a supported image");
            return;
        }
        auto *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            op->fail("Unavailable", "System clipboard is unavailable");
            return;
        }
        clipboard->setImage(image, QClipboard::Clipboard);
        op->succeed();
    });
    return op;
}
} // namespace EchoMusic
