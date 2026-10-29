#pragma once
#include "RecognitionCapture.h"
#include "SongCatalogModel.h"
#include "SongCatalogActions.h"
#include "RouteController.h"
#include "SettingsStore.h"
#include <QElapsedTimer>
#include <QTimer>
namespace EchoMusic
{
class RecognitionServiceTestAccess;
class RecognitionService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned recognition service")
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool starting READ starting NOTIFY changed)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(int recordingSeconds READ recordingSeconds NOTIFY changed)
    Q_PROPERTY(EchoMusic::SongCatalogModel *matches READ matches CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackAudioControls *audio READ audio CONSTANT)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    enum State
    {
        Idle,
        Recording,
        Recognizing,
        Success,
        Failed
    };
    Q_ENUM(State)
    RecognitionService(ApiClient &, PlaybackService &, RecognitionCapture &,
                       EchoMusic::SettingsStore &, RouteController &, QObject *parent = nullptr);
    ~RecognitionService() override;
    int state() const
    {
        return m_state;
    }
    bool busy() const
    {
        return m_state == Recording || m_state == Recognizing;
    }
    bool starting() const
    {
        return m_starting;
    }
    QString source() const
    {
        return m_source;
    }
    void setSource(const QString &);
    QString errorMessage() const
    {
        return m_error;
    }
    int recordingSeconds() const
    {
        return m_seconds;
    }
    SongCatalogModel *matches()
    {
        return &m_matches;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    PlaybackAudioControls *audio()
    {
        return m_playback.audioControls();
    }
    Q_INVOKABLE void setAvailable(bool);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void addSong(const QString &id);
    Q_INVOKABLE QVariant confidence(int row) const;
    void shutdown();
signals:
    void changed();

private:
    friend class RecognitionServiceTestAccess;
    void cancel();
    void failCapture(const QString &);
    void finish(quint64, const QByteArray &);
    ApiClient &m_api;
    PlaybackService &m_playback;
    RecognitionCapture &m_capture;
    EchoMusic::SettingsStore &m_settings;
    SongCatalogModel m_matches;
    SongCatalogActions m_actions;
    State m_state = Idle;
    QString m_source, m_error;
    int m_seconds = 0;
    bool m_available = false, m_starting = false, m_closing = false;
    quint64 m_generation = 0, m_request = 0;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    QList<QVariant> m_scores;
};
} // namespace EchoMusic
