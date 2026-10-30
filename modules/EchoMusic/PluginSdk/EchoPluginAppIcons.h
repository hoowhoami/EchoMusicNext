#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include <QPointer>
#include <QHash>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginAppIcons final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
public:
    explicit EchoPluginAppIcons(EchoPluginContext &);
    QVariantMap snapshot() const;
    bool drained() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *set(const QString &id, const QVariantMap &icons,
                                                    int priority = 0);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *setRuntimeWindowIcon(const QString &path,
                                                                     int priority = 0);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *restoreDefaultWindowIcon();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *restoreDefaultDesktopIcon();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *restoreDefaultTaskbarIcon();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *refresh();
signals:
    void changed();
    void drainedChanged();

private:
    EchoPluginOperation *restore(const QString &target);
    EchoPluginContext &m_scope;
    QHash<QString, QString> m_tokens;
    QString m_error;
    int m_pending = 0;
    quint64 m_shortcutBarrier = 0;
    bool m_withdrawing = false;
};
} // namespace EchoMusic
