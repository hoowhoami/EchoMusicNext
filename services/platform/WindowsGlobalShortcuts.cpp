#include "GlobalShortcuts.h"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QSet>
#include <qt_windows.h>
namespace EchoMusic
{
struct GlobalShortcuts::Impl : QAbstractNativeEventFilter
{
    GlobalShortcuts &owner;
    QSet<int> keys;
    explicit Impl(GlobalShortcuts &object) : owner(object)
    {
        qApp->installNativeEventFilter(this);
    }
    void clear()
    {
        for (int id : std::as_const(keys))
            UnregisterHotKey(nullptr, id);
        keys.clear();
    }
    ~Impl()
    {
        clear();
        qApp->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        const auto msg = static_cast<MSG *>(message);
        if (msg->message != WM_HOTKEY || !keys.contains(int(msg->wParam)))
            return false;
        emit owner.activated(int(msg->wParam) - 0x4540);
        *result = 0;
        return true;
    }
};
GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this))
{
}
GlobalShortcuts::~GlobalShortcuts() = default;
void GlobalShortcuts::replace(const QList<Binding> &bindings)
{
    m_impl->clear();
    for (const auto &binding : bindings) {
        const auto combination = binding.sequence[0];
        const auto modifiers = combination.keyboardModifiers();
        UINT flags = MOD_NOREPEAT;
        if (modifiers.testFlag(Qt::ControlModifier))
            flags |= MOD_CONTROL;
        if (modifiers.testFlag(Qt::MetaModifier))
            flags |= MOD_WIN;
        if (modifiers.testFlag(Qt::AltModifier))
            flags |= MOD_ALT;
        if (modifiers.testFlag(Qt::ShiftModifier))
            flags |= MOD_SHIFT;
        const int qtKey = combination.key();
        UINT key = 0;
        if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
            key = VK_F1 + qtKey - Qt::Key_F1;
        else if ((qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) ||
                 (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z))
            key = qtKey;
        else {
            switch (qtKey) {
            case Qt::Key_Left:
                key = VK_LEFT;
                break;
            case Qt::Key_Right:
                key = VK_RIGHT;
                break;
            case Qt::Key_Up:
                key = VK_UP;
                break;
            case Qt::Key_Down:
                key = VK_DOWN;
                break;
            case Qt::Key_Space:
                key = VK_SPACE;
                break;
            case Qt::Key_Return:
                key = VK_RETURN;
                break;
            case Qt::Key_Tab:
                key = VK_TAB;
                break;
            case Qt::Key_Home:
                key = VK_HOME;
                break;
            case Qt::Key_End:
                key = VK_END;
                break;
            case Qt::Key_PageUp:
                key = VK_PRIOR;
                break;
            case Qt::Key_PageDown:
                key = VK_NEXT;
                break;
            default: {
                const auto scan = VkKeyScanW(wchar_t(qtKey));
                if (scan != -1)
                    key = LOBYTE(scan);
            }
            }
        }
        const int id = 0x4540 + binding.command;
        if (key && RegisterHotKey(nullptr, id, flags, key)) {
            m_impl->keys.insert(id);
            emit registered(binding.command);
        } else
            emit registrationFailed(
                binding.command,
                tr("系统未能注册此快捷键（%1），可能已被占用").arg(GetLastError()));
    }
}
} // namespace EchoMusic
