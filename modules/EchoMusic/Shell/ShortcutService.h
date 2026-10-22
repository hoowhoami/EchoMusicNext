#pragma once
#include "GlobalShortcuts.h"
#include <QAbstractListModel>
#include <QPointer>
#include <QQuickWindow>
#include <QSettings>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// One owner for recording, persisted bindings and native registration. Views
/// contain no shortcut maps; the application connects typed commands to owners.
class ShortcutService final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned shortcut service")
    Q_PROPERTY(bool globalEnabled READ globalEnabled WRITE setGlobalEnabled NOTIFY changed)
    Q_PROPERTY(bool conflictPrompt READ conflictPrompt WRITE setConflictPrompt NOTIFY changed)
    Q_PROPERTY(bool suppressDefault READ suppressDefault WRITE setSuppressDefault NOTIFY changed)
    Q_PROPERTY(int recordingRow READ recordingRow NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingGlobal READ recordingGlobal NOTIFY recordingChanged)
    Q_PROPERTY(QString registrationError READ registrationError NOTIFY changed)
public:
    enum Command
    {
        TogglePlayback,
        PreviousTrack,
        NextTrack,
        SeekBackward,
        SeekForward,
        VolumeUp,
        VolumeDown,
        ToggleMute,
        ToggleFavorite,
        TogglePlayMode,
        ToggleMiniPlayer,
        ToggleWindow,
        ToggleSidebar,
        ToggleMainLyric,
        ToggleDesktopLyric
    };
    Q_ENUM(Command)
    enum Role
    {
        Title = Qt::UserRole + 1,
        Description,
        LocalLabel,
        GlobalLabel,
        Modified,
        GlobalError
    };
    explicit ShortcutService(QSettings &settings, bool nativeRegistration = true,
                             QObject *parent = nullptr);
    ~ShortcutService() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool globalEnabled() const
    {
        return m_globalEnabled;
    }
    bool conflictPrompt() const
    {
        return m_conflictPrompt;
    }
    bool suppressDefault() const
    {
        return m_suppressDefault;
    }
    int recordingRow() const
    {
        return m_recordingRow;
    }
    bool recordingGlobal() const
    {
        return m_recordingGlobal;
    }
    QString registrationError() const
    {
        return m_registrationError;
    }
    void setGlobalEnabled(bool value);
    void setConflictPrompt(bool value);
    void setSuppressDefault(bool value);
    void attach(QQuickWindow *window);
    void attachMini(QQuickWindow *window);
    Q_INVOKABLE void beginRecording(int row, bool global);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void clearBinding(int row, bool global);
    Q_INVOKABLE void resetBinding(int row);
    Q_INVOKABLE void resetAll();
    /// Records Qt's logical key, preserving platform modifier semantics.
    bool recordKey(int key, Qt::KeyboardModifiers modifiers, bool repeat = false);
    void shutdown();
signals:
    void changed();
    void recordingChanged();
    void triggered(EchoMusic::ShortcutService::Command command);
    void notice(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry
    {
        QString key, title, description;
        QKeySequence local, global, defaultLocal, defaultGlobal;
        QString error;
    };
    void persist(int row);
    void registerGlobal();
    bool editableFocus(QQuickWindow *window) const;
    QSettings &m_settings;
    QList<Entry> m_entries;
    QPointer<QQuickWindow> m_window, m_miniWindow;
    GlobalShortcuts m_global;
    bool m_nativeRegistration, m_globalEnabled, m_conflictPrompt, m_suppressDefault;
    bool m_recordingGlobal = false, m_closed = false;
    int m_recordingRow = -1;
    QString m_registrationError;
};
} // namespace EchoMusic
