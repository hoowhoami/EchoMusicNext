#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtLogging>
#include <memory>
#include "SettingsStore.h"
namespace EchoMusic
{
class LogSink;
// Qt owns the unified logging sink. echo-log only bridges Rust records into Qt.
// Lifetime: before services/QML, after their shutdown and GUI event-loop exit.
class LogService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString level READ level WRITE setLevel NOTIFY changed)
    Q_PROPERTY(bool apiResponseBody READ apiResponseBody WRITE setApiResponseBody NOTIFY changed)
    Q_PROPERTY(bool diagnosticActive READ diagnosticActive NOTIFY changed)
    Q_PROPERTY(int diagnosticMinutes READ diagnosticMinutes NOTIFY changed)
public:
    explicit LogService(const QString &directory, int level = 2, bool console = true,
                        EchoMusic::SettingsStore *preferences = nullptr,
                        bool commandLineLevel = false);
    ~LogService() override;
    bool active() const
    {
        return m_active;
    }
    QString directory() const
    {
        return m_directory;
    }
    QString level() const;
    void setLevel(const QString &level);
    bool apiResponseBody() const
    {
        return m_responseBody;
    }
    void setApiResponseBody(bool enabled);
    bool diagnosticActive() const;
    int diagnosticMinutes() const;
    Q_INVOKABLE bool enableTemporaryDiagnostics();
    Q_INVOKABLE void disableTemporaryDiagnostics();
    Q_INVOKABLE bool openDirectory();
signals:
    void changed();
    void notice(const QString &message);

private:
    bool persist(const QString &key, const QVariant &value);
    void applyThreshold();
    QString m_directory;
    QtMessageHandler m_previous = nullptr;
    QTimer m_poll;
    std::shared_ptr<LogSink> m_sink;
    bool m_active = false;
    int m_level = 2;
    bool m_responseBody = false;
    qint64 m_diagnosticUntil = 0;
    QTimer m_diagnostics;
    std::unique_ptr<EchoMusic::SettingsStore> m_preferences;
};
} // namespace EchoMusic
