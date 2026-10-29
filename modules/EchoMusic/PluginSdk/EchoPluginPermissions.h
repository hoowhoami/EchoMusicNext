#pragma once
#include "EchoPluginOperation.h"
#include "SettingsStore.h"
#include <QPointer>
#include <QRect>
#include <functional>
class QMessageBox;
namespace EchoMusic
{
class EchoPluginContext;
/// Device-local grants are bound to the exact installed content, never a public ID alone.
class PluginPermissionStore final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin runtime")
    Q_PROPERTY(QVariantList records READ records NOTIFY changed)
public:
    explicit PluginPermissionStore(EchoMusic::SettingsStore &, QObject *parent);
    QVariantList records() const;
    QRect windowBounds(const QString &installation, const QString &id) const;
    void saveWindowBounds(const QString &installation, const QString &id, const QRect &bounds);
    bool granted(const EchoPluginContext &, const QString &, const QString &resource = {}) const;
    bool waiting(const QString &installation) const;
    void authorize(EchoPluginContext &, const QString &, const QString &, EchoPluginOperation *,
                   std::function<void()> ready, const QString &resource = {});
    Q_INVOKABLE bool revoke(const QString &id);
    Q_INVOKABLE void decide(const QString &requestId, bool allow);
    void cancelScope(EchoPluginContext *);
    void forgetInstallation(const QString &installation);
signals:
    void changed();
    void pendingChanged();
    void confirmationRequested(const QString &requestId, const QVariantMap &request);
    void revoked(const QString &installationId, const QString &capability);

private:
    struct Pending
    {
        QPointer<EchoPluginContext> scope;
        QPointer<EchoPluginOperation> operation;
        QString permissionKey;
        QVariantMap record;
        quint64 epoch;
        std::function<void()> ready;
    };
    QString key(const EchoPluginContext &, const QString &, const QString &resource = {}) const;
    void prunePrompts();
    QHash<QString, QString> m_promptKeys;
    QHash<QString, QPointer<QMessageBox>> m_dialogs;
    EchoMusic::SettingsStore &m_settings;
    QHash<QString, Pending> m_pending;
    QHash<QString, QVariantMap> m_session;
};
class EchoPluginPermissions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
public:
    explicit EchoPluginPermissions(EchoPluginContext &);
    Q_INVOKABLE bool granted(const QString &capability) const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *request(const QString &capability,
                                                        const QString &purpose = {});
    EchoPluginOperation *authorize(const QString &, const QString &,
                                   std::function<void(EchoPluginOperation *)>);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
