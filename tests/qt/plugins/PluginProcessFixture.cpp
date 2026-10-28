#include <cstdlib>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
#ifdef Q_OS_UNIX
    if (args.contains("--ignore-term"))
        ::signal(SIGTERM, SIG_IGN);
#endif
    if (args.contains("--child"))
        return app.exec();
    if (args.size() < 2)
        return 2;
    QProcess child;
    child.setStandardOutputFile(QProcess::nullDevice());
    child.setStandardErrorFile(QProcess::nullDevice());
    child.start(app.applicationFilePath(), {"--child", "--ignore-term"});
    if (!child.waitForStarted(3000))
        return 3;
    QFile marker(args[1]);
    if (!marker.open(QIODevice::WriteOnly))
        return 4;
    marker.write(
        QJsonDocument(QJsonObject{{"pid", app.applicationPid()}, {"child", child.processId()}})
            .toJson());
    marker.close();
    if (args.contains("--exit-parent")) {
        // Deliberately bypass QProcess's destructor so the host must reap the tree.
        QTimer::singleShot(50, [] { std::_Exit(0); });
    }
    return app.exec();
}
