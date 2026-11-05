#include "ContentBlacklistService.h"
#include <QJsonDocument>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
ContentBlacklistService::ContentBlacklistService(ApiClient &api, AccountService &account,
                                                 QObject *parent)
    : QObject(parent), m_api(api), m_account(account), m_revision(api.sessionRevision())
{
    for (auto &b : m_buckets)
        b.model = std::make_unique<ContentBlacklistModel>(this);
    connect(&api, &ApiClient::succeeded, this, &ContentBlacklistService::succeeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { failed(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        clear();
        m_revision = revision;
        emit changed();
        if (m_active && current())
            read(m_category, true);
    });
    connect(&account, &AccountService::sessionAdopted, this, [this] {
        clear();
        m_revision = m_api.sessionRevision();
        emit changed();
        if (m_active && current())
            read(m_category, true);
    });
}
ContentBlacklistService::~ContentBlacklistService()
{
    shutdown();
}
bool ContentBlacklistService::current() const
{
    return !m_closing && m_account.userId() > 0 &&
           m_account.userId() == m_api.currentAccountUserId() &&
           m_revision == m_api.sessionRevision();
}
void ContentBlacklistService::clear()
{
    const auto singerWrite = std::exchange(m_singerWrite, 0);
    m_singerIntentId = 0;
    m_singerFullRead = false;
    m_singerName.clear();
    if (singerWrite)
        m_api.cancel(singerWrite);
    ++m_activation;
    for (auto &b : m_buckets) {
        const auto request = std::exchange(b.request, 0);
        b.page = 0;
        b.requestedPage = 0;
        b.pageSize = 30;
        b.lastPageCount = 0;
        b.total = -1;
        b.loaded = false;
        b.error.clear();
        b.model->update({}, false);
        if (request)
            m_api.cancel(request);
    }
    const auto write = std::exchange(m_write, 0);
    m_pending = {};
    m_submitted = {};
    if (write)
        m_api.cancel(write);
    // Song-level index and any pending menu intent belong to the old session.
    const auto index = std::exchange(m_songIndex.request, 0);
    m_songIndex = {};
    if (index)
        m_api.cancel(index);
    const auto menu = std::exchange(m_menuWrite, 0);
    m_menuIntent = {};
    if (menu)
        m_api.cancel(menu);
}
void ContentBlacklistService::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    clear();
}
QString ContentBlacklistService::countText(int category) const
{
    const auto &b = m_buckets[category];
    return b.loaded && b.total >= 0 ? QString::number(b.total) : QString();
}
bool ContentBlacklistService::more(int category) const
{
    const auto &b = m_buckets[category];
    return b.loaded &&
           (b.total >= 0 ? qint64(b.page) * b.pageSize < b.total : b.lastPageCount >= b.pageSize);
}
QString ContentBlacklistService::pendingName() const
{
    const auto name = m_pending.value("name").toString().trimmed();
    return name.isEmpty() ? (pendingSinger() ? tr("未命名歌手") : tr("未命名歌曲")) : name;
}
void ContentBlacklistService::activate()
{
    m_active = true;
    ++m_activation;
    read(m_category, true);
}
void ContentBlacklistService::deactivate()
{
    m_active = false;
    ++m_activation;
    m_pending = {};
    emit changed();
}
void ContentBlacklistService::selectCategory(int category)
{
    if (category < 0 || category > 1 || category == m_category)
        return;
    m_category = category;
    m_pending = {};
    emit changed();
    if (m_active && !loaded())
        read(category, true);
}
void ContentBlacklistService::refresh()
{
    if (m_active && !removing())
        read(m_category, true);
}
void ContentBlacklistService::loadMore()
{
    if (m_active && !removing() && hasMore())
        read(m_category, false);
}
void ContentBlacklistService::read(int category, bool refresh)
{
    auto &b = m_buckets[category];
    if (!current() || b.request)
        return;
    b.error.clear();
    b.requestedPage = refresh ? 1 : b.page + 1;
    b.request = m_api.contentBlacklist(category == 0 ? ApiClient::BlacklistKind::Song
                                                     : ApiClient::BlacklistKind::Singer,
                                       b.requestedPage, b.pageSize);
    if (!b.request) {
        b.error = tr("黑名单列表加载失败，请稍后重试");
        if (category == 1 && m_singerFullRead) {
            m_singerFullRead = false;
            m_singerIntentId = 0;
            m_singerName.clear();
            emit singerWriteFinished(false, b.error);
        }
    }
    emit changed();
}
void ContentBlacklistService::requestRemoval(const QString &key)
{
    if (!m_active || !current() || loading() || removing())
        return;
    m_pending = entries()->entry(key);
    m_pendingCategory = m_category;
    emit changed();
}
void ContentBlacklistService::cancelRemoval()
{
    if (!removing()) {
        m_pending = {};
        emit changed();
    }
}
void ContentBlacklistService::confirmRemoval()
{
    if (!m_active || !current() || !pendingRemoval() || removing() || singerBusy() || menuBusy())
        return;
    auto &b = m_buckets[m_pendingCategory];
    const auto key = m_pending.value("key").toString();
    if (b.model->entry(key) != m_pending) {
        m_pending = {};
        emit changed();
        return;
    }
    // Supersede an older read before the write can commit; offset pages must not
    // reinsert a removed entry. Partially loaded buckets realign from page one.
    const auto readRequest = std::exchange(b.request, 0);
    if (readRequest)
        m_api.cancel(readRequest);
    m_realign = more(m_pendingCategory);
    m_writeCategory = m_pendingCategory;
    m_submitted = m_pending;
    m_writeActivation = m_activation;
    b.error.clear();
    m_write = m_api.editContentBlacklist(
        m_writeCategory == 0 ? ApiClient::BlacklistKind::Song : ApiClient::BlacklistKind::Singer,
        key, m_pending.value("name").toString(), m_pending.value("mix_song_id").toString(), true);
    if (!m_write) {
        m_submitted = {};
        b.error = tr("撤销失败，请稍后重试");
    }
    emit changed();
}
void ContentBlacklistService::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!current())
        return;
    if (id && id == m_singerWrite) {
        m_singerWrite = 0;
        auto &b = m_buckets[1];
        const auto key = QString::number(m_singerIntentId);
        const auto old = std::exchange(b.request, 0);
        if (old)
            m_api.cancel(old);
        if (m_singerRemoving) {
            b.model->remove(key);
            if (b.total >= 0)
                b.total = qMax<qint64>(0, b.total - 1);
        } else {
            b.model->update(QJsonArray{QJsonObject{{"key", key}, {"name", m_singerName}}}, true);
            if (b.total >= 0)
                ++b.total;
        }
        m_singerIntentId = 0;
        m_singerName.clear();
        emit changed();
        emit singerWriteFinished(true, m_singerRemoving ? tr("已取消屏蔽") : tr("已屏蔽歌手"));
        return;
    }
    if (id && id == m_menuWrite) {
        // Keep the song index coherent with the successful page mutation without a second read.
        if (m_menuWriteRemove)
            m_songIndex.entries.remove(m_menuWriteKey);
        else
            m_songIndex.entries.insert(m_menuWriteKey,
                                       QJsonObject{{"key", m_menuWriteKey},
                                                   {"name", m_menuIntent.name},
                                                   {"mix_song_id", m_menuIntent.mixSongId}});
        m_menuWrite = 0;
        const auto message = m_menuWriteRemove ? tr("已撤销不感兴趣") : tr("已标记为不感兴趣");
        finishMenu(true, false, message);
        return;
    }
    if (id && id == m_songIndex.request) {
        const auto page = QJsonDocument::fromJson(bytes).object();
        const auto rows = page.value("items").toArray();
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto key = normalizedKey(row.value("key").toString());
            if (!key.isEmpty())
                m_songIndex.entries.insert(key, row);
        }
        const auto served = page.value("page").toInt(m_songIndex.requestedPage);
        m_songIndex.pageSize = qMax(1, page.value("page_size").toInt(m_songIndex.pageSize));
        m_songIndex.lastPageCount = rows.size();
        m_songIndex.total = page.value("total").isDouble() ? page.value("total").toInteger(-1) : -1;
        m_songIndex.request = 0;
        const auto morePages =
            !rows.isEmpty() &&
            (m_songIndex.total >= 0 ? qint64(served) * m_songIndex.pageSize < m_songIndex.total
                                    : rows.size() >= m_songIndex.pageSize);
        if (!morePages) {
            m_songIndex.loaded = true;
            m_songIndex.error.clear();
            emit changed();
            continueMenuIntent();
            return;
        }
        if (served >= 1000) {
            m_songIndex.error = tr("不感兴趣的歌曲过多，未能完成加载");
            if (m_menuIntent.active)
                finishMenu(false, false, m_songIndex.error);
            emit changed();
            return;
        }
        m_songIndex.requestedPage = quint32(served) + 1;
        advanceSongIndex();
        emit changed();
        return;
    }
    if (id && id == m_write) {
        auto &b = m_buckets[m_writeCategory];
        const auto name = m_submitted.value("name").toString().trimmed();
        const auto key = removingKey();
        // Closing/reopening can start a fresh read while this write is pending.
        // Its pre-commit snapshot must not restore the entry after removal.
        const auto overlappingRead = std::exchange(b.request, 0);
        if (overlappingRead)
            m_api.cancel(overlappingRead);
        b.model->remove(key);
        if (m_writeCategory == 0)
            m_songIndex.entries.remove(normalizedKey(key));
        if (b.total >= 0)
            b.total = qMax<qint64>(0, b.total - 1);
        m_write = 0;
        m_submitted = {};
        const bool visibleCompletion = m_active && m_activation == m_writeActivation;
        if (visibleCompletion) {
            m_pending = {};
            emit removalFinished(
                true,
                m_writeCategory == 0
                    ? tr("已撤销对“%1”的不感兴趣").arg(name.isEmpty() ? tr("未命名歌曲") : name)
                    : tr("已取消屏蔽“%1”").arg(name.isEmpty() ? tr("未命名歌手") : name));
        }
        if (m_realign || overlappingRead) {
            b.page = 0;
            read(m_writeCategory, true);
        }
        emit changed();
        return;
    }
    for (auto &b : m_buckets)
        if (id && id == b.request) {
            const auto page = QJsonDocument::fromJson(bytes).object();
            const auto rows = page.value("items").toArray();
            b.model->update(rows, b.requestedPage > 1);
            b.page = page.value("page").toInt(b.requestedPage);
            if (b.page <= 0)
                b.page = b.requestedPage;
            b.pageSize = page.value("page_size").toInt(b.pageSize);
            if (b.pageSize <= 0)
                b.pageSize = 30;
            b.lastPageCount = rows.size();
            b.total = page.value("total").isDouble() ? page.value("total").toInteger(-1) : -1;
            b.loaded = true;
            b.request = 0;
            b.error.clear();
            if (&b == &m_buckets[1] && m_singerFullRead) {
                if (more(1)) {
                    read(1, false);
                } else {
                    m_singerFullRead = false;
                    continueSinger();
                }
            }
            emit changed();
            return;
        }
}
void ContentBlacklistService::failed(quint64 id, const QByteArray &bytes)
{
    if (!current())
        return;
    const auto message = ApiClient::errorMessage(bytes);
    if (id && id == m_singerWrite) {
        m_singerWrite = 0;
        m_singerIntentId = 0;
        m_singerName.clear();
        emit changed();
        emit singerWriteFinished(false,
                                 message.isEmpty() ? tr("屏蔽操作失败，请稍后重试") : message);
        return;
    }
    if (id && id == m_buckets[1].request && m_singerFullRead) {
        m_singerFullRead = false;
        m_singerIntentId = 0;
        m_singerName.clear();
        emit singerWriteFinished(false,
                                 message.isEmpty() ? tr("屏蔽列表加载失败，请稍后重试") : message);
    }
    if (id && id == m_menuWrite) {
        m_menuWrite = 0;
        const auto fallback =
            m_menuWriteRemove ? tr("撤销失败，请稍后重试") : tr("操作失败，请稍后重试");
        finishMenu(false, false, message.isEmpty() ? fallback : message);
        return;
    }
    if (id && id == m_songIndex.request) {
        m_songIndex.request = 0;
        m_songIndex.error = message.isEmpty() ? tr("不感兴趣列表加载失败，请稍后重试") : message;
        if (m_menuIntent.active)
            finishMenu(false, false, m_songIndex.error);
        emit changed();
        return;
    }
    if (id && id == m_write) {
        m_write = 0;
        m_submitted = {};
        auto &b = m_buckets[m_writeCategory];
        b.error = message.isEmpty() ? tr("撤销失败，请稍后重试") : message;
        if (m_active && m_activation == m_writeActivation)
            emit removalFinished(false, b.error);
        emit changed();
        return;
    }
    for (auto &b : m_buckets)
        if (id && id == b.request) {
            b.request = 0;
            b.error = message.isEmpty() ? tr("黑名单列表加载失败，请稍后重试") : message;
            emit changed();
            return;
        }
}
ContentBlacklistService::SongStatus ContentBlacklistService::singerStatus(quint64 id) const
{
    if (!current() || !m_buckets[1].loaded || more(1))
        return SongStatus::Unknown;
    return m_buckets[1].model->entry(QString::number(id)).isEmpty() ? SongStatus::Absent
                                                                    : SongStatus::Present;
}
void ContentBlacklistService::ensureSingerIndex()
{
    if (!current() || m_singerFullRead)
        return;
    if (singerStatus(1) != SongStatus::Unknown)
        return;
    m_singerFullRead = true;
    if (!m_buckets[1].request) {
        if (m_buckets[1].loaded && more(1))
            read(1, false);
        else {
            m_buckets[1].pageSize = 500;
            read(1, true);
        }
    }
}
void ContentBlacklistService::toggleSinger(quint64 id, const QString &name)
{
    if (!current() || !id || name.trimmed().isEmpty() || singerBusy() || removing() || menuBusy())
        return;
    m_singerIntentId = id;
    m_singerName = name;
    emit changed();
    if (singerStatus(id) == SongStatus::Unknown)
        ensureSingerIndex();
    else
        continueSinger();
}
void ContentBlacklistService::continueSinger()
{
    if (!m_singerIntentId || m_singerWrite)
        return;
    m_singerRemoving = singerStatus(m_singerIntentId) == SongStatus::Present;
    m_singerWrite = m_api.editContentBlacklist(ApiClient::BlacklistKind::Singer,
                                               QString::number(m_singerIntentId), m_singerName, {},
                                               m_singerRemoving);
    if (!m_singerWrite) {
        m_singerIntentId = 0;
        emit singerWriteFinished(false, tr("屏蔽操作未能开始"));
    }
    emit changed();
}
QString ContentBlacklistService::normalizedKey(const QString &hash)
{
    return hash.trimmed().toLower();
}
ContentBlacklistService::SongStatus ContentBlacklistService::songStatus(const QString &hash) const
{
    if (!current() || !m_songIndex.loaded)
        return SongStatus::Unknown;
    const auto key = normalizedKey(hash);
    return !key.isEmpty() && m_songIndex.entries.contains(key) ? SongStatus::Present
                                                               : SongStatus::Absent;
}
void ContentBlacklistService::requestSongBlacklist(const QString &hash, const QString &name,
                                                   const QString &mixSongId, bool remove)
{
    if (m_closing || !current() || m_menuWrite || m_menuIntent.active)
        return;
    const auto key = normalizedKey(hash);
    const bool validKey = key.size() == 32 && std::all_of(key.cbegin(), key.cend(), [](QChar c) {
                              const auto n = c.unicode();
                              return (n >= '0' && n <= '9') || (n >= 'a' && n <= 'f');
                          });
    if (!validKey)
        return;
    m_menuIntent = {true, remove, key, name, mixSongId};
    emit changed();
    if (m_songIndex.loaded || m_songIndex.request) {
        // Loaded → decide now; a load already in flight → its completion resumes.
        continueMenuIntent();
        return;
    }
    ensureSongIndex();
}
void ContentBlacklistService::ensureSongIndex()
{
    if (m_songIndex.request)
        return;
    if (m_songIndex.loaded) {
        continueMenuIntent();
        return;
    }
    m_songIndex.requestedPage = 1;
    m_songIndex.error.clear();
    advanceSongIndex();
}
void ContentBlacklistService::advanceSongIndex()
{
    m_songIndex.request = m_api.contentBlacklist(
        ApiClient::BlacklistKind::Song, m_songIndex.requestedPage, quint32(m_songIndex.pageSize));
    if (!m_songIndex.request) {
        m_songIndex.error = tr("不感兴趣列表加载失败，请稍后重试");
        if (m_menuIntent.active)
            finishMenu(false, false, m_songIndex.error);
    }
    emit changed();
}
void ContentBlacklistService::continueMenuIntent()
{
    if (!m_menuIntent.active || m_menuWrite)
        return;
    if (!m_songIndex.loaded) {
        if (!m_songIndex.request && m_songIndex.error.isEmpty())
            ensureSongIndex();
        return;
    }
    const bool present = m_songIndex.entries.contains(m_menuIntent.key);
    if (!m_menuIntent.remove && present) {
        // 已标记时提示，并提供前往个人中心的入口。
        finishMenu(true, true, tr("已标记为不感兴趣，可在个人中心管理"));
        return;
    }
    if (m_menuIntent.remove && !present) {
        // 条目已不存在时静默清除待处理意图。
        m_menuIntent = {};
        emit changed();
        return;
    }
    beginMenuWrite();
}
void ContentBlacklistService::beginMenuWrite()
{
    m_menuWriteRemove = m_menuIntent.remove;
    m_menuWriteKey = m_menuIntent.key;
    m_menuWrite =
        m_api.editContentBlacklist(ApiClient::BlacklistKind::Song, m_menuIntent.key,
                                   m_menuIntent.name, m_menuIntent.mixSongId, m_menuIntent.remove);
    if (!m_menuWrite) {
        const auto message =
            m_menuIntent.remove ? tr("撤销失败，请稍后重试") : tr("操作失败，请稍后重试");
        finishMenu(false, false, message);
        return;
    }
    emit changed();
}
void ContentBlacklistService::finishMenu(bool success, bool informational, const QString &message)
{
    m_menuIntent = {};
    emit changed();
    emit menuWriteFinished(success, informational, message);
}
} // namespace EchoMusic
