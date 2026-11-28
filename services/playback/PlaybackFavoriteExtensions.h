#pragma once
#include "PlaybackTrack.h"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace EchoMusic
{
/// GUI-thread favorite providers for hashless plugin tracks. Catalog favorites
/// remain in FavoriteSongIndex. Only bounded public projections are cached.
class PlaybackFavoriteExtensions final : public QObject
{
    Q_OBJECT
public:
    using Reply = std::function<void(bool, const QVariant &, const QString &)>;
    struct Provider
    {
        QString key, revision, sourcePrefix;
        std::function<bool()> valid;
        std::function<void(const QString &, const QVariantMap &, QObject *, Reply)> invoke;
    };
    struct State
    {
        bool available = false, known = false, favorite = false, loading = false, busy = false;
        QString error;
    };
    explicit PlaybackFavoriteExtensions(QObject *parent);
    ~PlaybackFavoriteExtensions() override;
    static bool custom(const PlaybackTrack &);
    bool add(Provider);
    void remove(const QString &key, const QString &revision);
    bool invalidate(const QString &key, const QString &revision, const QString &sourceId);
    State state(const PlaybackTrack &);
    void toggle(const PlaybackTrack &, bool requireCurrent);
    void setCurrent(const PlaybackTrack &);
    void reset();
    void shutdown();
signals:
    void changed();
    void notice(const QString &message, bool warning);

private:
    struct Entry
    {
        QString key, sourceId, providerKey, providerRevision;
        QVariantMap track;
        State state;
        QPointer<QObject> job;
        quint64 generation = 0, used = 0;
        qsizetype cost = 0;
        bool currentScoped = false, dirty = false, toggleAfterRead = false;
    };
    using Item = std::shared_ptr<Entry>;
    static QString identity(const PlaybackTrack &);
    const Provider *provider(const QString &sourceId) const;
    Item ensure(const PlaybackTrack &);
    void cancel(const Item &);
    void request(const Item &, bool write, bool favorite = false);
    QList<Provider> m_providers;
    QHash<QString, Item> m_entries;
    QString m_current;
    bool m_closed = false;
    quint64 m_used = 0;
    qsizetype m_cost = 0;
};
} // namespace EchoMusic
