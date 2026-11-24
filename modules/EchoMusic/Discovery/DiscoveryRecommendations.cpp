#include "DiscoveryService.h"
#include <QJsonDocument>
namespace EchoMusic
{
void DiscoveryService::initializeCards()
{
    auto add = [this](const QString &key, const QString &title, SongCatalogModel *model = nullptr,
                      SongCatalogActions *actions = nullptr) {
        auto *card = new RecommendationCard(key, title, m_playback, model, actions, this);
        m_recommendationCards.add(card);
        connect(card->groups(), &RecommendationItemsModel::invalidated, this, [this, card] {
            card->setState(Idle);
            refreshCard(card->key());
        });
        if (!model)
            connect(card->collection(), &SongCatalogModel::cacheInvalidated, this, [this, card] {
                card->setState(Idle);
                refreshCard(card->key());
            });
        connect(card, &RecommendationCard::changed, this, [card] {
            if (card->status() == Ready || card->status() == Error)
                card->collection()->finishCacheRecovery();
        });
    };
    add("daily", tr("每日推荐"), &m_dailySongs, &m_dailyActions);
    add("style", tr("风格推荐"), &m_styleSongs, &m_styleActions);
    for (const auto &[id, title] :
         QList<QPair<QString, QString>>{{"3001", tr("私人专属好歌")},
                                        {"3004", tr("小众宝藏佳作")},
                                        {"3005", tr("潮流尝鲜")},
                                        {"3006", tr("VIP 专属推荐")},
                                        {"3014", tr("喜欢这首歌的 TA 也喜欢")},
                                        {"3101", tr("概念 er 新推")},
                                        {"3102", tr("喜欢「…」的 TA 也喜欢")},
                                        {"3103", tr("曲风盲盒·随机心动")},
                                        {"new", tr("新歌速递")},
                                        {"3107", tr("AI 免费听")}})
        add(id, title);
    m_recommendationCards.setPreferences(nullptr);
    connect(this, &DiscoveryService::changed, this, [this] {
        for (const auto &[key, part] :
             QList<QPair<QString, Part>>{{"style", Style}, {"daily", Daily}}) {
            auto *card = m_recommendationCards.find(key);
            if (card->status() != m_states[part] || card->errorMessage() != m_errors[part])
                card->setState(m_states[part], m_errors[part]);
        }
    });
}
void DiscoveryService::setDetailCard(const QString &key)
{
    auto *next = m_recommendationCards.find(key);
    if (m_detailCard != next) {
        cancelCardRequests();
        cancel(Daily);
        cancel(Style);
        if (m_detailCard)
            activateCard(m_detailCard->key(), false);
    }
    m_detailCard = next;
    emit changed();
}
void DiscoveryService::activateCard(const QString &key, bool active)
{
    auto *card = m_recommendationCards.find(key);
    if (!card || m_closed)
        return;
    if (active) {
        m_cardDemand.insert(key);
        card->collection()->setMetadataActive(m_networkEnabled);
        if (card->status() == Idle)
            requestCard(card);
    } else {
        m_cardDemand.remove(key);
        card->collection()->setMetadataActive(false);
    }
}
void DiscoveryService::refreshCard(const QString &key)
{
    auto *card = m_recommendationCards.find(key);
    if (!card || m_closed)
        return;
    bool recovered = card->groups()->retryRead();
    recovered = card->collection()->retryStorage() || recovered;
    if (recovered) {
        card->setState(Ready);
        return;
    }
    requestCard(card);
}
void DiscoveryService::requestCard(RecommendationCard *card)
{
    if (m_closed || !m_networkEnabled ||
        (m_activePage != "home" && m_activePage != "recommendation-detail"))
        return;
    int inFlight = m_cardPending.size();
    for (const auto &pending : m_pending)
        if (pending.part == Daily || pending.part == Style)
            ++inFlight;
    if (inFlight >= 3 && card->status() != Loading)
        return;
    if (card->key() == "daily") {
        request(Daily);
        return;
    }
    if (card->key() == "style") {
        request(Style);
        return;
    }
    for (auto id : m_cardPending.keys())
        if (m_cardPending[id].first == card) {
            m_cardPending.remove(id);
            m_api.cancel(id);
        }
    // At most three independent card requests. A completed batch admits the next
    // currently visible demand; offscreen cards never trigger eager full loading.
    if (m_cardPending.size() >= 3) {
        card->setState(Idle);
        return;
    }
    card->setState(Loading);
    const auto id = card->key() == "new" ? m_api.newSongCatalog()
                                         : m_api.discoveryCard(card->key().toUInt(), {});
    if (id)
        m_cardPending.insert(id, {card, m_api.sessionRevision()});
    else
        card->setState(Error, tr("请求未能启动"));
}
void DiscoveryService::cancelCardRequests()
{
    for (auto id : m_cardPending.keys()) {
        auto *card = m_cardPending[id].first;
        m_api.cancel(id);
        if (card->status() == Loading)
            card->setState(Idle);
    }
    m_cardPending.clear();
}
bool DiscoveryService::acceptCard(quint64 id, const QByteArray &bytes)
{
    if (!m_cardPending.contains(id))
        return false;
    const auto pending = m_cardPending.take(id);
    if (m_closed || pending.second != m_api.sessionRevision())
        return true;
    auto o = QJsonDocument::fromJson(bytes).object();
    pending.first->accept(o);
    for (const auto &key : m_cardDemand) {
        auto *card = m_recommendationCards.find(key);
        if (card && card->status() == Idle)
            requestCard(card);
    }
    return true;
}
bool DiscoveryService::failCard(quint64 id, const QString &message)
{
    if (!m_cardPending.contains(id))
        return false;
    const auto pending = m_cardPending.take(id);
    if (!m_closed && pending.second == m_api.sessionRevision())
        pending.first->setState(Error, message);
    for (const auto &key : m_cardDemand) {
        auto *card = m_recommendationCards.find(key);
        if (card && card->status() == Idle)
            requestCard(card);
    }
    return true;
}
} // namespace EchoMusic
