#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include "PlaybackSourcePlan.h"
#include "CloudAudioIndex.h"
#include <QObject>
#include <QPointer>
#include "PlaybackSourceExtensions.h"

namespace EchoMusic
{
/// Shared current/preloaded source resolution. Consumes only the Rust domain
/// contract; HTTP identity, privilege-first ordering and effect fallback are
/// identical for both callers. Signed URLs never enter persisted queue state.
class PlaybackSourceRequest final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackSourceRequest(ApiClient &api, MembershipService &membership,
                                   QObject *parent = nullptr, CloudAudioIndex *cloud = nullptr,
                                   PlaybackSourceExtensions *extensions = nullptr);
    ~PlaybackSourceRequest() override
    {
        cancel();
    }
    void resolve(const PlaybackTrack &track, ApiClient::PlaybackQuality quality,
                 ApiClient::SoundEffect effect, const QJsonObject &knownPrivileges = {},
                 bool compatibility = true, bool viperEnabled = true, const QString &roomId = {},
                 const QString &preference = {}, bool normalization = false,
                 bool forceReload = false, bool preloading = false);
    void cancel();
    bool fallbackFromRoom();
    void ensureCatalogRights(const PlaybackTrack &track);
    bool continueAfterFailure();
    bool tryMv(const PlaybackTrack &track);
    bool mvAttempted() const
    {
        return m_mvTried;
    }
    bool hasContext(const PlaybackTrack &track) const
    {
        return m_context && m_track.stableKey() == track.stableKey();
    }
    static bool mediaError(const QJsonObject &event);
    static QStringList mvHashes(const QJsonArray &videos);
    static QStringList usableUrls(const QJsonObject &source);
    void disableMv();
signals:
    void privilegesReady(const EchoMusic::PlaybackTrack &track, const QJsonObject &privileges);
    void resolved(const EchoMusic::PlaybackTrack &track, const QJsonObject &privileges,
                  const QJsonObject &source, bool effectFallback, const QString &resolvedQuality);
    void failed(const QString &message);

private:
    friend class PlaybackSourceRequestTestAccess;
    QVariantMap extensionContext() const;
    void runExtension(const QString &stage, std::function<void()> fallback);
    void afterCatalog();
    void requestSource();
    void startCatalog();
    void startCloud();
    void publishCloud(const QJsonObject &source);
    void finishFailure();
    void nextMvUrl();
    void publish(QJsonObject source, const QString &kind, const QString &quality = {});
    void preparePlan();
    void tryStart();
    void succeed(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    PlaybackSourceExtensions *m_extensions = nullptr;
    QPointer<QObject> m_extensionJob;
    bool m_beforeTried = false, m_afterTried = false, m_finalTried = false;
    bool m_forceReload = false, m_preloading = false;
    QString m_pluginStage;
    CloudAudioIndex *m_cloud = nullptr;
    QString m_catalogHash, m_preference, m_kind;
    bool m_cloudTried = false, m_catalogStarted = false, m_waitingCloud = false,
         m_exhausted = false, m_context = false, m_mvTried = false;
    quint64 m_cloudRequest = 0, m_mvRequest = 0;
    QHash<quint64, int> m_videoRequests;
    QMap<int, QStringList> m_videoResults;
    quint64 m_loudnessRequest = 0;
    bool m_normalize = false;
    QJsonObject m_cloudPending, m_catalogLoudness;
    QStringList m_mvHashes;
    QJsonArray m_mvUrls;
    ApiClient &m_api;
    MembershipService &m_membership;
    bool m_roomResolved = false;
    bool m_active = false, m_waitingPrivileges = false, m_hasPlan = false;
    PlaybackTrack m_track;
    QJsonObject m_privileges;
    ApiClient::PlaybackQuality m_quality = ApiClient::PlaybackQuality::Standard;
    ApiClient::SoundEffect m_effect = ApiClient::SoundEffect::None;
    quint64 m_revision = 0, m_privilegeRequest = 0, m_sourceRequest = 0, m_roomRequest = 0;
    QList<PlaybackSourceCandidate> m_candidates;
    int m_candidateIndex = -1;
    QString m_lastError, m_mvError;
    bool m_compatibility = true, m_viperEnabled = true;
    quint64 m_generation = 0;
};
} // namespace EchoMusic
