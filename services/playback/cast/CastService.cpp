#include "SettingsKeys.h"
#include "CastService.h"
#include "PlaybackService.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <utility>
namespace EchoMusic
{
CastService::CastService(NativeBridge &native, PlaybackService &playback,
                         EchoMusic::SettingsStore &settings)
    : m_native(native), m_playback(playback), m_settings(settings), m_media(native, this),
      m_enabled(settings.value(EchoMusic::SettingsKeys::Cast::Enabled, false).toBool())
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &CastService::deviceCountChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &CastService::deviceCountChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &CastService::deviceCountChanged);
    connect(&m_media, &DlnaSession::outputEvent, this, &CastService::mediaEvent);
    connect(&m_media, &DlnaSession::sourceExpired, this, &CastService::mediaSourceExpired);
    connect(&m_media, &DlnaSession::volumeSettled, this, [this](int volume) {
        if (!remoteMedia() || m_active.isEmpty() || m_closed)
            return;
        m_volume = volume;
        m_playback.applyOutputVolume(volume);
        m_settings.setValue(
            EchoMusic::SettingsKeys::Cast::VolumesPrefix +
                QString::fromLatin1(m_active.toUtf8().toBase64(QByteArray::Base64UrlEncoding)),
            volume);
    });
    connect(&m_media, &DlnaSession::connected, this, [this](int volume) {
        if (m_closed)
            return;
        m_active = std::exchange(m_connecting, {});
        if (m_active.isEmpty())
            return;
        const auto key =
            EchoMusic::SettingsKeys::Cast::VolumesPrefix +
            QString::fromLatin1(m_active.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
        if (!m_settings.contains(key) && volume >= 0)
            m_connectVolume = volume;
        m_volume = m_connectVolume;
        m_playback.applyOutputVolume(m_volume);
        emit mediaRouteChanged(true);
        if (volume != qRound(m_connectVolume))
            m_media.volume(m_connectVolume);
        if (!m_devices.isEmpty())
            emit dataChanged(index(0), index(m_devices.size() - 1));
        emit changed();
    });
    connect(&m_media, &DlnaSession::failed, this, [this](const QString &message) {
        m_error = message;
        if (!m_media.active())
            m_connecting.clear();
        emit changed();
    });
    connect(&m_media, &DlnaSession::takenOver, this, [this] {
        m_error = tr("设备已被其他控制端接管，已停止自动推进");
        emit changed();
    });
    for (auto pair :
         {std::pair{&m_dlna, QString("dlna")}, std::pair{&m_airplay, QString("airplay")}}) {
        pair.first->setSourceModel(this);
        pair.first->setFilterRole(Protocol);
        pair.first->setFilterFixedString(pair.second);
    }
    connect(&native, &NativeBridge::completed, this, &CastService::completed);
    connect(&native, &NativeBridge::completed, this,
            [this] { QTimer::singleShot(0, this, &CastService::pumpDescriptions); });
    connect(&m_ssdp, &SsdpDiscovery::searchStarted, this, [this] {
        m_ssdpSearching = true;
        emit changed();
    });
    connect(&m_ssdp, &SsdpDiscovery::searchFinished, this, [this] {
        m_ssdpSearching = false;
        emit changed();
    });
    connect(&m_ssdp, &SsdpDiscovery::errorOccurred, this, [this](const QString &error) {
        m_error = error;
        emit changed();
    });
    connect(&m_ssdp, &SsdpDiscovery::candidateChanged, this,
            [this](const SsdpCandidate &candidate) {
                if (!m_enabled || !m_browsing ||
                    (m_descriptions.size() >= 64 && !m_descriptions.contains(candidate.identity)))
                    return;
                m_descriptions.insert(candidate.identity, candidate.location);
                if (!m_descriptionQueue.contains(candidate.identity) &&
                    (m_descriptionId != candidate.identity ||
                     m_descriptionLocation != candidate.location))
                    m_descriptionQueue.append(candidate.identity);
                emit changed();
                pumpDescriptions();
            });
    connect(&m_ssdp, &SsdpDiscovery::candidateRemoved, this, [this](const QString &id) {
        m_descriptions.remove(id);
        m_descriptionQueue.removeAll(id);
        emit changed();
        const auto row = find(id);
        if (row < 0)
            return;
        if (m_devices[row].id == m_active) {
            m_devices[row].available = false;
            m_devices[row].detail = tr("设备已离线");
            m_error = tr("投放设备已离线");
            if (remoteMedia())
                emit mediaEvent({{"event", "state-change"},
                                 {"state", QJsonObject{{"playing", false}, {"paused", true}}}});
            emit dataChanged(index(row), index(row));
            emit changed();
            return;
        }
        beginRemoveRows({}, row, row);
        m_devices.removeAt(row);
        endRemoveRows();
    });
    m_scan.setInterval(15000);
    connect(&m_scan, &QTimer::timeout, this, &CastService::refresh);
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!m_status && remote())
            m_status = enqueueNative(NativeBridge::AirplayStatus);
    });
    connect(&playback, &PlaybackService::changed, this, &CastService::syncPlayback);
    connect(&playback, &PlaybackService::timelineReset, this, [this] {
        if (remote() && !remoteMedia() && m_track != m_playback.currentTrack().key()) {
            m_track = m_playback.currentTrack().key();
            enqueueNative(NativeBridge::AirplayFlush);
        }
    });
}
int CastService::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}
int CastService::find(const QString &id) const
{
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices[i].id == id)
            return i;
    return -1;
}
QString CastService::activeTitle() const
{
    const auto row = find(m_active);
    return row >= 0 ? m_devices[row].title : tr("本机播放");
}
QVariant CastService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
        return {};
    const auto &device = m_devices[index.row()];
    switch (role) {
    case Id:
        return device.id;
    case Protocol:
        return device.protocol;
    case Title:
        return device.title;
    case Detail:
        return device.detail;
    case NeedsPin:
        return device.pin;
    case Available:
        return device.available;
    case Active:
        return device.id == m_active;
    case Connecting:
        return device.id == m_connecting;
    default:
        return {};
    }
}
QHash<int, QByteArray> CastService::roleNames() const
{
    return {{Id, "targetId"},         {Protocol, "protocol"},
            {Title, "deviceTitle"},   {Detail, "deviceDetail"},
            {NeedsPin, "needsPin"},   {Available, "deviceAvailable"},
            {Active, "deviceActive"}, {Connecting, "deviceConnecting"}};
}
void CastService::upsert(Device device)
{
    const int row = find(device.id);
    if (row >= 0) {
        m_devices[row] = std::move(device);
        emit dataChanged(index(row), index(row));
    } else if (m_devices.size() < 128) {
        beginInsertRows({}, m_devices.size(), m_devices.size());
        m_devices.append(std::move(device));
        endInsertRows();
    }
}
quint64 CastService::enqueueNative(NativeBridge::Operation operation, const QJsonValue &value,
                                   const QString &id)
{
    if (m_closed)
        return 0;
    const auto request = m_native.submit(operation, value, m_revision);
    if (request)
        m_pending.insert(request, {operation, m_revision, id});
    else {
        m_error = tr("投放任务队列繁忙，请稍后重试");
        emit changed();
    }
    return request;
}
void CastService::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Cast::Enabled, enabled);
    if (!enabled)
        useLocal();
    syncPolicy();
    emit changed();
}
void CastService::syncBrowsing()
{
    const bool active = !m_browseViews.isEmpty();
    if (m_browsing == active)
        return;
    m_browsing = active;
    syncPolicy();
    emit changed();
}
void CastService::setBrowsingFor(QObject *view, bool browsing)
{
    if (!view || m_closed)
        return;
    if (browsing && !m_browseViews.contains(view))
        m_browseViews.insert(view, connect(view, &QObject::destroyed, this, [this, view] {
                                 m_browseViews.remove(view);
                                 syncBrowsing();
                             }));
    else if (!browsing)
        disconnect(m_browseViews.take(view));
    syncBrowsing();
}
void CastService::syncPolicy()
{
    if (m_enabled && m_browsing && !m_closed) {
        m_ssdp.start();
        m_scan.start();
        refresh();
    } else {
        m_ssdp.stop();
        m_scan.stop();
        m_ssdpSearching = false;
        cancelDiscovery();
    }
}
void CastService::cancelDiscovery()
{
    for (const auto request : m_pending.keys()) {
        const auto operation = m_pending.value(request).operation;
        if (operation == NativeBridge::UpnpDevice || operation == NativeBridge::AirplayDiscover) {
            m_native.cancel(request);
            m_pending.remove(request);
        }
    }
    if (!m_descriptionId.isEmpty() && m_descriptions.contains(m_descriptionId) &&
        !m_descriptionQueue.contains(m_descriptionId))
        m_descriptionQueue.prepend(m_descriptionId);
    m_descriptionRequest = m_discovery = 0;
    m_descriptionId.clear();
    m_descriptionLocation.clear();
}
void CastService::pumpDescriptions()
{
    if (m_closed || !m_enabled || !m_browsing || busy() || m_playback.loading() ||
        m_descriptionRequest || m_native.hasPendingWork() || m_descriptionQueue.isEmpty())
        return;
    m_descriptionId = m_descriptionQueue.takeFirst();
    m_descriptionLocation = m_descriptions.value(m_descriptionId);
    if (m_descriptionLocation.isEmpty()) {
        m_descriptionId.clear();
        return;
    }
    m_descriptionRequest =
        enqueueNative(NativeBridge::UpnpDevice, m_descriptionLocation, m_descriptionId);
    if (!m_descriptionRequest) {
        m_descriptionQueue.prepend(m_descriptionId);
        m_descriptionId.clear();
        m_descriptionLocation.clear();
    }
    emit changed();
}
void CastService::refresh()
{
    if (!m_enabled || m_closed || m_discovery || busy())
        return;
    m_error.clear();
    m_discovery = enqueueNative(NativeBridge::AirplayDiscover, 1500);
    m_ssdp.refresh();
    emit changed();
}
void CastService::connectTarget(const QString &id, const QString &pin)
{
    const auto row = find(id);
    if (!m_enabled || busy() || row < 0 || !m_devices[row].available)
        return;
    if (id == m_active)
        return;
    cancelDiscovery();
    if (remote()) {
        m_nextTarget = id;
        m_nextPin = pin;
        useLocal();
        return;
    }
    ++m_revision;
    m_connecting = id;
    m_error.clear();
    const double volume =
        m_settings
            .value(EchoMusic::SettingsKeys::Cast::VolumesPrefix +
                       QString::fromLatin1(id.toUtf8().toBase64(QByteArray::Base64UrlEncoding)),
                   m_playback.volume())
            .toDouble();
    m_connectVolume = qBound(0., volume, 100.);
    if (m_devices[row].protocol == "dlna") {
        m_media.connectDevice(m_devices[row].snapshot);
        emit dataChanged(index(0), index(m_devices.size() - 1));
        emit changed();
        return;
    }
    if (!enqueueNative(NativeBridge::AirplayConnect,
                       QJsonObject{{"id", id}, {"pin", pin}, {"volume", qBound(0., volume, 100.)}},
                       id))
        m_connecting.clear();
    emit dataChanged(index(0), index(m_devices.size() - 1));
    emit changed();
}
void CastService::useLocal()
{
    if (m_closed || (!remote() && m_connecting.isEmpty()))
        return;
    ++m_revision;
    cancelDiscovery();
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it->operation == NativeBridge::AirplayConnect)
            m_native.cancel(it.key());
    const auto targetRow = find(m_connecting.isEmpty() ? m_active : m_connecting);
    const bool dlna =
        m_media.active() || (targetRow >= 0 && m_devices[targetRow].protocol == "dlna");
    const bool wasRemoteMedia = m_media.active();
    m_connecting.clear();
    m_stopping = true;
    m_poll.stop();
    if (dlna) {
        m_media.disconnectDevice([this, wasRemoteMedia] {
            m_active.clear();
            m_stopping = false;
            m_playback.restoreLocalVolume();
            if (wasRemoteMedia)
                emit mediaRouteChanged(false);
            if (!m_devices.isEmpty())
                emit dataChanged(index(0), index(m_devices.size() - 1));
            emit changed();
            if (!m_nextTarget.isEmpty()) {
                const auto target = std::exchange(m_nextTarget, {});
                const auto pin = std::exchange(m_nextPin, {});
                connectTarget(target, pin);
            }
        });
        return;
    }
    enqueueNative(NativeBridge::AirplayTap, QJsonObject{{"port", 0}, {"enabled", false}});
    enqueueNative(NativeBridge::AirplayDisconnect);
    emit changed();
}
void CastService::clearRecords()
{
    if (busy())
        return;
    m_settings.remove(EchoMusic::SettingsKeys::Cast::Volumes);
    if (remote())
        useLocal();
    enqueueNative(NativeBridge::AirplayClear);
}
void CastService::syncPlayback()
{
    if (!remote() || busy())
        return;
    if (m_volume != m_playback.volume()) {
        cancelDiscovery();
        m_volume = m_playback.volume();
        if (remoteMedia())
            m_media.volume(m_volume);
        else
            enqueueNative(NativeBridge::AirplayVolume, qBound(0., m_volume, 100.));
        if (!remoteMedia())
            m_settings.setValue(
                EchoMusic::SettingsKeys::Cast::VolumesPrefix +
                    QString::fromLatin1(m_active.toUtf8().toBase64(QByteArray::Base64UrlEncoding)),
                qBound(0., m_volume, 100.));
    }
    if (!remoteMedia() && m_playing != m_playback.playing()) {
        m_playing = m_playback.playing();
        enqueueNative(m_playing ? NativeBridge::AirplayResume : NativeBridge::AirplayPause);
    }
}
void CastService::seek(qint64 positionMs)
{
    cancelDiscovery();
    if (remoteMedia()) {
        m_media.seek(positionMs);
        return;
    }
    if (remote() && !busy())
        enqueueNative(NativeBridge::AirplaySeek, double(positionMs) / 1000.);
}
void CastService::completed(quint64 request, quint64 revision, bool ok, const QByteArray &bytes)
{
    auto it = m_pending.find(request);
    if (it == m_pending.end())
        return;
    const auto pending = it.value();
    m_pending.erase(it);
    QString description;
    if (request == m_descriptionRequest) {
        m_descriptionRequest = 0;
        m_descriptionId.clear();
        description = std::exchange(m_descriptionLocation, {});
        emit changed();
    }
    if (request == m_discovery)
        m_discovery = 0;
    if (request == m_status)
        m_status = 0;
    if (m_closed || revision != m_revision || pending.revision != revision)
        return;
    if (pending.operation == NativeBridge::UpnpDevice &&
        m_descriptions.value(pending.id) != description)
        return;
    const auto document = QJsonDocument::fromJson(bytes);
    const auto value = document.object();
    const auto operation = pending.operation;
    if (!ok) {
        m_error = value.value("message").toString(tr("投放操作失败"));
        if (operation == NativeBridge::AirplayConnect ||
            (operation == NativeBridge::AirplayTap && !pending.id.isEmpty()))
            useLocal();
        if (operation == NativeBridge::AirplayDisconnect) {
            m_stopping = false;
            m_active.clear();
            m_playback.restoreLocalVolume();
            m_nextTarget.clear();
            m_nextPin.clear();
        }
        emit changed();
        return;
    }
    if (operation == NativeBridge::AirplayDiscover) {
        QSet<QString> found;
        for (const auto &entry : document.array()) {
            const auto device = entry.toObject();
            const auto id = device.value("id").toString();
            if (id.isEmpty() || id.size() > 4096)
                continue;
            found.insert(id);
            upsert({id,
                    "airplay",
                    device.value("name").toString().simplified().left(256),
                    device.value("model").toString().simplified().left(256),
                    device.value("needs_pin").toBool(),
                    device.value("supports_raop").toBool() ||
                        device.value("supports_airplay2").toBool(),
                    {}});
        }
        for (int i = m_devices.size() - 1; i >= 0; --i)
            if (m_devices[i].protocol == "airplay" && !found.contains(m_devices[i].id) &&
                m_devices[i].id != m_active) {
                beginRemoveRows({}, i, i);
                m_devices.removeAt(i);
                endRemoveRows();
            }
    } else if (operation == NativeBridge::UpnpDevice) {
        bool transport = false;
        for (const auto &service : value.value("services").toArray())
            transport |=
                service.toObject().value("service_type").toString().contains(":AVTransport:");
        if (transport) {
            QJsonArray endpoints;
            QSet<QString> retained;
            for (const auto &entry : value.value("services").toArray()) {
                const auto endpoint = entry.toObject();
                const auto type = endpoint.value("service_type").toString();
                const auto id = endpoint.value("service_id").toString();
                for (const auto &family : {QString("AVTransport"), QString("RenderingControl"),
                                           QString("ConnectionManager")})
                    if (!retained.contains(family) && id.size() <= 1024 && type.size() <= 1024 &&
                        type.contains(':' + family + ':')) {
                        endpoints.append(QJsonObject{{"service_type", type}, {"service_id", id}});
                        retained.insert(family);
                    }
            }
            upsert({pending.id, "dlna",
                    value.value("friendly_name").toString().simplified().left(256),
                    QStringList{value.value("manufacturer").toString().simplified().left(128),
                                value.value("model_name").toString().simplified().left(128),
                                QUrl(value.value("description_url").toString()).host().left(256)}
                        .filter(QRegularExpression(".+"))
                        .join(" · "),
                    false, true,
                    QJsonObject{{"description_url", value.value("description_url")},
                                {"services", endpoints}}});
        }
    } else if (operation == NativeBridge::AirplayConnect) {
        const int port = value.value("pcm_port").toInt();
        if (!value.value("ok").toBool() || port < 1 || port > 65535) {
            m_error = value.value("error").toString(tr("无法连接 AirPlay 音频出口"));
            useLocal();
        } else
            enqueueNative(NativeBridge::AirplayTap, QJsonObject{{"port", port}, {"enabled", true}},
                          pending.id);
    } else if (operation == NativeBridge::AirplayTap && !pending.id.isEmpty()) {
        m_active = pending.id;
        m_connecting.clear();
        m_track = m_playback.currentTrack().key();
        m_volume = m_connectVolume;
        m_playback.applyOutputVolume(m_volume);
        m_playing = !m_playback.playing();
        syncPlayback();
        m_poll.start();
    } else if (operation == NativeBridge::AirplayDisconnect) {
        m_active.clear();
        m_playback.restoreLocalVolume();
        m_stopping = false;
        if (!m_nextTarget.isEmpty()) {
            const auto target = std::exchange(m_nextTarget, {});
            const auto pin = std::exchange(m_nextPin, {});
            connectTarget(target, pin);
        }
    } else if (operation == NativeBridge::AirplayFlush || operation == NativeBridge::AirplaySeek) {
        enqueueNative(NativeBridge::AirplayEpoch, value.value("epoch"));
    } else if (operation == NativeBridge::AirplayStatus && !value.value("connected").toBool()) {
        m_error = value.value("error").toString(tr("投放连接已断开"));
        useLocal();
    } else if (operation == NativeBridge::AirplayClear)
        emit notice(tr("已清除设备音量和 AirPlay 配对记录"));
    if (!m_devices.isEmpty())
        emit dataChanged(index(0), index(m_devices.size() - 1));
    emit changed();
}
void CastService::shutdown(std::function<void()> complete)
{
    if (m_closed) {
        if (complete)
            complete();
        return;
    }
    m_closed = true;
    for (const auto &connection : std::as_const(m_browseViews))
        disconnect(connection);
    m_browseViews.clear();
    m_browsing = false;
    m_nextPin.clear();
    m_scan.stop();
    m_poll.stop();
    m_ssdp.stop();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        m_native.cancel(it.key());
    m_pending.clear();
    m_media.shutdown(complete ? std::move(complete) : [] {});
    // NativeBridge's actor teardown disables the PCM tap, then joins AirPlay.
}
} // namespace EchoMusic
