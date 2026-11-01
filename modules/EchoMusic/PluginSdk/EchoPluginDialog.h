#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
class QQmlComponent;
namespace EchoMusic
{
class EchoPluginContext;
/// Owns one on-demand main-window popup and its presentation scope.
class EchoPluginDialog final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by ui.showDialog")
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    explicit EchoPluginDialog(EchoPluginContext &, EchoPluginOperation *);
    ~EchoPluginDialog() override;
    bool active() const
    {
        return m_owner != nullptr || (m_opening && !m_opening->finished());
    }
    void open(const QVariantMap &, EchoPluginOperation *);
    Q_INVOKABLE void close();
signals:
    void changed();
    void closed();
private slots:
    void ready();
    void finished();
    void retirePresentation();
    void failed();

private:
    EchoPluginContext &m_scope;
    QPointer<QObject> m_owner;
    QPointer<QQmlComponent> m_component;
    QPointer<EchoPluginOperation> m_opening;
};
} // namespace EchoMusic
