#pragma once
#include "LyricModel.h"
#include "LyricSourceController.h"
#include "LyricTextController.h"
#include "PlaybackTrack.h"
#include "SettingsStore.h"
#include <QJsonObject>
#include <QUrl>
#include <memory>

namespace EchoMusic
{
/// One retained, host-parsed lyric presentation. Not a transport or clock.
class PlaybackTransitionTrack final : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackTransitionTrack(QObject *parent) : QObject(parent), lyrics(this) {}
    PlaybackTrack track;
    QUrl cover;
    QString sequence, lyricState = "empty", lyricError;
    quint64 lyricRevision = 0;
    LyricModel lyrics;
    void clear();
    QVariantMap lyricsInfo() const;
signals:
    void changed();
};

/// GUI-owned A/B snapshots, authenticated by the real native request/sequence.
/// A single native output position supplies both coordinates; no second player.
class PlaybackTransitionPresentation final : public QObject
{
    Q_OBJECT
public:
    PlaybackTransitionPresentation(ApiClient &, NativeBridge &, SettingsStore &, QObject *parent);
    ~PlaybackTransitionPresentation() override;
    void prepare(const PlaybackTrack &, quint64 request, quint64 incomingSequence);
    void invalidate(const QString &reason = "cancelled");
    void observe(const QJsonObject &native, quint64 currentSequence, const PlaybackTrack &current,
                 const QUrl &cover, const LyricModel &lyrics, const QString &lyricState,
                 const QString &lyricError);
    void updateCurrent(quint64 sequence, const PlaybackTrack &, const QUrl &cover,
                       const LyricModel &, const QString &state, const QString &error,
                       bool lyricsChanged);
    bool valid() const
    {
        return m_valid;
    }
    const QJsonObject &snapshot() const
    {
        return m_native;
    }
    const QString &reason() const
    {
        return m_reason;
    }
    PlaybackTransitionTrack &outgoing()
    {
        return m_outgoing;
    }
    PlaybackTransitionTrack &incoming()
    {
        return m_incoming;
    }
    bool permitsCover(const QUrl &) const;
signals:
    void changed();
    void positionChanged();

private:
    void startIncoming();
    void cancelIncoming();
    ApiClient &m_api;
    NativeBridge &m_nativeBridge;
    SettingsStore &m_settings;
    PlaybackTransitionTrack m_outgoing, m_incoming;
    std::unique_ptr<LyricSourceController> m_lyricSource;
    std::unique_ptr<LyricTextController> m_lyricText;
    PlaybackTrack m_target;
    QString m_request, m_incomingSequence, m_account;
    QJsonObject m_native;
    QString m_reason;
    quint64 m_lastEpoch = 0;
    bool m_valid = false, m_copying = false;
};
} // namespace EchoMusic
