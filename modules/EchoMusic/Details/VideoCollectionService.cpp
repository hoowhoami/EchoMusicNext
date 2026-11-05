#include "VideoCollectionService.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlEngine>
namespace EchoMusic
{
VideoCollectionService::VideoCollectionService(ApiClient &a, bool network, QObject *p)
    : QObject(p), m_api(a), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&a, &ApiClient::succeeded, this, &VideoCollectionService::accept);
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { fail(id, b); });
    connect(&a, &ApiClient::sessionChanged, this, &VideoCollectionService::reset);
    connect(&a, &ApiClient::closed, this, &VideoCollectionService::shutdown);
}
VideoCollectionService::~VideoCollectionService()
{
    shutdown();
}
void VideoCollectionService::reset()
{
    const auto readId = m_read;
    m_read = 0;
    if (readId)
        m_api.cancel(readId);
    const auto ids = m_writes.keys();
    m_writes.clear();
    for (auto id : ids)
        m_api.cancel(id);
    m_ids.clear();
    m_draft.clear();
    m_loaded = false;
    m_page = 1;
    m_error.clear();
    emit changed();
}
void VideoCollectionService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    reset();
    disconnect(&m_api, nullptr, this, nullptr);
}
bool VideoCollectionService::pending(const QString &id) const
{
    for (const auto &w : m_writes)
        if (w.first == id)
            return true;
    return false;
}
void VideoCollectionService::ensureLoaded()
{
    if (m_closed || !m_network || !m_api.currentAccountUserId() || m_loaded || m_read)
        return;
    m_page = 1;
    m_draft.clear();
    m_error.clear();
    read();
}
void VideoCollectionService::read()
{
    m_read = m_api.userVideoCollect(m_page, 30);
    if (!m_read)
        m_error = tr("收藏状态加载失败");
    emit changed();
}
void VideoCollectionService::toggle(const QString &id)
{
    if (m_closed || pending(id) || id.toULongLong() == 0)
        return;
    if (!m_api.currentAccountUserId()) {
        emit notice(tr("请先登录后再收藏"), true);
        return;
    }
    if (!m_loaded) {
        ensureLoaded();
        return;
    }
    const bool collect = !m_ids.contains(id);
    const auto request = m_api.collectVideo(id.toULongLong(), collect);
    if (request)
        m_writes.insert(request, {id, collect});
    else
        emit notice(tr("收藏请求未能启动"), true);
    emit changed();
}
void VideoCollectionService::accept(quint64 id, const QByteArray &b)
{
    if (m_closed)
        return;
    if (id == m_read && id) {
        m_read = 0;
        auto v = QJsonDocument::fromJson(b).object().value("page").toObject();
        auto items = v.value("items").toArray();
        const auto previousCount = m_draft.size();
        for (const auto &item : items) {
            const auto video = item.toObject().value("id").toVariant().toString();
            if (!video.isEmpty())
                m_draft.insert(video);
        }
        const auto total = v.value("total");
        bool more = m_draft.size() > previousCount &&
                    (!total.isNull() && !total.isUndefined()
                         ? quint64(m_page) * 30 < total.toVariant().toULongLong()
                         : items.size() >= 30);
        if (more) {
            ++m_page;
            read();
        } else {
            m_ids = m_draft;
            m_draft.clear();
            m_loaded = true;
            m_error.clear();
            emit changed();
        }
        return;
    }
    if (!m_writes.contains(id))
        return;
    auto w = m_writes.take(id);
    if (w.second)
        m_ids.insert(w.first);
    else
        m_ids.remove(w.first);
    emit mutated(w.first, w.second);
    emit notice(w.second ? tr("已收藏 MV") : tr("已取消收藏 MV"), false);
    emit changed();
}
void VideoCollectionService::fail(quint64 id, const QByteArray &b)
{
    auto message = ApiClient::errorMessage(b);
    if (message.isEmpty())
        message = tr("收藏状态加载失败");
    if (id == m_read && id) {
        m_read = 0;
        m_draft.clear();
        m_error = message;
        emit changed();
    } else if (m_writes.remove(id)) {
        emit notice(message, true);
        emit changed();
    }
}
} // namespace EchoMusic
