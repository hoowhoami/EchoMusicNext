#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginAssetAnimation.h"
#include "EchoPluginAssetArchive.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <atomic>
#include <memory>

class QFileDialog;
class QTemporaryDir;
namespace EchoMusic
{
class EchoPluginContext;
struct EchoPluginAssetStore;
struct EchoPluginAssetJob;
struct EchoPluginAnimationCache;
struct EchoPluginArchiveSession;
class EchoPluginImageDropArea;

/// User-picked assets persist per installation. Every asynchronous request and
/// font registration belongs to its visible/activation scope; no path API.
class EchoPluginAssets final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    EchoPluginAssets(EchoPluginContext &scope, QString dataPath,
                     EchoPluginAssets *activationAssets = nullptr);
    ~EchoPluginAssets() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *importImages(const QString &collection);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *importImages(const QString &collection,
                                                             const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *importFonts(const QString &collection);
    Q_INVOKABLE EchoMusic::EchoPluginAssetArchive *openImageArchive(const QStringList &jsonPaths);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *exportImageArchive(const QString &collection,
                                                                   const QVariantList &entries,
                                                                   const QVariantMap &jsonFiles,
                                                                   const QString &suggestedName);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *list(const QString &collection);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *remove(const QString &handle);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *clear(const QString &collection);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *sampleImage(const QUrl &source, int width = 64,
                                                            int height = 64);
    Q_INVOKABLE EchoMusic::EchoPluginAssetAnimation *animation(const QUrl &source, int width = 128,
                                                               int height = 128,
                                                               const QString &fit = "contain");
    void shutdown();
    bool isDrained() const;
    static qint64 archiveReservedBytes();
    static int archiveSlotCount();
signals:
    void drained();
    void changed();

private:
    friend class EchoPluginAssetAnimation;
    friend class EchoPluginAssetArchive;
    friend class EchoPluginImageDropArea;
    EchoPluginOperation *importDroppedImages(const QString &collection, const QList<QUrl> &urls,
                                             int maxCount);
    EchoPluginAssetArchive *openDroppedImageArchive(const QStringList &jsonPaths,
                                                    const QUrl &source);
    EchoPluginAssetArchive *openArchive(const QStringList &jsonPaths, const QUrl &selected = {});
    EchoPluginOperation *importArchive(EchoPluginAssetArchive *lease, const QString &collection,
                                       const QVariantList &entries);
    void releaseArchive(EchoPluginAssetArchive *lease);
    void cleanupArchive(const std::shared_ptr<EchoPluginArchiveSession> &session,
                        std::function<void()> completion = {});
    EchoPluginOperation *imageRequest(int command, const QUrl &source, int width, int height,
                                      const QString &fit = "contain");
    void processAnimationQueue();
    void releaseAnimation(EchoPluginAssetAnimation *lease);
    void updateAnimation(const std::shared_ptr<EchoPluginAnimationCache> &cache);
    void cleanupDirectory(std::shared_ptr<QTemporaryDir> directory, qint64 bytes = 0,
                          bool cacheEntry = false, std::function<void()> completion = {});
    EchoPluginOperation *import(const QString &collection, bool fonts,
                                const QVariantMap &options = {});
    EchoPluginOperation *request(int command, const QString &value);
    void run(const std::shared_ptr<EchoPluginAssetJob> &job);
    void complete(const std::shared_ptr<EchoPluginAssetJob> &job);
    void releaseFonts();
    std::shared_ptr<EchoPluginAssetJob> begin(int command, const QString &value,
                                              bool countPending = true);
    EchoPluginContext &m_scope;
    std::shared_ptr<EchoPluginAssetStore> m_store;
    QNetworkAccessManager m_network;
    QList<std::shared_ptr<EchoPluginAssetJob>> m_jobs;
    QPointer<QFileDialog> m_dialog;
    QSet<QString> m_fontReferences;
    QHash<QString, std::shared_ptr<EchoPluginAnimationCache>> m_animations;
    QSet<EchoPluginAssetAnimation *> m_animationLeases;
    QSet<EchoPluginAssetArchive *> m_archiveLeases;
    int m_cleanupCount = 0;
    bool m_closed = false;
};
} // namespace EchoMusic
