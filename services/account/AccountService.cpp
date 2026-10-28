#include "AccountService.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QThread>
#include <QScopedValueRollback>
#include <utility>
#include <QJsonArray>
#include <QDateTime>
#include <QLocale>
namespace EchoMusic
{
AccountService::AccountService(ApiClient &api, QObject *parent, MembershipService *membership)
    : QObject(parent), m_api(api),
      m_ownedMembership(membership ? nullptr : std::make_unique<MembershipService>(api)),
      m_membership(membership ? membership : m_ownedMembership.get())
{
    connect(m_membership, &MembershipService::settled, this, &AccountService::applyMembership);
    connect(&api, &ApiClient::succeeded, this, &AccountService::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { fail(id, payload); });
    connect(&api, &ApiClient::authenticationExpired, this, [this] {
        if (m_userId) {
            m_state = State::ReauthenticationRequired;
            m_error = QStringLiteral("登录已失效，请重新登录");
            emit changed();
        }
    });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        if (m_adopting)
            return;
        const bool rebound =
            m_userId && m_api.matchesAccountSnapshot(m_userId, m_token, m_credential);
        if (rebound) {
            m_refresh = 0;
            m_revision = revision;
            m_detail = m_identity = m_gradeRequest = 0;
            m_membershipPending = false;
            m_profileLoaded = false;
            m_profileExtrasStarted = false;
            if (m_state == State::Refreshing)
                m_state = State::SignedIn;
        } else {
            resetProfile();
        }
        emit changed();
        if (rebound && m_userId && m_api.matchesAccountSnapshot(m_userId, m_token, m_credential))
            emit requestContextChanged();
    });
}
AccountService::~AccountService()
{
    for (auto id : {m_refresh, m_detail, m_identity, m_gradeRequest})
        if (id)
            m_api.cancel(id);
}
void AccountService::resetProfile()
{
    ++m_generation;
    const auto pending = std::exchange(m_refresh, 0);
    const QList<quint64> profileRequests{std::exchange(m_detail, 0), std::exchange(m_identity, 0),
                                         std::exchange(m_gradeRequest, 0)};
    m_membershipPending = false;
    m_profileLoaded = false;
    m_profileExtrasStarted = false;
    m_statistics.clear();
    m_memberships.clear();
    m_biography.clear();
    m_profileIdentity = {};
    m_student = false;
    m_identityTags.clear();
    m_province.clear();
    m_city.clear();
    m_ipLocation.clear();
    m_birthday.clear();
    m_studentSchool.clear();
    m_studentExpiry.clear();
    m_profileError.clear();
    m_gender = 2;
    m_nextGrade = 0;
    m_registeredSeconds = m_visitors = m_gradePoints = m_nextGradePoints = 0;
    m_grade = 0;
    m_listeningSeconds = -1;
    m_revision = 0;
    m_userId = 0;
    m_expires = 0;
    m_nickname.clear();
    m_avatar.clear();
    m_token.clear();
    m_credential.clear();
    m_error.clear();
    m_state = State::SignedOut;
    if (pending)
        m_api.cancel(pending);
    for (auto id : profileRequests)
        if (id)
            m_api.cancel(id);
}
bool AccountService::adoptSession(const QByteArray &payload)
{
    return adopt(QJsonDocument::fromJson(payload).object(), true);
}
bool AccountService::adoptLoginAuthorization(const QByteArray &payload)
{
    return adopt(QJsonDocument::fromJson(payload).object(), false);
}
bool AccountService::adoptQrAuthorization(const QByteArray &payload)
{
    auto object = QJsonDocument::fromJson(payload).object();
    return adopt(object, false);
}
bool AccountService::adoptStoredSession(const QByteArray &payload, const QJsonObject &profile)
{
    return adopt(QJsonDocument::fromJson(payload).object(), false, profile);
}
QByteArray AccountService::storedSession() const
{
    if (!m_userId)
        return {};
    return QJsonDocument(QJsonObject{{"user_id", qint64(m_userId)},
                                     {"token", m_token},
                                     {"refresh_credential", m_credential},
                                     {"expires_at_seconds", qint64(m_expires)},
                                     {"nickname", m_nickname},
                                     {"avatar_url", m_avatar}})
        .toJson(QJsonDocument::Compact);
}
bool AccountService::adopt(const QJsonObject &value, bool complete, const QJsonObject &profile)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = value.value("user_id").toInteger();
    const auto expires = value.value("expires_at_seconds").toInteger();
    const auto token = value.value("token").toString();
    const auto credential = value.value("refresh_credential").toString();
    if (id <= 0 || token.isEmpty() || (complete && credential.isEmpty()))
        return false;
    const auto generation = ++m_generation;
    bool accepted;
    {
        QScopedValueRollback adopting(m_adopting, true);
        accepted = m_api.setAccount(quint64(id), token, credential);
    }
    if (!accepted || generation != m_generation ||
        !m_api.matchesAccountSnapshot(quint64(id), token, credential))
        return false;
    // Identical credentials may leave the ApiClient revision unchanged. Cancel
    // owned requests explicitly before replacing their IDs and presentation.
    for (auto *request : {&m_refresh, &m_detail, &m_identity, &m_gradeRequest}) {
        const auto pending = std::exchange(*request, 0);
        if (pending)
            m_api.cancel(pending);
    }
    if (generation != m_generation)
        return false;
    m_revision = m_api.sessionRevision();
    m_userId = quint64(id);
    m_expires = expires > 0 ? quint64(expires) : 0;
    m_token = token;
    m_credential = credential;
    {
        m_detail = m_identity = m_gradeRequest = 0;
        m_membershipPending = false;
        m_profileLoaded = false;
        m_profileExtrasStarted = false;
        m_statistics.clear();
        m_memberships.clear();
        m_biography.clear();
        m_profileIdentity = {};
        m_student = false;
        m_identityTags.clear();
        m_province.clear();
        m_city.clear();
        m_ipLocation.clear();
        m_birthday.clear();
        m_studentSchool.clear();
        m_studentExpiry.clear();
        m_profileError.clear();
        m_gender = 2;
        m_nextGrade = 0;
        m_registeredSeconds = m_visitors = m_gradePoints = m_nextGradePoints = 0;
        m_grade = 0;
        m_listeningSeconds = -1;
    }
    m_nickname = value.value("nickname").toString();
    m_avatar = value.value("avatar_url").toString();
    if (!profile.isEmpty())
        applyProfile(profile);
    m_error.clear();
    m_state = State::SignedIn;
    emit changed();
    if (generation != m_generation || !m_api.matchesAccountSnapshot(quint64(id), token, credential))
        return false;
    emit sessionAdopted();
    return generation == m_generation &&
           m_api.matchesAccountSnapshot(quint64(id), token, credential);
}
void AccountService::refresh()
{
    if (m_refresh || !m_userId || m_state == State::ReauthenticationRequired ||
        m_revision != m_api.sessionRevision())
        return;
    m_refresh = m_api.refreshLogin();
    if (!m_refresh) {
        m_error = QStringLiteral("无法刷新登录，请重试");
        emit changed();
        return;
    }
    m_state = State::Refreshing;
    m_error.clear();
    emit changed();
}
void AccountService::signOut()
{
    resetProfile();
    m_api.setAccount(0, {}); // Invalidates every old request and verification flow.
    emit changed();
}
void AccountService::applyProfile(const QJsonObject &object)
{
    m_profileLoaded = true;
    m_nickname = object.value("nickname").toString();
    m_avatar = object.value("avatar_url").toString();
    m_biography = object.value("biography").toString();
    m_profileIdentity = AccountIdentity::fromDomain(object.value("identity").toObject());
    m_gender = object.value("gender").toInt(2);
    m_province = object.value("province").toString();
    m_city = object.value("city").toString();
    m_ipLocation = object.value("ip_location").toString().trimmed();
    m_birthday = object.value("birthday").toString();
    m_registeredSeconds = object.value("registered_at_seconds").toInteger();
    m_visitors = object.value("visitors").toInteger();
    m_grade = object.value("grade").toInt();
    if (m_listeningSeconds < 0)
        m_listeningSeconds = object.value("listening_minutes").toInteger() * 60;
    m_statistics = {
        QVariantMap{{"label", tr("好友")}, {"value", object.value("friends").toInteger()}},
        QVariantMap{{"label", tr("关注")}, {"value", object.value("follows").toInteger()}},
        QVariantMap{{"label", tr("粉丝")}, {"value", object.value("fans").toInteger()}}};
}
void AccountService::succeed(quint64 id, const QByteArray &payload)
{
    if (id && m_revision == m_api.sessionRevision() &&
        (id == m_detail || id == m_identity || id == m_gradeRequest)) {
        const auto object = QJsonDocument::fromJson(payload).object();
        if (id == m_detail) {
            m_detail = 0;
            if (object.value("id").toInteger() != qint64(m_userId))
                return;
            applyProfile(object);
        } else if (id == m_gradeRequest) {
            m_gradeRequest = 0;
            if (object.value("user_id").toInteger() != qint64(m_userId))
                return;
            m_grade = object.value("grade").toInt();
            m_listeningSeconds = object.value("listening_seconds").toInteger(-1);
            m_gradePoints = object.value("current_points").toInteger();
            m_nextGradePoints = object.value("next_grade_points").toInteger();
            m_nextGrade = object.value("next_grade").toInt();
        } else if (id == m_identity) {
            m_identity = 0;
            m_student = object.value("student_status").toInt() == 1;
            m_studentSchool = m_student ? object.value("student_school").toString() : QString();
            m_studentExpiry =
                m_student ? object.value("student_expire_time").toString() : QString();
            m_identityTags.clear();
            for (const auto &tag : object.value("tags").toArray())
                m_identityTags.append(tag.toString());
        }
        emit changed();
        return;
    }
    if (!id || id != m_refresh || m_revision != m_api.sessionRevision())
        return;
    m_refresh = 0;
    const auto refreshedUser =
        QJsonDocument::fromJson(payload).object().value("user_id").toInteger();
    if (refreshedUser > 0 && quint64(refreshedUser) == m_userId && adoptSession(payload))
        return;
    m_state = State::SignedIn;
    m_error = QStringLiteral("登录刷新结果无效，请重试");
    emit changed();
}
void AccountService::fail(quint64 id, const QByteArray &payload)
{
    if (id && m_revision == m_api.sessionRevision()) {
        for (auto *request : {&m_detail, &m_identity, &m_gradeRequest}) {
            if (id == *request) {
                *request = 0;
                m_profileError = tr("个人资料加载失败，请重试");
                emit changed();
                return;
            }
        }
    }
    if (!id || id != m_refresh || m_revision != m_api.sessionRevision())
        return;
    m_refresh = 0;
    const auto kind = QJsonDocument::fromJson(payload).object().value("kind").toString();
    m_state = kind == QStringLiteral("authentication") ? State::ReauthenticationRequired
                                                       : State::SignedIn;
    m_error = kind == QStringLiteral("authentication") ? QStringLiteral("登录已失效，请重新登录")
                                                       : QStringLiteral("登录刷新失败，请重试");
    emit changed();
}
void AccountService::loadProfileOnce()
{
    if (!m_userId || (m_profileLoaded && m_profileExtrasStarted) || m_detail ||
        m_membershipPending || m_identity || m_revision != m_api.sessionRevision())
        return;
    if (!m_profileLoaded)
        m_detail = m_api.userDetail();
    m_profileExtrasStarted = true;
    m_membershipPending = true;
    m_membership->ensure(true);
    m_identity = m_api.userInfo();
    m_profileError.clear();
    emit changed();
}
void AccountService::reloadProfile()
{
    if (profileLoading())
        return;
    m_membershipPending = false;
    m_profileLoaded = false;
    m_profileExtrasStarted = false;
    loadProfileOnce();
}
void AccountService::applySocialCountChange(int followsDelta, int friendsDelta)
{
    if (!m_userId)
        return;
    bool updated = false;
    for (int index = 0; index < m_statistics.size() && index < 2; ++index) {
        auto item = m_statistics[index].toMap();
        bool known = false;
        const auto count = item.value("value").toLongLong(&known);
        const auto delta = index == 0 ? friendsDelta : followsDelta;
        if (known && delta) {
            item["value"] = qMax<qint64>(0, count + delta);
            m_statistics[index] = item;
            updated = true;
        }
    }
    if (updated)
        emit changed();
}
void AccountService::loadGrade()
{
    if (!m_userId || m_gradeRequest || m_revision != m_api.sessionRevision())
        return;
    m_gradeRequest = m_api.userGradeInfo();
    emit changed();
}
QString AccountService::listeningMinutes() const
{
    return m_listeningSeconds < 0
               ? QString()
               : QLocale(QLocale::Chinese).toString(m_listeningSeconds / 60) + tr(" 分钟");
}
QString AccountService::listeningDuration() const
{
    if (m_listeningSeconds < 0)
        return {};
    if (m_listeningSeconds < 60)
        return tr("%1 秒").arg(m_listeningSeconds);
    QStringList parts;
    const auto days = m_listeningSeconds / 86400;
    const auto hours = m_listeningSeconds % 86400 / 3600;
    const auto minutes = m_listeningSeconds % 3600 / 60;
    if (days)
        parts.append(tr("%1 天").arg(days));
    if (hours)
        parts.append(tr("%1 小时").arg(hours));
    if (minutes)
        parts.append(tr("%1 分钟").arg(minutes));
    return parts.join(' ');
}
QString AccountService::genderLabel() const
{
    return m_gender == 0 ? tr("女") : m_gender == 1 ? tr("男") : tr("保密");
}
void AccountService::applyAvatar(const QString &url)
{
    // An older profile read must not put the previous photo back after upload.
    const auto user = m_userId, revision = m_api.sessionRevision();
    const auto oldRead = std::exchange(m_detail, 0);
    if (oldRead)
        m_api.cancel(oldRead);
    if (user != m_userId || revision != m_api.sessionRevision())
        return;
    m_avatar = url;
    emit changed();
}
void AccountService::applyProfilePatch(const ApiClient::ProfilePatch &patch)
{
    // A read launched before this acknowledgement must not overwrite the
    // accepted form with its old snapshot after we update the shared account.
    const auto user = m_userId, revision = m_api.sessionRevision();
    const auto oldRead = std::exchange(m_detail, 0);
    if (oldRead)
        m_api.cancel(oldRead);
    if (user != m_userId || revision != m_api.sessionRevision())
        return;
    if (patch.nickname)
        m_nickname = *patch.nickname;
    if (patch.gender)
        m_gender = int(*patch.gender);
    if (patch.birthday)
        m_birthday = *patch.birthday;
    if (patch.signature)
        m_biography = *patch.signature;
    if (patch.province)
        m_province = *patch.province;
    if (patch.city)
        m_city = *patch.city;
    emit changed();
}
QString AccountService::location() const
{
    QStringList parts;
    if (!m_province.isEmpty())
        parts.append(m_province);
    if (!m_city.isEmpty() && m_city != m_province)
        parts.append(m_city);
    return parts.isEmpty() ? tr("未知") : parts.join(' ');
}
QString AccountService::accountAge() const
{
    if (!m_registeredSeconds)
        return tr("未知");
    const auto start = QDateTime::fromSecsSinceEpoch(qint64(m_registeredSeconds)).date();
    const auto today = QDate::currentDate();
    if (!start.isValid() || start > today)
        return tr("未知");
    // QDate arithmetic preserves local calendar anniversaries (including month
    // ends) without treating a daylight-saving day as exactly 86400 seconds.
    int months = (today.year() - start.year()) * 12 + today.month() - start.month();
    if (start.addMonths(months) > today)
        --months;
    const auto days = start.addMonths(months).daysTo(today);
    QStringList parts;
    if (months / 12)
        parts.append(tr("%1 年").arg(months / 12));
    if (months % 12)
        parts.append(tr("%1 个月").arg(months % 12));
    if (days)
        parts.append(tr("%1 天").arg(days));
    return parts.isEmpty() ? tr("不足 1 天") : parts.join(' ');
}
QString AccountService::membershipLabel() const
{
    if (m_userId && m_memberships.isEmpty() && !m_membership->facts().known)
        return tr("未确认");
    return m_memberships.isEmpty() ? QStringLiteral("NOVIP")
                                   : m_memberships.front().toMap().value("label").toString();
}
} // namespace EchoMusic

