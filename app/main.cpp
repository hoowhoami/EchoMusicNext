#include "ApplicationTranslations.h"
#include "Application.h"
#include "NetworkBootstrap.h"
#include "ApplicationUrlHandler.h"
#include "PluginSdkContract.h"
#include "EchoGpuSurface.h"
#include <QJsonDocument>
#include <QTextStream>
#include "LogService.h"
#include "CrashDiagnostics.h"
#include "SingleInstance.h"
#include "RelaunchService.h"
#include "WindowsAppIdentity.h"
#include <QCommandLineParser>
#include <QCommandLineOption>
#include "UiTypography.h"
#include <QApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QMetaObject>
#include <QStandardPaths>
#include <QFileInfo>
#include <QIcon>

namespace EchoMusic
{

// Commits a pending spin box draft when a mouse press lands outside the text
// field: steppers, other controls and empty space all count as leaving the
// field. The focused item (or its spin box container) exposes the QML helper
// commitDraft(); presses inside the field keep normal caret editing.
//
// GUI thread only: it is installed on the application object and therefore
// sees every press before delivery, but it only reads the focused item and
// calls into QML, so it must stay on the thread that owns the window.
class SpinBoxCommitFilter : public QObject
{
public:
    explicit SpinBoxCommitFilter(QObject *parent) : QObject(parent)
    {
        if (auto *instance = QCoreApplication::instance())
            instance->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress)
            return QObject::eventFilter(watched, event);
        auto *window = qobject_cast<QQuickWindow *>(watched);
        QQuickItem *focus = window ? window->activeFocusItem() : nullptr;
        if (!focus)
            return QObject::eventFilter(watched, event);
        const auto ownsHelper = [](QQuickItem *item) {
            return item && item->metaObject() &&
                   item->metaObject()->indexOfMethod("commitDraft()") >= 0;
        };
        QQuickItem *target =
            ownsHelper(focus) ? focus
                              : (ownsHelper(focus->parentItem()) ? focus->parentItem() : nullptr);
        if (!target)
            return QObject::eventFilter(watched, event);
        // Six pixels of slack keep an edge click inside the field editing.
        const QPointF local =
            focus->mapFromScene(static_cast<const QMouseEvent *>(event)->position());
        if (!QRectF(-6, -6, focus->width() + 12, focus->height() + 12).contains(local))
            QMetaObject::invokeMethod(target, "commitDraft", Qt::DirectConnection);
        return QObject::eventFilter(watched, event);
    }
};

} // namespace EchoMusic

