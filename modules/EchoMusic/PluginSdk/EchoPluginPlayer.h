#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "EchoPluginAudioSource.h"
#include <QJSValue>
#include <QPointer>
#include <QAbstractListModel>
#include <QVariantMap>
#include <QUrl>
namespace EchoMusic
{
class EchoPluginContext;
class PlaybackService;
/// Read-only projections forward incremental model notifications; plugin code
/// cannot invoke mutation slots on the host's queue/lyric models.
class EchoPluginReadModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginPlayer")
public:
    EchoPluginReadModel(QAbstractItemModel &source, QObject *parent);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap row(int index) const;

private:
    QAbstractItemModel &m_source;
};
class EchoPluginPlayer final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(EchoMusic::EchoPluginAudioSource *audioSource READ audioSource CONSTANT)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(QVariantMap lyricsInfo READ lyricsInfo NOTIFY lyricChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QUrl coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(double volume READ volume NOTIFY changed)
    Q_PROPERTY(double speed READ speed NOTIFY changed)
    Q_PROPERTY(EchoMusic::EchoPluginReadModel *queue READ queue CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginReadModel *lyrics READ lyrics CONSTANT)
public:
    EchoPluginPlayer(EchoPluginContext &scope, PlaybackService &host);
    EchoPluginAudioSource *audioSource()
    {
        return &m_audioSource;
    }
    QVariantMap snapshot() const;
    QVariantMap clock() const;
    Q_INVOKABLE double samplePosition() const;
    QVariantMap lyricsInfo() const;
    QString revision() const;
    Q_INVOKABLE QVariantMap lyricSnapshot() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *control(const QString &action,
                                                        const QVariantMap &arguments = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    on(const QString &id, const QString &event, const QJSValue &callback, bool immediate = false);
    QString title() const;
    QString artist() const;
    QString album() const;
    QUrl coverUrl() const;
    bool loading() const;
    bool hasTrack() const;
    QString errorMessage() const;
    bool playing() const;
    qint64 positionMs() const;
    qint64 durationMs() const;
    double volume() const;
    double speed() const;
    EchoPluginReadModel *queue()
    {
        return &m_queue;
    }
    EchoPluginReadModel *lyrics()
    {
        return &m_lyrics;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *play();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *pause();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *toggle();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *stop();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *previous();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *next();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *seek(qint64 positionMs);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *setVolume(double volume);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *setSpeed(double speed);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *playIndex(int row);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *remove(int row);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *move(int from, int to);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *clear();
signals:
    void lyricChanged();
    void eventOccurred(const QString &event, const QVariantMap &payload);
    void changed();
    void positionChanged();
    void clockChanged();
    void snapshotChanged();

private:
    EchoPluginOperation *command(std::function<void()> invoke);
    void publishStateEvents();
    QVariantMap m_previous;
    QPointer<QObject> m_library;
    QMetaObject::Connection m_libraryConnection;
    EchoPluginContext &m_scope;
    PlaybackService &m_host;
    EchoPluginAudioSource m_audioSource;
    EchoPluginReadModel m_queue, m_lyrics;
};
} // namespace EchoMusic
