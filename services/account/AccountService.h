#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include "ApiClient.h"
#include "AccountIdentity.h"
#include "MembershipService.h"
#include <memory>
namespace EchoMusic
{
class SavedAccountModel;
/// Application owner of visible account state. ApiClient retains the credentials
/// for immutable request injection; ordinary account QML properties never expose credentials.
/// Explicit laboratory inspection is isolated in ExperimentalSettingsService.
class AccountService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(quint64 userId READ userId NOTIFY changed)
    Q_PROPERTY(QString nickname READ nickname NOTIFY changed)
    Q_PROPERTY(QString avatarUrl READ avatarUrl NOTIFY changed)
    Q_PROPERTY(QString avatarIdentityIcon READ avatarIdentityIcon NOTIFY changed)
    Q_PROPERTY(QString avatarIdentityLabel READ avatarIdentityLabel NOTIFY changed)
    Q_PROPERTY(QVariantList identityBadges READ identityBadges NOTIFY changed)
    Q_PROPERTY(QStringList identityTags READ identityTags NOTIFY changed)
    Q_PROPERTY(quint64 expiresAtSeconds READ expiresAtSeconds NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString biography READ biography NOTIFY changed)
    Q_PROPERTY(int grade READ grade NOTIFY changed)
    Q_PROPERTY(QString listeningDuration READ listeningDuration NOTIFY changed)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY changed)
    Q_PROPERTY(QVariantList memberships READ memberships NOTIFY changed)
    Q_PROPERTY(QString membershipLabel READ membershipLabel NOTIFY changed)
    Q_PROPERTY(QString genderLabel READ genderLabel NOTIFY changed)
    Q_PROPERTY(int gender READ gender NOTIFY changed)
    Q_PROPERTY(QString birthday READ birthday NOTIFY changed)
    Q_PROPERTY(QString province READ province NOTIFY changed)
    Q_PROPERTY(QString city READ city NOTIFY changed)
    Q_PROPERTY(QString location READ location NOTIFY changed)
    Q_PROPERTY(QString ipLocation READ ipLocation NOTIFY changed)
    Q_PROPERTY(QString accountAge READ accountAge NOTIFY changed)
    Q_PROPERTY(QString studentSchool READ studentSchool NOTIFY changed)
    Q_PROPERTY(QString studentExpiry READ studentExpiry NOTIFY changed)
    Q_PROPERTY(quint64 visitors READ visitors NOTIFY changed)
    Q_PROPERTY(bool profileLoading READ profileLoading NOTIFY changed)
    Q_PROPERTY(QString profileError READ profileError NOTIFY changed)
    Q_PROPERTY(bool gradeLoading READ gradeLoading NOTIFY changed)
    Q_PROPERTY(bool gradeAvailable READ gradeAvailable NOTIFY changed)
    Q_PROPERTY(quint64 gradePoints READ gradePoints NOTIFY changed)
    Q_PROPERTY(quint64 nextGradePoints READ nextGradePoints NOTIFY changed)
    Q_PROPERTY(int nextGrade READ nextGrade NOTIFY changed)
