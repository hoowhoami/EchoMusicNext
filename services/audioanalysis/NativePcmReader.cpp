// SPDX-License-Identifier: GPL-3.0-only
#include "NativePcmReader.h"
#include "echo_ffi.h"
#include <QJsonDocument>
#include <chrono>
#include <cmath>
#include <thread>

namespace EchoMusic::AudioAnalysis
{
namespace
{
struct Reply
{
    QJsonObject value;
    QString code, error;
};
void reply(void *context, const EchoResult *result)
{
    auto &output = *static_cast<Reply *>(context);
    if (result->payload_len > 65536) {
        output.code = "InvalidResult";
        output.error = "native source metadata budget";
        return;
    }
    output.value =
        QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(result->payload),
                                           qsizetype(result->payload_len)))
            .object();
    if (result->status != EchoStatus::Success) {
        output.code = output.value.value("code").toString("DecodeError");
        output.error = output.value.value("message").toString("native source decoding failed");
    }
}
quint64 decimal(const QJsonValue &value)
{
    bool valid = false;
    const auto number = value.toString().toULongLong(&valid);
    return valid ? number : 0;
}
} // namespace
CapturedPcmSource captureCurrentPcmSource(const QJsonObject &facts)
{
    CapturedPcmSource output;
    const auto sequence = decimal(facts.value("trackSequence"));
    const auto locator = facts.value("locator").toString().toUtf8();
    const auto ordinal = facts.value("audioStreamOrdinal").toInteger(-2);
    if (!sequence || locator.isEmpty() || locator.size() > 32768 || ordinal < -1 || ordinal > 31) {
        output.code = "Unavailable";
        output.error = "no acknowledged native audio source is available";
        return output;
    }
    if (facts.value("preview").toBool()) {
        output.code = "IncompleteSource";
        output.error = "preview audio cannot produce whole-track analysis";
        return output;
    }
    Reply result;
    const auto epoch = decimal(facts.value("analysisTransitionEpoch"));
    auto *source = facts.contains("analysisTransitionEpoch")
                       ? echo_audio_analysis_capture_outgoing(
                             sequence, reinterpret_cast<const uint8_t *>(locator.constData()),
                             size_t(locator.size()), ordinal, epoch, reply, &result)
                       : echo_audio_analysis_capture(
                             sequence, reinterpret_cast<const uint8_t *>(locator.constData()),
                             size_t(locator.size()), ordinal, reply, &result);
    if (!source || !result.code.isEmpty()) {
        if (source)
            echo_audio_analysis_source_destroy(source);
        output.code = result.code.isEmpty() ? "Unavailable" : result.code;
        output.error =
            result.error.isEmpty() ? "current native audio source unavailable" : result.error;
        return output;
    }
    output.source = std::make_shared<NativePcmSource>();
    output.source->m_source = source;
    output.source->m_info = result.value;
    output.source->m_statedSeconds =
        facts.value("standardDurationMs").toDouble(facts.value("statedDurationMs").toDouble()) /
        1000.;
    return output;
}
NativePcmSource::~NativePcmSource()
{
    echo_audio_analysis_source_destroy(m_source);
}
void NativePcmSource::cancel()
{
    echo_audio_analysis_cancel(m_source);
}
Reader NativePcmSource::reader(Kind kind)
{
    return [owned = shared_from_this(), kind](const auto &canceled) {
        struct PcmReply : Reply
        {
            Pcm pcm;
            std::shared_ptr<std::atomic_bool> canceled;
            int rate;
        };
        PcmReply output;
        output.canceled = canceled;
        output.rate = kind == Kind::Beats ? 22050 : 44100;
        if (canceled->load()) {
            output.pcm.code = "Canceled";
            output.pcm.error = "analysis source retired";
            return output.pcm;
        }
        std::atomic_bool deadline{false};
        std::jthread monitor([owned, canceled, &deadline](std::stop_token stop) {
            const auto start = std::chrono::steady_clock::now();
            while (!stop.stop_requested()) {
                if (canceled->load() ||
                    std::chrono::steady_clock::now() - start > std::chrono::seconds(120)) {
                    deadline.store(!canceled->load());
                    owned->cancel();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        const auto receive = [](void *context, const float *left, const float *right,
                                size_t frames) -> bool {
            auto &output = *static_cast<PcmReply *>(context);
            if (output.canceled->load() || !left || frames > size_t(output.rate) * 600 ||
                frames * sizeof(float) >
                    size_t(output.rate) * 600 * 4 - size_t(output.pcm.left.size()))
                return false;
            output.pcm.left.append(reinterpret_cast<const char *>(left),
                                   qsizetype(frames * sizeof(float)));
            if (right)
                output.pcm.right.append(reinterpret_cast<const char *>(right),
                                        qsizetype(frames * sizeof(float)));
            return true;
        };
        const auto finished = [](void *context, const EchoResult *result) {
            reply(static_cast<Reply *>(static_cast<PcmReply *>(context)), result);
        };
        const bool success = echo_audio_analysis_decode(
            owned->m_source, kind == Kind::Beats ? 0 : 1, receive, finished, &output);
        monitor.request_stop();
        monitor.join();
        if (canceled->load()) {
            output.pcm.code = "Canceled";
            output.pcm.error = "analysis source retired";
        } else if (deadline.load()) {
            output.pcm.code = "Timeout";
            output.pcm.error = "whole-source decoding absolute deadline";
        } else if (!success || !output.code.isEmpty()) {
            output.pcm.code = output.code.isEmpty() ? "DecodeError" : output.code;
            output.pcm.error =
                output.error.isEmpty() ? "native source decoding failed" : output.error;
        } else {
            const auto frames = output.value.value("frames").toInteger(-1);
            const double seconds = output.value.value("duration_seconds").toDouble(-1);
            if (output.value.value("sample_rate").toInt() != output.rate ||
                output.value.value("channels").toInt() != (kind == Kind::Beats ? 1 : 2) ||
                !output.value.value("whole_track").toBool() || frames < 1024 ||
                frames > qint64(output.rate) * 600 || !std::isfinite(seconds) || seconds <= 0 ||
                std::abs(seconds - double(frames) / output.rate) > 1. / output.rate ||
                frames * 4 != output.pcm.left.size() ||
                (kind == Kind::DrumHits && output.pcm.right.size() != output.pcm.left.size())) {
                output.pcm.code = "InvalidResult";
                output.pcm.error = "native whole-source PCM summary/shape rejected";
            } else if (owned->m_statedSeconds > 0 && seconds < owned->m_statedSeconds * .5) {
                output.pcm.code = "IncompleteSource";
                output.pcm.error = "decoded audio is shorter than half the stated song";
            } else {
                output.pcm.sampleRate = output.rate;
                output.pcm.wholeTrack = true;
            }
        }
        if (!output.pcm.error.isEmpty()) {
            output.pcm.left.clear();
            output.pcm.right.clear();
        }
        return output.pcm;
    };
}
} // namespace EchoMusic::AudioAnalysis
