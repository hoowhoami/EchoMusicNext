#include "StartupController.h"
#include "StorageService.h"
#include "SavedAccountModel.h"
#include "ApiClient.h"
#include "DeviceRegistrationService.h"
#include "PlaybackService.h"
#include "NetworkPreferences.h"
#include <utility>
namespace EchoMusic
{
StartupController::StartupController(QObject *parent) : QObject(parent)
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(30000);
    m_finish.setSingleShot(true);
    m_finish.setInterval(200); // EchoMusic's completed-state handoff, never fake progress.
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        const auto hint = m_stage == Stage::Device     ? tr("可继续进入，部分在线功能受限。")
                          : m_stage == Stage::Playback ? tr("可继续进入，播放功能暂不可用。")
                                                       : QString();
        fail(tr("%1超时，请重试").arg(m_message), hint);
    });
    connect(&m_finish, &QTimer::timeout, this, [this] {
        if (m_stage == Stage::Finishing)
            enter();
    });
}
StartupController::StartupController(QString recoveryError, QObject *parent)
    : StartupController(parent)
{
    m_recoveryError = std::move(recoveryError);
}
StartupController::StartupController(StorageService &storage, ApiClient &api,
                                     DeviceRegistrationService &device, PlaybackService &playback,
                                     NetworkPreferences &network, SavedAccountModel &accounts,
                                     QString databasePath, QObject *parent)
    : StartupController(parent)
{
    m_storage = &storage;
    m_accounts = &accounts;
    connect(&accounts, &SavedAccountModel::changed, this, &StartupController::observeAccounts);
    m_api = &api;
    m_device = &device;
    m_playback = &playback;
    m_network = &network;
    m_databasePath = std::move(databasePath);
    connect(&storage, &StorageService::initialized, this, [this] {
        if (m_stage == Stage::Storage)
            beginRuntime();
    });
    connect(&storage, &StorageService::failed, this, [this](const QString &message) {
        if (m_stage == Stage::Storage)
            fail(tr("本地存储初始化失败：%1").arg(message));
    });
    connect(&network, &NetworkPreferences::changed, this, [this] {
        // Transport recovery can synchronously clear a preference error. Queue
        // the observer so it cannot submit a second capability probe reentrantly.
        const auto attempt = m_attempt;
        QTimer::singleShot(0, this, [this, attempt] {
            if (attempt == m_attempt && m_stage == Stage::Runtime && !m_request &&
                !m_network->busy())
                beginRuntime();
        });
    });
    connect(&api, &ApiClient::succeeded, this, [this](quint64 id, const QByteArray &) {
        if (m_stage != Stage::Runtime || !m_request || id != m_request)
            return;
        m_request = 0;
        beginDevice();
    });
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (m_stage != Stage::Runtime || !m_request || id != m_request)
            return;
        m_request = 0;
        fail(ApiClient::errorMessage(payload, tr("基础服务初始化失败，请重试")));
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        if (m_stage != Stage::Runtime || !m_request)
            return;
        cancelRuntimeRequest();
        const auto attempt = m_attempt;
        QTimer::singleShot(0, this, [this, attempt] {
            if (attempt == m_attempt && m_stage == Stage::Runtime && !m_request)
                beginRuntime();
        });
    });
    connect(&device, &DeviceRegistrationService::changed, this, &StartupController::observeDevice);
    connect(&playback, &PlaybackService::engineInitializationChanged, this,
            &StartupController::observePlayback);
}
StartupController::~StartupController()
{
    shutdown();
}
void StartupController::setStage(Stage stage, const QString &message)
{
    m_stage = stage;
    m_message = message;
    m_hint.clear();
    m_allowContinue = false;
    m_deadline.start();
    emit changed();
}
void StartupController::start()
{
    if (m_stage != Stage::Idle)
        return;
    ++m_attempt;
    if (!m_recoveryError.isEmpty()) {
        setStage(Stage::Storage, tr("正在恢复设置..."));
        fail(tr("无法恢复设置备份：%1").arg(m_recoveryError));
        return;
    }
    m_accounts->setStartupHeld(true);
    beginStorage();
}
void StartupController::beginStorage()
{
    setStage(Stage::Storage, tr("正在初始化本地存储..."));
    if (m_storage->state() == StorageService::State::Ready)
        beginRuntime();
    else
        m_storage->initialize(m_databasePath);
}
void StartupController::beginRuntime()
{
    setStage(Stage::Runtime,
             m_network->busy() ? tr("正在恢复网络设置...") : tr("正在初始化音乐引擎..."));
    if (m_network->busy())
        return;
    if (!m_api->initializeRuntime() || !m_network->ensureStartupTransport()) {
        fail(tr("基础服务或网络传输初始化失败，请重试"));
        return;
    }
    m_request = m_api->capabilities();
    if (!m_request)
        fail(tr("无法检查基础服务，请重试"));
}
void StartupController::beginDevice()
{
    setStage(Stage::Device, m_device->state() == DeviceRegistrationService::State::Ready
                                ? tr("正在同步设备信息...")
                                : tr("正在注册设备信息..."));
    if (std::exchange(m_retryDevice, false))
        m_device->retry();
    else
        m_device->start();
    observeDevice();
}
void StartupController::observeDevice()
{
    if (m_stage != Stage::Device)
        return;
    if (m_device->state() == DeviceRegistrationService::State::Ready)
        beginAccounts();
    else if (m_device->state() == DeviceRegistrationService::State::Error)
        fail(m_device->errorMessage(), tr("可继续进入，部分在线功能受限。"));
    else if (m_device->state() == DeviceRegistrationService::State::Idle) {
        const auto attempt = m_attempt;
        QTimer::singleShot(0, this, [this, attempt] {
            if (m_stage == Stage::Device && attempt == m_attempt)
                m_device->start();
        });
    }
}
void StartupController::beginAccounts()
{
    setStage(Stage::Account, tr("正在恢复账号..."));
    m_accounts->setStartupHeld(false);
    observeAccounts();
}
void StartupController::observeAccounts()
{
    if (m_stage == Stage::Account && !m_accounts->busy())
        beginPlayback();
}
void StartupController::beginPlayback()
{
    setStage(Stage::Playback, tr("正在检查播放引擎..."));
    if (std::exchange(m_retryEngine, false) && !m_playback->engineReady())
        m_playback->retryEngineInitialization();
    observePlayback();
}
void StartupController::observePlayback()
{
    if (m_stage != Stage::Playback)
        return;
    if (m_playback->engineReady()) {
        setStage(Stage::Finishing, tr("引擎就绪，正在开启音乐世界..."));
        m_deadline.stop();
        m_finish.start();
    } else if (!m_playback->engineInitializationError().isEmpty())
        fail(tr("播放引擎初始化失败"), tr("可继续进入，播放功能暂不可用。"));
}
void StartupController::fail(const QString &message, const QString &hint)
{
    if (m_stage == Stage::Entered || m_stage == Stage::Closing || m_stage == Stage::Failed)
        return;
    m_failedStage = m_stage;
    m_stage = Stage::Failed;
    m_deadline.stop();
    m_finish.stop();
    cancelRuntimeRequest();
    m_message = message;
    m_hint = hint;
    m_allowContinue = !hint.isEmpty();
    emit changed();
}
void StartupController::cancelRuntimeRequest()
{
    if (const auto id = std::exchange(m_request, 0); id && m_api)
        m_api->cancel(id);
}
void StartupController::retry()
{
    if (!failed())
        return;
    ++m_attempt;
    if (!m_recoveryError.isEmpty()) {
        setStage(Stage::Storage, tr("正在重新启动..."));
        emit restartApplicationRequested();
        return;
    }
    m_retryDevice = m_failedStage == Stage::Device ||
                    m_device->state() == DeviceRegistrationService::State::Error;
    m_retryEngine =
        m_failedStage == Stage::Playback || !m_playback->engineInitializationError().isEmpty();
    beginStorage();
}
void StartupController::enter()
{
    if (entered() || closing())
        return;
    ++m_attempt;
    m_deadline.stop();
    m_finish.stop();
    cancelRuntimeRequest();
    // Explicit limited entry also releases the existing account restore queue.
    if (m_accounts)
        m_accounts->setStartupHeld(false);
    m_entered = true;
    m_stage = Stage::Entered;
    m_allowContinue = false;
    m_hint.clear();
    emit changed();
    emit enteredMain();
}
void StartupController::forceEnter()
{
    if (canForceEnter())
        enter();
}
void StartupController::requestQuit()
{
    if (!closing())
        emit quitRequested();
}
void StartupController::reportRestartFailure(const QString &message)
{
    if (!m_recoveryError.isEmpty() && m_stage == Stage::Storage)
        fail(message);
}
void StartupController::shutdown()
{
    if (closing())
        return;
    ++m_attempt;
    m_stage = Stage::Closing;
    m_message = tr("正在退出...");
    m_hint.clear();
    m_deadline.stop();
    m_finish.stop();
    cancelRuntimeRequest();
    emit changed();
}
} // namespace EchoMusic
