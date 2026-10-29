#include "PlaybackTransitionPresentation.h"
#include <cmath>

namespace EchoMusic
{
namespace
{
QString identity(const QJsonValue &value)
{
    if (!value.isString())
        return {};
    const auto text = value.toString();
    bool ok = false;
    const auto number = text.toULongLong(&ok);
    return ok && text == QString::number(number) ? text : QString();
}
bool finite(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0;
}
int phaseOrder(const QString &phase)
{
    return QStringList{"queued", "mixing", "tail", "completed"}.indexOf(phase);
}
} // namespace
void PlaybackTransitionTrack::clear()
{
    track = {};
    cover = QUrl();
    sequence.clear();
    lyricState = "empty";
    lyricError.clear();
    lyrics.clear();
    ++lyricRevision;
    emit changed();
}
QVariantMap PlaybackTransitionTrack::lyricsInfo() const
{
    return {{"count", lyrics.rowCount()},
            {"offsetMs", lyrics.offsetMs()},
            {"currentIndex", lyrics.currentIndex()},
            {"scrollable", lyrics.scrollable()},
            {"hasTranslation", lyrics.hasTranslation()},
            {"hasRomanization", lyrics.hasRomanization()},
            {"state", lyricState},
            {"errorMessage", lyricError},
            {"metadata", lyrics.metadata()}};
}
PlaybackTransitionPresentation::PlaybackTransitionPresentation(ApiClient &api, NativeBridge &native,
                                                               SettingsStore &settings,
                                                               QObject *parent)
    : QObject(parent), m_api(api), m_nativeBridge(native), m_settings(settings), m_outgoing(this),
      m_incoming(this)
{
    connect(&api, &ApiClient::sessionChanged, this, [this] { invalidate("account-changed"); });
    connect(&api, &ApiClient::closed, this, [this] { invalidate("account-closed"); });
}
PlaybackTransitionPresentation::~PlaybackTransitionPresentation()
{
    cancelIncoming();
}
void PlaybackTransitionPresentation::cancelIncoming()
{
    m_lyricSource.reset();
    m_lyricText.reset();
}
void PlaybackTransitionPresentation::prepare(const PlaybackTrack &track, quint64 request,
                                             quint64 sequence)
{
    if (!request || !sequence || !track.playable())
        return;
    const auto account = QString::number(m_api.sessionRevision());
    if (m_request == QString::number(request) && m_incomingSequence == QString::number(sequence) &&
        m_target.stableKey() == track.stableKey() && m_account == account)
        return;
    m_target = track;
    m_request = QString::number(request);
    m_incomingSequence = QString::number(sequence);
    m_account = account;
    // A subsequent real target may be prepared while the previous mix still
    // holds A/B. Keep at most these two lyric models until the new PCM epoch.
    if (!m_valid)
        startIncoming();
}
void PlaybackTransitionPresentation::startIncoming()
{
    cancelIncoming();
    m_copying = true;
    m_incoming.clear();
    m_incoming.track = m_target;
    m_incoming.cover = QUrl(m_target.cover);
    m_incoming.sequence = m_incomingSequence;
    m_incoming.lyricState = "loading";
    m_copying = false;
    m_lyricSource = std::make_unique<LyricSourceController>(m_api, m_settings);
    m_lyricText =
        std::make_unique<LyricTextController>(m_incoming.lyrics, m_nativeBridge, m_settings);
    connect(m_lyricSource.get(), &LyricSourceController::changed, this, [this] {
        if (!m_lyricSource || m_copying)
            return;
        m_incoming.lyricState = m_lyricSource->state();
        m_incoming.lyricError = m_lyricSource->errorMessage();
        emit m_incoming.changed();
        emit changed();
    });
    connect(m_lyricSource.get(), &LyricSourceController::activated, this,
            [this](const QJsonObject &lyrics, const QString &key) {
                if (!m_lyricText || key != m_incoming.track.stableKey() ||
                    m_account != QString::number(m_api.sessionRevision()))
                    return;
                if (!m_lyricText->replace(lyrics, key)) {
                    m_incoming.lyricState = "error";
                    m_incoming.lyricError = "Incoming parsed lyric presentation was rejected";
                    emit changed();
                }
            });
    connect(&m_incoming.lyrics, &LyricModel::changed, m_lyricText.get(), [this] {
        if (m_copying)
            return;
        ++m_incoming.lyricRevision;
        emit m_incoming.changed();
        emit changed();
    });
    connect(m_lyricText.get(), &LyricTextController::changed, this, [this] {
        if (!m_lyricText || !m_lyricSource || m_copying)
            return;
        m_incoming.lyricError = m_lyricSource->errorMessage().isEmpty()
                                    ? m_lyricText->errorMessage()
                                    : m_lyricSource->errorMessage();
        emit m_incoming.changed();
        emit changed();
    });
    m_lyricSource->load(m_target);
    m_lyricSource->setDuration(qint64(m_target.durationMs));
}
void PlaybackTransitionPresentation::invalidate(const QString &reason)
{
    const bool hadState = m_valid || !m_request.isEmpty();
    m_valid = false;
    m_native = {};
    m_reason = reason;
    m_request.clear();
    m_incomingSequence.clear();
    m_account.clear();
    m_target = {};
    cancelIncoming();
    m_copying = true;
    m_outgoing.clear();
    m_incoming.clear();
    m_copying = false;
    if (hadState) {
        emit changed();
        emit positionChanged();
    }
}
void PlaybackTransitionPresentation::observe(const QJsonObject &native, quint64 currentSequence,
                                             const PlaybackTrack &current, const QUrl &cover,
                                             const LyricModel &lyrics, const QString &lyricState,
                                             const QString &lyricError)
{
    if (native.isEmpty()) {
        if (m_valid)
            invalidate("native-invalidated");
        return;
    }
    const auto epoch = identity(native.value("epoch"));
    const auto generation = identity(native.value("generation"));
    const auto request = identity(native.value("request_id"));
    const auto a = identity(native.value("outgoing_seq")),
               b = identity(native.value("incoming_seq"));
    const auto sequence = QString::number(currentSequence);
    const auto phase = native.value("phase").toString();
    if (epoch.isEmpty() || epoch == "0" || generation.isEmpty() || a.isEmpty() || b.isEmpty() ||
        (sequence != a && sequence != b) || a == b || phaseOrder(phase) < 0 ||
        !finite(native.value("cue_elapsed_secs")) || !finite(native.value("cue_duration_secs")) ||
        !finite(native.value("output_frames")) || !finite(native.value("overlap_frames")))
        return;
    const auto cue = native.value("presentation_cue").toObject();
    if (!cue.value("ready").isBool() || !cue.value("reason").isString() ||
        (cue.value("ready").toBool() &&
         (!finite(cue.value("crossover")) || cue.value("crossover").toDouble() <= 0 ||
          cue.value("crossover").toDouble() >= 1 ||
          native.value("cue_duration_secs").toDouble() <= 0)))
        return;
    const bool same = m_valid && m_native.value("epoch").toString() == epoch;
    if (!same) {
        if (epoch.toULongLong() <= m_lastEpoch || request.isEmpty() || request != m_request ||
            b != m_incomingSequence || sequence != a || !m_target.playable() ||
            m_account != QString::number(m_api.sessionRevision()))
            return;
        m_valid = false;
        m_copying = true;
        m_outgoing.track = current;
        m_outgoing.cover = cover;
        m_outgoing.sequence = a;
        m_outgoing.lyricState = lyricState;
        m_outgoing.lyricError = lyricError;
        m_outgoing.lyrics.copyPresentationFrom(lyrics);
        ++m_outgoing.lyricRevision;
        m_copying = false;
        if (m_incoming.sequence != b || m_incoming.track.stableKey() != m_target.stableKey())
            startIncoming();
    } else if (m_native.value("generation") != native.value("generation") ||
               m_native.value("outgoing_seq") != native.value("outgoing_seq") ||
               m_native.value("incoming_seq") != native.value("incoming_seq") ||
               native.value("output_frames").toInteger() <
                   m_native.value("output_frames").toInteger() ||
               phaseOrder(phase) < phaseOrder(m_native.value("phase").toString()))
        return;
    const auto oldPhase = m_native.value("phase");
    m_native = native;
    m_valid = true;
    m_lastEpoch = epoch.toULongLong();
    m_reason.clear();
    if (finite(native.value("outgoing_position_secs")))
        m_outgoing.lyrics.setPosition(
            qint64(native.value("outgoing_position_secs").toDouble() * 1000));
    if (finite(native.value("incoming_position_secs")))
        m_incoming.lyrics.setPosition(
            qint64(native.value("incoming_position_secs").toDouble() * 1000));
    if (!same || oldPhase != native.value("phase"))
        emit changed();
    emit positionChanged();
}
void PlaybackTransitionPresentation::updateCurrent(quint64 sequence, const PlaybackTrack &track,
                                                   const QUrl &cover, const LyricModel &lyrics,
                                                   const QString &state, const QString &error,
                                                   bool lyricsChanged)
{
    if (!m_valid || m_incoming.sequence != QString::number(sequence) ||
        m_incoming.track.stableKey() != track.stableKey())
        return;
    m_copying = true;
    // The live B parser has authority once the native boundary adopts it.
    cancelIncoming();
    m_incoming.track = track;
    m_incoming.cover = cover;
    m_incoming.lyricState = state;
    m_incoming.lyricError = error;
    if (lyricsChanged &&
        !(state == "loading" && !lyrics.rowCount() && m_incoming.lyrics.rowCount())) {
        m_incoming.lyrics.copyPresentationFrom(lyrics);
        ++m_incoming.lyricRevision;
    }
    m_copying = false;
    emit m_incoming.changed();
    emit changed();
}
bool PlaybackTransitionPresentation::permitsCover(const QUrl &url) const
{
    return m_valid && !url.isEmpty() && (url == m_outgoing.cover || url == m_incoming.cover);
}
} // namespace EchoMusic
