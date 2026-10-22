#pragma once
#include "PluginFileService.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QNetworkReply>
#include <QSet>
#include <QCryptographicHash>
#include <functional>
namespace EchoMusic
{
struct PluginFileFailure
{
    QString code, message;
};
using FileCancellation = std::shared_ptr<std::atomic_bool>;
struct PluginFileGrant
{
    PluginFileOwner owner;
    QString id, path, kind, access, revision;
    bool persistent = false, revoked = false;
};
struct PluginFileResolved
{
    QString path;
    PluginFileGrant grant;
    QVariantMap reference;
};
struct PluginDownload
{
    PluginFileOwner owner;
    QString id, state = "queued", errorCode, errorMessage, partPath, etag, lastModified;
    QVariantMap options, target, result;
    qint64 received = 0, total = -1, created = 0, updated = 0;
    quint64 run = 1, revision = 0, attempt = 0;
    QPointer<QNetworkReply> reply;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QTimer deadline, idle, publish;
    FileCancellation canceled = std::make_shared<std::atomic_bool>(false);
    bool headers = false;
    qint64 startedAt = 0, startedBytes = 0, expectedEnd = -1;
    int redirects = 0;
};
class PluginFileWorker final : public QObject
{
public:
    PluginFileWorker(PluginFileService &service, QString root);
    ~PluginFileWorker() override;
    void initialize();
    void execute(quint64 id, const PluginFileOwner &, const QString &method, const QVariantMap &,
                 FileCancellation);
    void closeScope(const QString &scope);
    void forget(const QString &installation);
    void stop();
    void complete(quint64 id, const QVariant &value = {});
    void fail(quint64 id, const QString &code, const QString &message);
    void publishGrants();
    void saveGrants();
    QVariantMap describe(const PluginFileGrant &) const;
    QVariantMap reference(const PluginFileGrant &, const QString &path = {}) const;
    PluginFileResolved resolve(const PluginFileOwner &, const QVariantMap &ref, bool write,
                               bool existing = true) const;
    bool authorized(const PluginFileOwner &, const QVariantMap &ref, bool write = false) const;
    QVariant files(quint64 id, const PluginFileOwner &, const QString &, const QVariantMap &,
                   FileCancellation);
    QVariant openMedia(const PluginFileOwner &, const QVariantMap &);
    void releaseMedia(const QString &id);
    void
    retireMedia(const std::function<bool(const PluginFileOwner &, const QVariantMap &)> &predicate);
    QVariant downloads(const PluginFileOwner &, const QString &, const QVariantMap &);
    void loadDownloads();
    void saveDownloads();
    void publishDownload(const std::shared_ptr<PluginDownload> &);
    void startDownload(const std::shared_ptr<PluginDownload> &);
    void finishDownload(const std::shared_ptr<PluginDownload> &, const QString &state,
                        const QString &code = {}, const QString &message = {});
    void consumeDownload(const std::shared_ptr<PluginDownload> &);
    void commitDownload(const std::shared_ptr<PluginDownload> &);
    void pumpDownloads();
    void retireDownloads(const std::function<bool(const PluginDownload &)> &, const QString &code);
    QVariantMap downloadSnapshot(const PluginDownload &) const;
    QString privateRoot(const QString &installation, const QString &kind) const;
    static void check(FileCancellation cancel);
    static QString relative(const QVariant &value);
    static QString conflictPath(const QString &path, const QString &mode);
    PluginFileService &service;
    QString root, initializationError;
    QHash<QString, PluginFileGrant> grants;
    QSet<QString> closedScopes;
    struct Media
    {
        PluginFileOwner owner;
        QVariantMap ref;
        QUrl url;
        QSet<QObject *> clients;
    };
    QHash<QString, Media> media;
    QSet<QObject *> mediaClients;
    QTcpServer *server = nullptr;
    QHash<QString, std::shared_ptr<PluginDownload>> jobs;
    QNetworkAccessManager *network = nullptr;
    bool closing = false;
};
} // namespace EchoMusic
