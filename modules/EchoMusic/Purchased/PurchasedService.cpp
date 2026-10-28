#include "PurchasedService.h"
#include "PlaybackService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlEngine>
namespace EchoMusic
{
PurchasedService::PurchasedService(ApiClient &api, PlaybackService &playback,
                                   RouteController &router, bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_router(router),
      m_actions(m_songs, playback, this), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_actions.setQueueContext(tr("已购音乐"), "queue:purchased");
    connect(&router, &RouteController::pageActivated, this, &PurchasedService::activate);
    connect(&api, &ApiClient::succeeded, this, &PurchasedService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &PurchasedService::reset);
    connect(&api, &ApiClient::closed, this, &PurchasedService::shutdown);
}
PurchasedService::~PurchasedService()
{
    shutdown();
}
const QStringList &PurchasedService::tabs()
{
    static const QStringList value{"songs", "albums"};
    return value;
}
bool PurchasedService::hasPending(PendingKind kind) const
{
    for (const auto &pending : m_pending)
        if (pending.kind == kind)
            return true;
    return false;
}
bool PurchasedService::hasMore() const
{
    if (m_tab == 0)
        return m_songPage > 0 && !m_songExhausted;
    return m_albumPage > 0 && !m_albumExhausted;
}
void PurchasedService::selectTab(int tab)
{
    if (!m_active || tab < 0 || tab >= tabs().size() || !m_router.current())
        return;
    auto query = m_router.current()->query();
    query["tab"] = tabs().at(tab);
    m_router.replaceQuery(query);
}
void PurchasedService::activate(RouteContext *context)
{
    m_songs.setMetadataActive(!m_closed && m_network && context && context->name() == "purchased");
    if (m_closed)
        return;
    if (!context || context->name() != "purchased") {
        m_active = false;
        cancelAll();
        return;
    }
    if (m_instance != context->instanceId()) {
        m_active = false;
        reset();
        m_instance = context->instanceId();
    }
    m_active = true;
    const int next = tabs().indexOf(context->query().value("tab", "songs").toString());
    if (next >= 0 && next != m_tab)
        m_tab = next;
    loadTab();
    emit changed();
}
void PurchasedService::loadTab()
{
    if (!m_active || !m_network || m_closed || !m_api.currentAccountUserId())
        return;
    // Load both containers on account changes so both tab totals are available
    // before the user switches tabs.
    if (!m_songPage && !m_songExhausted && m_songError.isEmpty())
        request(PendingKind::Songs, 1);
    else if (m_songPage)
        continueSongs();
    if (!m_albumPage && !m_albumExhausted && m_albumError.isEmpty())
        request(PendingKind::Albums, 1);
}
void PurchasedService::continueSongs()
{
    if (m_closed || !m_active || !m_network || m_songExhausted || !m_songError.isEmpty() ||
        songsLoading() || !m_api.currentAccountUserId())
        return;
    m_songContinuationQueued = true;
    const auto generation = m_generation, session = m_api.sessionRevision();
    const auto page = m_songPage;
    QMetaObject::invokeMethod(
        this,
        [this, generation, session, page] {
            if (generation != m_generation)
                return;
            m_songContinuationQueued = false;
            if (!m_closed && m_active && m_network && session == m_api.sessionRevision() &&
                page == m_songPage && !m_songExhausted && m_songError.isEmpty() &&
                !hasPending(PendingKind::Songs))
                request(PendingKind::Songs, page + 1);
        },
        Qt::QueuedConnection);
}
void PurchasedService::request(PendingKind kind, quint32 page)
{
    if (hasPending(kind))
        return;
    const auto request = kind == PendingKind::Songs ? m_api.purchasedSongs(page, 30)
                                                    : m_api.purchasedAlbums(page, 30);
    auto &error = kind == PendingKind::Songs ? m_songError : m_albumError;
    if (!request) {
        error = tr("已购音乐加载失败");
        emit changed();
        return;
    }
    m_pending.insert(request, {kind, m_api.sessionRevision(), page});
    error.clear();
    emit changed();
}
void PurchasedService::refresh()
{
    if (m_closed)
        return;
    cancelAll();
    m_songPage = m_albumPage = 0;
    m_songReceived = m_albumReceived = 0;
    m_songTotal = m_albumTotal = 0;
    m_albumRows = {};
    m_songs.replaceSongs({});
    m_albums.replaceCards({});
    m_songExhausted = m_albumExhausted = false;
    m_songError.clear();
    m_albumError.clear();
    if (m_active)
        loadTab();
    emit changed();
}
void PurchasedService::loadMore()
{
    if (m_closed || !m_active || !m_network || !m_api.currentAccountUserId())
        return;
    const auto kind = m_tab == 0 ? PendingKind::Songs : PendingKind::Albums;
    const auto previous = m_tab == 0 ? m_songPage : m_albumPage;
    if (hasPending(kind) || (m_tab == 0 && m_songContinuationQueued) ||
        (previous > 0 && !hasMore()))
        return;
    const auto page = m_tab == 0 ? m_songPage + 1 : m_albumPage + 1;
    request(kind, page);
}
void PurchasedService::cancelAll()
{
    ++m_generation;
    m_songContinuationQueued = false;
    for (const auto id : m_pending.keys()) {
        m_pending.remove(id);
        m_api.cancel(id);
    }
}
void PurchasedService::reset()
{
    cancelAll();
    m_songPage = m_albumPage = 0;
    m_songReceived = m_albumReceived = 0;
    m_songTotal = m_albumTotal = 0;
    m_albumRows = {};
    m_songs.replaceSongs({});
    m_albums.replaceCards({});
    m_songExhausted = m_albumExhausted = false;
    m_songError.clear();
    m_albumError.clear();
    if (m_active && m_router.current() && m_router.current()->name() == "purchased")
        loadTab();
    emit changed();
}
void PurchasedService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    const auto generation = m_generation;
    if (m_closed || pending.session != m_api.sessionRevision())
        return;
    // These operations return Page<T> directly. Cloud/video responses have an
    // enclosing page object; applying that different contract loses all rows.
    const auto page = QJsonDocument::fromJson(bytes).object();
    const auto items = page.value("items").toArray();
    const auto total = page.value("total").isDouble()
                           ? quint64(qMax<qint64>(0, page.value("total").toInteger()))
                           : quint64(0);
    if (pending.kind == PendingKind::Songs) {
        const QPointer<PurchasedService> guard(this);
        m_songs.appendSongs(items);
        if (!guard || m_closed || generation != m_generation ||
            pending.session != m_api.sessionRevision())
            return;
        m_songTotal = total;
        m_songReceived += quint64(items.size());
        m_songPage = pending.page;
        m_songExhausted =
            items.isEmpty() ||
            (page.value("total").isDouble() ? m_songReceived >= total : items.size() < 30);
        m_songError.clear();
        continueSongs();
    } else {
        for (const auto &value : items) {
            const auto album = value.toObject();
            QStringList names;
            for (const auto &artist : album.value("artists").toArray()) {
                const auto name = artist.toObject().value("name").toString();
                if (!name.isEmpty())
                    names.append(name);
            }
            m_albumRows.append(QJsonObject{{"id", album.value("id")},
                                           {"title", album.value("title")},
                                           {"creator", names.join(" / ")},
                                           {"cover_url", album.value("cover_url")},
                                           {"song_count", album.value("song_count")}});
        }
        const QPointer<PurchasedService> guard(this);
        m_albums.replaceCards(m_albumRows);
        if (!guard || m_closed || generation != m_generation ||
            pending.session != m_api.sessionRevision())
            return;
        m_albumTotal = total;
        m_albumReceived += quint64(items.size());
        m_albumPage = pending.page;
        m_albumExhausted =
            items.isEmpty() ||
            (page.value("total").isDouble() ? m_albumReceived >= total : items.size() < 30);
        m_albumError.clear();
    }
    emit changed();
}
void PurchasedService::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (m_closed || pending.session != m_api.sessionRevision())
        return;
    const auto error = QJsonDocument::fromJson(bytes).object();
    const auto kind = error.value("kind").toString();
    const auto message = error.value("message").toString();
    auto &messageForTab = pending.kind == PendingKind::Songs ? m_songError : m_albumError;
    messageForTab = kind == QStringLiteral("authentication") ? tr("登录已失效，请重新登录")
                    : !message.isEmpty()                     ? message
                                                             : tr("已购音乐加载失败");
    emit changed();
}
void PurchasedService::shutdown()
{
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    cancelAll();
    emit changed();
}
} // namespace EchoMusic

namespace EchoMusic
{
void PurchasedService::registerRoutes(RouteController &router)
{
    router.registerPage({"purchased",
                         "/main/purchased",
                         QUrl("qrc:/qt/qml/EchoMusic/Purchased/PurchasedPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
