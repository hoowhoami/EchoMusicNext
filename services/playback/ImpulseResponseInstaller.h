#pragma once
#include <QObject>
#include <QThread>
#include <QStringList>
namespace EchoMusic
{
/// A single bounded install. Network callbacks and atomic file writes share a
/// worker event loop; cancellation releases both before closed is emitted.
class ImpulseResponseInstaller final : public QObject
{
    Q_OBJECT
public:
    explicit ImpulseResponseInstaller(QObject *parent = nullptr);
    ~ImpulseResponseInstaller() override;
    void install(quint64 generation, const QStringList &urls, const QString &destination);
    void cancel(const QStringList &unpublishedPaths = {});
    void shutdown();
signals:
    void progress(quint64 generation, double value);
    void finished(quint64 generation, bool success, const QString &path, const QString &error);
    void closed();

private:
    QThread m_thread;
    QObject *m_worker;
    bool m_closing = false;
};
} // namespace EchoMusic
