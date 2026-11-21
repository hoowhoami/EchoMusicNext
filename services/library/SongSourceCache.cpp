#include "SongSourceCache.h"
#include "SongPresentation.h"
#include <QJsonArray>
#include <QTimer>
namespace EchoMusic
{
SongSourceCache::SongSourceCache(SessionDatabase &database, QObject *parent)
    : QObject(parent), m_database(database), m_store(database)
{
}
void SongSourceCache::cache(QJsonArray songs, QObject *consumer, Completion done,
                            SongDatasetStore::Loader loader, QList<QJsonObject> presentations)
{
    if (!consumer)
        return;
    if (!presentations.isEmpty() && presentations.size() != songs.size()) {
        done({}, tr("歌曲展示缓存数量不匹配"));
        return;
    }
    struct Page
    {
        QJsonArray songs;
        QList<QJsonObject> presentations;
        QList<SongDatasetStore::CachedRow> rows;
        SongDatasetStore::Generation generation;
        qsizetype cursor = 0;
        bool waiting = false;
        Completion done;
    };
    auto page = std::make_shared<Page>();
    page->songs = std::move(songs);
    page->generation = m_store.create(std::move(loader));
    page->presentations = std::move(presentations);
    page->done = std::move(done);
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    const QPointer<QObject> lifetime(request), guard(consumer);
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, page, lifetime, guard, weak] {
        if (!lifetime)
            return;
        if (!guard) {
            lifetime->deleteLater();
            return;
        }
        if (page->waiting)
            return;
        if (page->cursor == page->songs.size()) {
            page->waiting = true;
            lifetime->deleteLater();
            page->done(std::move(page->rows), {});
            return;
        }
        QList<SongDatasetStore::Row> rows;
        const auto first = page->cursor;
        for (auto i = first; i < page->songs.size() && i < first + 128; ++i) {
            auto row = SongPresentation::row(page->songs[i].toObject(), i);
            if (!page->presentations.isEmpty())
                row.presentation = page->presentations[i];
            row.extra = row.presentation.value("extra").toString();
            rows.append(std::move(row));
        }
        page->waiting = true;
        if (!m_store.append(
                page->generation, std::move(rows), lifetime,
                [page, lifetime, guard, step = weak.lock()](auto result) {
                    if (!lifetime)
                        return;
                    if (!guard) {
                        lifetime->deleteLater();
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        lifetime->deleteLater();
                        page->done({}, result.error);
                        return;
                    }
                    for (const auto &row : result.rows) {
                        const auto cursor = page->cursor++;
                        auto cached = SongPresentation::reference(page->songs[cursor].toObject(),
                                                                  result.generation, row);
                        if (!page->presentations.isEmpty()) {
                            cached.identity.playable =
                                page->presentations[cursor].value("playable").toBool();
                            cached.identity.trackPlayable =
                                page->presentations[cursor].value("track_playable").toBool();
                        }
                        page->rows.append(std::move(cached));
                    }
                    page->waiting = false;
                    (*step)();
                }))
            page->waiting = false;
    };
    connect(&m_database, &SessionDatabase::capacityAvailable, request, [step] { (*step)(); });
    QTimer::singleShot(0, request, [step] { (*step)(); });
}
} // namespace EchoMusic
