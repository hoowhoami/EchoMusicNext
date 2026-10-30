#pragma once
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <memory>
#include <atomic>
namespace EchoMusic
{
/// Value-only worker transaction. Tracks icon fields, leaving unrelated shortcut
/// contents and subsequent user edits intact when a contribution is withdrawn.
class AppIconShortcuts
{
public:
    QVariantMap apply(const QHash<QString, QString> &paths);
    QVariantMap restoreDefault(const QString &target,
                               std::shared_ptr<std::atomic_bool> canceled = {});
    // Explicit locations are useful for isolated tests; production discovers only
    // this executable's own desktop/TaskBar entries, never arbitrary user files.
    QStringList desktopLocations, taskbarLocations;

private:
    struct Previous
    {
        QString icon, applied;
        int index = 0;
    };
    QHash<QString, Previous> m_previous;
    QStringList candidates(const QString &target) const;
    bool update(const QString &path, const QString &icon, QString *previous, int *index,
                QString &error, int newIndex = 0);
    QString currentIcon(const QString &path, int *index, QString &error) const;
};
} // namespace EchoMusic
