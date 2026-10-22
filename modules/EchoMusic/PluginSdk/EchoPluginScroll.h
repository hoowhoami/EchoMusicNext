#pragma once
#include "EchoUiPartRef.h"
#include "EchoPluginRegistration.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginScroll final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginScroll(EchoPluginContext &scope) : QObject(), m_scope(scope) {}
    Q_INVOKABLE QVariant snapshot(EchoMusic::EchoUiPartRef *container) const;
    Q_INVOKABLE EchoMusic::EchoUiPartRef *current();
    Q_INVOKABLE EchoMusic::EchoUiPartRef *query(const QVariantMap &selector = {});
    Q_INVOKABLE QVariantList queryAll(const QVariantMap &selector = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *observe(const QString &id,
                                                           const QVariantMap &selector,
                                                           const QJSValue &callback,
                                                           bool immediate = true);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *scrollTo(EchoMusic::EchoUiPartRef *container,
                                                         double position);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *scrollBy(EchoMusic::EchoUiPartRef *container,
                                                         double distance);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *top(EchoMusic::EchoUiPartRef *container);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *bottom(EchoMusic::EchoUiPartRef *container);

private:
    EchoPluginOperation *invoke(EchoUiPartRef *, const QString &, double = 0);
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
