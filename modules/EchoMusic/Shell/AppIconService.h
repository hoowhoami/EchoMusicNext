#pragma once
#include <QObject>
#include <QIcon>
#include <QHash>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>
#include <QImage>
#include <functional>
#include <memory>
#include <atomic>
class QWindow;
namespace EchoMusic
{
struct PreparedAppIcons
{
    QHash<QString, QImage> images;
    QHash<QString, QString> paths;
    QString error;
};
/// Application-owned, reversible icon contributions. Disk/decoder work is serial
/// and drained before package retirement; no tray polling or window recreation.
class AppIconService final : public QObject
{
    Q_OBJECT
public:
    explicit AppIconService(QObject *parent = nullptr);
    ~AppIconService() override;
    void prepare(const QHash<QString, QString> &paths, std::function<void(PreparedAppIcons)> done,
                 const QHash<QString, QByteArray> &data = {});
    bool put(const QString &token, PreparedAppIcons icons, int priority, QVariantMap owner);
    void remove(const QString &token);
    void removeTarget(const QString &token, const QString &target);
    bool hasTarget(const QString &target) const;
    void attach(QWindow *window);
    QIcon trayIcon(const QIcon &fallback) const;
    QVariantMap snapshot() const;
    quint64 shortcutRevision() const
    {
        return m_revision;
    }
    quint64 completedShortcutRevision() const
    {
        return m_completedRevision;
    }
    bool drained() const
    {
        return m_pending == 0;
    }
    void refresh();
    void restoreShortcut(const QString &target, std::function<void(QVariantMap)> done,
                         std::shared_ptr<std::atomic_bool> canceled = {});
    static bool supported(const QString &target);
signals:
    void changed();
    void drainedChanged();
    void contributionFailed(const QString &token, const QString &message);

private:
    void apply();
    void scheduleShortcuts();
    struct Entry
    {
        PreparedAppIcons icons;
        QVariantMap owner;
        int priority;
        quint64 order;
    };
    QHash<QString, Entry> m_entries;
    QHash<QWindow *, QIcon> m_windows;
    QIcon m_default;
    QThreadPool m_worker;
    struct ShortcutState;
    std::shared_ptr<ShortcutState> m_shortcuts;
    quint64 m_order = 0, m_revision = 0, m_applyRevision = 0, m_completedRevision = 0;
    int m_pending = 0;
    QString m_error;
    QHash<QString, QString> m_desiredShortcuts, m_submittedShortcuts;
    bool m_shortcutBusy = false, m_forceShortcuts = false;
};
} // namespace EchoMusic
