#pragma once
#include "AccountService.h"
#include "SocialUserModel.h"
#include "LoginDeviceModel.h"
#include "ListeningPreferencesService.h"
#include "ContentBlacklistService.h"
#include <array>
#include <memory>
#include <QUrl>
#include <QTimer>
#include <QVariantList>
namespace EchoMusic
{
/// One application owner for profile editing, cached social tabs and devices.
/// Requests inherit the ApiClient account revision; QML owns only form drafts.
class ProfileService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(EchoMusic::AccountService *account READ account CONSTANT)
    Q_PROPERTY(EchoMusic::SocialUserModel *socialUsers READ socialUsers NOTIFY changed)
    Q_PROPERTY(SocialTab socialTab READ socialTab NOTIFY changed)
    Q_PROPERTY(bool socialLoading READ socialLoading NOTIFY changed)
    Q_PROPERTY(QString socialError READ socialError NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(QString editorError READ editorError NOTIFY changed)
    Q_PROPERTY(bool uploadingAvatar READ uploadingAvatar NOTIFY changed)
    Q_PROPERTY(EchoMusic::LoginDeviceModel *devices READ devices CONSTANT)
    Q_PROPERTY(EchoMusic::ListeningPreferencesService *preferences READ preferences CONSTANT)
    Q_PROPERTY(EchoMusic::ContentBlacklistService *blacklist READ blacklist CONSTANT)
    Q_PROPERTY(bool devicesLoading READ devicesLoading NOTIFY changed)
    Q_PROPERTY(QString devicesError READ devicesError NOTIFY changed)
    Q_PROPERTY(bool pendingDeviceRemoval READ pendingDeviceRemoval NOTIFY changed)
    Q_PROPERTY(QString pendingDeviceTitle READ pendingDeviceTitle NOTIFY changed)
    Q_PROPERTY(bool removingDevice READ removingDevice NOTIFY changed)
    Q_PROPERTY(QString removingDeviceKey READ removingDeviceKey NOTIFY changed)
    Q_PROPERTY(bool chatActive READ chatActive NOTIFY changed)
    Q_PROPERTY(QVariantMap chatTarget READ chatTarget NOTIFY changed)
    Q_PROPERTY(QVariantList chatMessages READ chatMessages NOTIFY changed)
    Q_PROPERTY(bool chatLoading READ chatLoading NOTIFY changed)
    Q_PROPERTY(bool chatSending READ chatSending NOTIFY changed)
    Q_PROPERTY(QString chatError READ chatError NOTIFY changed)
public:
    bool chatActive() const
    {
        return !m_chatTarget.isEmpty();
    }
    QVariantMap chatTarget() const
    {
        return m_chatTarget.toVariantMap();
    }
    QVariantList chatMessages() const
    {
        return m_chatMessages;
    }
    bool chatLoading() const
    {
        return m_chatRead != 0;
    }
    bool chatSending() const
    {
        return m_chatWrite != 0;
    }
    QString chatError() const
    {
        return m_chatError;
    }
    Q_INVOKABLE bool socialPending(quint64 userId) const;
    Q_INVOKABLE void toggleSocialFollow(const QString &key);
    Q_INVOKABLE void openChat(const QString &key);
    Q_INVOKABLE void closeChat();
    Q_INVOKABLE void sendChat(const QString &text);
    Q_INVOKABLE void refreshChat();
    void ensureArtistFollows();
    bool artistFollowsLoaded() const
    {
        return m_social[int(SocialTab::Follow)].loaded;
    }
    bool artistFollowed(quint64 id) const;
    bool artistPending(quint64 id) const;
    void toggleArtistFollow(quint64 id);
    enum class SocialTab
    {
        Friends,
        Follow,
        Fans,
        Visitors
    };
    Q_ENUM(SocialTab)
    explicit ProfileService(ApiClient &api, AccountService &account, QObject *parent = nullptr);
    ~ProfileService() override;
    AccountService *account() const
    {
        return &m_account;
    }
    SocialUserModel *socialUsers() const
    {
        return m_social[int(m_tab)].model.get();
    }
    SocialTab socialTab() const
    {
        return m_tab;
    }
    bool socialLoading() const
    {
        return m_social[int(m_tab)].request != 0;
    }
    QString socialError() const
    {
        return m_social[int(m_tab)].error;
    }
    bool saving() const
    {
        return m_save != 0;
    }
    QString editorError() const
    {
        return m_editorError;
    }
    bool uploadingAvatar() const
    {
        return m_avatarReading || m_avatarRequest;
    }
    LoginDeviceModel *devices()
    {
        return &m_devices;
    }
    ListeningPreferencesService *preferences()
    {
        return &m_preferences;
    }
    ContentBlacklistService *blacklist()
    {
        return &m_blacklist;
    }
    bool devicesLoading() const
    {
        return m_deviceRequest != 0;
    }
    QString devicesError() const
    {
        return m_devicesError;
    }
    Q_INVOKABLE void openSocial(SocialTab tab);
    Q_INVOKABLE void refreshSocial();
    Q_INVOKABLE void loadDevices(bool force = false);
    bool pendingDeviceRemoval() const
    {
        return !m_pendingDevice.isEmpty();
    }
    QString pendingDeviceTitle() const
    {
        return m_pendingDeviceTitle;
    }
    bool removingDevice() const
    {
        return m_deviceKick || m_deviceKickRefresh;
    }
    QString removingDeviceKey() const
    {
        return m_kickingDeviceKey;
    }
    Q_INVOKABLE void activateDevices();
    Q_INVOKABLE void deactivateDevices();
    Q_INVOKABLE void requestDeviceRemoval(const QString &key);
    Q_INVOKABLE void cancelDeviceRemoval();
    Q_INVOKABLE void confirmDeviceRemoval();
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void uploadAvatar(const QUrl &file);
    Q_INVOKABLE void saveProfile(const QString &nickname, int gender, const QString &birthday,
                                 const QString &signature, const QString &province,
                                 const QString &city);
    void shutdown();
signals:
    void changed();
    void notice(const QString &message, bool warning);
    /// Editor feedback raised as a toast so the scrollable form never hides it.
    /// tone follows EchoMusic::ToastService::Tone (0 Info, 1 Success, 2 Warning,
    /// 3 Danger), matching the reference profile form.
    void editNotice(const QString &message, int tone);
    void chatSent(const QString &submitted);
    void relationshipsMutated();
    void profileSaved();
    void avatarFinished(bool success, const QString &message);
    void deviceRemovalFinished(bool success, const QString &message);

private:
    friend class ProfileServiceTestAccess;
    struct TabState
    {
        std::unique_ptr<SocialUserModel> model;
        quint64 request = 0;
        bool loaded = false;
        QString error;
    };
    struct FollowWrite
    {
        quint64 id;
        bool following;
        bool artist;
        QJsonObject user;
    };
    struct ConfirmedFollow
    {
        QJsonObject user;
        bool following;
        std::optional<bool> mutual;
        QSet<int> remaining;
    };
    QHash<quint64, FollowWrite> m_followWrites;
    QHash<quint64, ConfirmedFollow> m_confirmedFollows;
    QHash<quint64, bool> m_confirmedArtists;
    QJsonObject m_chatTarget;
    QVariantList m_chatMessages;
    QJsonArray m_unmappedChatRecords;
    QString m_chatTag, m_chatError, m_chatSubmitted;
    quint64 m_chatRead = 0, m_chatWrite = 0;
    QTimer m_chatRefresh;
    QJsonArray reconcileFollows(int tab, QJsonArray rows, bool readback);
    void loadSocial(SocialTab tab, bool force);
    void clear();
    void succeeded(quint64 request, const QByteArray &payload);
    void failed(quint64 request, const QByteArray &payload);
    bool current() const;
    ApiClient &m_api;
    AccountService &m_account;
    std::array<TabState, 4> m_social;
    SocialTab m_tab = SocialTab::Friends;
    LoginDeviceModel m_devices;
    ListeningPreferencesService m_preferences;
    ContentBlacklistService m_blacklist;
    quint64 m_revision = 0, m_deviceRequest = 0, m_save = 0;
    bool m_devicesLoaded = false, m_closing = false;
    QString m_editorError, m_devicesError;
    QJsonObject m_pendingDevice;
    QString m_pendingDeviceTitle, m_kickingDeviceKey;
    quint64 m_deviceKick = 0, m_deviceActivation = 0, m_kickActivation = 0;
    bool m_devicesActive = false, m_deviceKickRefresh = false;
    quint64 m_avatarRequest = 0, m_avatarGeneration = 0;
    bool m_avatarReading = false, m_avatarSubmitting = false;
    ApiClient::ProfilePatch m_patch;
};
} // namespace EchoMusic
