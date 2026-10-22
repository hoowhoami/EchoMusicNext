#pragma once
#include "EchoPluginRegistration.h"
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
/// One callback's cancellation scope. Link I/O operations so a canceled source
/// request aborts their transports as well as ignoring late Promise results.
class EchoPluginSourceRequest final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided to resolver callbacks")
    Q_PROPERTY(bool canceled READ canceled NOTIFY canceledChanged)
public:
    explicit EchoPluginSourceRequest(QObject *parent) : QObject(parent) {}
    bool canceled() const
    {
        return m_canceled;
    }
    Q_INVOKABLE bool track(EchoMusic::EchoPluginOperation *operation);
    void cancel();
signals:
    void canceledChanged();

private:
    bool m_canceled = false;
    QList<QPointer<EchoPluginOperation>> m_operations;
};
/// Registration-only facade. Resolution runs in the shared playback pipeline,
/// and only committed main-context registrations participate.
class EchoPluginAudioSource final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginPlayer")
public:
    explicit EchoPluginAudioSource(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerResolver(const QString &id,
                                                                    const QJSValue &definition);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
