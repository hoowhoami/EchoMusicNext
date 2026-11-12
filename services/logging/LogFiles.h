#pragma once
#include <QString>
#include <QDateTime>

namespace EchoMusic
{
// One process identity shared by the pre-Qt crash collector and the log sink.
const QString &sessionLogFileName();
const QDateTime &sessionStartedAt();
} // namespace EchoMusic
