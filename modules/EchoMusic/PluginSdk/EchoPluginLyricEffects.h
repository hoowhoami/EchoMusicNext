#pragma once
#include "EchoPluginRegistration.h"
#include "EchoUiPartRef.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginLyricEffects final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginLyricEffects(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerEffect(const QString &id,
                                                                  const QJSValue &definition);
    Q_INVOKABLE QVariant snapshot(EchoMusic::EchoUiPartRef *part) const;

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
