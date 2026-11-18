#pragma once
#include "EchoPluginRegistration.h"
#include "SpectrumFrame.h"
#include <QJSValue>
#include <QObject>
#include <QVariant>
namespace EchoMusic
{
class EchoPluginContext;
class PlayerSpectrum;
class EchoPluginSpectrumFrame final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by spectrum subscription")
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(quint64 sequence READ sequence NOTIFY changed)
    Q_PROPERTY(double peak READ peak NOTIFY changed)
    Q_PROPERTY(double rms READ rms NOTIFY changed)
    Q_PROPERTY(double timestamp READ timestamp NOTIFY changed)
    Q_PROPERTY(double timePos READ timePos NOTIFY changed)
    Q_PROPERTY(double minFrequency READ minFrequency NOTIFY changed)
    Q_PROPERTY(double maxFrequency READ maxFrequency NOTIFY changed)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY changed)
    Q_PROPERTY(int fftSize READ fftSize NOTIFY changed)
    Q_PROPERTY(int binCount READ binCount NOTIFY changed)
    Q_PROPERTY(int waveformCount READ waveformCount NOTIFY changed)
    Q_PROPERTY(QVariantList bandMeans READ bandMeans NOTIFY changed)
public:
    explicit EchoPluginSpectrumFrame(EchoPluginContext &scope, QObject *parent);
    void replace(const SpectrumFrameData &frame);
    QVariant legacy() const;
    QString state() const
    {
        return m_data.state;
    }
    QString source() const
    {
        return m_data.source;
    }
    quint64 sequence() const
    {
        return m_data.sequence;
    }
    double peak() const
    {
        return m_data.peak;
    }
    double rms() const
    {
        return m_data.rms;
    }
    double timestamp() const
    {
        return m_data.timestamp;
    }
    double timePos() const
    {
        return m_data.timePos;
    }
    double minFrequency() const
    {
        return m_data.minFrequency;
    }
    double maxFrequency() const
    {
        return m_data.maxFrequency;
    }
    int sampleRate() const
    {
        return m_data.sampleRate;
    }
    int fftSize() const
    {
        return m_data.fftSize;
    }
    int binCount() const
    {
        return int(m_data.bins.size());
    }
    int waveformCount() const
    {
        return int(m_data.waveform().size());
    }
    QVariantList bandMeans() const;
    Q_INVOKABLE QJSValue copyBins() const;
    Q_INVOKABLE QJSValue copyWaveform() const;
signals:
    void changed();

private:
    QJSValue copy(QSpan<const double>) const;
    EchoPluginContext &m_scope;
    SpectrumFrameData m_data;
    mutable QVariant m_legacy;
};
class EchoPluginSpectrumSubscription final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by spectrum.subscribe")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(EchoMusic::EchoPluginSpectrumFrame *currentFrame READ currentFrame CONSTANT)
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
        return m_frame.legacy();
    }
    EchoPluginSpectrumFrame *currentFrame()
    {
        return &m_frame;
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
    EchoPluginSpectrumFrame m_frame;
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
