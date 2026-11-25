#pragma once
#include <QQmlNetworkAccessManagerFactory>
#include <QString>
#include <memory>

namespace EchoMusic
{
class ImageDiskCacheOwner;
/// Shared HTTP image cache for QML, media artwork and SDK image processing.
/// Construct on the GUI thread and outlive the engine/client teardown.
class QmlImageNetworkFactory final : public QQmlNetworkAccessManagerFactory
{
public:
    static constexpr qint64 MaximumCacheSize = 1024LL * 1024 * 1024;
    explicit QmlImageNetworkFactory(QString directory, qint64 maximumCacheSize = MaximumCacheSize);
    ~QmlImageNetworkFactory() override;
    QNetworkAccessManager *create(QObject *parent) override;
    /// GUI-owned image client. HTTP/cache work runs on the shared owner thread;
    /// encoded delivery is bounded independently of the disk cache budget.
    QNetworkAccessManager *createImageClient(QObject *parent, qint64 maximumResponseBytes);

private:
    std::shared_ptr<ImageDiskCacheOwner> m_cache;
};
} // namespace EchoMusic
