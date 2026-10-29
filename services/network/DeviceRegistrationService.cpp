#include "DeviceRegistrationService.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <utility>
namespace EchoMusic
{
DeviceRegistrationService::DeviceRegistrationService(ApiClient &api,
                                                     const EchoMusic::SettingsStore &settings,
                                                     QObject *parent)
    : QObject(parent), m_api(api), m_path(settings.fileName())
{
    const auto device = api.deviceIdentitySnapshot();
    if (!device.dfid.isEmpty() && device.dfid != QStringLiteral("-"))
        m_state = State::Ready;
    connect(&api, &ApiClient::succeeded, this, &DeviceRegistrationService::succeed);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (id != m_request || !m_request || m_closing || m_revision != m_api.sessionRevision())
            return;
        m_request = 0;
        const auto kind = QJsonDocument::fromJson(payload).object().value("kind").toString();
        m_state = State::Error;
        m_error = kind == QStringLiteral("device_registration_unassigned")
                      ? tr("设备服务未返回有效设备身份，请重试")
                      : tr("设备注册失败，请重试");
        emit changed();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        if (m_request && revision != m_revision) {
            m_request = 0;
            m_state = State::Idle;
            m_error.clear();
            emit changed();
        }
    });
}
DeviceRegistrationService::~DeviceRegistrationService()
{
    shutdown();
}
void DeviceRegistrationService::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || m_writer || m_request || m_state != State::Idle)
        return;
    m_registrationDevice = m_api.deviceIdentitySnapshot();
    m_revision = m_api.sessionRevision();
    m_request = m_api.registerDevice();
    m_state = m_request ? State::Registering : State::Error;
    m_error = m_request ? QString() : QStringLiteral("无法注册设备，请重试");
    emit changed();
}
void DeviceRegistrationService::retry()
{
    if (m_closing || m_writer || m_state == State::Ready)
        return;
    if (const auto id = std::exchange(m_request, 0))
        m_api.cancel(id);
    m_state = State::Idle;
    m_error.clear();
    start();
}
void DeviceRegistrationService::succeed(quint64 id, const QByteArray &payload)
{
    if (!m_request || id != m_request || m_closing || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto dfid = QJsonDocument::fromJson(payload).object().value("dfid").toString();
    if (dfid.isEmpty() || dfid == QStringLiteral("-")) {
        m_state = State::Error;
        m_error = QStringLiteral("设备注册结果无效，请重试");
        emit changed();
        return;
    }
    auto device = m_registrationDevice;
    device.dfid = dfid;
    const auto revision = m_revision;
    const auto saved = std::make_shared<bool>(false);
    auto writer = QThread::create([path = m_path, device, saved] {
        EchoMusic::SettingsStore settings(
            path); // Worker-local settings object, never shared across threads.
        *saved = DeviceIdentity::saveRegistration(settings, device);
    });
    m_writer = writer;
    connect(writer, &QThread::finished, this, [this, device, saved, revision] {
        m_writer = nullptr;
        if (m_closing) {
            emit closed();
            return;
        }
        if (revision != m_api.sessionRevision()) {
            m_state = State::Idle;
            m_error.clear();
            emit changed();
            return;
        }
        if (*saved && m_api.setDeviceIdentity(device)) {
            if (m_closing)
                return; // Direct session slots may initiate app shutdown.
            m_state = State::Ready;
            m_error.clear();
        } else {
            m_state = State::Error;
            m_error = QStringLiteral("设备信息保存失败，请重试");
        }
        emit changed();
    });
    // Writer owns its settings/snapshot beyond service destruction; callback is
    // automatically disconnected if the QObject dies. Normal app exit drains it.
    connect(writer, &QThread::finished, writer, &QObject::deleteLater);
    writer->start();
}
void DeviceRegistrationService::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    const auto request = std::exchange(m_request, 0);
    if (request)
        m_api.cancel(request);
    if (!m_writer)
        emit closed(); // Otherwise writer completion closes asynchronously.
}
} // namespace EchoMusic
