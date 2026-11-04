#include "DlnaSession.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <utility>
namespace EchoMusic
{
namespace
{
QString serviceId(const QJsonObject &device, const QString &type)
{
    for (const auto &entry : device.value("services").toArray()) {
        const auto service = entry.toObject();
        if (service.value("service_type").toString().contains(':' + type + ':'))
            return service.value("service_id").toString();
    }
    return {};
}
QString metadata(const PlaybackTrack &track, const QUrl &url, const QString &mime)
{
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement("DIDL-Lite");
    writer.writeDefaultNamespace("urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/");
    writer.writeNamespace("http://purl.org/dc/elements/1.1/", "dc");
    writer.writeNamespace("urn:schemas-upnp-org:metadata-1-0/upnp/", "upnp");
    writer.writeStartElement("item");
    writer.writeAttribute("id", "0");
    writer.writeAttribute("parentID", "0");
    writer.writeAttribute("restricted", "1");
    writer.writeTextElement("dc:title", track.title);
    writer.writeTextElement("upnp:class", "object.item.audioItem.musicTrack");
    if (!track.artist.isEmpty())
        writer.writeTextElement("upnp:artist", track.artist);
    if (!track.album.isEmpty())
        writer.writeTextElement("upnp:album", track.album);
    if (!track.cover.isEmpty())
        writer.writeTextElement("upnp:albumArtURI", track.cover);
    writer.writeStartElement("res");
    writer.writeAttribute("protocolInfo", "http-get:*:" + mime + ":*");
    if (track.durationMs > 0)
        writer.writeAttribute("duration", DlnaSession::formatTime(qint64(track.durationMs)));
    writer.writeCharacters(url.toString(QUrl::FullyEncoded));
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    return xml;
}
} // namespace
DlnaSession::DlnaSession(NativeBridge &native, QObject *parent) : QObject(parent), m_native(native)
{
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &DlnaSession::observe);
    m_volumeTimer.setSingleShot(true);
    m_volumeTimer.setInterval(60);
    connect(&m_volumeTimer, &QTimer::timeout, this, &DlnaSession::flushVolume);
    connect(&native, &NativeBridge::completed, this,
            [this](quint64 id, quint64 revision, bool ok, const QByteArray &bytes) {
                const auto it = m_pending.find(id);
                if (it == m_pending.end())
                    return;
                auto pending = m_pending.take(id);
                if (revision == m_revision && pending.revision == revision)
                    pending.complete(ok, QJsonDocument::fromJson(bytes).object());
            });
}
qint64 DlnaSession::parseTime(const QString &value)
{
    static const QRegularExpression pattern("^(\\d+):(\\d{2}):(\\d{2}(?:\\.\\d+)?)$");
    const auto match = pattern.match(value);
    if (!match.hasMatch())
        return -1;
    bool ok = false;
    const auto hours = match.captured(1).toLongLong(&ok);
    const auto minutes = match.captured(2).toInt();
    const auto seconds = match.captured(3).toDouble();
    if (!ok || hours > 1000000 || minutes >= 60 || seconds >= 60)
        return -1;
    return hours * 3600000 + minutes * 60000 + qint64(seconds * 1000);
}
DlnaSession::~DlnaSession()
{
    m_closed = true;
    m_poll.stop();
    cancelPending();
    releaseResource();
}
void DlnaSession::setNetwork(NetworkPreferences *network)
{
    disconnect(m_rejection);
    m_network = network;
    if (network)
        m_rejection = connect(network, &NetworkPreferences::mediaRequestRejected, this,
                              [this](const QString &token, int status) {
                                  if (!m_closed && m_active && !m_sourceExpired &&
                                      token == m_resource.path().mid(1) &&
                                      (status == 401 || status == 403 || status == 410)) {
                                      m_sourceExpired = true;
                                      cancelPending();
                                      m_loading = true;
                                      emit sourceExpired();
                                  }
                              });
}
QString DlnaSession::formatTime(qint64 value)
{
    const auto seconds = qMax<qint64>(0, value) / 1000;
    return QString("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
bool DlnaSession::naturalEnd(const QString &state, const QString &expectedUri,
                             const QString &reportedUri, qint64 position, qint64 duration,
                             bool pendingCommand)
{
    return !pendingCommand && !expectedUri.isEmpty() && reportedUri == expectedUri &&
           (state == "STOPPED" || state == "NO_MEDIA_PRESENT" || state == "IDLE") && duration > 0 &&
           position >= duration - 2500;
}
void DlnaSession::action(const QString &service, const QString &name, const Arguments &arguments,
                         Completion complete)
{
    if (service.isEmpty()) {
        complete(false, {});
        return;
    }
    QString xml;
    QXmlStreamWriter writer(&xml);
    for (const auto &[argumentName, argumentValue] : arguments)
        writer.writeTextElement(argumentName, argumentValue);
    const auto id = m_native.submit(
        NativeBridge::UpnpAction,
        QJsonObject{
            {"location", m_location}, {"service", service}, {"action", name}, {"args_xml", xml}},
        m_revision);
    if (!id) {
        complete(false, {{"message", tr("投放命令队列繁忙")}});
        return;
    }
    m_pending.insert(id, {m_revision, name, std::move(complete)});
}
void DlnaSession::cancelObservation()
{
    for (const auto id : m_pending.keys())
        if (m_pending.value(id).action == "GetPositionInfo" ||
            m_pending.value(id).action == "GetTransportInfo") {
            m_native.cancel(id);
            m_pending.remove(id);
        }
    m_polling = false;
}
void DlnaSession::cancelPending()
{
    ++m_revision;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        m_native.cancel(it.key());
    m_pending.clear();
    m_polling = false;
    m_volumeTimer.stop();
    m_volumeInFlight = false;
}
void DlnaSession::releaseResource()
{
    if (m_network && !m_resource.isEmpty())
        m_network->releaseMedia(m_resource);
    m_resource = QUrl();
    m_uri.clear();
}
void DlnaSession::connectDevice(const QJsonObject &device)
{
    if (m_closed)
        return;
    cancelPending();
    releaseResource();
    m_desiredVolume = m_confirmedVolume = -1;
    m_location = device.value("description_url").toString();
    m_av = serviceId(device, "AVTransport");
    m_rc = serviceId(device, "RenderingControl");
    m_cm = serviceId(device, "ConnectionManager");
    if (m_av.isEmpty() || m_location.isEmpty()) {
        emit failed(tr("设备没有 AVTransport，不能播放"));
        return;
    }
    const auto readVolume = [this](bool, QJsonObject) {
        action(m_rc, "GetVolume", {{"InstanceID", "0"}, {"Channel", "Master"}},
               [this](bool ok, QJsonObject result) {
                   bool valid = false;
                   const auto value = result.value("CurrentVolume").toString().toInt(&valid);
                   m_confirmedVolume = ok && valid ? qBound(0, value, 100) : -1;
                   m_active = true;
                   m_offline = 0;
                   m_takenOver = false;
                   m_poll.start();
                   emit connected(m_confirmedVolume);
               });
    };
    // Optional capabilities never prevent an otherwise valid AVTransport link.
    action(m_cm, "GetProtocolInfo", {}, readVolume);
}
void DlnaSession::load(const QString &source, const PlaybackTrack &track, quint64 sequence)
{
    if (!m_active || m_closed)
        return;
    cancelPending();
    releaseResource();
    m_sequence = sequence;
    m_position = 0;
    m_duration = qint64(track.durationMs);
    m_loading = true;
    m_sourceExpired = false;
    m_ended = m_seenPlaying = m_takenOver = false;
    const auto generation = m_revision;
    if (!m_network) {
        m_loading = false;
        publish({{"event", "error"}, {"message", tr("投放网络服务不可用")}});
        return;
    }
    QUrl url(source);
    if (url.scheme().isEmpty())
        url = QUrl::fromLocalFile(source);
    const QPointer<DlnaSession> guard(this);
    const QPointer<NetworkPreferences> network(m_network);
    m_network->registerCastMedia(
        url, QHostAddress(QUrl(m_location).host()),
        [guard, network, generation, track, url](QUrl resource, const QString &error) {
            if (!guard || guard->m_closed || generation != guard->m_revision) {
                if (network && !resource.isEmpty())
                    network->releaseMedia(resource);
                return;
            }
            auto *self = guard.data();
            if (!error.isEmpty()) {
                self->m_loading = false;
                self->publish({{"event", "error"}, {"message", error}});
                return;
            }
            self->m_resource = resource;
            self->m_uri = resource.toString(QUrl::FullyEncoded);
            auto mime =
                QMimeDatabase().mimeTypeForFile(url.path(), QMimeDatabase::MatchExtension).name();
            if (mime == "application/octet-stream")
                mime = "audio/*";
            self->action(self->m_av, "SetAVTransportURI",
                         {{"InstanceID", "0"},
                          {"CurrentURI", self->m_uri},
                          {"CurrentURIMetaData", metadata(track, resource, mime)}},
                         [self](bool ok, QJsonObject result) {
                             if (!ok) {
                                 self->m_loading = false;
                                 self->publish({{"event", "error"},
                                                {"message", result.value("message").toString(
                                                                tr("设备无法加载当前音源"))}});
                                 return;
                             }
                             self->action(self->m_av, "SetPlayMode",
                                          {{"InstanceID", "0"}, {"NewPlayMode", "NORMAL"}},
                                          [self](bool, QJsonObject) {
                                              self->m_loading = false;
                                              self->flushVolume();
                                              self->publish({{"event", "file-loaded"},
                                                             {"seq", double(self->m_sequence)}});
                                          });
                         });
        });
}
void DlnaSession::publish(QJsonObject value)
{
    value.insert("track_seq", double(m_sequence));
    emit outputEvent(value);
}
void DlnaSession::setPlaying(bool playing)
{
    m_playing = playing;
    if (playing)
        m_seenPlaying = true;
    publish({{"event", "state-change"},
             {"state", QJsonObject{{"playing", playing}, {"paused", !playing}}}});
}
void DlnaSession::play()
{
    if (!m_active || m_loading || m_closed || m_takenOver || m_uri.isEmpty())
        return;
    cancelObservation();
    m_commandTime.start();
    action(m_av, "Play", {{"InstanceID", "0"}, {"Speed", "1"}},
           [this](bool ok, QJsonObject result) {
               if (ok)
                   setPlaying(true);
               else
                   publish({{"event", "error"},
                            {"message", result.value("message").toString(tr("投放播放失败"))}});
           });
}
void DlnaSession::pause()
{
    if (!m_active || m_closed || m_takenOver)
        return;
    cancelObservation();
    m_commandTime.start();
    action(m_av, "Pause", {{"InstanceID", "0"}}, [this](bool ok, QJsonObject result) {
        if (ok)
            setPlaying(false);
        else
            emit failed(result.value("message").toString(tr("设备不支持暂停")));
    });
}
void DlnaSession::stop()
{
    if (!m_active || m_closed || m_takenOver)
        return;
    cancelPending();
    m_loading = false;
    m_seenPlaying = false;
    m_commandTime.start();
    action(m_av, "Stop", {{"InstanceID", "0"}}, [this](bool, QJsonObject) {
        setPlaying(false);
        releaseResource();
    });
}
void DlnaSession::seek(qint64 position)
{
    if (!m_active || m_closed || m_loading || m_takenOver || m_uri.isEmpty())
        return;
    cancelObservation();
    m_commandTime.start();
    action(m_av, "Seek",
           {{"InstanceID", "0"}, {"Unit", "REL_TIME"}, {"Target", formatTime(position)}},
           [this, position](bool ok, QJsonObject) {
               if (ok) {
                   m_position = position;
                   publish({{"event", "seeked"}, {"time", double(position) / 1000.}});
               } else
                   emit failed(tr("设备不支持按时间定位"));
           });
}
void DlnaSession::volume(double value)
{
    if (!m_active || m_closed || m_takenOver || m_rc.isEmpty())
        return;
    m_desiredVolume = qBound(0, qRound(value), 100);
    // Keep one pending value and one request, rather than queueing a SOAP call
    // for every slider event. Source changes preserve this desired value.
    m_volumeTimer.start();
}
void DlnaSession::flushVolume()
{
    if (!m_active || m_closed || m_loading || m_takenOver || m_desiredVolume < 0 ||
        m_volumeInFlight)
        return;
    const auto requested = m_desiredVolume;
    m_volumeInFlight = true;
    action(
        m_rc, "SetVolume",
        {{"InstanceID", "0"}, {"Channel", "Master"}, {"DesiredVolume", QString::number(requested)}},
        [this, requested](bool ok, QJsonObject) {
            m_volumeInFlight = false;
            if (ok)
                m_confirmedVolume = requested;
            if (!ok)
                emit failed(tr("设备音量调节失败"));
            if (m_desiredVolume == requested) {
                m_desiredVolume = -1;
                if (m_confirmedVolume >= 0)
                    emit volumeSettled(m_confirmedVolume);
            } else
                m_volumeTimer.start();
        });
}
void DlnaSession::observe()
{
    if (!m_active || m_closed || m_loading || m_polling || m_takenOver || m_uri.isEmpty())
        return;
    m_polling = true;
    action(m_av, "GetPositionInfo", {{"InstanceID", "0"}}, [this](bool ok, QJsonObject position) {
        if (!ok) {
            m_polling = false;
            if (++m_offline == 3) {
                setPlaying(false);
                emit failed(tr("投放设备已无法访问"));
            }
            return;
        }
        m_offline = 0;
        const auto uri = position.value("TrackURI").toString();
        if (!uri.isEmpty() && uri != m_uri) {
            m_takenOver = true;
            m_polling = false;
            setPlaying(false);
            emit takenOver();
            return;
        }
        const auto time = parseTime(position.value("RelTime").toString());
        const auto duration = parseTime(position.value("TrackDuration").toString());
        if (duration > 0 && duration != m_duration) {
            m_duration = duration;
            publish({{"event", "duration-change"}, {"duration", double(duration) / 1000.}});
        }
        // Several renderers reset RelTime at EOF; retain the last near-end
        // observation until transport classification, rather than flashing zero.
        if (time >= 0 && !(time == 0 && m_duration > 0 && m_position >= m_duration - 2500)) {
            m_position = time;
            publish({{"event", "time-update"}, {"time", double(time) / 1000.}});
        }
        action(m_av, "GetTransportInfo", {{"InstanceID", "0"}},
               [this, uri](bool valid, QJsonObject state) {
                   m_polling = false;
                   if (!valid)
                       return;
                   const auto transport = state.value("CurrentTransportState").toString();
                   if (!m_ended && m_seenPlaying &&
                       naturalEnd(transport, m_uri, uri, m_position, m_duration,
                                  !m_pending.isEmpty() || (m_commandTime.isValid() &&
                                                           m_commandTime.elapsed() < 2500))) {
                       m_ended = true;
                       setPlaying(false);
                       publish({{"event", "playback-end"}, {"reason", "eof"}});
                   } else if (transport == "PLAYING" && !m_playing)
                       setPlaying(true);
                   else if ((transport == "PAUSED_PLAYBACK" || transport == "STOPPED") && m_playing)
                       setPlaying(false);
               });
    });
}
void DlnaSession::disconnectDevice(std::function<void()> complete)
{
    m_poll.stop();
    cancelPending();
    m_loading = false;
    m_desiredVolume = -1;
    const auto revision = m_revision;
    const auto finish = [this, revision, complete = std::move(complete)](bool, QJsonObject) {
        releaseResource();
        m_active = m_playing = false;
        if (m_network)
            m_network->afterPendingMediaWork(
                [guard = QPointer<DlnaSession>(this), revision, complete] {
                    if (guard && guard->m_revision == revision)
                        complete();
                });
        else
            complete();
    };
    if (m_active && !m_takenOver)
        action(m_av, "Stop", {{"InstanceID", "0"}}, finish);
    else
        finish(true, {});
}
void DlnaSession::shutdown(std::function<void()> complete)
{
    m_closed = true;
    disconnectDevice(std::move(complete));
}
} // namespace EchoMusic
