#include "SavedAccountModel.h"
#include "AccountService.h"
#include "StorageService.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QLocale>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
SavedAccountModel::SavedAccountModel(ApiClient &api, AccountService &account,
                                     StorageService &storage, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_account(account), m_storage(storage)
{
    m_validationDeadline.setSingleShot(true);
    m_validationDeadline.setInterval(15000);
    connect(&m_validationDeadline, &QTimer::timeout, this, [this] {
        if (!m_operation || !m_operation->validationRequest)
            return;
        const auto id = m_operation->validationRequest;
        validationFailed(id, QByteArrayLiteral("{\"kind\":\"timeout\"}"));
        m_api.cancelRequest(id);
    });
    connect(&api, &ApiClient::verificationRequired, this, [this](quint64 id, const QString &) {
        if (m_operation && m_operation->validationRequest == id)
            m_validationDeadline.stop();
    });
    connect(&api, &ApiClient::verificationDismissed, this, [this] {
        if (m_operation && m_operation->validationRequest && !m_operation->cancelled)
            m_validationDeadline.start();
    });
    connect(&storage, &StorageService::accountsReady, this, &SavedAccountModel::sqlReady);
    connect(&storage, &StorageService::accountsFailed, this, [this](quint64 id) {
        if (!id) {
            m_error = tr("无法保存账号身份显示");
            emit changed();
            return;
        }
        if (m_operation && id == m_operation->id)
            fail();
    });
    connect(&storage, &StorageService::failed, this, [this] {
        m_ready = false;
        cancelSwitch();
        m_validationDeadline.stop();
        m_queue.clear();
        m_operation.reset();
        m_error = tr("账号存储无法初始化，请重启后重试");
        emit changed();
        pump();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        if (!m_restoring)
            cancelSwitch();
        const bool reopen = m_durationOpen;
        endDurationQueries();
        if (reopen) {
            m_durationOpen = true;
            QTimer::singleShot(0, this, [this] {
                if (m_durationOpen)
                    beginDurationQueries();
            });
        }
    });
    connect(&api, &ApiClient::succeeded, this, &SavedAccountModel::validationSucceeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { validationFailed(id, bytes); });
    connect(&api, &ApiClient::succeeded, this, [this](quint64 id, const QByteArray &bytes) {
        if (!m_durationRequests.contains(id) || m_durationRevision != m_api.sessionRevision())
            return;
        const auto user = m_durationRequests.take(id);
        const auto data = QJsonDocument::fromJson(bytes).object();
        if (data.value("user_id").toInteger() == qint64(user) &&
            data.value("listening_seconds").toInteger(-1) >= 0)
            m_durations.insert(user, data.value("listening_seconds").toInteger());
        if (!m_rows.isEmpty())
            emit dataChanged(index(0, 0), index(count() - 1, 0),
                             {DurationRole, DurationLoadingRole});
        pumpDurationQueries();
    });
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (!m_durationRequests.remove(id))
            return;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0, 0), index(count() - 1, 0), {DurationLoadingRole});
        pumpDurationQueries();
    });
    connect(&account, &AccountService::sessionAdopted, this, [this] {
        if (!m_restoring)
            cancelSwitch();
    });
    connect(&account, &AccountService::changed, this, [this] {
        auto session = m_account.storedSession();
        if (session != m_lastSession) {
            const auto previous = QJsonDocument::fromJson(m_lastSession).object();
            const auto next = QJsonDocument::fromJson(session).object();
            // Display metadata may finish loading while a candidate is checked.
            // Only a credential change invalidates that candidate generation.
            if (previous.value("user_id") != next.value("user_id") ||
                previous.value("token") != next.value("token") ||
                previous.value("refresh_credential") != next.value("refresh_credential"))
                ++m_generation;
            m_lastSession = session;
            if (!session.isEmpty() && !m_restoring && !m_closing) {
                Operation op;
                op.kind = Kind::Save;
                op.session = session;
                const auto authorization = QJsonDocument::fromJson(session).object();
                op.record = {m_account.userId(),
                             m_account.nickname(),
                             m_account.avatarUrl(),
                             authorization.value("token").toString(),
                             QDateTime::currentMSecsSinceEpoch(),
                             {},
                             authorization.value("refresh_credential").toString(),
                             authorization.value("expires_at_seconds").toInteger()};
                enqueue(std::move(op));
            }
        }
        m_switchErrors.remove(m_account.userId());
        m_expiredAccounts.remove(m_account.userId());
        publish(m_rows);
    });
}
int SavedAccountModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QVariant SavedAccountModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= count())
        return {};
    const auto &v = m_rows[index.row()];
    switch (role) {
    case UserIdRole:
        return QVariant::fromValue(v.userId);
    case Qt::DisplayRole:
    case NicknameRole:
        return v.userId == m_account.userId() ? m_account.nickname() : v.nickname;
    case AvatarRole:
        return v.userId == m_account.userId() ? m_account.avatarUrl() : v.avatarUrl;
    case AvatarIdentityIconRole:
        return v.userId == m_account.userId() ? m_account.avatarIdentityIcon()
                                              : AccountIdentity::avatarIcon(v.avatarIdentityKind);
    case AvatarIdentityLabelRole:
        return v.userId == m_account.userId() ? m_account.avatarIdentityLabel()
                                              : AccountIdentity::avatarLabel(v.avatarIdentityKind);
    case CurrentRole:
        return v.userId == m_account.userId();
    case DurationRole: {
        if (v.userId == m_account.userId() && !m_account.listeningMinutes().isEmpty())
            return m_account.listeningMinutes();
        const auto duration = m_durations.constFind(v.userId);
        return duration == m_durations.cend()
                   ? QString()
                   : QLocale(QLocale::Chinese).toString(*duration / 60) + tr(" 分钟");
    }
    case SwitchCheckingRole:
        return v.userId == switchingUserId();
    case SwitchErrorRole:
        return m_switchErrors.value(v.userId);
    case LoginExpiredRole:
        return m_expiredAccounts.contains(v.userId);
    case DurationLoadingRole:
        return v.userId == m_account.userId() ? m_account.gradeLoading()
                                              : m_durationQueue.contains(v.userId) ||
                                                    m_durationRequests.values().contains(v.userId);
    default:
        return {};
    }
}
QHash<int, QByteArray> SavedAccountModel::roleNames() const
{
    return {{UserIdRole, "accountId"},
            {NicknameRole, "accountName"},
            {AvatarRole, "avatarUrl"},
            {CurrentRole, "isCurrent"},
            {DurationRole, "listeningDuration"},
            {DurationLoadingRole, "durationLoading"},
            {AvatarIdentityIconRole, "avatarIdentityIcon"},
            {AvatarIdentityLabelRole, "avatarIdentityLabel"},
            {SwitchCheckingRole, "switchChecking"},
            {SwitchErrorRole, "switchError"},
            {LoginExpiredRole, "loginExpired"}};
}
const StoredAccount *SavedAccountModel::find(quint64 userId) const
{
    for (const auto &v : m_rows)
        if (v.userId == userId)
            return &v;
    return nullptr;
}
void SavedAccountModel::publish(const StoredAccounts &accounts)
{
    auto rows = accounts;
    std::stable_sort(rows.begin(), rows.end(), [this](const auto &a, const auto &b) {
        if ((a.userId == m_account.userId()) != (b.userId == m_account.userId()))
            return a.userId == m_account.userId();
        return a.lastUsedAt > b.lastUsedAt;
    });
    bool sameOrder = rows.size() == m_rows.size();
    for (int i = 0; sameOrder && i < rows.size(); ++i)
        sameOrder = rows[i].userId == m_rows[i].userId;
    if (sameOrder) {
        for (int i = 0; i < rows.size(); ++i) {
            const bool change = rows[i] != m_rows[i] || m_presentedUser != m_account.userId();
            m_rows[i] = rows[i];
            if (change)
                emit dataChanged(index(i, 0), index(i, 0));
        }
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    // Update only a completed current profile. Do not clear stored identity
    // during login restoration or loading, or rotate credentials for a profile update.
    if (m_ready && !m_closing && m_account.profileDetailAvailable())
        for (const auto &row : m_rows)
            if (row.userId == m_account.userId() &&
                row.avatarIdentityKind != m_account.avatarIdentityKind())
                m_storage.updateAccountIdentity(row.userId, m_account.avatarIdentityKind());
    m_presentedUser = m_account.userId();
    emit changed();
    for (int i = 0; i < count(); ++i)
        if (m_rows[i].userId == m_account.userId())
            emit dataChanged(index(i, 0), index(i, 0),
                             {NicknameRole, AvatarRole, DurationRole, DurationLoadingRole,
                              AvatarIdentityIconRole, AvatarIdentityLabelRole});
}
bool SavedAccountModel::ownsSwitchRequest(quint64 requestId) const
{
    return requestId && m_operation && m_operation->validationRequest == requestId;
}
quint64 SavedAccountModel::switchingUserId() const
{
    return m_operation && m_operation->kind == Kind::Switch && !m_operation->restoring &&
                   !m_operation->cancelled
               ? m_operation->record.userId
               : 0;
}
void SavedAccountModel::publishSwitchState()
{
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0), index(count() - 1, 0),
                         {SwitchCheckingRole, SwitchErrorRole, LoginExpiredRole});
    emit changed();
}
void SavedAccountModel::cancelSwitch()
{
    if (!m_operation || m_operation->kind != Kind::Switch || m_operation->restoring ||
        m_operation->cancelled)
        return;
    m_operation->cancelled = true;
    const auto request = std::exchange(m_operation->validationRequest, 0);
    const bool committing = m_operation->committing;
    if (request)
        m_api.cancelRequest(request);
    if (!committing)
        finish();
    else
        publishSwitchState();
}
bool SavedAccountModel::adoptSwitch()
{
    const auto record = m_operation->record;
    const auto profile = m_operation->profile;
    const auto bytes = QJsonDocument(QJsonObject{{"user_id", qint64(record.userId)},
                                                 {"token", record.token},
                                                 {"refresh_credential", record.refreshCredential},
                                                 {"expires_at_seconds", record.expiresAtSeconds},
                                                 {"nickname", record.nickname},
                                                 {"avatar_url", record.avatarUrl}})
                           .toJson(QJsonDocument::Compact);
    // Suppress only the persistence echo of this adoption. Session listeners
    // must still reset every account-owned consumer after the commit succeeds.
    m_restoring = true;
    const bool adopted = m_account.adoptStoredSession(bytes, profile);
    m_restoring = false;
    if (adopted && m_operation && m_account.userId() == record.userId)
        m_operation->generation = m_generation;
    return adopted;
}
void SavedAccountModel::validationSucceeded(quint64 id, const QByteArray &payload)
{
    if (!m_operation || m_operation->kind != Kind::Switch || m_operation->restoring || !id ||
        m_operation->validationRequest != id)
        return;
    if (m_operation->cancelled || m_operation->generation != m_generation || m_closing) {
        cancelSwitch();
        return;
    }
    const auto profile = QJsonDocument::fromJson(payload).object();
    if (profile.value("id").toInteger() != qint64(m_operation->record.userId)) {
        validationFailed(id, QByteArrayLiteral("{\"kind\":\"invalid_profile\"}"));
        return;
    }
    auto &op = *m_operation;
    m_validationDeadline.stop();
    op.validationRequest = 0;
    op.profile = profile;
    op.record.nickname = profile.value("nickname").toString();
    op.record.avatarUrl = profile.value("avatar_url").toString();
    op.record.lastUsedAt = QDateTime::currentMSecsSinceEpoch();
    op.committing = true;
    m_storage.saveAccount(op.id, op.record, true);
}
void SavedAccountModel::validationFailed(quint64 id, const QByteArray &payload)
{
    if (!m_operation || m_operation->kind != Kind::Switch || m_operation->restoring ||
        m_operation->validationRequest != id || m_operation->committing)
        return;
    const auto user = m_operation->record.userId;
    const auto kind = QJsonDocument::fromJson(payload).object().value("kind").toString();
    if (!m_operation->cancelled && m_operation->generation == m_generation &&
        kind != QStringLiteral("cancelled")) {
        if (kind == QStringLiteral("authentication") || kind == QStringLiteral("login_required")) {
            m_expiredAccounts.insert(user);
            m_switchErrors.insert(user, tr("登录已失效，请重新登录"));
        } else {
            m_switchErrors.insert(user, tr("暂时无法验证，请重试"));
        }
    }
    finish();
}
bool SavedAccountModel::switchAccount(quint64 userId)
{
    if (!m_ready || m_closing || busy() || userId == m_account.userId())
        return false;
    const auto *record = find(userId);
    if (!record)
        return false;
    Operation op;
    op.kind = Kind::Switch;
    op.record = *record;
    m_switchErrors.remove(userId);
    m_expiredAccounts.remove(userId);
    enqueue(std::move(op));
    publishSwitchState();
    return true;
}
bool SavedAccountModel::forgetAccount(quint64 userId)
{
    if (m_closing)
        return false;
    if (userId == m_account.userId()) {
        emit notice(tr("当前登录账号不能移除，请先切换账号"), true);
        return false;
    }
    if (!m_ready || m_closing || busy())
        return false;
    const auto *record = find(userId);
    if (!record)
        return false;
    Operation op;
    op.kind = Kind::Remove;
    op.explicitRemoval = true;
    op.record = *record;
    enqueue(std::move(op));
    return true;
}
void SavedAccountModel::logout()
{
    if (m_closing)
        return;
    // Even a logout before asynchronous startup restoration must invalidate
    // the pending SQL restoration; an empty visible session is not a no-op here.
    cancelSwitch();
    ++m_generation;
    const auto userId = m_account.userId();
    const auto *record = find(userId);
    Operation op;
    op.kind = userId ? Kind::Remove : Kind::Deselect;
    op.record = record ? *record : StoredAccount{userId, {}, {}, {}, 0};
    m_account.signOut();
    enqueue(std::move(op));
}
void SavedAccountModel::enqueue(Operation op)
{
    op.id = ++m_next;
    op.generation = m_generation;
    m_error.clear();
    m_queue.enqueue(std::move(op));
    emit changed();
    pump();
}
bool SavedAccountModel::restoringStartupAccount() const
{
    return m_restoring && m_operation && m_operation->restoring;
}
void SavedAccountModel::setStartupHeld(bool held)
{
    if (m_closing || m_startupHeld == held)
        return;
    m_startupHeld = held;
    if (!held)
        pump();
}
void SavedAccountModel::pump()
{
    // A successful device registration must precede session adoption and the
    // account/profile/playlist requests it triggers. Keep the actual queued op.
    if (!m_closing && m_startupHeld && !m_operation && !m_queue.isEmpty() &&
        m_queue.head().restoring)
        return;
    if (m_operation || !m_ready || m_queue.isEmpty()) {
        if (m_closing && !busy() && !m_drained) {
            m_drained = true;
            emit closed();
        }
        return;
    }
    m_operation = m_queue.dequeue();
    auto &op = *m_operation;
    if (op.kind == Kind::Save) {
        m_storage.saveAccount(op.id, op.record, op.session == m_account.storedSession());
    } else if (op.kind == Kind::Switch) {
        if (!op.restoring) {
            op.validationRequest = m_api.validateAccount(op.record.userId, op.record.token,
                                                         op.record.refreshCredential);
            m_validationDeadline.start();
            if (!op.validationRequest)
                validationFailed(0, QByteArrayLiteral("{\"kind\":\"unavailable\"}"));
            publishSwitchState();
            return;
        }
        // Startup restoration keeps its existing offline behavior. Only an
        // explicit switch validates before installing a candidate session.
        const auto id = op.id;
        QTimer::singleShot(0, this, [this, id] {
            if (!m_operation || m_operation->id != id)
                return;
            if (m_operation->generation != m_generation || m_closing) {
                finish();
                return;
            }
            if (!adoptSwitch()) {
                fail();
                return;
            }
            m_operation->record.lastUsedAt = QDateTime::currentMSecsSinceEpoch();
            m_storage.saveAccount(id, m_operation->record, true);
        });
    } else if (op.kind == Kind::Deselect) {
        m_storage.selectAccount(op.id, 0);
    } else {
        m_storage.removeAccount(op.id, op.record.userId);
    }
}
void SavedAccountModel::sqlReady(quint64 id, const StoredAccounts &accounts, quint64 active)
{
    if (!id) {
        m_ready = true;
        m_error.clear();
        publish(accounts);
        if (active && !m_account.userId() && m_generation == 0 && !m_closing) {
            if (const auto *record = find(active)) {
                Operation op;
                op.kind = Kind::Switch;
                op.record = *record;
                op.restoring = true;
                enqueue(std::move(op));
            }
        }
        pump();
        return;
    }
    if (!m_operation || id != m_operation->id)
        return;
    auto &pending = *m_operation;
    if (pending.kind == Kind::Switch && !pending.restoring) {
        if (pending.rollingBack) {
            publish(accounts);
            finish();
            return;
        }
        if (pending.cancelled || pending.generation != m_generation || m_closing ||
            !adoptSwitch()) {
            // The SQL transaction may have committed while the dialog closed.
            // Serialize restoration before subsequent login/logout writes.
            m_operation->rollingBack = true;
            const auto current = find(m_account.userId()) ? m_account.userId() : 0;
            m_storage.selectAccount(id, current);
            return;
        }
    }
    publish(accounts);
    auto &op = *m_operation;
    const auto name =
        op.record.nickname.isEmpty() ? QString::number(op.record.userId) : op.record.nickname;
    const bool currentNotice = !m_closing && op.generation == m_generation;
    if (op.kind == Kind::Switch && !op.restoring && currentNotice) {
        m_switchErrors.remove(op.record.userId);
        m_expiredAccounts.remove(op.record.userId);
        emit switched();
        if (currentNotice)
            emit notice(tr("已切换至 %1").arg(name), false);
    } else if (op.kind == Kind::Remove) {
        m_switchErrors.remove(op.record.userId);
        m_expiredAccounts.remove(op.record.userId);
        if (currentNotice && op.explicitRemoval)
            emit notice(tr("已移除账号"), false);
    }
    finish();
}
void SavedAccountModel::fail()
{
    m_error = tr("无法保存或读取本地账号，请重试");
    if (!m_closing && m_operation && m_operation->generation == m_generation &&
        ((m_operation->kind == Kind::Switch && !m_operation->restoring) ||
         m_operation->explicitRemoval))
        emit notice(m_error, true);
    finish();
}
void SavedAccountModel::finish()
{
    m_validationDeadline.stop();
    m_operation.reset();
    publishSwitchState();
    QTimer::singleShot(0, this, &SavedAccountModel::pump);
}
void SavedAccountModel::beginDurationQueries()
{
    endDurationQueries();
    if (m_closing)
        return;
    m_durationOpen = true;
    m_durationRevision = m_api.sessionRevision();
    m_durations.clear();
    m_account.loadGrade();
    for (const auto &row : m_rows)
        if (row.userId != m_account.userId())
            m_durationQueue.enqueue(row.userId);
    pumpDurationQueries();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0), index(count() - 1, 0),
                         {NicknameRole, AvatarRole, DurationRole, DurationLoadingRole,
                          AvatarIdentityIconRole, AvatarIdentityLabelRole});
}
void SavedAccountModel::pumpDurationQueries()
{
    if (!m_durationOpen || m_closing || m_durationRevision != m_api.sessionRevision())
        return;
    // Query semantics match the reference switcher; bound active native work
    // while preserving all account IDs, anonymous auth and verification bypass.
    while (m_durationRequests.size() < 4 && !m_durationQueue.isEmpty()) {
        const auto user = m_durationQueue.dequeue();
        const auto id = m_api.userGradeInfo(user);
        if (id)
            m_durationRequests.insert(id, user);
    }
}
void SavedAccountModel::endDurationQueries()
{
    m_durationOpen = false;
    m_durationRevision = 0;
    m_durationQueue.clear();
    const auto pending = std::exchange(m_durationRequests, {});
    for (auto id : pending.keys())
        m_api.cancel(id);
}
void SavedAccountModel::suppressStartupRestoration()
{
    if (!m_account.userId())
        ++m_generation;
}
void SavedAccountModel::shutdown()
{
    cancelSwitch();
    m_closing = true;
    endDurationQueries();
    pump();
}
} // namespace EchoMusic
