#pragma once
#include "EchoPluginOperation.h"
#include "PluginFileService.h"
#include <QUrl>
#include <QPointer>
#include <QSet>
#include <functional>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginDownload;
class EchoPluginMediaLease;
class EchoPluginFilePicker;
/// Typed access to host-owned file references. Paths are never used as authority;
/// every operation is revalidated on the I/O worker with an installation/revision.
class EchoPluginFiles final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginFiles(EchoPluginContext &scope);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *requestDirectory(const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *requestFiles(const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *reauthorize(const QString &id,
                                                            const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *listGrants();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *revoke(const QString &id);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *privateDirectory(const QString &kind = "data");
    Q_INVOKABLE EchoMusic::EchoPluginOperation *stat(const QVariantMap &file,
                                                     const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *mkdir(const QVariantMap &file,
                                                      const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *listFiles(const QVariantMap &file,
                                                          const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *readText(const QVariantMap &file,
                                                         const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *readBytes(const QVariantMap &file,
                                                          const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *metadata(const QVariantMap &file,
                                                         const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    writeText(const QVariantMap &file, const QString &text, const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    writeBytes(const QVariantMap &file, const QByteArray &bytes, const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *deleteFile(const QVariantMap &file,
                                                           const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    copyFile(const QVariantMap &source, const QVariantMap &target, const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *openMedia(const QVariantMap &file,
                                                          const QVariantMap &options = {});
    void shutdown();
    bool isDrained() const;
signals:
    void drained();

private:
    friend class EchoPluginDownloads;
    friend class EchoPluginDownload;
    friend class EchoPluginMediaLease;
    using Adapt = std::function<void(EchoPluginOperation *, const QVariant &)>;
    EchoPluginOperation *invoke(const QString &method, QVariantMap input = {},
                                const QVariantMap &options = {}, Adapt adapt = {},
                                EchoPluginOperation *existing = nullptr);
    EchoPluginOperation *select(bool directory, const QVariantMap &options,
                                const QString &replace = {});
    EchoPluginOperation *fileOperation(const QString &method, const QVariantMap &file,
                                       const QVariantMap &options, const QStringList &allowed);
    bool validOptions(const QVariantMap &, const QStringList &) const;
    void resetScope();
    void retireResources();
    void checkDrained();
    EchoPluginDownload *download(const QVariantMap &snapshot, bool task = false);
    EchoPluginContext &m_scope;
    PluginFileService *m_service;
    PluginFileOwner m_owner;
    struct Request
    {
        QPointer<EchoPluginOperation> operation;
        Adapt adapt;
        PluginFileOwner owner;
        QString method;
    };
    QHash<quint64, Request> m_requests;
    QList<QPointer<EchoPluginMediaLease>> m_media;
    QHash<QString, QPointer<EchoPluginDownload>> m_downloads;
    QHash<QString, QString> m_boundDownloads;
    QSet<QString> m_closingScopes;
    bool m_closing = false;
};
class EchoPluginMediaLease final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by EchoPluginFiles")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    QString id() const
    {
        return m_id;
    }
    QUrl url() const
    {
        return m_active ? m_url : QUrl();
    }
    bool active() const
    {
        return m_active;
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class EchoPluginFiles;
    EchoPluginMediaLease(EchoPluginFiles &files, const QVariantMap &value);
    void invalidate();
    EchoPluginFiles &m_files;
    QString m_id;
    QUrl m_url;
    PluginFileOwner m_owner;
    QString m_revision;
    bool m_active = true;
};
} // namespace EchoMusic