public:
    enum class State
    {
        SignedOut,
        SignedIn,
        Refreshing,
        ReauthenticationRequired
    };
    Q_ENUM(State)
    explicit AccountService(ApiClient &api, QObject *parent = nullptr,
                            MembershipService *membership = nullptr);
    ~AccountService() override;
    State state() const
    {
        return m_state;
    }
    quint64 userId() const
    {
        return m_userId;
    }
    QString nickname() const
    {
        return m_nickname;
    }
    QString avatarUrl() const
    {
        return m_avatar;
    }
    void applyAvatar(const QString &url);
    QString avatarIdentityKind() const
    {
        return m_profileIdentity.avatarKind();
    }
    QString avatarIdentityIcon() const
    {
        return AccountIdentity::avatarIcon(avatarIdentityKind());
    }
    QString avatarIdentityLabel() const
    {
        return AccountIdentity::avatarLabel(avatarIdentityKind());
    }
    QVariantList identityBadges() const
    {
        return m_profileIdentity.badges(m_student);
    }
    QStringList identityTags() const
    {
        return m_identityTags;
    }
    bool profileDetailAvailable() const
    {
        return m_profileLoaded;
    }
    quint64 expiresAtSeconds() const
    {
        return m_expires;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString biography() const
    {
        return m_biography;
    }
    int grade() const
    {
        return m_grade;
    }
    QString listeningDuration() const;
    /// Compact catalog presentation uses total minutes; profile keeps duration units.
    QString listeningMinutes() const;
    bool gradeLoading() const
    {
        return m_gradeRequest != 0;
    }
    QVariantList statistics() const
    {
        return m_statistics;
    }
    QVariantList memberships() const
    {
        return m_memberships;
    }
    QString membershipLabel() const;
    QString genderLabel() const;
    int gender() const
    {
        return m_gender;
    }
    QString birthday() const
    {
        return m_birthday;
    }
    QString province() const
    {
        return m_province;
    }
    QString city() const
    {
        return m_city;
    }
    /// Only the application-owned profile writer calls this after acknowledgement.
    /// Applies the accepted canonical patch without another reference-absent read.
    void applyProfilePatch(const ApiClient::ProfilePatch &patch);
    QString location() const;
    QString ipLocation() const
    {
        return m_ipLocation;
    }
    QString accountAge() const;
    QString studentSchool() const
    {
        return m_studentSchool;
    }
    QString studentExpiry() const
    {
        return m_studentExpiry;
    }
    quint64 visitors() const
    {
        return m_visitors;
    }
    bool profileLoading() const
    {
        return m_detail || m_membershipPending || m_identity;
    }
    QString profileError() const
    {
        return m_profileError;
    }
    bool gradeAvailable() const
    {
        return m_nextGrade > m_grade && m_nextGradePoints > 0;
    }
    quint64 gradePoints() const
    {
        return m_gradePoints;
    }
    quint64 nextGradePoints() const
    {
        return m_nextGradePoints;
    }
    int nextGrade() const
    {
        return m_nextGrade;
    }
    /// Trusted canonical Rust AccountSession only; deliberately not Q_INVOKABLE.
    bool adoptSession(const QByteArray &payload);
    /// Accept the distinct canonical QR contract, without adding a refresh call.
    /// Login response may omit refresh/expiry; only refresh uses the strict session contract.
    bool adoptLoginAuthorization(const QByteArray &payload);
    bool adoptQrAuthorization(const QByteArray &payload);
    Q_INVOKABLE void refresh();
    // UI logout goes through SavedAccountModel to remove persisted credentials;
    // this low-level command is reserved for trusted C++ lifecycle callers.
    void signOut();
    Q_INVOKABLE void loadProfileOnce();
    Q_INVOKABLE void reloadProfile();
    void applySocialCountChange(int followsDelta, int friendsDelta);
    Q_INVOKABLE void loadGrade();
signals:
    void changed();
    void sessionAdopted();
    /// Existing account, updated device context. Resume canceled reads after
    /// all ApiClient session listeners have drained.
    void requestContextChanged();

private:
    friend class AccountServiceTestAccess;
    friend class SavedAccountModel;
    bool adoptStoredSession(const QByteArray &payload, const QJsonObject &profile = {});
    bool adopt(const QJsonObject &value, bool complete, const QJsonObject &profile = {});
    void applyProfile(const QJsonObject &profile);
    QByteArray storedSession() const;
    void succeed(quint64 id, const QByteArray &payload);
    void fail(quint64 id, const QByteArray &payload);
    void resetProfile();
    void applyMembership();
    ApiClient &m_api;
    std::unique_ptr<MembershipService> m_ownedMembership;
    MembershipService *m_membership = nullptr;
    bool m_membershipPending = false;
    bool m_profileExtrasStarted = false;
    State m_state = State::SignedOut;
    quint64 m_userId = 0, m_expires = 0, m_refresh = 0, m_revision = 0, m_generation = 0;
    QString m_nickname, m_avatar, m_error;
    QString m_token, m_credential;
    QString m_biography;
    AccountIdentity m_profileIdentity;
    bool m_student = false;
    QStringList m_identityTags;
    QString m_province, m_city, m_ipLocation, m_birthday, m_studentSchool, m_studentExpiry,
        m_profileError;
    int m_gender = 2, m_nextGrade = 0;
    quint64 m_registeredSeconds = 0, m_visitors = 0, m_gradePoints = 0, m_nextGradePoints = 0;
    QVariantList m_statistics, m_memberships;
    int m_grade = 0;
    qint64 m_listeningSeconds = -1;
    quint64 m_detail = 0, m_identity = 0, m_gradeRequest = 0;
    bool m_profileLoaded = false;
    bool m_adopting = false;
};
} // namespace EchoMusic
