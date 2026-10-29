#pragma once
#include "ApiClient.h"
#include "NetworkConfiguration.h"
#include "NetworkTransport.h"
#include "LocalCredentialStore.h"
#include <functional>
#include <memory>
#include <QObject>
#include "SettingsStore.h"

namespace EchoMusic
{
/// Application-owned applied network policy. The form holds presentation drafts;
/// Apply commits configuration plus a local SQLite password record. A blank password
/// retains the old secret; stored password bytes are never returned to QML.
class NetworkPreferences final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString proxyMode READ proxyMode NOTIFY changed)
    Q_PROPERTY(QString proxyPacScript READ proxyPacScript NOTIFY changed)
    Q_PROPERTY(QString proxyRules READ proxyRules NOTIFY changed)
    Q_PROPERTY(QString proxyUsername READ proxyUsername NOTIFY changed)
    Q_PROPERTY(QString proxyBypassRules READ proxyBypassRules NOTIFY changed)
    Q_PROPERTY(QString githubProxyUrl READ githubProxyUrl WRITE setGithubProxyUrl NOTIFY changed)
    Q_PROPERTY(bool hasProxyPassword READ hasProxyPassword NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double apiTimeoutSeconds READ apiTimeoutSeconds NOTIFY timeoutsChanged)
    Q_PROPERTY(double playerTimeoutSeconds READ playerTimeoutSeconds NOTIFY timeoutsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
public:
    explicit NetworkPreferences(EchoMusic::SettingsStore &settings, ApiClient &api,
                                QObject *parent = nullptr, LocalCredentialStore *store = nullptr);
    QString proxyMode() const
    {
        return NetworkConfiguration::modeKey(m_configuration.mode);
    }
    QString proxyPacScript() const
    {
        return m_configuration.pacScript;
    }
    QString proxyRules() const
    {
        return m_configuration.proxyRules;
    }
    QString proxyUsername() const
    {
        return m_configuration.username;
    }
    QString proxyBypassRules() const
    {
        return m_configuration.bypassRules;
    }
    QString githubProxyUrl() const
    {
        return m_githubProxy;
    }
    void setGithubProxyUrl(const QString &value);
    bool hasProxyPassword() const
    {
        return !m_passwordId.isEmpty();
    }
    bool busy() const
    {
        return m_busy;
    }
    QUrl transportEndpoint() const
    {
        return m_transport->endpoint();
    }
    QUrl acceleratedGithubUrl(const QUrl &url) const;
    QUrl registerMedia(const QUrl &source)
    {
        return m_transport->registerMedia(source);
    }
    void registerCastMedia(const QUrl &source, const QHostAddress &receiver,
                           std::function<void(QUrl, QString)> complete)
    {
        m_transport->registerCastMedia(source, receiver, std::move(complete));
    }
    void releaseMedia(const QUrl &resource)
    {
        m_transport->releaseMedia(resource);
    }
    void afterPendingMediaWork(std::function<void()> complete)
    {
        m_transport->afterPendingChanges(std::move(complete));
    }
    void recoverAfterWake();
    void installApplicationTransport();
    bool ensureStartupTransport();
    bool transportAvailable() const
    {
        return m_transport && m_transport->isReady();
    }
    Q_INVOKABLE void apply(const QString &mode, const QString &pac, const QString &rules,
                           const QString &username, const QString &password, bool clearPassword,
                           const QString &bypass, double apiSeconds, double playerSeconds);
    double apiTimeoutSeconds() const
    {
        return m_apiSeconds;
    }
    double playerTimeoutSeconds() const
    {
        return m_playerSeconds;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    /// GUI-thread command; rejects invalid values or an already closed runtime.
    /// In-flight readers retain the deadline captured when they were opened.
    bool applyTimeouts(double apiSeconds, double playerSeconds);
    /// Reset only, after account/API/storage shutdown. Close the local SQLite
    /// connection before the application data directory is removed.
    void eraseCredentialForReset(std::function<void(QString)> complete);
signals:
    void changed();
    void notice(const QString &message);
    void applied();
    void timeoutsChanged();
    void errorChanged();
    void mediaRequestRejected(const QString &token, int status);

private:
    void setError(const QString &error);
    bool commit(const NetworkConfiguration &configuration, const QString &password,
                const QString &reference);
    void discardSecret(const QString &reference);
    void finishApply();
    QVariantMap record(const NetworkConfiguration &configuration, const QString &reference) const;
    EchoMusic::SettingsStore &m_settings;
    ApiClient &m_api;
    double m_apiSeconds = 0;
    double m_playerSeconds = 60;
    QString m_error;
    NetworkConfiguration m_configuration;
    std::unique_ptr<NetworkTransport> m_transport;
    QString m_password, m_passwordId, m_githubProxy;
    bool m_busy = false, m_secretAvailable = true;
    LocalCredentialStore *m_store;
    quint64 m_nextSecretOperation = 0, m_pendingSecretOperation = 0;
    std::function<void(bool, const QByteArray &)> m_secretCompletion;
};
} // namespace EchoMusic
