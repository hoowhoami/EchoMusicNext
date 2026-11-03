#pragma once
#include "ApiClient.h"
#include "RouteController.h"
#include <QJsonArray>
#include <QPointer>
#include <QQueue>
namespace EchoMusic
{
class BarrageController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a resource service")
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int sendCooldownSeconds READ sendCooldownSeconds NOTIFY sendCooldownChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY changed)
public:
    int sendCooldownSeconds() const
    {
        return m_api.messageSendCooldownSeconds();
    }
    BarrageController(ApiClient &, RouteContext *, bool network, QObject *parent,
                      bool song = false);
    ~BarrageController() override;
    bool loading() const
    {
        return m_read != 0 && m_pool.isEmpty();
    }
    bool busy() const
    {
        return m_write != 0;
    }
    bool empty() const
    {
        return m_pool.isEmpty() && m_own.isEmpty();
    }
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString draft() const
    {
        return m_draft;
    }
    void setDraft(const QString &);
    void setResource(const QString &hash, const QString &name);
    void setPresented(bool);
    Q_INVOKABLE void setEnabled(bool);
    Q_INVOKABLE QVariantMap next(bool ownOnly = false);
    Q_INVOKABLE void refreshFlying(const QVariantMap &);
    Q_INVOKABLE void markFlying(const QVariantMap &, int durationMs);
    Q_INVOKABLE void send();
    void shutdown();
signals:
    void sendCooldownChanged();
    void changed();
    void resetFlights();
    void ownReady();
    void submitted();
    void verificationStarted();
    void submissionFailed();
    void scopeReset();
    void notice(const QString &, bool);

private:
    friend class DetailsTestAccess;
    void cancel(bool cancelWrite = true);
    void load(bool preserve = false);
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QByteArray &);
    bool active() const;
    QString identity(const QVariantMap &) const;
    ApiClient &m_api;
    QPointer<RouteContext> m_route;
    bool m_song;
    QJsonArray m_pool;
    QQueue<QVariantMap> m_own;
    QHash<QString, qint64> m_recent;
    QString m_hash, m_name, m_draft, m_sentDraft, m_error;
    quint64 m_read = 0, m_write = 0, m_session = 0;
    qsizetype m_cursor = 0;
    bool m_network, m_closed = false, m_enabled = false, m_presented = false, m_preserve = false;
};
} // namespace EchoMusic
