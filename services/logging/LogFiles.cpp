#include "LogFiles.h"
#include <QCoreApplication>
#include <QDateTime>

namespace EchoMusic
{
const QDateTime &sessionStartedAt()
{
    static const auto started = QDateTime::currentDateTimeUtc();
    return started;
}
const QString &sessionLogFileName()
{
    static const auto name =
        QStringLiteral("echomusicnext-%1-%2.log")
            .arg(sessionStartedAt().toLocalTime().toString("yyyy-MM-dd_HH-mm-ss-zzz"))
            .arg(QCoreApplication::applicationPid());
    return name;
}
} // namespace EchoMusic