void EchoMusic::AccountService::applyMembership()
{
    m_membershipPending = false;
    const auto &object = m_membership->domain();
    if (object.isEmpty()) {
        if (m_userId && !m_membership->errorMessage().isEmpty())
            m_profileError = m_membership->errorMessage();
        emit changed();
        return;
    }
    if (object.value("user_id").toInteger() != qint64(m_userId))
        return;
    m_memberships.clear();
    const auto append = [this](const QString &kind, const QString &label, const QString &begin,
                               const QString &expiry) {
        const auto date = QDateTime::fromString(expiry, Qt::ISODate).date();
        const auto format = [](const QString &value) {
            const auto time = QDateTime::fromString(value, Qt::ISODate);
            return time.isValid()    ? time.toString("yyyy-MM-dd HH:mm")
                   : value.isEmpty() ? QStringLiteral("--")
                                     : value;
        };
        m_memberships.append(
            QVariantMap{{"kind", kind},
                        {"label", label},
                        {"beginsAt", format(begin)},
                        {"expiresAt", format(expiry)},
                        {"expires", date.isValid() ? date.toString("yyyy-MM-dd") + tr(" 到期")
                                                   : tr("已开通")}});
    };
    // Membership precedence is a display rule; credentials and provider parsing
    // remain in the Rust endpoint.
    if (object.value("user_type_code").toInt() & 16)
        append("super", tr("超级VIP"), object.value("super_begins_at").toString(),
               object.value("super_expires_at").toString());
    if (object.value("vip_type_code").toInt() == 6)
        append("deluxe", tr("豪华VIP"), object.value("deluxe_begins_at").toString(),
               object.value("deluxe_expires_at").toString());
    for (const auto &value : object.value("products").toArray()) {
        const auto product = value.toObject();
        if (!product.value("active").toBool() ||
            product.value("business").toString() != QStringLiteral("concept"))
            continue;
        const auto type = product.value("product").toString();
        if (type == QStringLiteral("svip"))
            append("concept", tr("概念会员"), product.value("begins_at").toString(),
                   product.value("expires_at").toString());
        else if (type == QStringLiteral("tvip"))
            append("music", tr("畅听会员"), product.value("begins_at").toString(),
                   product.value("expires_at").toString());
    }
    emit changed();
}
