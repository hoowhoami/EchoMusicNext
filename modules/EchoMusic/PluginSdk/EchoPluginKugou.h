#pragma once
#include "EchoPluginOperation.h"
#include <QHash>
#include <QPointer>
namespace EchoMusic
{
class EchoPluginContext;
/// Business commands plus explicitly authorized current-session credential snapshots.
class EchoPluginKugou final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(QVariantMap account READ account NOTIFY accountChanged)
public:
    static QStringList methods();
    QVariantMap account() const;
    explicit EchoPluginKugou(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *request(const QString &method,
                                                        const QVariantMap &arguments = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *authSnapshot();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *verify(const QString &eventId);

signals:
    void accountChanged();

private:
    bool m_accountNoticePending = false;
    EchoPluginOperation *submit(const std::function<quint64()> &start);
    EchoPluginContext &m_scope;
    QHash<quint64, QPointer<EchoPluginOperation>> m_pending;
};
} // namespace EchoMusic
