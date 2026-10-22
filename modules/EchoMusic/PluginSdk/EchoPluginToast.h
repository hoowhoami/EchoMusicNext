#pragma once
#include "EchoPluginOperation.h"
#include <QSet>
#include <QPointer>
namespace EchoMusic
{
class EchoPluginContext;
class ToastService;
class EchoPluginToast final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginToast(EchoPluginContext &scope);
    ~EchoPluginToast() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *info(const QString &message,
                                                     const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *success(const QString &message,
                                                        const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *warning(const QString &message,
                                                        const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *danger(const QString &message,
                                                       const QVariantMap &options = {});
    Q_INVOKABLE void dismiss(const QString &id);

private:
    EchoPluginOperation *show(const QString &, const QVariantMap &, int);
    void clear();
    EchoPluginContext &m_scope;
    QPointer<ToastService> m_service;
    QSet<quint64> m_owned;
};
} // namespace EchoMusic
