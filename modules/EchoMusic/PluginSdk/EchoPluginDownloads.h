#pragma once
#include "EchoPluginFiles.h"
namespace EchoMusic
{
class EchoPluginTask;
class EchoPluginDownloads final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginDownloads(EchoPluginFiles &files);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *start(const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *list(int offset = 0, int limit = 100);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *get(const QString &id);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *attach(const QString &id);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *remove(const QString &id);

private:
    EchoPluginFiles &m_files;
};
class EchoPluginDownload final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by EchoPluginDownloads")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
public:
    QString id() const
    {
        return m_snapshot.value("id").toString();
    }
    bool active() const
    {
        return m_active;
    }
    QVariantMap snapshot() const
    {
        return m_snapshot;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *pause();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *resume(const QVariantMap &source = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *retry(const QVariantMap &source = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *cancel();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *wait();
signals:
    void changed();

private:
    friend class EchoPluginFiles;
    friend class EchoPluginDownloads;
    EchoPluginDownload(EchoPluginFiles &files, const QVariantMap &snapshot);
    void accept(const QVariantMap &snapshot);
    void invalidate();
    void ensureTask();
    EchoPluginOperation *command(const QString &, const QVariantMap &source = {});
    EchoPluginFiles &m_files;
    QVariantMap m_snapshot;
    QPointer<EchoPluginTask> m_task;
    bool m_active = true;
};
} // namespace EchoMusic
