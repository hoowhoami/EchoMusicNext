#include "ProfileService.h"
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <utility>
#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrentRun>
namespace EchoMusic
{
ProfileService::ProfileService(ApiClient &api, AccountService &account, QObject *parent)
    : QObject(parent), m_api(api), m_account(account), m_devices(this),
      m_preferences(api, account, this), m_blacklist(api, account, this),
      m_revision(api.sessionRevision())
{
    m_chatRefresh.setSingleShot(true);
    m_chatRefresh.setInterval(350);
    connect(&m_chatRefresh, &QTimer::timeout, this, &ProfileService::refreshChat);
    for (auto &tab : m_social)
        tab.model = std::make_unique<SocialUserModel>(this);
    connect(&api, &ApiClient::succeeded, this, &ProfileService::succeeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { failed(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        clear();
        m_revision = revision;
        emit changed();
    });
    connect(&account, &AccountService::sessionAdopted, this, [this] {
        clear();
        m_revision = m_api.sessionRevision();
        emit changed();
    });
}
ProfileService::~ProfileService()
{
    shutdown();
}
bool ProfileService::current() const
{
    return !m_closing && m_account.userId() > 0 &&
           m_api.currentAccountUserId() == m_account.userId() &&
           m_revision == m_api.sessionRevision();
}
void ProfileService::clear()
{
    closeChat();
    const auto writes = m_followWrites.keys();
    m_followWrites.clear();
    m_confirmedFollows.clear();
    m_confirmedArtists.clear();
    for (auto id : writes)
        m_api.cancel(id);
    QList<quint64> requests;
    for (auto &tab : m_social) {
        requests.append(std::exchange(tab.request, 0));
        tab.loaded = false;
        tab.error.clear();
        tab.model->replace({});
    }
    requests.append(std::exchange(m_deviceRequest, 0));
    requests.append(std::exchange(m_deviceKick, 0));
    m_deviceKickRefresh = false;
    m_kickingDeviceKey.clear();
    m_pendingDevice = {};
    m_pendingDeviceTitle.clear();
    m_devicesActive = false;
    ++m_deviceActivation;
    requests.append(std::exchange(m_save, 0));
    requests.append(std::exchange(m_avatarRequest, 0));
    ++m_avatarGeneration;
    m_avatarReading = false;
    m_avatarSubmitting = false;
    m_devicesLoaded = false;
    m_devices.replace({});
    m_devicesError.clear();
    m_editorError.clear();
    m_patch = {};
    m_tab = SocialTab::Friends;
    // Invalidate owned IDs before cancellation signals can re-enter services.
    for (auto id : requests)
        if (id)
            m_api.cancel(id);
}
void ProfileService::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    m_preferences.shutdown();
    m_blacklist.shutdown();
    clear();
}
void ProfileService::openSocial(SocialTab tab)
{
    if (int(tab) < 0 || int(tab) >= int(m_social.size()))
        return;
    closeChat();
    m_tab = tab;
    loadSocial(tab, false);
    emit changed();
}
void ProfileService::refreshSocial()
{
    loadSocial(m_tab, true);
}
void ProfileService::loadSocial(SocialTab tab, bool force)
{
    if (!current())
        return;
    auto &state = m_social[int(tab)];
    if (force && state.request) {
        const auto old = std::exchange(state.request, 0);
        m_api.cancel(old);
    }
    if (state.request || (!force && state.loaded))
        return;
    state.error.clear();
    const std::array<ApiClient::SocialList, 4> lists{
        ApiClient::SocialList::Friends, ApiClient::SocialList::Follow, ApiClient::SocialList::Fans,
        ApiClient::SocialList::Visitors};
    state.request = m_api.socialUsers(lists[int(tab)]);
    emit changed();
}
bool ProfileService::socialPending(quint64 userId) const
{
    for (const auto &w : m_followWrites)
        if (!w.artist && w.id == userId)
            return true;
    return false;
}
bool ProfileService::artistPending(quint64 id) const
{
    for (const auto &w : m_followWrites)
        if (w.artist && w.id == id)
            return true;
    return false;
}
bool ProfileService::artistFollowed(quint64 id) const
{
    if (m_confirmedArtists.contains(id))
        return m_confirmedArtists[id];
    for (const auto &v : m_social[int(SocialTab::Follow)].model->rows())
        if (quint64(v.toObject()["artist_id"].toInteger()) == id)
            return true;
    return false;
}
void ProfileService::ensureArtistFollows()
{
    loadSocial(SocialTab::Follow, false);
}
void ProfileService::toggleArtistFollow(quint64 id)
{
    if (!current() || !id || artistPending(id))
        return;
    if (!artistFollowsLoaded()) {
        ensureArtistFollows();
        emit notice(tr("关注状态尚未加载，请稍后重试"), true);
        return;
    }
    const bool following = !artistFollowed(id);
    const auto req = m_api.followArtist(id, following);
    if (req)
        m_followWrites.insert(req, {id, following, true, {}});
    emit changed();
}
void ProfileService::toggleSocialFollow(const QString &key)
{
    if (!current() || socialLoading())
        return;
    const auto user = socialUsers()->entry(key);
    const auto id = quint64(user["user_id"].toInteger());
    if (!id || id == m_account.userId() || !user["following"].isBool() || socialPending(id))
        return;
    const bool following = !user["following"].toBool();
    const auto req = m_api.followUser(id, following);
    if (req)
        m_followWrites.insert(req, {id, following, false, user});
    emit changed();
}
QJsonArray ProfileService::reconcileFollows(int tab, QJsonArray rows, bool readback)
{
    if (tab > 2)
        return rows;
    for (auto it = m_confirmedFollows.begin(); it != m_confirmedFollows.end();) {
        auto &c = it.value();
        const auto uid = it.key();
        int found = -1;
        for (int i = 0; i < rows.size(); ++i)
            if (quint64(rows[i].toObject()["user_id"].toInteger()) == uid) {
                found = i;
                break;
            }
        const bool belongs = tab == 1   ? c.following
                             : tab == 0 ? (c.following && c.mutual.value_or(found >= 0))
                                        : found >= 0;
        const bool confirmed =
            tab == 2 ? (found < 0 || rows[found].toObject()["following"].toBool() == c.following)
                     : ((found >= 0) == belongs);
        if (readback && confirmed)
            c.remaining.remove(tab);
        if (found >= 0 && !belongs)
            rows.removeAt(found);
        else if (belongs) {
            auto row = found >= 0 ? rows[found].toObject() : c.user;
            row["following"] = c.following;
            if (c.mutual)
                row["mutual"] = *c.mutual;
            if (found >= 0)
                rows[found] = row;
            else
                rows.prepend(row);
        }
        if (c.remaining.isEmpty())
            it = m_confirmedFollows.erase(it);
        else
            ++it;
    }
    if (tab == 1) {
        for (auto it = m_confirmedArtists.begin(); it != m_confirmedArtists.end();) {
            bool present = false;
            for (const auto &v : rows)
                if (quint64(v.toObject()["artist_id"].toInteger()) == it.key())
                    present = true;
            if (readback && present == it.value())
                it = m_confirmedArtists.erase(it);
            else
                ++it;
        }
    }
    return rows;
}
void ProfileService::closeChat()
{
    m_chatRefresh.stop();
    const auto read = std::exchange(m_chatRead, 0), write = std::exchange(m_chatWrite, 0);
    m_chatTarget = {};
    m_chatMessages.clear();
    m_unmappedChatRecords = {};
    m_chatTag.clear();
    m_chatError.clear();
    m_chatSubmitted.clear();
    if (read)
        m_api.cancel(read);
    if (write)
        m_api.cancel(write);
    emit changed();
}
void ProfileService::openChat(const QString &key)
{
    if (!current())
        return;
    const auto user = socialUsers()->entry(key);
    const auto id = quint64(user["user_id"].toInteger());
    if (!id || id == m_account.userId())
        return;
    closeChat();
    m_chatTarget = user;
    refreshChat();
    emit changed();
}
void ProfileService::refreshChat()
{
    if (!current() || !chatActive() || m_chatWrite)
        return;
    const auto old = std::exchange(m_chatRead, 0);
    if (old)
        m_api.cancel(old);
    m_chatError.clear();
    m_chatRead = m_api.socialMessages(quint64(m_chatTarget["user_id"].toInteger()), m_chatTag);
    emit changed();
}
void ProfileService::sendChat(const QString &draft)
{
    if (!current() || !chatActive() || m_chatWrite)
        return;
    const auto text = draft.trimmed();
    if (text.isEmpty() || text.size() > 200) {
        emit notice(tr("私信最多200字"), true);
        return;
    }
    m_chatRefresh.stop();
    m_chatSubmitted = text;
    m_chatWrite = m_api.sendSocialMessage(quint64(m_chatTarget["user_id"].toInteger()), m_chatTag,
                                          text, m_account.nickname());
    if (!m_chatWrite) {
        m_chatSubmitted.clear();
        emit notice(tr("私信发送未能开始"), true);
    }
    emit changed();
}
void ProfileService::loadDevices(bool force)
{
    if (!current() || (!force && (m_devicesLoaded || m_deviceRequest)))
        return;
    if (force) {
        auto old = std::exchange(m_deviceRequest, 0);
        if (old)
            m_api.cancel(old);
        if (!current())
            return;
    }
    m_devicesError.clear();
    m_deviceRequest = m_api.loginDevices();
    emit changed();
}
void ProfileService::activateDevices()
{
    m_devicesActive = true;
    ++m_deviceActivation;
    loadDevices();
}
void ProfileService::deactivateDevices()
{
    m_devicesActive = false;
    ++m_deviceActivation;
    m_pendingDevice = {};
    m_pendingDeviceTitle.clear();
    emit changed();
}
void ProfileService::requestDeviceRemoval(const QString &key)
{
    if (!current() || !m_devicesActive || removingDevice())
        return;
    const auto device = m_devices.device(key);
    if (!LoginDeviceModel::removable(device))
        return;
    m_pendingDevice = device;
    for (int row = 0; row < m_devices.count(); ++row)
        if (m_devices.data(m_devices.index(row), LoginDeviceModel::Key).toString() == key) {
            m_pendingDeviceTitle =
                m_devices.data(m_devices.index(row), LoginDeviceModel::Title).toString();
            break;
        }
    emit changed();
}
void ProfileService::cancelDeviceRemoval()
{
    if (removingDevice())
        return;
    m_pendingDevice = {};
    m_pendingDeviceTitle.clear();
    emit changed();
}
void ProfileService::confirmDeviceRemoval()
{
    if (!current() || !m_devicesActive || removingDevice() || m_pendingDevice.isEmpty())
        return;
    const auto key = m_pendingDevice.value("id").toString();
    const auto device = m_devices.device(key);
    if (device != m_pendingDevice || !LoginDeviceModel::removable(device)) {
        cancelDeviceRemoval();
        return;
    }
    m_devicesError.clear();
    m_kickActivation = m_deviceActivation;
    m_kickingDeviceKey = key;
    m_deviceKick = m_api.kickLoginDevice({key, device.value("transport_fingerprint").toString(),
                                          quint64(device.value("active_at_seconds").toInteger()),
                                          quint32(device.value("app_id").toInteger()),
                                          quint32(device.value("client_version").toInteger())});
    if (!m_deviceKick) {
        m_kickingDeviceKey.clear();
        m_devicesError = tr("设备移除请求未能提交，请刷新后重试");
    }
    emit changed();
}
void ProfileService::beginEdit()
{
    m_editorError.clear();
    emit changed();
}
void ProfileService::uploadAvatar(const QUrl &file)
{
    if (!current() || uploadingAvatar())
        return;
    if (!file.isLocalFile()) {
        emit avatarFinished(false, tr("请选择本地 JPEG、PNG 或 GIF 图片"));
        return;
    }
    struct Input
    {
        QByteArray bytes;
        QString error;
    };
    const auto generation = ++m_avatarGeneration;
    m_avatarReading = true;
    emit changed();
    auto *watcher = new QFutureWatcher<Input>(this);
    connect(watcher, &QFutureWatcher<Input>::finished, this, [this, watcher, generation] {
        const auto input = watcher->result();
        watcher->deleteLater();
        if (!current() || generation != m_avatarGeneration)
            return;
        m_avatarReading = false;
        if (!input.error.isEmpty()) {
            emit changed();
            emit avatarFinished(false, input.error);
            return;
        }
        m_avatarSubmitting = false;
        m_avatarRequest = m_api.uploadUserAvatar(input.bytes);
        emit changed();
        if (!m_avatarRequest)
            emit avatarFinished(false, tr("头像上传失败，请稍后重试"));
    });
    watcher->setFuture(QtConcurrent::run([path = file.toLocalFile()] {
        QFile image(path);
        if (!image.open(QIODevice::ReadOnly))
            return Input{{}, QObject::tr("图片读取失败")};
        constexpr qint64 limit = 8 * 1024 * 1024;
        if (image.size() > limit)
            return Input{{}, QObject::tr("头像图片不能超过 8 MB")};
        auto bytes = image.read(limit + 1);
        if (bytes.size() > limit)
            return Input{{}, QObject::tr("头像图片不能超过 8 MB")};
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto format = reader.format().toLower();
        if ((format != "jpeg" && format != "png" && format != "gif") || !reader.canRead())
            return Input{{}, QObject::tr("请选择 JPEG、PNG 或 GIF 图片")};
        return Input{std::move(bytes), {}};
    }));
}
void ProfileService::saveProfile(const QString &name, int gender, const QString &birthday,
                                 const QString &signature, const QString &province,
                                 const QString &city)
{
    if (!current() || m_save)
        return;
    auto reject = [this](const QString &message, int tone = 2) {
        m_editorError = message;
        emit changed();
        emit editNotice(message, tone);
    };
    const auto nickname = name.trimmed(), p = province.trimmed(), c = city.trimmed(),
               b = birthday.trimmed();
    if (nickname.isEmpty()) {
        reject(tr("昵称不能为空"));
        return;
    }
    if (gender < 0 || gender > 2) {
        reject(tr("性别无效"));
        return;
    }
    // QString length counts UTF-16 code units just like the reference form.
    if (signature.size() > 120) {
        reject(tr("个性签名最多 120 个字符"));
        return;
    }
    if ((p.isEmpty() && !m_account.province().isEmpty()) ||
        (c.isEmpty() && !m_account.city().isEmpty())) {
        reject(tr("当前接口暂不支持清空所在地区"));
        return;
    }
    if (b != m_account.birthday() && !b.isEmpty()) {
        const auto date = QDate::fromString(b, Qt::ISODate);
        if (!date.isValid() || date.toString(Qt::ISODate) != b || date < QDate(1900, 1, 1) ||
            date > QDate::currentDate()) {
            reject(tr("请选择有效生日"));
            return;
        }
    }
    ApiClient::ProfilePatch patch;
    if (nickname != m_account.nickname().trimmed())
        patch.nickname = nickname;
    if (gender != m_account.gender())
        patch.gender = quint32(gender);
    if (b != m_account.birthday())
        patch.birthday = b;
    if (signature != m_account.biography())
        patch.signature = signature;
    if (!p.isEmpty() && p != m_account.province().trimmed())
        patch.province = p;
    if (!c.isEmpty() && c != m_account.city().trimmed())
        patch.city = c;
    if (!patch.nickname && !patch.gender && !patch.birthday && !patch.signature &&
        !patch.province && !patch.city) {
        reject(tr("资料没有变化"), 0);
        return;
    }
    m_patch = patch;
    m_editorError.clear();
    m_save = m_api.updateUserProfile(patch);
    if (!m_save) {
        m_editorError = tr("个人资料保存失败，请稍后重试");
        emit changed();
        emit editNotice(m_editorError, 3);
        return;
    }
    emit changed();
}
void ProfileService::succeeded(quint64 id, const QByteArray &payload)
{
    if (!id || !current())
        return;
    const auto document = QJsonDocument::fromJson(payload);
    if (id == m_chatRead) {
        m_chatRead = 0;
        m_unmappedChatRecords = document.object()["records"].toArray();
        // No sampled nonempty history contract yet. Retain provider records
        // without guessing identities or replacing acknowledged outgoing rows.
        m_chatError = m_unmappedChatRecords.isEmpty() ? QString() : tr("暂无法显示这些历史消息");
        emit changed();
        return;
    }
    if (id == m_chatWrite) {
        m_chatWrite = 0;
        const auto o = document.object();
        if (!o["tag"].toString().isEmpty())
            m_chatTag = o["tag"].toString();
        const auto old = std::exchange(m_chatRead, 0);
        if (old)
            m_api.cancel(old);
        const auto submitted = std::exchange(m_chatSubmitted, QString());
        const auto msgid = o["msgid"].toString();
        bool duplicate = false;
        for (const auto &v : std::as_const(m_chatMessages))
            if (!msgid.isEmpty() && v.toMap()["id"].toString() == msgid)
                duplicate = true;
        if (!duplicate)
            m_chatMessages.append(
                QVariantMap{{"id", msgid},
                            {"text", submitted},
                            {"isSelf", true},
                            {"nickname", m_account.nickname()},
                            {"avatar", m_account.avatarUrl()},
                            {"time", o["addtime"].isDouble()
                                         ? QDateTime::fromSecsSinceEpoch(o["addtime"].toInteger())
                                               .toString("MM-dd HH:mm")
                                         : QString()}});
        m_chatRefresh.start();
        emit changed();
        emit chatSent(submitted);
        emit notice(tr("私信已发送"), false);
        return;
    }
    if (m_followWrites.contains(id)) {
        const auto w = m_followWrites.take(id);
        const auto ack = document.object();
        if (w.artist) {
            m_confirmedArtists.insert(w.id, w.following);
            loadSocial(SocialTab::Follow, true);
        } else {
            ConfirmedFollow c{w.user, w.following, {}, QSet<int>{0, 1, 2}};
            if (ack["mutual"].isBool())
                c.mutual = ack["mutual"].toBool();
            else if (!w.following)
                c.mutual = false;
            else if (w.user["mutual"].isBool() && w.user["mutual"].toBool())
                c.mutual = true;
            const bool wasFriend = w.user["mutual"].toBool();
            const int friendDelta = c.mutual ? int(*c.mutual) - int(wasFriend) : 0;
            m_account.applySocialCountChange(ack["already"].toBool() ? 0
                                             : w.following           ? 1
                                                                     : -1,
                                             friendDelta);
            m_confirmedFollows.insert(w.id, c);
            for (int t = 0; t < 3; ++t) {
                auto &tab = m_social[t];
                const auto old = std::exchange(tab.request, 0);
                if (old)
                    m_api.cancel(old);
                tab.model->replace(reconcileFollows(t, tab.model->rows(), false));
                loadSocial(SocialTab(t), true);
            }
        }
        emit relationshipsMutated();
        emit changed();
        emit notice(ack["already"].toBool() ? tr("已关注该用户")
                    : w.following           ? tr("关注成功")
                                            : tr("已取消关注"),
                    false);
        return;
    }
    if (id == m_deviceKick) {
        m_deviceKick = 0;
        if (!document.object().value("removed").toBool()) {
            m_kickingDeviceKey.clear();
            m_devicesError = tr("设备移除失败，请重试");
            emit changed();
            return;
        }
        m_deviceKickRefresh = true;
        // Cancel any pre-write snapshot and fetch again, matching EchoMusic.
        loadDevices(true);
        if (!m_deviceRequest) {
            m_pendingDevice = {};
            m_pendingDeviceTitle.clear();
            m_deviceKickRefresh = false;
            m_kickingDeviceKey.clear();
            m_devicesError = tr("移除请求已提交，请刷新确认设备状态");
            emit changed();
        }
        return;
    }
    if (id == m_avatarRequest) {
        m_avatarRequest = 0;
        const auto object = document.object();
        if (!m_avatarSubmitting) {
            const auto file = object.value("file_name").toString();
            m_avatarSubmitting = true;
            if (!file.isEmpty())
                m_avatarRequest = m_api.updateUserAvatar(file);
            const auto generation = m_avatarGeneration;
            emit changed();
            if (current() && generation == m_avatarGeneration && !m_avatarRequest)
                emit avatarFinished(false, tr("图片已上传，但头像未能提交，请稍后重试"));
            return;
        }
        const auto avatar = object.value("avatar_url").toString();
        if (avatar.isEmpty()) {
            emit changed();
            emit avatarFinished(false, tr("头像上传失败，请稍后重试"));
            return;
        }
        const auto generation = m_avatarGeneration;
        m_account.applyAvatar(avatar);
        if (!current() || generation != m_avatarGeneration)
            return;
        emit changed();
        emit avatarFinished(true, object.value("review_pending").toBool()
                                      ? tr("头像已上传，正在审核中")
                                      : tr("头像已提交，审核完成后将正式生效"));
        return;
    }
    for (auto &tab : m_social) {
        if (id != tab.request)
            continue;
        tab.request = 0;
        const auto users = document.object().value("users");
        if (!users.isArray()) {
            tab.error = tr("列表响应无效");
            emit changed();
            return;
        }
        tab.loaded = true;
        const auto unmapped = document.object().value("unmapped_entries");
        // A successful transport must not masquerade as an empty friends list
        // when a nonempty provider shape still needs a real sampling model.
        tab.error = !unmapped.isUndefined() && !unmapped.isNull() ? tr("暂无法显示这部分好友资料")
                                                                  : QString();
        const int tabIndex = int(&tab - m_social.data());
        tab.model->replace(reconcileFollows(tabIndex, users.toArray(), true));
        emit changed();
        return;
    }
    if (id == m_deviceRequest) {
        m_deviceRequest = 0;
        if (!document.isArray()) {
            m_devicesError = tr("登录设备响应无效");
            if (m_deviceKickRefresh) {
                m_pendingDevice = {};
                m_pendingDeviceTitle.clear();
            }
            m_deviceKickRefresh = false;
            m_kickingDeviceKey.clear();
            emit changed();
            return;
        }
        m_devices.replace(document.array());
        m_devicesLoaded = true;
        if (m_deviceKickRefresh) {
            m_pendingDevice = {};
            m_pendingDeviceTitle.clear();
            const bool removed = m_devices.device(m_kickingDeviceKey).isEmpty();
            m_deviceKickRefresh = false;
            m_kickingDeviceKey.clear();
            if (!removed)
                m_devicesError = tr("设备仍在列表中，请刷新确认移除结果");
            if (m_devicesActive && m_kickActivation == m_deviceActivation)
                emit deviceRemovalFinished(removed,
                                           removed ? tr("登录设备已移除") : m_devicesError);
        }
        emit changed();
        return;
    }
    if (id == m_save) {
        m_save = 0;
        if (!document.object().value("updated").toBool()) {
            m_editorError = tr("个人资料保存失败，请稍后重试");
            emit changed();
            emit editNotice(m_editorError, 3);
            return;
        }
        const auto revision = m_revision;
        const auto patch = std::exchange(m_patch, {});
        m_account.applyProfilePatch(patch);
        if (!current() || revision != m_revision)
            return;
        emit changed();
        if (!m_save) {
            emit profileSaved();
            emit editNotice(tr("个人资料已更新"), 1);
        }
    }
}
void ProfileService::failed(quint64 id, const QByteArray &payload)
{
    if (!id || !current())
        return;
    const auto message = QJsonDocument::fromJson(payload).object()["message"].toString();
    if (id == m_chatRead) {
        m_chatRead = 0;
        m_chatError = message.isEmpty() ? tr("私信记录获取失败，请稍后重试") : message;
        emit changed();
        return;
    }
    if (id == m_chatWrite) {
        m_chatWrite = 0;
        m_chatSubmitted.clear();
        emit changed();
        emit notice(message.isEmpty() ? tr("私信发送失败，草稿已保留；请确认会话后重试") : message,
                    true);
        return;
    }
    if (m_followWrites.contains(id)) {
        m_followWrites.remove(id);
        emit changed();
        emit notice(message.isEmpty() ? tr("关注操作失败，请稍后重试") : message, true);
        return;
    }
    if (id == m_deviceKick) {
        m_deviceKick = 0;
        m_kickingDeviceKey.clear();
        const bool expired =
            QJsonDocument::fromJson(payload).object().value("kind") == "authentication";
        m_devicesError = expired ? tr("登录已失效，请重新登录") : tr("设备移除失败，请重试");
        emit changed();
        if (m_devicesActive && m_kickActivation == m_deviceActivation)
            emit deviceRemovalFinished(false, m_devicesError);
        return;
    }
    if (id == m_avatarRequest) {
        m_avatarRequest = 0;
        emit changed();
        emit avatarFinished(false, tr("头像上传失败，请稍后重试"));
        return;
    }
    for (auto &tab : m_social) {
        if (tab.request != id)
            continue;
        tab.request = 0;
        tab.error = tr("列表加载失败，请稍后重试");
        emit changed();
        return;
    }
    if (id == m_deviceRequest) {
        m_deviceRequest = 0;
        if (m_deviceKickRefresh) {
            m_pendingDevice = {};
            m_pendingDeviceTitle.clear();
        }
        const bool expired =
            QJsonDocument::fromJson(payload).object().value("kind") == "authentication";
        m_devicesError =
            expired ? (m_deviceKickRefresh ? tr("移除请求已成功，当前登录已失效，请重新登录")
                                           : tr("登录已失效，请重新登录"))
            : m_deviceKickRefresh ? tr("移除请求已提交，设备列表刷新失败，请刷新确认")
                                  : tr("登录设备获取失败");
        m_deviceKickRefresh = false;
        m_kickingDeviceKey.clear();
        emit changed();
        return;
    }
    if (id == m_save) {
        m_save = 0;
        m_patch = {};
        m_editorError = tr("个人资料保存失败，请稍后重试");
        emit changed();
        emit editNotice(m_editorError, 3);
    }
}
} // namespace EchoMusic
