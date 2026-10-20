#include "AppContext_p.h"
#include "ApiClient.h"
#include "PluginRuntime.h"
#include "QQmlApplicationEngine"
#include "RouteController.h"
#include "ShareTarget.h"
#include "ShellSettings.h"
#include "StartupController.h"
#include "StorageService.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>
#include <QUrlQuery>

namespace EchoMusic
{
void AppContext::wireSmoke()
{
    auto &shell = *m_shell;
    auto &router = *m_router;
    auto &api = *m_api;
    auto &storage = *m_storage;
    auto &startup = *m_startup;
    auto &engine = *m_engine;
    auto &plugins = *m_plugins;
    finishSmoke = [&] {
        // QML construction can process events before start() imports any package.
        // API readiness alone must not finish an empty plugin catalog smoke.
        if (quitting || !smoke || !smokeApiReady || !pluginStartupReady || plugins.loading() ||
            (useStartup && !startup.entered()))
            return;
        const auto incoming = EchoMusic::parseShareTarget(m_openUrl.toString());
        const bool pluginShare = incoming && incoming->type == EchoMusic::ShareResource::Plugin;
        const auto pageName = m_openUrl.isEmpty() ? QStringLiteral("homePage")
                              : pluginShare       ? QStringLiteral("pluginShareResolvePage")
                                                  : QStringLiteral("shareResolvePage");
        bool pageCreated = false;
        std::function<bool(QQuickItem *)> findPage = [&](QQuickItem *item) {
            if (!item)
                return false;
            if (item->objectName() == pageName)
                return true;
            for (auto *child : item->childItems())
                if (findPage(child))
                    return true;
            return false;
        };
        for (auto *root : engine.rootObjects()) {
            auto *surface = qobject_cast<QQuickWindow *>(root);
            pageCreated = pageCreated || findPage(surface ? surface->contentItem()
                                                          : qobject_cast<QQuickItem *>(root));
        }
        // RouteHost incubates real pages asynchronously. API readiness alone
        // previously let a normal smoke finish before its startup page existed.
        if (!pageCreated)
            return;
        if (!m_openUrl.isEmpty()) {
            if (!router.current() || router.current()->name() !=
                                         (pluginShare ? "plugin-share-resolve" : "share-resolve"))
                exitStatus = 1;
            qInfo("Share smoke: production receiving page created after startup");
        } else {
            if (!router.current() || router.current()->name() != "home")
                exitStatus = 1;
            qInfo("Discovery smoke: production homepage created after startup");
        }
        if (m_openUrl.isEmpty()) {
            QSet<QObject *> visited;
            QList<QObject *> popups;
            QObject *agreement = nullptr;
            QObject *acceptAgreement = nullptr;
            const auto visit = [&](auto &&self, QObject *object) -> void {
                if (!object || visited.contains(object))
                    return;
                visited.insert(object);
                if (object->objectName() == "userAgreementDialog")
                    agreement = object;
                if (object->objectName() == "userAgreementAccept")
                    acceptAgreement = object;
                if (object->inherits("QQuickPopup"))
                    popups.append(object);
                for (auto *child : object->children())
                    self(self, child);
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    for (auto *child : item->childItems())
                        self(self, child);
            };
            for (auto *root : engine.rootObjects())
                visit(visit, root);
            // Fresh smoke preferences exercise the real first-home consent
            // trigger, then audit zero idle popups only after full disposal.
            if (!shell.userAgreementAccepted()) {
                if (!agreement || !agreement->property("opened").toBool() || !acceptAgreement)
                    return;
                if (!QMetaObject::invokeMethod(acceptAgreement, "clicked")) {
                    qCritical("Could not accept first-home terms in smoke");
                    exitStatus = 1;
                    beginShutdown();
                } else {
                    qInfo("First-home terms shown and explicitly accepted in smoke");
                }
                return;
            }
            if (agreement)
                return;
            for (auto *popup : popups)
                qWarning() << "Unexpected idle popup" << popup->objectName()
                           << popup->metaObject()->className();
            qInfo() << "Production homepage idle native Popups:" << popups.size();
            if (!popups.isEmpty())
                exitStatus = 1;
        }
        smokePageProbe.stop();
        for (int row = 0; row < plugins.rowCount(); ++row) {
            const auto state = plugins.data(plugins.index(row), PluginRuntime::StateRole).toInt();
            if (state == PluginRuntime::Failed || state == PluginRuntime::Invalid ||
                state == PluginRuntime::Incompatible)
                exitStatus = 1;
        }
        beginShutdown();
    };
    bind(&plugins, &PluginRuntime::loadingChanged, &m_callbacks, finishSmoke);
    if (smoke) {
        smokePageProbe.setInterval(20);
        bind(&smokePageProbe, &QTimer::timeout, &m_callbacks, finishSmoke);
        smokePageProbe.start();
        // Smoke uses temporary storage and waits for API, device writer, account
        // account catalog, then SQL to drain in their ownership order.
        if (!useStartup)
            storage.initialize(smokeDirectory.path() + QStringLiteral("/music.sqlite"));
        bind(&storage, &EchoMusic::StorageService::initialized, &api, [&] { api.capabilities(); });
        bind(&api, &EchoMusic::ApiClient::succeeded, &m_callbacks, [&] {
            smokeApiReady = true;
            finishSmoke();
        });
        bind(&api, &EchoMusic::ApiClient::failed, &m_callbacks, [&] {
            if (!useStartup) {
                exitStatus = 1;
                beginShutdown();
            }
        });
        bind(&startup, &EchoMusic::StartupController::changed, &m_callbacks, [&] {
            if (useStartup && startup.failed()) {
                qCritical().noquote() << "Startup smoke:" << startup.statusMessage();
                exitStatus = 1;
                beginShutdown();
            }
        });
        QTimer::singleShot(startupSmoke ? 60000 : 10000, &m_callbacks, [&] {
            qCritical("Smoke timed out");
            exitStatus = 1;
            beginShutdown();
        });
    }
}
} // namespace EchoMusic
