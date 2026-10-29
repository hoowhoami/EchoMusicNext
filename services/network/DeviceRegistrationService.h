#pragma once
#include <QObject>
#include "SettingsStore.h"
#include <QThread>
#include "ApiClient.h"
namespace EchoMusic
{
/// Registers the persistent device without account credentials; saves its DFID/GUID snapshot off
/// the GUI thread before adopting it. ApiClient must outlive this service.
class DeviceRegistrationService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    enum class State
    {
        Idle,
        Registering,
        Ready,
        Error
    };
    Q_ENUM(State)
    explicit DeviceRegistrationService(ApiClient &api, const EchoMusic::SettingsStore &settings,
                                       QObject *parent = nullptr);
    ~DeviceRegistrationService() override;
    State state() const
    {
        return m_state;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    Q_INVOKABLE void start();
    Q_INVOKABLE void retry();
    void shutdown();
signals:
    void changed();
    void closed();

private:
    friend class DeviceRegistrationServiceTestAccess;
    void succeed(quint64 id, const QByteArray &payload);
    ApiClient &m_api;
    QString m_path, m_error;
    State m_state = State::Idle;
    quint64 m_request = 0, m_revision = 0;
    QThread *m_writer = nullptr;
    DeviceIdentity m_registrationDevice;
    bool m_closing = false;
};
} // namespace EchoMusic
