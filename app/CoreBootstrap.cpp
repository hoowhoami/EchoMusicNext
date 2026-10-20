#include "AppContext_p.h"
#include "AboutService.h"
#include "ApiClient.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "TaskCenterModel.h"
#include "ToastService.h"
#include "WindowController.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>
#include <QUrlQuery>

namespace EchoMusic
{
void AppContext::createNetworkPlayback()
{
    auto &settings = *preferences;
    auto &window = *m_window;
    auto &shellController = *m_shellController;
    m_api = std::make_unique<ApiClient>(smoke ? EchoMusic::DeviceIdentity::temporary()
                                              : EchoMusic::DeviceIdentity::load(settings));
    auto &api = *m_api;
    m_network = std::make_unique<NetworkPreferences>(settings, api);
    auto &network = *m_network;
    if (network.transportAvailable())
        network.installApplicationTransport();
    m_about = std::make_unique<AboutService>(settings, network);
    auto &about = *m_about;
    bind(&about, &EchoMusic::AboutService::changed, &shellController,
         [&, handle = EchoMusic::TaskCenterModel::Handle{},
          previous = EchoMusic::AboutService::Idle]() mutable {
             using Tasks = EchoMusic::TaskCenterModel;
             const auto state = about.downloadState();
             if (state == previous && state == EchoMusic::AboutService::Downloading) {
                 shellController.tasks()->updateProgress(handle, about.downloadPercent());
                 return;
             }
             if (state == previous)
                 return;
             previous = state;
             if (state == EchoMusic::AboutService::Idle) {
                 shellController.tasks()->remove(handle);
                 handle = {};
                 return;
             }
             Tasks::Content content;
             content.name = QObject::tr("更新 EchoMusicNext v%1").arg(about.releaseVersion());
             content.error = about.downloadError();
             content.percent = about.downloadPercent();
             content.actions = {{"view", QObject::tr("查看"), EchoMusic::TaskAction::Ghost, true,
                                 true, [&] { about.check(); }}};
             if (state == EchoMusic::AboutService::Downloading)
                 content.actions.append({"cancel", QObject::tr("取消下载"),
                                         EchoMusic::TaskAction::Ghost, true, false,
                                         [&] { about.cancelDownload(); }});
             if (state == EchoMusic::AboutService::Downloaded)
                 content.actions.append({"install", QObject::tr("安装并重启"),
                                         EchoMusic::TaskAction::Primary, true, true,
                                         [&] { about.install(); }});
             if (state == EchoMusic::AboutService::DownloadFailed)
                 content.actions.append({"retry", QObject::tr("重试"),
                                         EchoMusic::TaskAction::Primary, true, false,
                                         [&] { about.download(); }});
             const auto status = state == EchoMusic::AboutService::DownloadFailed ? Tasks::Error
                                 : state == EchoMusic::AboutService::Downloaded   ? Tasks::Completed
                                                                                  : Tasks::Running;
             if (state == EchoMusic::AboutService::Downloading || !handle.generation)
                 handle =
                     shellController.tasks()->registerTask(about, {"application-update",
                                                                   content,
                                                                   status,
                                                                   10,
                                                                   Tasks::Policy::actionRequired(),
                                                                   {}});
             else if (status == Tasks::Running) {
                 shellController.tasks()->start(handle);
                 shellController.tasks()->update(handle, content);
             } else
                 shellController.tasks()->finish(handle, status, content);
         });
    bind(&about, &EchoMusic::AboutService::notice, &shellController,
         [&](const QString &message, bool failure) {
             shellController.toasts()->show(message, failure ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Info);
         });
    bind(&network, &EchoMusic::NetworkPreferences::applied, &shellController, [&] {
        shellController.toasts()->show(QObject::tr("全局网络设置已应用"),
                                       EchoMusic::ToastService::Success);
    });
    bind(&network, &EchoMusic::NetworkPreferences::notice, &shellController,
         [&](const QString &message) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Warning);
         });
    m_playback = std::make_unique<PlaybackService>(
        api, settings, nullptr,
        (smoke ? smokeDirectory.path()
               : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) +
            QStringLiteral("/playback.sqlite"),
        &network);
    auto &playback = *m_playback;
    playback.setStartupHeld(useStartup);
    window.attachPlayback(playback);
}
} // namespace EchoMusic
