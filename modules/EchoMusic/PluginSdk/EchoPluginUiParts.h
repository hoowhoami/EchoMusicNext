#pragma once
#include "EchoUiPartRef.h"
#include "EchoUiContribution.h"
#include "EchoUiBatch.h"
#include "EchoUiModelContribution.h"
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
    Q_INVOKABLE EchoMusic::EchoUiBatch *applyBatch(const QString &id, const QVariantList &entries);
    Q_INVOKABLE EchoMusic::EchoUiContribution *
    contribute(const QString &id, const QVariantMap &selector, const QVariantMap &definition);
    Q_INVOKABLE EchoMusic::EchoUiModelContribution *contributeModel(const QString &id,
                                                                    const QVariantMap &selector,
                                                                    const QVariantMap &definition,
                                                                    const QJSValue &provider = {});
    Q_INVOKABLE EchoMusic::EchoUiPartRef *query(const QVariantMap &selector = {});
    Q_INVOKABLE EchoMusic::EchoUiPartRef *queryUnique(const QVariantMap &selector);
    Q_INVOKABLE QVariantList queryAll(const QVariantMap &selector = {});
    Q_INVOKABLE QVariantMap preflightBatch(const QVariantList &entries, const QString &id = {});
    Q_INVOKABLE QVariantMap preflight(const QVariantMap &selector, const QVariantMap &definition);
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
