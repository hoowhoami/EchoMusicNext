// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AnalysisBroker.h"
#include <QJsonObject>

struct EchoAudioAnalysisSource;
namespace EchoMusic::AudioAnalysis
{
struct CapturedPcmSource;
class NativePcmSource : public std::enable_shared_from_this<NativePcmSource>
{
public:
    ~NativePcmSource();
    QJsonObject info() const
    {
        return m_info;
    }
    Reader reader(Kind);
    void cancel();

private:
    friend struct CapturedPcmSource;
    friend CapturedPcmSource captureCurrentPcmSource(const QJsonObject &);
    EchoAudioAnalysisSource *m_source = nullptr;
    QJsonObject m_info;
    double m_statedSeconds = 0;
};
struct CapturedPcmSource
{
    std::shared_ptr<NativePcmSource> source;
    QString code, error;
};
// Host-owned acknowledged facts only. No SDK caller can choose a locator/ordinal.
CapturedPcmSource captureCurrentPcmSource(const QJsonObject &facts);
} // namespace EchoMusic::AudioAnalysis
