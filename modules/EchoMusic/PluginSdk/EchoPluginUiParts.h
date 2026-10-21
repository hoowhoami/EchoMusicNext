#pragma once
#include "EchoUiPartRef.h"
#include "EchoUiPartMount.h"
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QHash>
namespace EchoMusic
{
class EchoPluginContext;
class EchoUiPartRegistry;
class EchoUiPartRegistration;
class EchoUiPartMount;
class EchoPluginUiParts final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ui")
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
public:
    explicit EchoPluginUiParts(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoUiPartRef *query(const QVariantMap &selector = {});
    Q_INVOKABLE QVariantList queryAll(const QVariantMap &selector = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *observe(const QString &id,
                                                           const QVariantMap &selector,
                                                           const QJSValue &callback,
                                                           bool immediate = true);
    Q_INVOKABLE EchoMusic::EchoUiPartMount *mount(const QString &id, EchoMusic::EchoUiPartRef *part,
                                                  const QString &position, const QString &component,
                                                  int priority = 0);
    EchoUiPartRef *reference(EchoUiPartRegistration &);
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
    EchoUiPartRegistry *registry() const;
    QString errorMessage() const
    {
        return m_error;
    }
    void error(const QString &);
signals:
    void errorChanged();

private:
    EchoPluginContext &m_scope;
    QHash<QString, QPointer<EchoUiPartRef>> m_refs;
    QString m_error;
};
} // namespace EchoMusic
