#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "ApiInterceptors.h"
#include <QJSValue>
#include <QPointer>
#include <QTimer>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginInterceptRequest final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created for a host API interception")
    Q_PROPERTY(QString stage READ stage CONSTANT)
    Q_PROPERTY(QVariantMap transport READ transport CONSTANT)
    Q_PROPERTY(QString operation READ operation CONSTANT)
    Q_PROPERTY(QVariantMap query READ query CONSTANT)
    Q_PROPERTY(QVariantMap headers READ headers CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    EchoPluginInterceptRequest(EchoPluginContext &, QJsonObject, ApiInterceptNext, ApiInterceptDone,
                               int timeout);
    QString stage() const
    {
        return m_request.value("stage").toString();
    }
    QVariantMap transport() const
    {
        return stage() == "transport" ? m_request.toVariantMap() : QVariantMap();
    }
    QString operation() const
    {
        return m_request.value("operation").toString();
    }
    QVariantMap query() const
    {
        return m_request.value("query").toObject().toVariantMap();
    }
    QVariantMap headers() const
    {
        return m_request.value("headers").toObject().toVariantMap();
    }
    bool active() const
    {
        return m_active;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *next(const QVariantMap &patch = {});
    void settle(const QVariant &);
    void fallback();
    void invalidate();
    static QVariantMap response(const ApiInterceptReply &, bool wire = false);
signals:
    void changed();

private:
    EchoPluginContext &m_scope;
    QJsonObject m_request;
    ApiInterceptNext m_next;
    ApiInterceptDone m_done;
    QList<QPointer<EchoPluginOperation>> m_waits;
    QTimer m_deadline;
    bool m_active = true;
};
class EchoPluginServer final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
public:
    explicit EchoPluginServer(EchoPluginContext &);
    Q_INVOKABLE QString encodeBody(const QVariant &value, const QString &encoding = "json");
    Q_INVOKABLE QVariant decodeBody(const QString &base64, const QString &encoding = "json");
    Q_INVOKABLE EchoMusic::EchoPluginOperation *intercept(const QJSValue &definition);

private:
    EchoPluginContext &m_scope;
    QList<QPointer<EchoPluginRegistration>> m_leases;
    void reset();
};
} // namespace EchoMusic
