#include "SettingsKeys.h"
#include "NetworkPreferences.h"
#include "SystemNetworkMonitor.h"
#include <QThread>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>
#include <memory>
#include <QNetworkProxyFactory>
#include <QUuid>
#include <QRegularExpression>
#include <QPointer>
#include <QLoggingCategory>

namespace EchoMusic
{
void NetworkPreferences::eraseCredentialForReset(std::function<void(QString)> complete)
{
    if (m_busy) {
        complete(tr("网络凭证仍在处理中"));
        return;
    }
    m_busy = true;
    auto close = [this, complete = std::move(complete)](bool success, const QByteArray &) {
        if (!success) {
            m_busy = false;
            complete(tr("无法清除本地代理凭证，请重试"));
            return;
        }
        m_pendingSecretOperation = ++m_nextSecretOperation;
        m_secretCompletion = [this, complete](bool closed, const QByteArray &) {
            m_busy = false;
            if (closed) {
                m_password.clear();
                m_passwordId.clear();
            }
            complete(closed ? QString() : tr("无法关闭本地凭证存储"));
        };
        m_store->close(m_pendingSecretOperation);
    };
    if (m_passwordId.isEmpty()) {
        close(true, {});
    } else {
        m_pendingSecretOperation = ++m_nextSecretOperation;
        m_secretCompletion = std::move(close);
        m_store->remove(m_pendingSecretOperation, m_passwordId);
    }
}
namespace
{
Q_LOGGING_CATEGORY(networkPolicyLog, "echo.network.policy")
constexpr auto timeoutsKey = EchoMusic::SettingsKeys::Network::Timeouts;
constexpr auto proxyKey = EchoMusic::SettingsKeys::Network::ProxyPolicy;

class TransportProxyFactory final : public QNetworkProxyFactory
{
public:
    explicit TransportProxyFactory(quint16 port) : m_port(port) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
    {
        if (query.queryType() == QNetworkProxyQuery::TcpServer ||
            query.queryType() == QNetworkProxyQuery::UdpSocket)
            return {QNetworkProxy::NoProxy};
        // Device discovery/control sockets keep their native LAN transport.
        // HTTP URLs (including loopback) enter the router so the user's ordered
        // <-loopback> override remains meaningful for application HTTP traffic.
        if (QStringList{"http", "https", "ws", "wss"}.contains(query.protocolTag().toLower()) ||
            (query.queryType() == QNetworkProxyQuery::TcpSocket && query.protocolTag().isEmpty() &&
             query.peerHostName().isEmpty()))
            return {QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", m_port)};
        return {QNetworkProxy::NoProxy};
    }

private:
    quint16 m_port;
};
double restoredNumber(const QVariant &value, double fallback, double minimum)
{
    bool valid = false;
    const double number = value.toDouble(&valid);
    return valid && std::isfinite(number) ? qBound(minimum, number, 300.) : fallback;
}
std::unique_ptr<EchoMusic::SettingsStore> freshWriter(const EchoMusic::SettingsStore &settings)
{
    // A new backend retries transient filesystem errors without sharing a thread-bound object.
    return std::make_unique<EchoMusic::SettingsStore>(settings.fileName());
}
} // namespace
NetworkPreferences::NetworkPreferences(EchoMusic::SettingsStore &settings, ApiClient &api,
                                       QObject *parent, LocalCredentialStore *store)
    : QObject(parent), m_settings(settings), m_api(api),
      m_store(store ? store
                    : new SqliteCredentialStore(
                          QFileInfo(settings.deviceFileName()).absolutePath() + "/network.sqlite",
                          this))
{
    connect(m_store, &LocalCredentialStore::finished, this,
            [this](quint64 operation, bool success, const QByteArray &bytes) {
                if (operation != m_pendingSecretOperation || !m_secretCompletion)
                    return;
                m_pendingSecretOperation = 0;
                auto complete = std::exchange(m_secretCompletion, {});
                complete(success, bytes);
            });
    const auto timeouts = settings.value(timeoutsKey).toMap();
    const auto saved = settings.value(proxyKey).toMap();
    m_apiSeconds = restoredNumber(timeouts.value("apiSeconds"), 0, 0);
    m_playerSeconds = restoredNumber(timeouts.value("playerSeconds"), 60, 1);
    m_configuration.mode = NetworkConfiguration::modeFromKey(saved.value("mode").toString());
    m_configuration.pacScript = saved.value("pacScript").toString();
    m_configuration.proxyRules = saved.value("proxyRules").toString();
    m_configuration.username = saved.value("username").toString();
    m_configuration.bypassRules = saved.value("bypassRules", "<local>").toString();
    m_configuration.apiTimeoutSeconds = m_apiSeconds;
    m_configuration.playerTimeoutSeconds = m_playerSeconds;
    m_passwordId = saved.value("passwordId").toString();
    m_githubProxy = settings.value(EchoMusic::SettingsKeys::Network::GithubProxyUrl).toString();
    m_transport = std::make_unique<NetworkTransport>();
    connect(m_transport.get(), &NetworkTransport::mediaRequestRejected, this,
            &NetworkPreferences::mediaRequestRejected);
    auto *monitor = new SystemNetworkMonitor(this);
    connect(monitor, &SystemNetworkMonitor::changed, this, [this](bool proxyOnly) {
        if (!proxyOnly || m_configuration.mode == NetworkConfiguration::Mode::System) {
            qCDebug(networkPolicyLog) << "Network configuration changed; retiring cached routes";
            m_transport->recoverAfterWake();
        }
    });
    if (!m_transport->isReady() ||
        !api.setNetworkTransport(quint16(m_transport->endpoint().port()))) {
        setError(tr("无法初始化统一网络传输"));
    }
    // Application creates this authority before account initialization/request
    // dispatch. Restoring preferences must not advance the account generation.
    if (!api.setNetworkTimeout(m_apiSeconds))
        setError(tr("网络超时设置未被接受"));
    if (m_passwordId.isEmpty()) {
        m_transport->apply(m_configuration, {});
    } else {
        m_busy = true;
        m_secretAvailable = false;
        m_pendingSecretOperation = ++m_nextSecretOperation;
        m_secretCompletion = [this](bool success, const QByteArray &bytes) {
            m_busy = false;
            if (success) {
                m_password = QString::fromUtf8(bytes);
                m_secretAvailable = true;
                m_transport->apply(m_configuration, m_password);
            } else {
                setError(tr("无法读取已保存的代理密码，请重新输入或清除后应用"));
                m_transport->apply(m_configuration, {});
            }
            emit changed();
        };
        m_store->read(m_pendingSecretOperation, m_passwordId);
    }
}
bool NetworkPreferences::ensureStartupTransport()
{
    if (m_busy || !m_api.initializeRuntime())
        return false;
    if (!m_transport->isReady()) {
        m_transport = std::make_unique<NetworkTransport>();
        connect(m_transport.get(), &NetworkTransport::mediaRequestRejected, this,
                &NetworkPreferences::mediaRequestRejected);
        m_transport->apply(m_configuration, m_password);
    }
    if (!m_transport->isReady() ||
        !m_api.setNetworkTransport(quint16(m_transport->endpoint().port())) ||
        !m_api.setNetworkTimeout(m_apiSeconds))
        return false;
    if (m_error == tr("无法初始化统一网络传输") || m_error == tr("网络超时设置未被接受"))
        setError({});
    installApplicationTransport();
    return true;
}
void NetworkPreferences::installApplicationTransport()
{
    QNetworkProxyFactory::setApplicationProxyFactory(
        new TransportProxyFactory(quint16(m_transport->endpoint().port())));
}
QUrl NetworkPreferences::acceleratedGithubUrl(const QUrl &url) const
{
    const auto host = url.host().toLower();
    const auto accelerator = m_githubProxy.trimmed().remove(QRegularExpression("/+$"));
    if (accelerator.isEmpty() || (url.scheme() != "http" && url.scheme() != "https") ||
        (host != "github.com" && host != "raw.githubusercontent.com" &&
         host != "codeload.github.com" && !host.endsWith(".githubusercontent.com")))
        return url;
    return QUrl(accelerator + '/' + url.toString(QUrl::FullyEncoded));
}
void NetworkPreferences::recoverAfterWake()
{
    m_transport->recoverAfterWake();
}
QVariantMap NetworkPreferences::record(const NetworkConfiguration &configuration,
                                       const QString &reference) const
{
    return {{"mode", NetworkConfiguration::modeKey(configuration.mode)},
            {"pacScript", configuration.pacScript},
            {"proxyRules", configuration.proxyRules},
            {"username", configuration.username},
            {"bypassRules", configuration.bypassRules},
            {"passwordId", reference}};
}
bool NetworkPreferences::commit(const NetworkConfiguration &configuration, const QString &password,
                                const QString &reference)
{
    if (!m_transport->isReady() || !m_api.setNetworkTimeout(configuration.apiTimeoutSeconds)) {
        setError(tr("网络设置未被接受"));
        return false;
    }
    auto writer = freshWriter(m_settings);
    const auto previous = writer->value(timeoutsKey);
    const auto previousProxy = writer->value(proxyKey);
    writer->setValue(timeoutsKey,
                     QVariantMap{{"apiSeconds", configuration.apiTimeoutSeconds},
                                 {"playerSeconds", configuration.playerTimeoutSeconds}});
    writer->setValue(proxyKey, record(configuration, reference));
    writer->sync();
    if (writer->status() != EchoMusic::SettingsStore::NoError) {
        if (previous.isValid())
            writer->setValue(timeoutsKey, previous);
        else
            writer->remove(timeoutsKey);
        if (previousProxy.isValid())
            writer->setValue(proxyKey, previousProxy);
        else
            writer->remove(proxyKey);
        writer->sync();
        m_api.setNetworkTimeout(m_apiSeconds);
        setError(tr("无法保存网络设置"));
        return false;
    }
    const bool changedTimeouts = m_apiSeconds != configuration.apiTimeoutSeconds ||
                                 m_playerSeconds != configuration.playerTimeoutSeconds;
    const auto oldReference = m_passwordId;
    m_configuration = configuration;
    m_password = password;
    m_passwordId = reference;
    m_secretAvailable = true;
    m_apiSeconds = configuration.apiTimeoutSeconds;
    m_playerSeconds = configuration.playerTimeoutSeconds;
    m_transport->apply(configuration, password);
    if (oldReference != reference)
        discardSecret(oldReference);
    setError({});
    if (changedTimeouts)
        emit timeoutsChanged();
    emit changed();
    return true;
}
void NetworkPreferences::apply(const QString &mode, const QString &pac, const QString &rules,
                               const QString &username, const QString &password, bool clearPassword,
                               const QString &bypass, double apiSeconds, double playerSeconds)
{
    if (m_busy)
        return;
    NetworkConfiguration requested;
    requested.mode = NetworkConfiguration::modeFromKey(mode);
    requested.pacScript = pac;
    requested.proxyRules = rules;
    requested.username = username;
    requested.bypassRules = bypass;
    requested.apiTimeoutSeconds = apiSeconds;
    requested.playerTimeoutSeconds = playerSeconds;
    const auto error = requested.validationError();
    if (!error.isEmpty()) {
        setError(error);
        return;
    }
    const auto next = requested.normalized();
    if (password.isEmpty() && !clearPassword && !m_secretAvailable) {
        setError(tr("请重新输入代理密码或清除已保存密码"));
        return;
    }
    setError({});
    if (password.isEmpty()) {
        m_busy = true;
        emit changed();
        if (commit(next, clearPassword ? QString() : m_password,
                   clearPassword ? QString() : m_passwordId))
            finishApply();
        else {
            m_busy = false;
            emit changed();
        }
        return;
    }
    m_busy = true;
    emit changed();
    const auto reference = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Store a new local SQLite row before committing the policy record. A failed
    // policy write retains the previously committed password.
    m_pendingSecretOperation = ++m_nextSecretOperation;
    m_secretCompletion = [this, next, password, reference](bool stored, const QByteArray &) {
        bool success = false;
        if (!stored)
            setError(tr("无法保存本地代理密码"));
        else if (!(success = commit(next, password, reference)))
            discardSecret(reference);
        if (success)
            finishApply();
        else {
            m_busy = false;
            emit changed();
        }
    };
    m_store->write(m_pendingSecretOperation, reference, password.toUtf8());
}
void NetworkPreferences::discardSecret(const QString &reference)
{
    if (!reference.isEmpty())
        m_store->remove(++m_nextSecretOperation, reference);
}
void NetworkPreferences::finishApply()
{
    m_transport->afterPendingChanges([guard = QPointer<NetworkPreferences>(this)] {
        if (!guard)
            return;
        guard->m_busy = false;
        emit guard->changed();
        emit guard->applied();
    });
}
void NetworkPreferences::setGithubProxyUrl(const QString &value)
{
    const auto normalized = value;
    if (m_githubProxy == normalized)
        return;
    auto writer = freshWriter(m_settings);
    writer->setValue(EchoMusic::SettingsKeys::Network::GithubProxyUrl, normalized);
    writer->sync();
    if (writer->status() != EchoMusic::SettingsStore::NoError) {
        setError(tr("无法保存 GitHub 加速地址"));
        return;
    }
    m_githubProxy = normalized;
    emit changed();
}
bool NetworkPreferences::applyTimeouts(double apiSeconds, double playerSeconds)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!std::isfinite(apiSeconds) || !std::isfinite(playerSeconds) || apiSeconds < 0 ||
        apiSeconds > 300 || playerSeconds < 1 || playerSeconds > 300) {
        setError(tr("网络超时设置无效"));
        return false;
    }
    if (apiSeconds == m_apiSeconds && playerSeconds == m_playerSeconds) {
        setError({});
        return true;
    }
    auto next = m_configuration;
    next.apiTimeoutSeconds = apiSeconds;
    next.playerTimeoutSeconds = playerSeconds;
    return commit(next, m_password, m_passwordId);
}
void NetworkPreferences::setError(const QString &error)
{
    if (!error.isEmpty())
        emit notice(error);
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}
} // namespace EchoMusic
