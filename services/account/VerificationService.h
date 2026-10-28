#pragma once
#include "ApiClient.h"
#include <QObject>
#include <QUrl>
#include <functional>

namespace EchoMusic
{
class BrowserVerificationSession;
/// Single GUI-thread owner of the shared SSA flow. Retains only the logical
/// request ID; ApiClient owns its immutable identity, suspended body and replay.
class VerificationService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ open NOTIFY changed)
    Q_PROPERTY(bool browserOpen READ browserOpen NOTIFY changed)
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString provider READ provider NOTIFY changed)
    Q_PROPERTY(QString providerName READ providerName NOTIFY changed)
    Q_PROPERTY(quint32 verificationType READ verificationType NOTIFY changed)
    Q_PROPERTY(QString displayMessage READ displayMessage NOTIFY changed)
    Q_PROPERTY(QString appId READ appId NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(quint64 generation READ generation NOTIFY changed)
public:
    enum class State
    {
        Idle,
        Loading,
        Ready,
        Verifying,
        Error
    };
    Q_ENUM(State)
    using BrowserOpener = std::function<bool(const QUrl &)>;
    explicit VerificationService(ApiClient &api, QObject *parent = nullptr,
                                 BrowserOpener browserOpener = {});
    bool open() const
    {
        return m_request != 0;
    }
    State state() const
    {
        return m_state;
    }
    QString provider() const
    {
        return m_provider;
    }
    QString providerName() const;
    quint32 verificationType() const
    {
        return m_type;
    }
    QString displayMessage() const
    {
        return m_displayMessage;
    }
    QString appId() const
    {
        return m_appId;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    quint64 generation() const
    {
        return m_generation;
    }
    bool browserOpen() const;
    Q_INVOKABLE void beginBrowserVerification();
    Q_INVOKABLE void refresh();
    /// Only the official SDK callback supplies these ephemeral values. A stale
    /// web page cannot submit proof for a newer event, even with the same app ID.
    void submitTencent(quint64 generation, const QString &ticket, const QString &randstr);
    Q_INVOKABLE void submitSms(const QString &code);
    Q_INVOKABLE void beginLogin();
    Q_INVOKABLE void cancel();
    void shutdown();
signals:
    void changed();
    void loginRequested(quint64 requestId);
    /// A current browser proof has entered native submission. Composition owns
    /// foreground activation; this does not claim upstream acceptance/login.
    void returnToApplicationRequested();

private:
    void clear();
    ApiClient &m_api;
    BrowserVerificationSession *m_browser;
    BrowserOpener m_browserOpener;
    quint64 m_request = 0, m_info = 0, m_submit = 0, m_generation = 0;
    State m_state = State::Idle;
    QString m_appId, m_error, m_provider, m_displayMessage, m_vendor;
    quint32 m_type = 0;
};
} // namespace EchoMusic
