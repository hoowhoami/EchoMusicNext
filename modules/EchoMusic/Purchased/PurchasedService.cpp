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
    // Raw goods rows paginate for the future mapping while the visible models
    // stay empty; exhaustion follows the provider totals.
    if (m_tab == 0)
        return !m_songExhausted && m_songTotal > 0 && m_songReceived < m_songTotal;
    return !m_albumExhausted && m_albumTotal > 0 && m_albumReceived < m_albumTotal;
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
    if (!m_songPage && !m_songExhausted)
        request(PendingKind::Songs, 1);
    if (!m_albumPage && !m_albumExhausted)
        request(PendingKind::Albums, 1);
}
void PurchasedService::request(PendingKind kind, quint32 page)
{
    if (hasPending(kind))
        return;
    const auto request = kind == PendingKind::Songs ? m_api.purchasedSongs(page, 30)
                                                    : m_api.purchasedAlbums(page, 30);
    if (!request) {
        m_error = tr("已购音乐加载失败");
        emit changed();
        return;
    }
    m_pending.insert(request, {kind, m_api.sessionRevision()});
    m_error.clear();
    emit changed();
}
void PurchasedService::refresh()
{
    if (m_closed)
        return;
    cancelAll();
    m_songPage = m_albumPage = 0;
    m_songReceived = m_albumReceived = 0;
    m_songRows = m_albumRows = {};
    m_songs.replaceSongs({});
    m_albums.replaceCards({});
    m_songExhausted = m_albumExhausted = false;
    m_error.clear();
    if (m_active)
        loadTab();
    emit changed();
}
void PurchasedService::loadMore()
{
    if (m_closed || !m_active || !m_network || !m_api.currentAccountUserId())
        return;
    const auto kind = m_tab == 0 ? PendingKind::Songs : PendingKind::Albums;
    if (hasPending(kind) || !hasMore())
        return;
    const auto page = m_tab == 0 ? m_songPage + 1 : m_albumPage + 1;
    request(kind, page);
}
void PurchasedService::cancelAll()
{
    for (const auto id : m_pending.keys()) {
        m_pending.remove(id);
        m_api.cancel(id);
    }
}
void PurchasedService::reset()
{
    if (m_pending.isEmpty() && !m_songPage && !m_albumPage)
        return;
    cancelAll();
    m_songPage = m_albumPage = 0;
    m_songReceived = m_albumReceived = 0;
    m_songTotal = m_albumTotal = 0;
    m_songRows = m_albumRows = {};
    m_songs.replaceSongs({});
    m_albums.replaceCards({});
    m_songExhausted = m_albumExhausted = false;
    m_error.clear();
    if (m_active && m_router.current() && m_router.current()->name() == "purchased")
        loadTab();
    emit changed();
}
void PurchasedService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    if (m_closed || pending.session != m_api.sessionRevision())
        return;
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto page = object.value("page").toObject();
    const auto items = page.value("items").toArray();
    const auto total = page.value("total").isDouble()
                           ? quint64(qMax<qint64>(0, page.value("total").toInteger()))
                           : quint64(0);
    if (pending.kind == PendingKind::Songs) {
        // Raw goods rows stay verbatim until a non-empty sample exists; the
        // model intentionally stays empty instead of fabricating fields.
        m_songRows += items;
        m_songTotal = total;
        m_songReceived += quint64(items.size());
        m_songPage = quint32(page.value("page").toInteger(1));
        m_songExhausted = items.isEmpty() || (total > 0 && m_songReceived >= total);
    } else {
        m_albumRows += items;
        m_albumTotal = total;
        m_albumReceived += quint64(items.size());
        m_albumPage = quint32(page.value("page").toInteger(1));
        m_albumExhausted = items.isEmpty() || (total > 0 && m_albumReceived >= total);
    }
    m_error.clear();
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
    m_error = kind == QStringLiteral("authentication") ? tr("登录已失效，请重新登录")
              : !message.isEmpty()                     ? message
                                                       : tr("已购音乐加载失败");
    emit changed();
}
void PurchasedService::shutdown()
{
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
