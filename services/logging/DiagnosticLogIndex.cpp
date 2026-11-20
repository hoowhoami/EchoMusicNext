#include "DiagnosticLogIndex.h"
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

namespace EchoMusic
{
QJsonObject diagnosticLogIndex(const QString &root, const QStringList &files,
                               const std::atomic_bool &cancel)
{
    const QRegularExpression pattern(
        R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (INFO |WARN |ERROR|FATAL) \[([^\]]{1,80})\] (.*)$)");
    const QRegularExpression qmlLocation(
        R"(^(?:qrc:|file:|/|[A-Za-z]:[\\/]).*\.qml(?::\d+){0,2}: )");
    QList<QJsonObject> recent;
    qint64 warnings = 0, errors = 0, qmlInfoDiagnostics = 0;
    QStringList unreadable;
    for (const auto &name : files) {
        if (!name.startsWith("logs/") || !name.endsWith(".log"))
            continue;
        QFile file(root + '/' + name);
        if (!file.open(QIODevice::ReadOnly)) {
            unreadable.append(name);
            continue;
        }
        qint64 lineNumber = 0;
        bool continuation = false;
        while (!file.atEnd()) {
            if (cancel)
                return {};
            const auto bytes = file.readLine(16385);
            if (!continuation)
                ++lineNumber;
            const auto skipped = continuation;
            continuation = !bytes.endsWith('\n') && !file.atEnd();
            if (skipped)
                continue;
            const auto match = pattern.match(QString::fromUtf8(bytes).trimmed());
            if (!match.hasMatch())
                continue;
            const auto level = match.captured(2).trimmed();
            const auto category = match.captured(3);
            // Qt reports canceled/invalid incubation at INFO. Index its original
            // severity and source, rather than losing it among ordinary info.
            const bool qmlInfo = level == "INFO" && category == "default" &&
                                 qmlLocation.match(match.captured(4)).hasMatch();
            if (qmlInfo)
                ++qmlInfoDiagnostics;
            if (level == "WARN")
                ++warnings;
            else if (level == "ERROR" || level == "FATAL")
                ++errors;
            else if (category != "echo.shell.window" && category != "echo.shell.route" &&
                     category != "echo.appearance" && !qmlInfo)
                continue;
            recent.append({{"timeLocal", match.captured(1)},
                           {"level", level},
                           {"category", category},
                           {"message", match.captured(4).left(512)},
                           {"file", name},
                           {"line", lineNumber}});
            // Merge rotated logs by their emission time, not directory order.
            // At most 100 recent events and one 16 KiB input chunk are retained.
            std::stable_sort(recent.begin(), recent.end(), [](const auto &a, const auto &b) {
                if (a.value("timeLocal") == b.value("timeLocal")) {
                    if (a.value("file") != b.value("file"))
                        return a.value("file").toString() < b.value("file").toString();
                    return a.value("line").toInteger() > b.value("line").toInteger();
                }
                return a.value("timeLocal").toString() > b.value("timeLocal").toString();
            });
            if (recent.size() > 100)
                recent.removeLast();
        }
        if (file.error() != QFileDevice::NoError)
            unreadable.append(name);
    }
    QJsonArray events;
    for (const auto &event : recent)
        events.append(event);
    return {{"scope", "included_sanitized_logs"},
            {"warnings", warnings},
            {"errors", errors},
            {"qmlInfoDiagnostics", qmlInfoDiagnostics},
            {"recentEvents", events},
            {"eventLimit", 100},
            {"unreadableFiles", QJsonArray::fromStringList(unreadable)}};
}
} // namespace EchoMusic
