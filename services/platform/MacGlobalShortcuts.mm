#include "GlobalShortcuts.h"
#include <Carbon/Carbon.h>
#include <QHash>

namespace EchoMusic
{
namespace
{
int nativeKey(int key)
{
    switch (key) {
    case Qt::Key_Space:
        return kVK_Space;
    case Qt::Key_Left:
        return kVK_LeftArrow;
    case Qt::Key_Right:
        return kVK_RightArrow;
    case Qt::Key_Up:
        return kVK_UpArrow;
    case Qt::Key_Down:
        return kVK_DownArrow;
    case Qt::Key_Return:
        return kVK_Return;
    case Qt::Key_Tab:
        return kVK_Tab;
    case Qt::Key_Home:
        return kVK_Home;
    case Qt::Key_End:
        return kVK_End;
    case Qt::Key_PageUp:
        return kVK_PageUp;
    case Qt::Key_PageDown:
        return kVK_PageDown;
    case Qt::Key_F1:
        return kVK_F1;
    case Qt::Key_F2:
        return kVK_F2;
    case Qt::Key_F3:
        return kVK_F3;
    case Qt::Key_F4:
        return kVK_F4;
    case Qt::Key_F5:
        return kVK_F5;
    case Qt::Key_F6:
        return kVK_F6;
    case Qt::Key_F7:
        return kVK_F7;
    case Qt::Key_F8:
        return kVK_F8;
    case Qt::Key_F9:
        return kVK_F9;
    case Qt::Key_F10:
        return kVK_F10;
    case Qt::Key_F11:
        return kVK_F11;
    case Qt::Key_F12:
        return kVK_F12;
    case Qt::Key_F13:
        return kVK_F13;
    case Qt::Key_F14:
        return kVK_F14;
    case Qt::Key_F15:
        return kVK_F15;
    case Qt::Key_F16:
        return kVK_F16;
    case Qt::Key_F17:
        return kVK_F17;
    case Qt::Key_F18:
        return kVK_F18;
    case Qt::Key_F19:
        return kVK_F19;
    case Qt::Key_F20:
        return kVK_F20;
    default:
        break;
    }
    // Resolve printable keys using the active hardware layout, rather than
    // pretending every keyboard uses the US ANSI layout.
    auto source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source)
        return -1;
    auto data =
        static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (!data) {
        // Input methods can omit layout data; ask macOS for the keyboard layout
        // used for ASCII shortcuts rather than substituting a hard-coded map.
        CFRelease(source);
        source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
        if (!source)
            return -1;
        data = static_cast<CFDataRef>(
            TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    }
    int result = -1;
    if (data) {
        auto layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        for (UInt16 code = 0; code < 128; ++code) {
            UInt32 state = 0;
            UniChar text[4] = {};
            UniCharCount length = 0;
            if (UCKeyTranslate(layout, code, kUCKeyActionDisplay, 0, LMGetKbdType(),
                               kUCKeyTranslateNoDeadKeysBit, &state, 4, &length, text) == noErr &&
                length == 1 && QChar(text[0]).toUpper().unicode() == key) {
                result = code;
                break;
            }
        }
    }
    CFRelease(source);
    return result;
}
} // namespace
struct GlobalShortcuts::Impl
{
    GlobalShortcuts &owner;
    EventHandlerRef handler = nullptr;
    QHash<int, EventHotKeyRef> keys;
    explicit Impl(GlobalShortcuts &object) : owner(object)
    {
        EventTypeSpec event{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(
            [](EventHandlerCallRef, EventRef event, void *context) -> OSStatus {
                EventHotKeyID id{};
                if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                      sizeof(id), nullptr, &id) != noErr ||
                    id.signature != 'EcNk')
                    return eventNotHandledErr;
                auto self = static_cast<Impl *>(context);
                if (!self->keys.contains(int(id.id)))
                    return eventNotHandledErr;
                emit self->owner.activated(int(id.id));
                return noErr;
            },
            1, &event, this, &handler);
    }
    void clear()
    {
        for (auto key : std::as_const(keys))
            UnregisterEventHotKey(key);
        keys.clear();
    }
    ~Impl()
    {
        clear();
        if (handler)
            RemoveEventHandler(handler);
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
    if (!m_impl->handler) {
        for (const auto &binding : bindings)
            emit registrationFailed(binding.command, tr("无法建立系统快捷键事件处理器"));
        return;
    }
    for (const auto &binding : bindings) {
        const auto combination = binding.sequence[0];
        const int key = nativeKey(combination.key());
        const auto modifiers = combination.keyboardModifiers();
        UInt32 nativeModifiers = 0;
        // Qt's Control modifier is Command on macOS; Meta is physical Control.
        if (modifiers.testFlag(Qt::ControlModifier))
            nativeModifiers |= cmdKey;
        if (modifiers.testFlag(Qt::MetaModifier))
            nativeModifiers |= controlKey;
        if (modifiers.testFlag(Qt::AltModifier))
            nativeModifiers |= optionKey;
        if (modifiers.testFlag(Qt::ShiftModifier))
            nativeModifiers |= shiftKey;
        EventHotKeyRef reference = nullptr;
        const OSStatus status =
            key < 0 || !m_impl->handler
                ? paramErr
                : RegisterEventHotKey(key, nativeModifiers,
                                      EventHotKeyID{'EcNk', UInt32(binding.command)},
                                      GetApplicationEventTarget(), 0, &reference);
        if (status == noErr) {
            m_impl->keys.insert(binding.command, reference);
            emit registered(binding.command);
        } else
            emit registrationFailed(binding.command,
                                    tr("系统未能注册此快捷键（%1），可能已被占用").arg(status));
    }
}
} // namespace EchoMusic
