#include "PlaybackFailureController.h"
#include "PlaybackService.h"
#include <QRandomGenerator>
#include <QRegularExpression>

namespace EchoMusic
{
namespace
{
QString presentationReason(const QString &error)
{
    const auto text = error.trimmed();
    if (text.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")))
        return text.left(160);
    const auto matches = [&text](const QString &pattern) {
        return text.contains(
            QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
    };
    // Same message precedence as EchoMusic's resolveAudioFailureReason. Rights
    // and business errors are already localized by PlaybackSourceRequest.
    if (matches("(?:need|require).*VIP|VIP.*(?:required|only)"))
        return QObject::tr("当前歌曲需要 VIP 权限");
    if (matches("insufficient prepared audio|replacement ended before hand-off"))
        return QObject::tr("新音源缓冲不足，未能完成切换");
    if (matches("timed? out|timeout"))
        return QObject::tr("音源加载超时");
    if (matches("403|forbidden|unauthorized"))
        return QObject::tr("音源访问被拒绝");
    if (matches("decode|invalid data|unsupported.*(?:codec|format)"))
        return QObject::tr("音频解码失败");
    if (matches("network|offline|connection|fetch failed"))
        return QObject::tr("网络连接异常");
    return QObject::tr("音频加载或播放过程中出现异常");
}
} // namespace

PlaybackFailureController::PlaybackFailureController(PlaybackService &playback)
    : QObject(&playback), m_playback(playback)
{
    m_timer.setSingleShot(true);
    connect(&playback.m_preferences, &PlaybackPreferences::failurePolicyChanged, this, [this] {
        // Changing policy cancels an already scheduled action. It does not
        // retroactively turn a paused/error track into a playback request.
        cancel(true);
    });
    connect(&m_timer, &QTimer::timeout, this, [this] {
        auto &p = m_playback;
        if (p.m_closing || m_sequence != p.m_sequence || m_revision != p.m_api.sessionRevision() ||
            m_trackKey != p.m_track.stableKey() || p.m_playing || p.m_loading || !p.m_autoplay ||
            !p.m_preferences.autoNext() || m_attempts >= p.m_preferences.autoNextMaxAttempts())
            return;
        if (p.m_personalFm->active()) {
            ++m_attempts;
            p.m_personalFm->advance(false, false, true);
            return;
        }
        if (p.sourceKey() == DiscoverQueueController::queueKey()) {
            ++m_attempts;
            const auto next = p.m_transition->nextIndex(false);
            if (next >= 0) {
                p.m_transition->consumeNext(next, false);
                p.startIndex(next, true);
            } else
                p.m_discoverQueue->advance(false, true);
            return;
        }
        auto *queue = p.queue();
        const auto count = queue->rowCount();
        if (count <= 1)
            return;
        const auto current = queue->currentIndex();
        int next = -1;
        if (p.m_mode == PlaybackService::Shuffle) {
            next = p.m_transition->failureIndex();
        } else {
            // EchoMusic's error traversal wraps even for sequential/single-loop
            // modes. Natural EOF and queued-next priority are separate policies.
            for (int offset = 1; offset < count; ++offset) {
                const auto index = (qMax(0, current) + offset) % count;
                if (queue->at(index)->playable()) {
                    next = index;
                    break;
                }
            }
        }
        if (next < 0)
            return;
        ++m_attempts;
        p.startIndex(next, true);
    });
}
void PlaybackFailureController::cancel(bool resetAttempts)
{
    m_timer.stop();
    if (resetAttempts)
        m_attempts = 0;
}
void PlaybackFailureController::started(bool preserveFailureChain)
{
    cancel(!preserveFailureChain);
    sourceAdopted();
}
void PlaybackFailureController::succeeded()
{
    cancel(true);
    m_noticeKey.clear();
    ++m_noticeGeneration;
}
void PlaybackFailureController::sourceOperation(const QString &title)
{
    m_operationTitle = title;
}
void PlaybackFailureController::sourceAdopted()
{
    m_operationTitle.clear();
}
void PlaybackFailureController::failed()
{
    auto &p = m_playback;
    if (p.m_closing || !p.hasTrack())
        return;
    const bool scheduled =
        !p.m_playing && p.m_autoplay && p.m_preferences.autoNext() &&
        (p.m_personalFm->active() || p.sourceKey() == DiscoverQueueController::queueKey() ||
         p.queue()->rowCount() > 1) &&
        m_attempts < p.m_preferences.autoNextMaxAttempts();
    const auto sequence = p.m_sequence;
    const auto session = p.m_api.sessionRevision();
    const auto reason = presentationReason(p.m_error);
    const auto key =
        QString::number(p.m_sourceRevision) + QChar(0) + reason + QChar(0) + p.m_mvFailure;
    if (scheduled) {
        m_sequence = sequence;
        m_revision = session;
        m_trackKey = p.m_track.stableKey();
        m_timer.start(p.m_preferences.autoNextDelaySeconds() * 1000);
    }
    if (key == m_noticeKey && sequence == m_noticeSequence && session == m_noticeSession)
        return;
    m_noticeKey = key;
    m_noticeSequence = sequence;
    m_noticeSession = session;
    const auto generation = ++m_noticeGeneration;
    if (!m_operationTitle.isEmpty()) {
        const auto heading =
            m_operationTitle +
            (p.m_track.title.trimmed().isEmpty() ? QString() : " · " + p.m_track.title.trimmed());
        m_operationTitle.clear();
        // An explicit quality/source operation may fail while the existing
        // stream remains audible. Its feedback must survive a playing event.
        emit p.sourceOperationFailed(reason, heading);
        return;
    }
    // Wait until this event's recovery/state handling has settled, like the
    // source player's post-flush watcher. A superseded track must stay silent.
    QTimer::singleShot(0, this, [this, sequence, session, reason, key, generation] {
        auto &player = m_playback;
        if (player.m_closing || sequence != player.m_sequence ||
            session != player.m_api.sessionRevision() || player.m_error.isEmpty() ||
            player.m_loading || key != m_noticeKey || generation != m_noticeGeneration)
            return;
        QStringList details;
        const auto mvReason = presentationReason(player.m_mvFailure);
        if (!player.m_mvFailure.isEmpty() && mvReason != reason)
            details.append(mvReason);
        if (m_timer.isActive())
            details.append(
                tr("%1 秒后尝试下一首").arg(player.m_preferences.autoNextDelaySeconds()));
        else if (!reason.contains(
                     QRegularExpression("VIP|会员|购买|付费|支付|权益|授权|版权|下架|请.*(?:重试|"
                                        "检查)|后重试|解码|不支持|缓冲不足|访问被拒绝",
                                        QRegularExpression::CaseInsensitiveOption)))
            details.append(
                reason.contains(QRegularExpression("网络|超时|离线|连接(?:失败|异常|中断)"))
                    ? tr("请检查网络后重试")
                    : tr("请稍后重试"));
        const auto title = player.m_track.title.trimmed().isEmpty()
                               ? tr("播放失败")
                               : tr("播放失败 · %1").arg(player.m_track.title.trimmed());
        emit player.failureNotice(
            reason + (details.isEmpty() ? QString() : "\n" + details.join("；")), title);
    });
}
} // namespace EchoMusic
