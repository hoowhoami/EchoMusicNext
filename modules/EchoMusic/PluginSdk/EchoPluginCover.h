#pragma once
#include "EchoPluginRegistration.h"
#include "EchoPluginOperation.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginCover final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by context and ui")
public:
    explicit EchoPluginCover(EchoPluginContext &);
    Q_INVOKABLE QString createThemedIconCoverUrl(const QVariantMap &options) const;
    Q_INVOKABLE QString createThemedIconCoverUrl(const QString &icon = "music",
                                                 const QString &color = {}) const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    setFallback(const QString &id, const QJSValue &resolver, int priority = 0);
    Q_INVOKABLE void invalidate();

private:
    EchoPluginContext &m_scope;
    QHash<QString, QString> m_tokens;
};
} // namespace EchoMusic
