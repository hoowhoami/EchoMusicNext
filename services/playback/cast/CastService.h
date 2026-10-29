#pragma once
#include "NativeBridge.h"
#include "SsdpDiscovery.h"
#include "DlnaSession.h"
#include <QAbstractListModel>
#include <QJsonObject>
#include "SettingsStore.h"
#include <QSortFilterProxyModel>
namespace EchoMusic
{
class PlaybackService;
/// GUI-side output selection. Native protocol work stays on the existing actor;
/// device projections and all views share this bounded model and connection epoch.
class CastService final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool browsing READ browsing NOTIFY changed)
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool remote READ remote NOTIFY changed)
    Q_PROPERTY(bool remoteMedia READ remoteMedia NOTIFY changed)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *dlna READ dlna CONSTANT)
    Q_PROPERTY(QAbstractItemModel *airplay READ airplay CONSTANT)
public:
    enum Role
    {
        Id = Qt::UserRole + 1,
        Protocol,
        Title,
        Detail,
        NeedsPin,
        Available,
        Active,
        Connecting
    };
    CastService(NativeBridge &native, PlaybackService &playback,
                EchoMusic::SettingsStore &settings);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool enabled() const
    {
        return m_enabled;
    }
    bool browsing() const
    {
        return m_browsing;
    }
    int deviceCount() const
    {
        return m_devices.size();
    }
    bool searching() const
    {
        return m_discovery || m_ssdpSearching ||
               (m_browsing && (m_descriptionRequest || !m_descriptionQueue.isEmpty()));
    }
    bool busy() const
    {
        return !m_connecting.isEmpty() || m_stopping;
    }
    bool remote() const
    {
        return !m_active.isEmpty();
    }
    bool remoteMedia() const
    {
        return m_media.active();
    }
    void setNetwork(NetworkPreferences *network)
    {
        m_media.setNetwork(network);
    }
    void loadMedia(const QString &source, const PlaybackTrack &track, quint64 sequence)
    {
        m_error.clear();
        m_media.load(source, track, sequence);
        emit changed();
    }
    void playMedia()
    {
        cancelDiscovery();
        m_media.play();
    }
    void pauseMedia()
    {
        cancelDiscovery();
        m_media.pause();
    }
    void stopMedia()
    {
        cancelDiscovery();
        m_media.stop();
    }
    void prepareTransportCommand()
    {
        cancelDiscovery();
    }
    QString activeTitle() const;
    QString errorMessage() const
    {
        return m_error;
    }
    QAbstractItemModel *dlna()
    {
        return &m_dlna;
    }
    QAbstractItemModel *airplay()
    {
        return &m_airplay;
    }
    void setEnabled(bool enabled);
    /// Views lease discovery; closing one view cannot stop another visible view.
    Q_INVOKABLE void setBrowsingFor(QObject *view, bool browsing);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectTarget(const QString &id, const QString &pin = {});
    Q_INVOKABLE void useLocal();
    Q_INVOKABLE void clearRecords();
    void seek(qint64 positionMs);
    void shutdown(std::function<void()> complete = {});
signals:
    void changed();
    void deviceCountChanged();
    void notice(const QString &message);
    void mediaRouteChanged(bool active);
    void mediaEvent(const QJsonObject &event);
    void mediaSourceExpired();

private:
    friend class CastServiceTestAccess;
    struct Device
    {
        QString id, protocol, title, detail;
        bool pin = false, available = true;
        QJsonObject snapshot;
    };
    struct Pending
    {
        NativeBridge::Operation operation;
        quint64 revision;
        QString id;
    };
    quint64 enqueueNative(NativeBridge::Operation operation,
                          const QJsonValue &value = QJsonObject{}, const QString &id = {});
    void completed(quint64 request, quint64 revision, bool ok, const QByteArray &bytes);
    void upsert(Device device);
    void syncPolicy();
    void cancelDiscovery();
    void pumpDescriptions();
    void syncBrowsing();
    void syncPlayback();
    int find(const QString &id) const;
    NativeBridge &m_native;
    PlaybackService &m_playback;
    EchoMusic::SettingsStore &m_settings;
    DlnaSession m_media;
    QSortFilterProxyModel m_dlna, m_airplay;
    SsdpDiscovery m_ssdp;
    QList<Device> m_devices;
    QHash<quint64, Pending> m_pending;
    QHash<QString, QString> m_descriptions;
    QStringList m_descriptionQueue;
    QString m_descriptionId, m_descriptionLocation;
    quint64 m_descriptionRequest = 0;
    QHash<QObject *, QMetaObject::Connection> m_browseViews;
    QTimer m_scan, m_poll;
    QString m_active, m_connecting, m_error, m_track, m_nextTarget, m_nextPin;
    quint64 m_revision = 1, m_discovery = 0, m_status = 0;
    double m_volume = -1, m_connectVolume = 50;
    bool m_enabled, m_browsing = false, m_stopping = false, m_ssdpSearching = false,
                    m_playing = false, m_closed = false;
};
} // namespace EchoMusic
