#pragma once
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
namespace EchoMusic
{
/// Canonical path to the running executable. Qt derives applicationFilePath()
/// from argv[0], so launchers and symlinks (for example AppRun.wrapped) would
/// otherwise make sibling helpers appear in the launcher's directory.
inline QString executableFilePath()
{
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    const auto canonical = executable.canonicalFilePath();
    return canonical.isEmpty() ? executable.absoluteFilePath() : canonical;
}
inline QString executableDirectory()
{
    return QFileInfo(executableFilePath()).absolutePath();
}
} // namespace EchoMusic