int main(int argc, char *argv[])
{
    // This process only waits for normal teardown and relaunches. It must not
    // initialize Qt GUI, account storage, audio or the instance lock.
    if (EchoMusic::RelaunchService::isHelperInvocation(argc, argv))
        return EchoMusic::RelaunchService::runHelper(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("hoowhoami"));
    QCoreApplication::setApplicationName(QStringLiteral("EchoMusicNext"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ECHO_APP_VERSION));
    // Publish the shell identity before the first window exists: Windows
    // resolves the media panel label and the taskbar group from it.
    EchoMusic::WindowsAppIdentity::publish();
    QString crashDirectory = EchoMusic::CrashDiagnostics::argumentLogDirectory(argc, argv);
    if (crashDirectory.isEmpty())
        crashDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                         QStringLiteral("/logs");
    // Its destructor runs after QApplication: initialization and native/Qt
    // teardown failures have the same emergency reporting path as runtime faults.
    EchoMusic::CrashDiagnostics crashDiagnostics(crashDirectory);
    EchoMusic::initializeNetworkEnvironment();
    // Keep every Qt Quick surface alpha-capable from its first native creation.
    // The theme paints an opaque skin when transparency is disabled.
    QQuickWindow::setDefaultAlphaBuffer(true);
    // D3D redirection policy belongs to the attached main HWND. A process
    // switch would also disable raster backing stores for widget dialogs.
    // Qt Widgets supplies native tray menus; the application UI stays Qt Quick.
    QApplication app(argc, argv);
    EchoMusic::EchoGpuSurface::installWindowPolicy();
    // Lives for the whole process: every popup, dialog and page reports mouse
    // presses through this single filter.
    EchoMusic::SpinBoxCommitFilter spinBoxCommitFilter(&app);
    EchoMusic::ApplicationTranslations translations;
    translations.load(QLocale::system());
    EchoMusic::ApplicationUrlHandler urlHandler;
    EchoMusic::configureUiTypography();
    // Use the publisher identity: Qt app-data paths are <publisher>/<application>.
    QGuiApplication::setApplicationDisplayName(QStringLiteral("EchoMusicNext"));
#ifndef Q_OS_MACOS
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(ECHO_APP_ICON_PATH)));
#endif
    QGuiApplication::setDesktopFileName(QStringLiteral(ECHO_APPLICATION_ID));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EchoMusicNext desktop application"));
    parser.addOption({QStringLiteral("safe-mode"),
                      QStringLiteral("Discover plugins without executing their code")});
    parser.addOption({QStringLiteral("export-plugin-sdk"),
                      QStringLiteral("Print the actual Qt plugin SDK contract and exit")});
    parser.addOption(
        {QStringLiteral("plugin-dir"),
         QStringLiteral("Import and enable a trusted .echomod package or development directory"),
         QStringLiteral("path")});
    parser.addPositionalArgument(QStringLiteral("plugin"),
                                 QStringLiteral("Install an .echomod package"),
                                 QStringLiteral("[plugin]"));
    parser.addOption({QStringLiteral("open-url"),
                      QStringLiteral("Open an EchoMusicNext plugin share link"),
                      QStringLiteral("url")});
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {QStringLiteral("smoke-test"), QStringLiteral("Isolated application smoke check")});
    parser.addOption({QStringLiteral("startup-smoke-test"),
                      QStringLiteral("Isolated real startup/device-registration smoke check")});
    parser.addOption({QStringLiteral("log-dir"),
                      QStringLiteral("Log directory (default: application data/logs)"),
                      QStringLiteral("directory")});
    parser.addOption({QStringLiteral("log-level"),
                      QStringLiteral("error, warn, info, debug or verbose"),
                      QStringLiteral("level"), QStringLiteral("info")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("export-plugin-sdk"))) {
        QTextStream(stdout) << QJsonDocument(EchoMusic::pluginSdkContract()).toJson();
        return 0;
    }
    const QStringList levels{QStringLiteral("error"), QStringLiteral("warn"),
                             QStringLiteral("info"), QStringLiteral("debug"),
                             QStringLiteral("verbose")};
    const auto level = levels.indexOf(parser.value(QStringLiteral("log-level")));
    if (level < 0)
        parser.showHelp(2);
    EchoMusic::SingleInstance instance;
    const bool smoke = parser.isSet(QStringLiteral("smoke-test")) ||
                       parser.isSet(QStringLiteral("startup-smoke-test"));
    QUrl openUrl(parser.value(QStringLiteral("open-url")), QUrl::StrictMode);
    const auto files = parser.positionalArguments();
    if (files.size() > 1)
        parser.showHelp(2);
    if (!files.isEmpty()) {
        const QFileInfo file(files.first());
        if (!file.isFile() || file.suffix().compare("echomod", Qt::CaseInsensitive) != 0 ||
            !openUrl.isEmpty())
            parser.showHelp(2);
        openUrl = QUrl::fromLocalFile(file.absoluteFilePath());
    }
    if (openUrl.isEmpty()) {
        const auto pending = urlHandler.takePending(1);
        if (!pending.isEmpty())
            openUrl = pending.first();
    }
    if (!smoke) {
        const auto result = instance.start(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), 5000, openUrl);
        if (result == EchoMusic::SingleInstance::Result::Activated) {
            // LaunchServices can deliver several files before services exist.
            // Forward the rest as well instead of losing all but the first plugin.
            for (const auto &url : urlHandler.takePending()) {
                EchoMusic::SingleInstance forward;
                if (forward.start(
                        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
                        5000, url) != EchoMusic::SingleInstance::Result::Activated) {
                    qCritical().noquote() << forward.errorString();
                    return 1;
                }
            }
            qInfo("Existing instance activated");
            return 0;
        }
        if (result == EchoMusic::SingleInstance::Result::Failed) {
            qCritical().noquote() << instance.errorString();
            return 1;
        }
    }
    // Windows labels a media session by resolving its AppUserModelID through a
    // Start Menu shortcut. Installed copies get one from the installer; a build
    // run from its output directory does not, so keep the entry in place.
    if (!smoke)
        EchoMusic::WindowsAppIdentity::ensureStartMenuShortcut();
    // Outlives all application services, including Rust runtime destruction.
    // The writer drains only after run() exits its GUI event loop.
    EchoMusic::SettingsStore logPreferences;
    EchoMusic::LogService logging(parser.value(QStringLiteral("log-dir")), int(level), true,
                                  smoke ? nullptr : &logPreferences,
                                  parser.isSet(QStringLiteral("log-level")));
    if (!crashDiagnostics.errorString().isEmpty())
        qCritical().noquote() << "Crash diagnostics degraded:" << crashDiagnostics.errorString();
    for (const auto &report : crashDiagnostics.previousAbnormalSessions())
        qWarning().noquote() << "Previous session ended without clean shutdown; crash report:"
                             << report;
    for (const auto &summary : crashDiagnostics.previousCrashSummaries())
        qCritical().noquote() << "Previous native crash:" << summary;
    EchoMusic::Application application(
        app, smoke ? nullptr : &instance, parser.values(QStringLiteral("plugin-dir")),
        parser.isSet(QStringLiteral("safe-mode")), openUrl, &urlHandler, &logging);
    return application.run();
}
