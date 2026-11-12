#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <QJsonObject>

namespace EchoMusic
{
// Installed before QApplication and destroyed after it. Fatal paths bypass
// the GUI/Rust queues and never attempt to resume the damaged process.
class CrashDiagnostics final
{
public:
    explicit CrashDiagnostics(const QString &directory);
    ~CrashDiagnostics();
    CrashDiagnostics(const CrashDiagnostics &) = delete;
    CrashDiagnostics &operator=(const CrashDiagnostics &) = delete;
    QString errorString() const;
    QStringList previousAbnormalSessions() const;
    QStringList previousCrashSummaries() const;
    static QString argumentLogDirectory(int argc, char **argv);
    static QJsonObject summarizeDump(const QString &path);
    static QString buildDescription();
    static QJsonObject buildIdentity();
    static void recordFatal(const char *reason) noexcept;
    static void recordQtFatal(const QString &message);
    // Only for an already-sanitized Qt fatal message, outside a signal handler.
    static void recordFatalMessage(const QByteArray &message) noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace EchoMusic
