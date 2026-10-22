#include "EchoPluginPlaylist.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include <QRegularExpression>
#include <QTimer>
#include <QJsonDocument>
#include <cmath>
namespace EchoMusic
{
namespace
{
QVariantMap trackValue(const PlaybackTrack &t)
{
    return t.publicValue();
}
} // namespace
EchoPluginPlaylist::EchoPluginPlaylist(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    connect(scope.host().playback.queueCatalog(), &PlaybackQueueCatalog::changed, this,
            &EchoPluginPlaylist::changed);
}
QVariantList EchoPluginPlaylist::snapshot() const
{
    QVariantList result;
    const auto *catalog = m_scope.host().playback.queueCatalog();
    for (int row = 0; row < catalog->rowCount(); ++row) {
        const auto key = catalog->data(catalog->index(row), Qt::UserRole + 1).toString();
        auto *model = catalog->modelForKey(key);
        result.append(QVariantMap{{"key", key},
                                  {"title", catalog->titleForKey(key)},
                                  {"count", model->rowCount()},
                                  {"currentIndex", model->currentIndex()},
                                  {"revision", QString::number(model->revision())},
                                  {"active", key == catalog->activeKey()}});
    }
    return result;
}
QVariantList EchoPluginPlaylist::tracks(const QString &key, int offset, int limit) const
{
    QVariantList result;
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!m_scope.active() || !model || offset < 0 || limit < 1 || limit > 500)
        return result;
    for (int i = offset; i < model->rowCount() && i - offset < limit; ++i)
        result.append(trackValue(*model->at(i)));
    return result;
}
bool EchoPluginPlaylist::parse(const QVariantList &values, PlaybackTracks &tracks) const
{
    if (values.size() > 10000 ||
        QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
        return false;
    const QRegularExpression idPattern("^[a-zA-Z0-9_.:-]{1,512}$");
    const QRegularExpression hashPattern("^[a-fA-F0-9]{32}$");
    const QStringList fields{"id",      "hash",   "title",      "artist",   "album", "cover",
                             "albumId", "songId", "durationMs", "sourceId", "data"};
    for (const auto &value : values) {
        if (value.metaType().id() != QMetaType::QVariantMap)
            return false;
        const auto map = value.toMap();
        for (auto i = map.begin(); i != map.end(); ++i)
            if (!fields.contains(i.key()) || (i.key() != "durationMs" && i.key() != "data" &&
                                              i.value().metaType().id() != QMetaType::QString))
                return false;
        if (map.contains("data")) {
            const auto data = map.value("data");
            if (data.metaType().id() != QMetaType::QVariantMap ||
                QJsonObject::fromVariantMap(data.toMap()).toVariantMap() != data.toMap())
                return false;
        }
        PlaybackTrack track;
        track.catalogMetadata["plugin_data"] =
            QJsonObject::fromVariantMap(map.value("data").toMap());
        track.id = map.value("id").toString();
        track.hash = map.value("hash").toString();
        track.title = map.value("title").toString();
        track.artist = map.value("artist").toString();
        track.album = map.value("album").toString();
        track.cover = map.value("cover").toString();
        if (!idPattern.match(track.id).hasMatch() || track.title.size() > 1024 ||
            track.artist.size() > 1024 || track.album.size() > 1024 || track.cover.size() > 16384)
            return false;
        const auto durationValue = map.value("durationMs", 0);
        if (durationValue.metaType().id() != QMetaType::Int &&
            durationValue.metaType().id() != QMetaType::LongLong &&
            durationValue.metaType().id() != QMetaType::Double &&
            durationValue.metaType().id() != QMetaType::ULongLong &&
            durationValue.metaType().id() != QMetaType::UInt)
            return false;
        const auto duration = durationValue.toDouble();
        if (!std::isfinite(duration) || duration < 0 || duration > 86400000)
            return false;
        track.durationMs = quint64(duration);
        for (const auto &field : {"albumId", "songId"}) {
            const auto number = map.value(field, "0").toString();
            bool ok = false;
            const auto id = number.toULongLong(&ok);
            if (!ok)
                return false;
            if (QString(field) == "albumId")
                track.albumId = id;
            else
                track.songId = id;
        }
        if (track.hash.isEmpty()) {
            if (!m_scope.package().capabilities.contains("audioSource") ||
                !QRegularExpression("^[a-zA-Z0-9_.:/-]{1,256}$")
                     .match(map.value("sourceId").toString())
                     .hasMatch())
                return false;
            // Resolver identity persists; ephemeral stream URLs never do.
            const auto source = map.value("sourceId").toString();
            const auto prefix = m_scope.id() + "/";
            const auto provider = source.section('/', 0, 0);
            const bool dependency = m_scope.package().dependencies.contains(provider) ||
                                    m_scope.package().optionalDependencies.contains(provider);
            track.catalogMetadata["plugin_source"] =
                source.startsWith(prefix) || dependency ? source : prefix + source;
            const auto identityPrefix = "plugin:" + m_scope.id() + ":";
            if (!track.id.startsWith(identityPrefix) &&
                !(dependency && track.id.startsWith("plugin:" + provider + ":")))
                track.id = identityPrefix + track.id;
        } else if (!hashPattern.match(track.hash).hasMatch())
            return false;
        tracks.append(track);
    }
    return true;
}
EchoPluginOperation *EchoPluginPlaylist::replace(const QString &id, const QString &title,
                                                 const QVariantList &values, int startIndex)
{
    PlaybackTracks tracks;
    if (!QRegularExpression("^[a-zA-Z0-9_.-]{1,128}$").match(id).hasMatch() ||
        title.size() > 1024 || !parse(values, tracks) || startIndex < -1 ||
        startIndex >= tracks.size())
        return m_scope.rejected("InvalidArgument", "Invalid queue, tracks or startIndex");
    const auto key = "plugin/" + m_scope.id() + "/" + id;
    return m_scope.execute([this, key, title, tracks, startIndex] {
        auto &host = m_scope.host().playback;
        if (startIndex >= 0)
            host.playTracks(tracks, startIndex, title, key);
        else
            host.queueCatalog()->retain(key, title, tracks);
    });
}
EchoPluginOperation *EchoPluginPlaylist::play(const QString &key, int index)
{
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!model || !model->at(index))
        return m_scope.rejected("InvalidArgument", "Queue or row does not exist");
    const auto revision = QString::number(model->revision());
    return mutate(key, revision, [this, key, index](PlaybackQueueModel &) {
        auto &host = m_scope.host().playback;
        const auto *model = host.queueCatalog()->modelForKey(key);
        host.playTracks(model->tracks(), index, host.queueCatalog()->titleForKey(key), key);
    });
}
EchoPluginOperation *EchoPluginPlaylist::mutate(const QString &key, const QString &revision,
                                                std::function<void(PlaybackQueueModel &)> command)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!model || QString::number(model->revision()) != revision) {
        op->fail("StaleQueue", "Queue was replaced or removed");
        return op;
    }
    const QPointer<PlaybackQueueModel> guard(model);
    const auto epoch = m_scope.accountEpoch();
    op->start();
    QTimer::singleShot(0, op, [this, op, guard, revision, epoch, command = std::move(command)] {
        if (op->finished())
            return;
        if (!m_scope.active() || epoch != m_scope.accountEpoch())
            op->cancel();
        else if (!guard || QString::number(guard->revision()) != revision)
            op->fail("StaleQueue", "Queue changed before the command ran");
        else {
            command(*guard);
            op->succeed();
        }
    });
    return op;
}
EchoPluginOperation *EchoPluginPlaylist::append(const QString &key, const QVariantList &values,
                                                const QString &placement)
{
    PlaybackTracks tracks;
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!model || !parse(values, tracks) ||
        !QStringList{"last", "next", "after"}.contains(placement) ||
        model->rowCount() + tracks.size() > 10000)
        return m_scope.rejected("InvalidArgument", "Invalid queue, tracks or placement");
    return mutate(key, QString::number(model->revision()),
                  [this, tracks, placement](PlaybackQueueModel &queue) {
                      const auto &current = m_scope.host().playback.currentTrack();
                      queue.append(tracks, placement != "last",
                                   current.id.isEmpty() ? current.key() : current.id,
                                   placement == "after");
                  });
}
EchoPluginOperation *EchoPluginPlaylist::remove(const QString &key, const QString &revision,
                                                int index)
{
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!model || !model->at(index))
        return m_scope.rejected("InvalidArgument", "Invalid row");
    return mutate(key, revision, [index](PlaybackQueueModel &model) { model.remove(index); });
}
EchoPluginOperation *EchoPluginPlaylist::move(const QString &key, const QString &revision, int from,
                                              int to)
{
    auto *model = m_scope.host().playback.queueCatalog()->modelForKey(key);
    if (!model || !model->at(from) || !model->at(to))
        return m_scope.rejected("InvalidArgument", "Invalid row");
    return mutate(key, revision, [from, to](PlaybackQueueModel &model) { model.move(from, to); });
}
EchoPluginOperation *EchoPluginPlaylist::clear(const QString &key, const QString &revision)
{
    return mutate(key, revision, [](PlaybackQueueModel &model) { model.clear(); });
}
} // namespace EchoMusic
