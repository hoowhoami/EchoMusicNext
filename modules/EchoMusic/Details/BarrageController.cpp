#include "BarrageController.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QQmlEngine>
namespace EchoMusic
{
BarrageController::BarrageController(ApiClient &a, RouteContext *r, bool network, QObject *p,
                                     bool song)
    : QObject(p), m_api(a), m_route(r), m_song(song), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&a, &ApiClient::messageSendCooldownChanged, this,
            &BarrageController::sendCooldownChanged);
    connect(&a, &ApiClient::succeeded, this, &BarrageController::accept);
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { fail(id, b); });
    connect(&a, &ApiClient::verificationRequired, this, [this](quint64 id, const QString &) {
        if (id && id == m_write)
            emit verificationStarted();
    });
    connect(&a, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_draft.clear();
        if (active())
            load();
        emit changed();
    });
    if (r)
        connect(r, &RouteContext::activeChanged, this, [this] {
            cancel();
            if (active())
                load();
        });
    connect(&a, &ApiClient::closed, this, &BarrageController::shutdown);
}
BarrageController::~BarrageController()
{
    shutdown();
}
void BarrageController::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    disconnect(&m_api, nullptr, this, nullptr);
}
bool BarrageController::active() const
{
    return !m_closed && m_enabled && m_presented && (!m_route || m_route->active()) &&
           !m_hash.isEmpty();
}
void BarrageController::cancel(bool cancelWrite)
{
    auto read = m_read, write = cancelWrite ? m_write : 0;
    m_read = 0;
    if (cancelWrite)
        m_write = 0;
    if (cancelWrite)
        emit scopeReset();
    if (read)
        m_api.cancel(read);
    if (write)
        m_api.cancel(write);
    m_pool = {};
    m_own.clear();
    m_recent.clear();
    m_cursor = 0;
    m_error.clear();
    emit resetFlights();
    emit changed();
}
void BarrageController::setResource(const QString &hash, const QString &name)
{
    m_name = name;
    if (m_hash == hash)
        return;
    cancel();
    m_hash = hash;
    m_draft.clear();
    emit changed();
    if (active())
        load();
}
void BarrageController::setPresented(bool v)
{
    if (m_presented == v)
        return;
    m_presented = v;
    cancel();
    if (active())
        load();
}
void BarrageController::setEnabled(bool v)
{
    if (m_enabled == v)
        return;
    m_enabled = v;
    cancel(false);
    if (active())
        load();
}
void BarrageController::setDraft(const QString &v)
{
    if (m_draft == v)
        return;
    m_draft = v;
    emit changed();
}
void BarrageController::load(bool preserve)
{
    if (!active() || !m_network)
        return;
    if (m_read)
        m_api.cancel(m_read);
    m_preserve = preserve;
    m_error.clear();
    m_session = m_api.sessionRevision();
    m_read = m_song ? m_api.songBarrage(m_hash) : m_api.videoBarrage(m_hash);
    if (!m_read)
        m_error = tr("弹幕加载失败，请关闭后重新开启");
    emit changed();
}
QString BarrageController::identity(const QVariantMap &v) const
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{v.value("user_id").toString(), v.value("text").toString()})
            .toJson(QJsonDocument::Compact));
}
QVariantMap BarrageController::next(bool ownOnly)
{
    if (!active())
        return {};
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto i = m_recent.begin(); i != m_recent.end();)
        if (i.value() <= now)
            i = m_recent.erase(i);
        else
            ++i;
    if (!m_own.isEmpty()) {
        auto v = m_own.dequeue();
        m_recent[identity(v)] = now + 20000;
        return v;
    }
    if (ownOnly)
        return {};
    for (qsizetype n = 0; n < m_pool.size(); ++n) {
        const auto v = m_pool[m_cursor++ % m_pool.size()].toObject();
        auto text = v.value("content").toString().trimmed();
        QVariantMap item{{"text", text},
                         {"user_id", v.value("user_id").toString()},
                         {"own", signedIn() && v.value("user_id").toString() ==
                                                   QString::number(m_api.currentAccountUserId())}};
        if (!text.isEmpty() && !m_recent.contains(identity(item)))
            return item;
    }
    return {};
}
void BarrageController::refreshFlying(const QVariantMap &v)
{
    const auto key = identity(v);
    if (m_recent.contains(key))
        m_recent[key] = QDateTime::currentMSecsSinceEpoch() + 5000;
}
void BarrageController::markFlying(const QVariantMap &v, int ms)
{
    auto key = identity(v);
    if (m_recent.contains(key))
        m_recent[key] = QDateTime::currentMSecsSinceEpoch() + qMax(20000, ms + 5000);
}
void BarrageController::send()
{
    if (m_closed || (m_route && !m_route->active()) || !m_presented || m_hash.isEmpty() || busy() ||
        sendCooldownSeconds() > 0)
        return;
    if (!signedIn()) {
        emit notice(tr("请先登录后再发送弹幕"), true);
        return;
    }
    auto text = m_draft.trimmed();
    if (text.isEmpty() || text.toUcs4().size() > 100) {
        emit notice(tr("弹幕不能为空，且最多 100 字"), true);
        return;
    }
    m_session = m_api.sessionRevision();
    m_sentDraft = m_draft;
    m_write = m_song ? m_api.sendSongBarrage(m_hash, m_name, text)
                     : m_api.sendVideoBarrage(m_hash, m_name, text);
    if (!m_write)
        emit notice(tr("弹幕请求未能启动"), true);
    emit changed();
}
void BarrageController::accept(quint64 id, const QByteArray &bytes)
{
    if (m_closed || (m_route && !m_route->active()) || !m_presented ||
        m_session != m_api.sessionRevision())
        return;
    if (id == m_read && id) {
        m_read = 0;
        auto rows = QJsonDocument::fromJson(bytes).object().value("comments").toArray();
        QJsonArray updated;
        for (const auto &value : rows) {
            if (value.toObject().value("content").toString().trimmed().isEmpty())
                continue;
            updated.append(value);
            if (updated.size() == 100)
                break;
        }
        if (!m_preserve || !updated.isEmpty()) {
            const auto nextRow =
                m_pool.isEmpty() ? QJsonObject{} : m_pool[m_cursor % m_pool.size()].toObject();
            const auto cursor = m_cursor;
            m_pool = std::move(updated);
            m_cursor = m_preserve && !m_pool.isEmpty() ? cursor % m_pool.size() : 0;
            if (m_preserve && !nextRow.isEmpty())
                for (qsizetype i = 0; i < m_pool.size(); ++i) {
                    const auto item = m_pool[i].toObject();
                    if (item.value("content") == nextRow.value("content") &&
                        item.value("user_id") == nextRow.value("user_id")) {
                        m_cursor = i;
                        break;
                    }
                }
        }
        emit changed();
    } else if (id == m_write && id) {
        m_write = 0;
        const auto content = m_sentDraft.trimmed();
        if (m_draft == m_sentDraft)
            m_draft.clear();
        if (active())
            m_own.enqueue({{"text", content},
                           {"user_id", QString::number(m_api.currentAccountUserId())},
                           {"own", true}});
        emit ownReady();
        emit notice(tr("已提交，展示结果以平台审核为准"), false);
        emit submitted();
        if (active())
            load(true);
        emit changed();
    }
}
void BarrageController::fail(quint64 id, const QByteArray &b)
{
    if (m_session != m_api.sessionRevision())
        return;
    if (id == m_read && id) {
        m_read = 0;
        if (m_pool.isEmpty())
            m_error = tr("弹幕加载失败，请关闭后重新开启");
        emit changed();
    } else if (id == m_write && id) {
        m_write = 0;
        auto message = ApiClient::errorMessage(b);
        emit notice(message.isEmpty() ? tr("弹幕发送失败，请重试") : message, true);
        emit submissionFailed();
        emit changed();
    }
}
} // namespace EchoMusic
