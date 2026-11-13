#pragma once
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
namespace EchoMusic
{
/// Resolve symlinks before locating sibling helpers. On Linux, Qt 6.12 prefers
/// an absolute AT_EXECFN path, which can retain a symlink such as AppRun.wrapped;
/// it falls back to /proc/self/exe, then argv[0] if the OS lookup fails.
/// Preserve the absolute application path if canonicalization is unavailable.
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
