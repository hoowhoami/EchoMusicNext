#pragma once
#include "EchoPluginRegistration.h"
#include <QObject>
#include <QVariant>
namespace EchoMusic
{
class EchoPluginContext;
class PlayerSpectrum;
class EchoPluginSpectrumSubscription final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by spectrum.subscribe")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QVariant frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QVariant status READ status NOTIFY statusChanged)
public:
    EchoPluginSpectrumSubscription(EchoPluginContext &, EchoPluginRegistration &,
                                   const QVariantMap &);
    ~EchoPluginSpectrumSubscription() override;
    bool active() const
    {
        return m_lease.active();
    }
    bool paused() const
    {
        return m_paused;
    }
    QVariant frame() const
    {
        return m_frame.isEmpty() ? QVariant() : QVariant(m_frame);
    }
    QVariant status() const;
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void dispose();
signals:
    void activeChanged();
    void pausedChanged();
    void frameChanged();
    void statusChanged();

private:
    PlayerSpectrum &m_broker;
    EchoPluginRegistration &m_lease;
    QVariantMap m_frame;
    quint64 m_id = 0;
    bool m_paused = false;
};
class EchoPluginSpectrum final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant status READ getStatus NOTIFY statusChanged)
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin audio")
public:
    explicit EchoPluginSpectrum(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoPluginSpectrumSubscription *
    subscribe(const QVariantMap &options = {});
    Q_INVOKABLE QVariant getStatus() const;
signals:
    void statusChanged();

private:
    EchoPluginContext &m_scope;
};
class EchoPluginAudio final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(EchoMusic::EchoPluginSpectrum *spectrum READ spectrum CONSTANT)
public:
    explicit EchoPluginAudio(EchoPluginContext &scope);
    EchoPluginSpectrum *spectrum()
    {
        return &m_spectrum;
    }

private:
    EchoPluginSpectrum m_spectrum;
};
} // namespace EchoMusic
