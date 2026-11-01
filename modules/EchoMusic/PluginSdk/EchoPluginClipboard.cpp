#include "EchoPluginClipboard.h"
#include "EchoPluginContext.h"
#include <QClipboard>
#include <QGuiApplication>
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
} // namespace EchoMusic
