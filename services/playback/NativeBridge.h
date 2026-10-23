#pragma once
#include <QObject>
#include <QJsonValue>
#include <QThread>
#include <QSet>
#include <memory>
struct EchoCapabilities;
namespace EchoMusic
{
struct NativeSink;
/// Closed native capability boundary. UI uses typed feature services; it cannot
/// submit raw operations, request routes, SOAP or capture buffers.
class NativeBridge final : public QObject
{
    Q_OBJECT
public:
    enum Operation : quint32
    {
        AirplayDiscover = 0,
        AirplayConnect = 1,
        AirplayDisconnect = 2,
        AirplayPause = 3,
        AirplayResume = 4,
        AirplaySeek = 5,
        AirplayStop = 6,
        AirplayVolume = 7,
        AirplayFlush = 8,
        AirplayStatus = 9,
        UpnpDevice = 10,
        UpnpAction = 11,
        CaptureDevices = 20,
        CaptureStart = 21,
        CaptureSnapshot = 22,
        CaptureStop = 23,
        CaptureCancel = 24,
        CaptureStatus = 25,
        Opencc = 30,
        AudioTracks = 31,
        InspectDsp = 32,
        EmbeddedMetadata = 33,
        AirplayTap = 40,
        AirplayEpoch = 41,
        AirplayClear = 43
    };
    explicit NativeBridge(QObject *parent = nullptr);
    ~NativeBridge() override;
    quint64 submit(Operation operation, const QJsonValue &data, quint64 revision);
    bool hasPendingWork() const
    {
        return !m_pending.isEmpty();
    }
    /// Stops cancellable audio/UPnP/connection work and discards queued delivery.
    /// Context lives until actor drain; bounded non-cancellable controls may finish.
    bool cancel(quint64 id);
    void shutdown();
signals:
    void completed(quint64 id, quint64 revision, bool success, const QByteArray &data);
    void closed();

private:
    friend struct NativeSink;
    void drain();
    EchoCapabilities *m_native = nullptr;
    std::shared_ptr<NativeSink> m_sink;
    QThread *m_destroy = nullptr;
    bool m_closing = false;
    QSet<quint64> m_pending;
};
} // namespace EchoMusic
