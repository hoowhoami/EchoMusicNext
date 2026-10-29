#include "SettingsKeys.h"
#include "ShortcutService.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>

namespace EchoMusic
{
namespace
{
QKeySequence normalizedShortcut(const QKeySequence &sequence)
{
#if defined(Q_OS_MACOS)
    if (sequence.count() == 1) {
        const auto combination = sequence[0];
        switch (combination.key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
            // Cocoa annotates ordinary arrow keys with KeypadModifier. It does
            // not distinguish an arrow binding; actual keypad keys still do.
            return QKeySequence(QKeyCombination(
                combination.keyboardModifiers() & ~Qt::KeypadModifier, combination.key()));
        default:
            break;
        }
    }
#endif
    return sequence;
}
QString displayShortcut(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return {};
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    QStringList parts;
    // Display modifiers in Command/Shift/Option/Control order.
    // Storage, recording, matching and registration use QKeySequence.
#if defined(Q_OS_MACOS)
    if (modifiers.testFlag(Qt::ControlModifier))
        parts.append(QStringLiteral("⌘"));
    if (modifiers.testFlag(Qt::ShiftModifier))
        parts.append(QStringLiteral("⇧"));
    if (modifiers.testFlag(Qt::AltModifier))
        parts.append(QStringLiteral("⌥"));
    if (modifiers.testFlag(Qt::MetaModifier))
        parts.append(QStringLiteral("⌃"));
#else
    if (modifiers.testFlag(Qt::ControlModifier))
        parts.append(QStringLiteral("Ctrl"));
    if (modifiers.testFlag(Qt::ShiftModifier))
        parts.append(QStringLiteral("Shift"));
    if (modifiers.testFlag(Qt::AltModifier))
        parts.append(QStringLiteral("Alt"));
    if (modifiers.testFlag(Qt::MetaModifier))
        parts.append(QStringLiteral("Win"));
#endif
    QString key;
    switch (combination.key()) {
#if defined(Q_OS_MACOS)
    case Qt::Key_Left:
        key = QStringLiteral("←");
        break;
    case Qt::Key_Right:
        key = QStringLiteral("→");
        break;
    case Qt::Key_Up:
        key = QStringLiteral("↑");
        break;
    case Qt::Key_Down:
        key = QStringLiteral("↓");
        break;
#endif
    case Qt::Key_Return:
        key = QStringLiteral("Enter");
        break;
    case Qt::Key_PageUp:
        key = QStringLiteral("PageUp");
        break;
    case Qt::Key_PageDown:
        key = QStringLiteral("PageDown");
        break;
    case Qt::Key_Print:
        key = QStringLiteral("PrintScreen");
        break;
    default:
        key = QKeySequence(combination.key()).toString(QKeySequence::PortableText);
        break;
    }
    parts.append(key);
#if defined(Q_OS_MACOS)
    return parts.join(QString());
#else
    return parts.join('+');
#endif
}
} // namespace
ShortcutService::ShortcutService(EchoMusic::SettingsStore &settings, bool nativeRegistration,
                                 QObject *parent)
    : QAbstractListModel(parent), m_settings(settings), m_nativeRegistration(nativeRegistration),
      m_globalEnabled(
          settings.value(EchoMusic::SettingsKeys::Shortcuts::GlobalEnabled, false).toBool()),
      m_conflictPrompt(
          settings.value(EchoMusic::SettingsKeys::Shortcuts::ConflictPrompt, true).toBool()),
      m_suppressDefault(
          settings.value(EchoMusic::SettingsKeys::Shortcuts::SuppressDefault, true).toBool())
{
    struct Definition
    {
        const char *key, *title, *description, *local, *global;
    };
    const Definition definitions[] = {
        {"togglePlayback", "播放 / 暂停", "切换当前歌曲的播放状态", "Ctrl+Space",
         "Ctrl+Shift+Space"},
        {"previousTrack", "上一首", "跳转到播放列表中的上一首歌曲", "Ctrl+Left", "Ctrl+Shift+Left"},
        {"nextTrack", "下一首", "跳转到播放列表中的下一首歌曲", "Ctrl+Right", "Ctrl+Shift+Right"},
        {"seekBackward", "快退", "按设置的快退步长回退当前播放进度", "Alt+Ctrl+Left",
         "Alt+Ctrl+Shift+Left"},
        {"seekForward", "快进", "按设置的快进步长前进当前播放进度", "Alt+Ctrl+Right",
         "Alt+Ctrl+Shift+Right"},
        {"volumeUp", "音量 +", "将播放器音量提高 5%", "Ctrl+Up", "Ctrl+Shift+Up"},
        {"volumeDown", "音量 -", "将播放器音量降低 5%", "Ctrl+Down", "Ctrl+Shift+Down"},
        {"toggleMute", "静音", "切换播放器静音状态", "Ctrl+M", "Ctrl+Shift+M"},
        {"toggleFavorite", "收藏当前歌曲", "切换播放器当前歌曲的收藏状态", "Ctrl+L",
         "Ctrl+Shift+L"},
        {"togglePlayMode", "切换播放模式", "在列表循环、单曲循环、随机播放之间切换", "Ctrl+P",
         "Ctrl+Shift+P"},
        {"toggleMiniPlayer", "Mini 模式切换", "在主窗口和 Mini 模式之间切换", "Ctrl+I",
         "Ctrl+Shift+I"},
        {"toggleWindow", "显示 / 隐藏窗口", "切换主窗口的显示和隐藏状态", "Ctrl+W", "Alt+Ctrl+S"},
        {"toggleSidebar", "侧边栏开关", "展开或收起侧边栏", "Ctrl+B", "Ctrl+Shift+B"},
        {"toggleMainLyric", "播放页歌词开关", "打开或关闭播放页歌词", "Ctrl+K", "Ctrl+Shift+K"},
        {"toggleDesktopLyric", "桌面歌词开关", "打开或关闭桌面歌词窗口", "Ctrl+D", "Ctrl+Shift+D"}};
    for (const auto &definition : definitions) {
        Entry entry;
        entry.key = QString::fromLatin1(definition.key);
        entry.title = tr(definition.title);
        entry.description = tr(definition.description);
        entry.defaultLocal =
            QKeySequence(QString::fromLatin1(definition.local), QKeySequence::PortableText);
        entry.defaultGlobal =
            QKeySequence(QString::fromLatin1(definition.global), QKeySequence::PortableText);
        entry.local =
            QKeySequence(settings
                             .value(EchoMusic::SettingsKeys::Shortcuts::LocalPrefix + entry.key,
                                    entry.defaultLocal.toString(QKeySequence::PortableText))
                             .toString(),
                         QKeySequence::PortableText);
        entry.global =
            QKeySequence(settings
                             .value(EchoMusic::SettingsKeys::Shortcuts::GlobalPrefix + entry.key,
                                    entry.defaultGlobal.toString(QKeySequence::PortableText))
                             .toString(),
                         QKeySequence::PortableText);
        entry.local = normalizedShortcut(entry.local);
        entry.global = normalizedShortcut(entry.global);
        m_entries.append(entry);
    }
    connect(&m_global, &GlobalShortcuts::activated, this, [this](int command) {
        if (command >= 1000) {
            const auto bindings = m_plugins.values();
            for (const auto &b : bindings)
                if (b.command == command && b.owner && !m_closed && m_recordingRow < 0 &&
                    b.global && m_globalEnabled && b.error.isEmpty() && b.invoke)
                    b.invoke();
            return;
        }
        if (!m_closed && m_recordingRow < 0 && command >= 0 && command < m_entries.size())
            emit triggered(Command(command));
    });
    connect(&m_global, &GlobalShortcuts::registered, this, [this](int command) {
        if (m_closed || m_recordingRow >= 0 || !m_nativeRegistration || !m_globalEnabled)
            return;
        for (auto &b : m_plugins)
            if (b.command == command && b.owner) {
                b.registered = true;
                b.error.clear();
                emit pluginRegistrationChanged(b.key, b.generation, true, {});
                break;
            }
    });
    connect(&m_global, &GlobalShortcuts::registrationFailed, this,
            [this](int row, const QString &reason) {
                if (row >= 1000) {
                    for (auto &b : m_plugins)
                        if (b.command == row) {
                            b.registered = false;
                            b.error = reason;
                            emit pluginRegistrationChanged(b.key, b.generation, false, reason);
                        }
                    return;
                }
                if (row < 0 || row >= m_entries.size())
                    return;
                m_entries[row].error = reason;
                m_registrationError = reason;
                emit dataChanged(index(row), index(row), {GlobalError});
                emit changed();
                if (m_conflictPrompt)
                    emit notice(tr("%1：%2").arg(m_entries[row].title, reason));
            });
    qGuiApp->installEventFilter(this);
    registerGlobal();
}
ShortcutService::~ShortcutService()
{
    shutdown();
}
int ShortcutService::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}
QVariant ShortcutService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries.at(index.row());
    switch (role) {
    case Title:
        return entry.title;
    case Description:
        return entry.description;
    case LocalLabel:
        return displayShortcut(entry.local);
    case GlobalLabel:
        return displayShortcut(entry.global);
    case Modified:
        return entry.local != entry.defaultLocal || entry.global != entry.defaultGlobal;
    case GlobalError:
        return entry.error;
    default:
        return {};
    }
}
QHash<int, QByteArray> ShortcutService::roleNames() const
{
    return {{Title, "shortcutTitle"},   {Description, "shortcutDescription"},
            {LocalLabel, "localLabel"}, {GlobalLabel, "globalLabel"},
            {Modified, "modified"},     {GlobalError, "globalError"}};
}
void ShortcutService::attach(QQuickWindow *window)
{
    m_window = window;
}
void ShortcutService::attachMini(QQuickWindow *window)
{
    m_miniWindow = window;
}
void ShortcutService::beginRecording(int row, bool global)
{
    if (m_closed || row < 0 || row >= m_entries.size() || (global && !m_globalEnabled))
        return;
    if (m_recordingRow == row && m_recordingGlobal == global)
        return;
    m_recordingRow = row;
    m_recordingGlobal = global;
    // OS registrations must not consume keys while the user is recording them.
    m_global.replace({});
    for (const auto &binding : m_plugins)
        if (binding.global)
            emit pluginRegistrationChanged(binding.key, binding.generation, false,
                                           tr("正在录制快捷键"));
    emit recordingChanged();
}
void ShortcutService::stopRecording()
{
    if (m_recordingRow < 0)
        return;
    m_recordingRow = -1;
    emit recordingChanged();
    registerGlobal();
}
bool ShortcutService::recordKey(int key, Qt::KeyboardModifiers modifiers, bool repeat)
{
    if (m_recordingRow < 0 || repeat)
        return false;
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown)
        return false;
    if (key == Qt::Key_Escape || key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        clearBinding(m_recordingRow, m_recordingGlobal);
        return true;
    }
    modifiers &= Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier |
                 Qt::KeypadModifier;
    if (!modifiers && key < Qt::Key_Escape) {
        emit notice(tr("普通按键快捷键至少需要包含一个修饰键"));
        return false;
    }
    const auto sequence =
        normalizedShortcut(QKeySequence(QKeyCombination(modifiers, Qt::Key(key))));
    for (const auto &binding : m_plugins)
        if (binding.owner && binding.global == m_recordingGlobal && binding.sequence == sequence) {
            emit notice(tr("该快捷键已由插件使用"));
            return false;
        }
    for (int row = 0; row < m_entries.size(); ++row) {
        if (row != m_recordingRow &&
            sequence == (m_recordingGlobal ? m_entries[row].global : m_entries[row].local)) {
            if (m_conflictPrompt)
                emit notice(tr("该快捷键已分配给“%1”").arg(m_entries[row].title));
            return false;
        }
    }
    auto &entry = m_entries[m_recordingRow];
    (m_recordingGlobal ? entry.global : entry.local) = sequence;
    persist(m_recordingRow);
    stopRecording();
    return true;
}
void ShortcutService::persist(int row)
{
    const auto &entry = m_entries.at(row);
    m_settings.setValue(EchoMusic::SettingsKeys::Shortcuts::LocalPrefix + entry.key,
                        entry.local.toString(QKeySequence::PortableText));
    m_settings.setValue(EchoMusic::SettingsKeys::Shortcuts::GlobalPrefix + entry.key,
                        entry.global.toString(QKeySequence::PortableText));
    emit dataChanged(index(row), index(row));
}
void ShortcutService::clearBinding(int row, bool global)
{
    if (row < 0 || row >= m_entries.size())
        return;
    (global ? m_entries[row].global : m_entries[row].local) = {};
    persist(row);
    if (m_recordingRow >= 0)
        stopRecording();
    else
        registerGlobal();
}
void ShortcutService::resetBinding(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    auto &entry = m_entries[row];
    entry.local = entry.defaultLocal;
    entry.global = entry.defaultGlobal;
    persist(row);
    if (m_recordingRow >= 0)
        stopRecording();
    else
        registerGlobal();
}
void ShortcutService::resetAll()
{
    for (int row = 0; row < m_entries.size(); ++row) {
        auto &entry = m_entries[row];
        entry.local = entry.defaultLocal;
        entry.global = entry.defaultGlobal;
        persist(row);
    }
    if (m_recordingRow >= 0)
        stopRecording();
    else
        registerGlobal();
}
void ShortcutService::registerGlobal()
{
    if (m_closed || m_recordingRow >= 0)
        return;
    m_registrationError.clear();
    QList<GlobalShortcuts::Binding> bindings;
    for (int row = 0; row < m_entries.size(); ++row) {
        m_entries[row].error.clear();
        if (m_nativeRegistration && m_globalEnabled && !m_entries[row].global.isEmpty())
            bindings.append({row, m_entries[row].global, m_entries[row].title});
    }
    emit dataChanged(index(0), index(m_entries.size() - 1), {GlobalError});
    for (auto &b : m_plugins)
        if (b.global) {
            b.registered = false;
            b.error = !m_globalEnabled        ? tr("全局快捷键已关闭")
                      : !m_nativeRegistration ? tr("当前运行环境不提供全局快捷键")
                                              : QString();
            if (b.error.isEmpty() && b.owner)
                bindings.append({b.command, b.sequence, b.key});
        }
    m_global.replace(bindings);
    for (const auto &b : m_plugins)
        if (b.global)
            emit pluginRegistrationChanged(b.key, b.generation,
                                           b.owner && b.registered && b.error.isEmpty(), b.error);
    emit changed();
}
void ShortcutService::setGlobalEnabled(bool value)
{
    if (m_globalEnabled == value)
        return;
    m_globalEnabled = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Shortcuts::GlobalEnabled, value);
    if (m_recordingRow >= 0)
        stopRecording();
    else
        registerGlobal();
}
void ShortcutService::setConflictPrompt(bool value)
{
    if (m_conflictPrompt == value)
        return;
    m_conflictPrompt = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Shortcuts::ConflictPrompt, value);
    emit changed();
}
void ShortcutService::setSuppressDefault(bool value)
{
    if (m_suppressDefault == value)
        return;
    m_suppressDefault = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Shortcuts::SuppressDefault, value);
    emit changed();
}
bool ShortcutService::editableFocus(QQuickWindow *window) const
{
    auto item = window ? window->activeFocusItem() : nullptr;
    // Input-method capability covers Qt text input/edit and editable controls
    // without importing private Quick text classes or guessing object names.
    for (; item; item = item->parentItem())
        if (item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod))
            return true;
    return false;
}
bool ShortcutService::eventFilter(QObject *watched, QEvent *event)
{
    if (m_closed)
        return false;
    if (event->type() == QEvent::ApplicationDeactivate)
        stopRecording();
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)
        return false;
    auto *window = qobject_cast<QQuickWindow *>(watched);
    if (!window || (window != m_window && window != m_miniWindow))
        return false;
    auto key = static_cast<QKeyEvent *>(event);
    if (m_recordingRow >= 0 && window == m_window) {
        if (event->type() == QEvent::KeyPress)
            recordKey(key->key(), key->modifiers(), key->isAutoRepeat());
        key->accept();
        return true;
    }
    if (m_recordingRow >= 0 || !window->isActive() || editableFocus(window) || key->isAutoRepeat())
        return false;
    const auto sequence = normalizedShortcut(QKeySequence(key->keyCombination()));
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries[row].local.isEmpty() || m_entries[row].local != sequence)
            continue;
        if (event->type() == QEvent::KeyPress)
            emit triggered(Command(row));
        if (m_suppressDefault) {
            key->accept();
            return true;
        }
        return false;
    }
    const auto bindings = m_plugins.values();
    for (const auto &binding : bindings)
        if (binding.owner && !binding.global && binding.sequence == sequence) {
            if (event->type() == QEvent::KeyPress && binding.invoke)
                binding.invoke();
            key->accept();
            return true;
        }
    return false;
}
quint64 ShortcutService::registerPlugin(PluginBinding binding, QString &error)
{
    if (m_closed || !binding.owner || binding.key.isEmpty() ||
        (!m_plugins.contains(binding.key) && m_plugins.size() >= 128)) {
        error = tr("快捷键服务不可用或注册已满");
        return 0;
    }
    binding.sequence = normalizedShortcut(binding.sequence);
    if (binding.sequence.isEmpty() || binding.sequence.count() != 1 ||
        binding.sequence[0].key() == Qt::Key_unknown ||
        binding.sequence[0].keyboardModifiers() == Qt::NoModifier) {
        error = tr("快捷键必须包含修饰键和一个按键");
        return 0;
    }
    for (const auto &entry : m_entries)
        if ((binding.global ? entry.global : entry.local) == binding.sequence) {
            error = tr("该快捷键已分配给“%1”").arg(entry.title);
            return 0;
        }
    for (const auto &entry : m_plugins)
        if (entry.key != binding.key && entry.global == binding.global &&
            entry.sequence == binding.sequence) {
            error = tr("该快捷键已由其他插件使用");
            return 0;
        }
    binding.generation = ++m_pluginGeneration;
    binding.command = ++m_pluginCommand;
    const auto key = binding.key;
    const auto generation = binding.generation;
    connect(binding.owner, &QObject::destroyed, this,
            [this, key, generation] { unregisterPlugin(key, generation); });
    m_plugins.insert(key, std::move(binding));
    registerGlobal();
    return generation;
}
void ShortcutService::unregisterPlugin(const QString &key, quint64 generation)
{
    if (m_plugins.value(key).generation == generation) {
        m_plugins.remove(key);
        registerGlobal();
    }
}
void ShortcutService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    m_recordingRow = -1;
    m_global.replace({});
    m_plugins.clear();
    qGuiApp->removeEventFilter(this);
}
} // namespace EchoMusic
