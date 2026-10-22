#pragma once
#include "PlaybackQueueModel.h"
#include <QAbstractListModel>
#include <memory>
#include <vector>
namespace EchoMusic
{
struct PlaybackQueueState
{
    QString key, title;
    PlaybackTracks tracks;
    int current = -1;
    QStringList next;
    quint64 lastUsed = 0;
};
using PlaybackQueueStates = QList<PlaybackQueueState>;
/// Each queue has exactly one mutable model. Drawer browsing does not change
/// playback authority. Four retained sources match EchoMusic's queue policy.
class PlaybackQueueCatalog final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY changed)
    /// Identity of the queue that currently owns playback ("default", a
    /// discovery/fm key, ...). Read-only: a view may follow the active source,
    /// but only explicit play commands change it.
    Q_PROPERTY(QString activeKey READ activeKey NOTIFY changed)
    Q_PROPERTY(int viewIndex READ viewIndex WRITE setViewIndex NOTIFY changed)
    Q_PROPERTY(EchoMusic::PlaybackQueueModel *viewModel READ viewModel NOTIFY changed)
    Q_PROPERTY(QString viewTitle READ viewTitle NOTIFY changed)
    Q_PROPERTY(QString viewSubtitle READ viewSubtitle NOTIFY changed)
    Q_PROPERTY(QString viewStatus READ viewStatus NOTIFY changed)
    Q_PROPERTY(int previewCount READ previewCount NOTIFY changed)
    Q_PROPERTY(int previewIndex READ previewIndex NOTIFY changed)
    Q_PROPERTY(bool viewActive READ viewActive NOTIFY changed)
public:
    explicit PlaybackQueueCatalog(QObject *parent);
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_entries.size());
    }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "queueKey"},
                {Qt::UserRole + 2, "queueTitle"},
                {Qt::UserRole + 3, "songCount"},
                {Qt::UserRole + 4, "activeQueue"}};
    }
    PlaybackQueueModel *retain(const QString &key, const QString &title,
                               const PlaybackTracks &tracks);
    PlaybackQueueModel *activate(const QString &key, const QString &title,
                                 const PlaybackTracks &tracks, int current);
    PlaybackQueueModel *modelForKey(const QString &key) const;
    QString titleForKey(const QString &key) const;
    QString priorityKey(const QString &exclude) const;
    QString priorityKey(const QString &exclude, const QString &currentTrackId) const;
    QPair<QString, int> takePriority(const QString &exclude, const QString &currentTrackId);
    QString lastLocalKey() const
    {
        return m_lastLocal;
    }
    PlaybackQueueModel *activeModel() const;
    PlaybackQueueModel *viewModel() const;
    QString activeTitle() const;
    QString activeKey() const
    {
        return m_active >= 0 ? m_entries[size_t(m_active)].key : QString();
    }
    QString viewTitle() const;
    QString viewSubtitle() const;
    QString viewStatus() const;
    int previewCount() const
    {
        return previewRows().size();
    }
    int previewIndex() const
    {
        return previewRows().indexOf(m_view);
    }
    bool viewActive() const
    {
        return m_view == m_active;
    }
    Q_INVOKABLE void switchPreview(int direction);
    Q_INVOKABLE void resetPreview();
    Q_INVOKABLE void removeViewed();
    int activeIndex() const
    {
        return m_active;
    }
    int viewIndex() const
    {
        return m_view;
    }
    Q_INVOKABLE void setViewIndex(int index);
    void activateViewed();
    bool appendTo(const QString &key, const PlaybackTracks &tracks);
    void clear();
    void removeInactiveRoomQueue();
    PlaybackQueueStates snapshot() const;
    void restore(const PlaybackQueueStates &states, const QString &activeKey,
                 const QString &lastLocal = {}, bool discardTransient = false);
signals:
    void changed();
    void modelAdded(EchoMusic::PlaybackQueueModel *model);
    void transientQueueRemoved(const QString &key);

private:
    int ensureQueue(const QString &key, const QString &title);
    QList<int> previewRows() const;
    QStringList prioritySources(const QString &exclude) const;
    void removeInactiveTransient();
    void trimCapacity();
    void materializeDefault();
    struct Entry
    {
        QString key, title;
        std::unique_ptr<PlaybackQueueModel> model;
        quint64 lastUsed = 0;
    };
    std::vector<Entry> m_entries;
    int m_active = -1, m_view = -1;
    QString m_lastLocal = "default";
    bool m_restoring = false;
    quint64 m_usage = 0;
    bool m_defaultPresent = false;
};
} // namespace EchoMusic
Q_DECLARE_METATYPE(EchoMusic::PlaybackQueueState)
Q_DECLARE_METATYPE(EchoMusic::PlaybackQueueStates)
