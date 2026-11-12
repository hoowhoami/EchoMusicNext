#include "CrashDiagnostics.h"
#include "LogService.h"
#include "LogFiles.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace
{
Q_NEVER_INLINE void accessViolation()
{
#ifdef Q_OS_WIN
    auto *page = static_cast<volatile unsigned char *>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    if (!page)
        std::abort();
    *page = 1;
#else
    auto *page = static_cast<volatile unsigned char *>(
        mmap(nullptr, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    if (page == MAP_FAILED)
        std::abort();
    *page = 1;
#endif
}
struct ExitFault
{
    QByteArray mode;
    ~ExitFault()
    {
        if (mode == "teardown")
            accessViolation();
        if (mode == "late-fatal")
            qFatal("late fatal password=do-not-store");
    }
};
} // namespace
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
#ifdef Q_OS_WIN
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif
    QCoreApplication::setOrganizationName("EchoMusicNextTests");
    QCoreApplication::setApplicationName("CrashProbe");
    // Exercise the same lifetime as main: native crash hooks surround Qt itself.
    EchoMusic::CrashDiagnostics crashes(QString::fromLocal8Bit(argv[2]));
    if (QString::fromLatin1(argv[1]) == "startup")
        accessViolation();
    if (QString::fromLatin1(argv[1]) == "early-fatal")
        qFatal("early fatal password=do-not-store");
    ExitFault exitFault{QByteArray(argv[1])};
    QCoreApplication app(argc, argv);
    EchoMusic::LogService logging(QString::fromLocal8Bit(argv[2]), 0, false);
    if (!crashes.errorString().isEmpty()) {
        qCritical().noquote() << crashes.errorString();
        return 3;
    }
    for (const auto &report : crashes.previousAbnormalSessions())
        qCritical().noquote() << "Previous unclean session:" << report;
    for (const auto &summary : crashes.previousCrashSummaries())
        qCritical().noquote() << "Previous native crash:" << summary;
    const QByteArray mode(argv[1]);
    if (mode == "hold") {
        std::puts("ready");
        std::fflush(stdout);
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (mode == "fatal-logged") {
        // This case specifically exercises the log attachment fallback. A fault
        // before the writer creates a file legitimately has no log attachment;
        // the early-fatal/startup cases continue covering that separate boundary.
        QElapsedTimer wait;
        wait.start();
        const auto path =
            QDir(QString::fromLocal8Bit(argv[2])).filePath(EchoMusic::sessionLogFileName());
        while (QFileInfo(path).size() <= 0 && wait.elapsed() < 5000) {
            qCritical(
                "log attachment probe ready"); // This probe intentionally logs at error level.
            QThread::msleep(5);
        }
        if (QFileInfo(path).size() <= 0)
            return 4;
        qFatal("logged crash probe password=do-not-store");
    }
    if (mode == "fatal")
        qFatal("crash probe password=do-not-store");
    if (mode == "terminate")
        std::terminate();
    if (mode == "abort")
        std::abort();
    if (mode == "access")
        accessViolation();
    if (mode == "worker") {
        std::thread worker(accessViolation);
        worker.join();
    }
    if (mode == "unclean")
        std::_Exit(77);
    return 0;
}
