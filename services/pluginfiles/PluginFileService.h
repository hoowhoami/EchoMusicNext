#pragma once
#include <QAbstractListModel>
#include <QThread>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <functional>
namespace EchoMusic
{
class PluginFileWorker;
struct PluginFileOwner
{
    QString installation, pluginId, name, scope;
    std::shared_ptr<std::atomic_bool> canceled;
};
/// One application-owned file/grant/download authority. Disk and streaming I/O
/// live on its worker; only snapshots and terminal results cross to the GUI.
class PluginFileService final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit PluginFileService(QString root, QObject *parent = nullptr);
    ~PluginFileService() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool loading() const
    {
        return m_loading;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    quint64 request(const PluginFileOwner &, const QString &method, const QVariantMap &input = {});
    void cancel(quint64 id);
    void closeScope(const QString &scope);
    void forgetInstallation(const QString &installation);
    void snapshotBarrier(std::function<void(bool)> ready);
    void freezeStorage(bool frozen)
    {
        m_snapshot = frozen;
    }
    void shutdown();
    bool isClosed() const
    {
        return m_closed;
    }
    QVariantMap grant(const QString &id) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void revoke(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void reauthorize(const QString &id);
signals:
    void completed(quint64 id, bool ok, const QVariant &result, const QString &code,
                   const QString &message);
    void progress(quint64 id, qint64 bytes, qint64 total);
    void downloadChanged(const QString &installation, const QVariantMap &snapshot);
    void downloadRemoved(const QString &installation, const QString &id);
    void leaseClosed(const QString &id);
    void mediaRevoked(const QUrl &url);
    void scopeClosed(const QString &scope);
    void selectionRequested(const QVariantMap &grant);
    void changed();
    void closed();

private:
    friend class PluginFileWorker;
    void publishGrants(const QVariantList &rows);
    QString m_root, m_error;
    QVariantList m_rows;
    QThread m_thread;
    PluginFileWorker *m_worker = nullptr;
    quint64 m_next = 0;
    struct Pending
    {
        QString scope, installation;
        QSet<QString> grants;
        std::shared_ptr<std::atomic_bool> canceled;
    };
    struct DownloadControl
    {
        QString installation, scope, grant;
        std::shared_ptr<std::atomic_bool> canceled;
    };
    QHash<QString, DownloadControl> m_downloadControls;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_scopes;
    QHash<quint64, Pending> m_pending;
    bool m_snapshot = false;
    bool m_closing = false, m_closed = false, m_loading = true;
};
} // namespace EchoMusic
