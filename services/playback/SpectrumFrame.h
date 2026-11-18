#pragma once
#include <QList>
#include <QSharedPointer>
#include <QSpan>
#include <QString>
#include <memory>

namespace EchoMusic
{
// Views borrow the original Rust vectors through owner; the last reader releases them.
struct RawSpectrumFrame
{
    std::shared_ptr<const void> owner;
    QSpan<const double> bins, waveform;
    double timestamp = 0, peak = 0, rms = 0;
    int sampleRate = 0, fftSize = 0;
};
struct SpectrumFrameData
{
    QString source, state;
    quint64 sequence = 0;
    double timestamp = 0, peak = 0, rms = 0, timePos = 0, minFrequency = 0, maxFrequency = 0;
    int sampleRate = 0, fftSize = 0;
    QList<double> bins, bandMeans;
    QSharedPointer<const RawSpectrumFrame> waveformOwner;
    bool includeWaveform = false;
    QSpan<const double> waveform() const
    {
        return waveformOwner ? waveformOwner->waveform : QSpan<const double>{};
    }
};
} // namespace EchoMusic
