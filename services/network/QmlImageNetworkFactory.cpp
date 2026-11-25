#include "QmlImageNetworkFactory.h"
#include <QAbstractNetworkCache>
#include <QBuffer>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPointer>
#include <QThread>
#include <QVariant>
#include <atomic>
#include <cstring>
#include <functional>
#include <type_traits>

namespace EchoMusic
{
namespace
{
constexpr qint64 MaximumEntrySize = 8LL * 1024 * 1024;
bool imageResponse(const QNetworkCacheMetaData &metadata);
class ImageDiskCache final : public QNetworkDiskCache
{
public:
    using QNetworkDiskCache::QNetworkDiskCache;
    QIODevice *prepare(const QNetworkCacheMetaData &metadata) override
    {
        return imageResponse(metadata) ? QNetworkDiskCache::prepare(metadata) : nullptr;
    }
    void insert(QIODevice *device) override
    {
        QNetworkDiskCache::insert(device);
        // Native expiry precedes insertion. Re-arm it after a crossing commit.
        if (cacheSize() > maximumCacheSize()) {
            const auto limit = maximumCacheSize();
            setMaximumCacheSize(limit - 1);
            setMaximumCacheSize(limit);
        }
    }
};
struct ImageDownload
{
    std::atomic_bool cancelled{false};
    QPointer<QNetworkReply> reply; // Accessed only on the disk/network owner thread.
    QByteArray bytes;
};
class ImageReply final : public QNetworkReply
{
public:
    ImageReply(const QNetworkRequest &request, QObject *parent,
               std::shared_ptr<ImageDownload> download, std::function<void()> cancel)
        : QNetworkReply(parent), m_download(std::move(download)), m_cancel(std::move(cancel))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    ~ImageReply() override
    {
        m_download->cancelled = true;
        m_cancel();
    }
    void abort() override
    {
        if (isFinished())
            return;
        m_download->cancelled = true;
        m_cancel();
        setError(OperationCanceledError, QStringLiteral("Image request cancelled"));
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }
    void complete(QUrl url, const QList<RawHeaderPair> &headers,
                  const QHash<QNetworkRequest::Attribute, QVariant> &attributes, QByteArray bytes,
                  NetworkError failure, const QString &message)
    {
        if (isFinished())
            return;
        setUrl(url);
        for (const auto &header : headers)
            setRawHeader(header.first, header.second);
        for (auto i = attributes.cbegin(); i != attributes.cend(); ++i)
            setAttribute(i.key(), i.value());
        m_bytes = std::move(bytes);
        if (failure != NoError)
            setError(failure, message);
        setFinished(true);
        emit metaDataChanged();
        if (!m_bytes.isEmpty())
            emit readyRead();
        if (error() != NoError)
            emit errorOccurred(error());
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_bytes.size() - m_position + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin<qint64>(maximum, m_bytes.size() - m_position);
        if (!count)
            return isFinished() ? -1 : 0;
        std::memcpy(data, m_bytes.constData() + m_position, size_t(count));
        m_position += count;
        if (m_position == m_bytes.size()) {
            m_bytes.clear();
            m_position = 0;
        }
        return count;
    }

private:
    std::shared_ptr<ImageDownload> m_download;
    std::function<void()> m_cancel;
    QByteArray m_bytes;
    qint64 m_position = 0;
};
bool imageResponse(const QNetworkCacheMetaData &metadata)
{
    for (const auto &header : metadata.rawHeaders())
        if (header.first.compare("Content-Type", Qt::CaseInsensitive) == 0)
            return header.second.trimmed().toLower().startsWith("image/");
    return false;
}
class CacheDevice final : public QBuffer
{
public:
    explicit CacheDevice(QNetworkCacheMetaData value, QObject *parent)
        : QBuffer(parent), metadata(std::move(value))
    {
        open(QIODevice::ReadWrite);
    }
    QNetworkCacheMetaData metadata;
    bool failed = false;

protected:
    qint64 writeData(const char *bytes, qint64 length) override
    {
        if (failed || length > MaximumEntrySize - pos()) {
            failed = true;
            return -1;
        }
        return QBuffer::writeData(bytes, length);
    }
};
} // namespace

// QNetworkDiskCache cannot share files between instances or cross QObject
// threads. Keep exactly one native owner; adapters exchange bounded encoded
// bytes. Only background QML managers may wait for disk work, never the GUI.
class ImageDiskCacheOwner : public std::enable_shared_from_this<ImageDiskCacheOwner>
{
public:
    ImageDiskCacheOwner(QString directory, qint64 maximumCacheSize)
        : m_directory(std::move(directory)), m_maximumCacheSize(qMax<qint64>(1, maximumCacheSize))
    {
        m_delivery = new QObject;
        m_worker = new QObject;
        m_worker->moveToThread(&m_thread);
        QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        m_thread.setObjectName("ImageDiskCache");
        m_thread.start();
    }
    ~ImageDiskCacheOwner()
    {
        m_thread.quit();
        m_thread.wait();
        delete m_delivery;
    }
    template <typename F> auto call(F action)
    {
        Q_ASSERT(!QThread::isMainThread());
        using Result = std::invoke_result_t<F, QNetworkDiskCache &>;
        Result result{};
        QMetaObject::invokeMethod(
            m_worker,
            [this, &result, &action] {
                initialize();
                result = action(*m_disk);
            },
            Qt::BlockingQueuedConnection);
        return result;
    }
    QNetworkReply *download(const QNetworkRequest &request, qint64 maximum, QObject *parent)
    {
        Q_ASSERT(QThread::isMainThread());
        auto job = std::make_shared<ImageDownload>();
        auto cancel = [owner = shared_from_this(), job] {
            QMetaObject::invokeMethod(owner->m_worker, [job] {
                if (job->reply)
                    job->reply->abort();
            });
        };
        auto *result = new ImageReply(request, parent, job, std::move(cancel));
        const QPointer<ImageReply> guard(result);
        QMetaObject::invokeMethod(m_worker, [this, job, request, maximum, guard] {
            if (job->cancelled)
                return;
            initialize();
            if (!m_network) {
                m_network = new QNetworkAccessManager(m_worker);
                m_network->setCache(m_disk);
            }
            auto *reply = m_network->get(request);
            job->reply = reply;
            reply->setReadBufferSize(65536);
            QObject::connect(reply, &QNetworkReply::readyRead, reply, [job, maximum] {
                job->bytes.append(job->reply->read(maximum + 1 - job->bytes.size()));
                if (job->bytes.size() > maximum || job->cancelled)
                    job->reply->abort();
            });
            QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                             [this, guard](qint64 received, qint64 total) {
                                 QMetaObject::invokeMethod(m_delivery, [guard, received, total] {
                                     if (guard && !guard->isFinished())
                                         emit guard->downloadProgress(received, total);
                                 });
                             });
            QObject::connect(
                reply, &QNetworkReply::finished, reply, [this, job, reply, maximum, guard] {
                    reply->deleteLater();
                    if (job->cancelled)
                        return;
                    if (reply->isOpen())
                        job->bytes.append(reply->read(maximum + 1 - job->bytes.size()));
                    QHash<QNetworkRequest::Attribute, QVariant> attributes;
                    for (const auto attribute : {QNetworkRequest::HttpStatusCodeAttribute,
                                                 QNetworkRequest::HttpReasonPhraseAttribute,
                                                 QNetworkRequest::SourceIsFromCacheAttribute,
                                                 QNetworkRequest::RedirectionTargetAttribute,
                                                 QNetworkRequest::ConnectionEncryptedAttribute})
                        attributes.insert(attribute, reply->attribute(attribute));
                    const auto url = reply->url();
                    const auto headers = reply->rawHeaderPairs();
                    const bool oversized = job->bytes.size() > maximum;
                    const auto error =
                        oversized ? QNetworkReply::UnknownContentError : reply->error();
                    const auto message = oversized
                                             ? QStringLiteral("Image response exceeds byte budget")
                                             : reply->errorString();
                    QMetaObject::invokeMethod(m_delivery, [guard, url, headers, attributes,
                                                           bytes = std::move(job->bytes), error,
                                                           message]() mutable {
                        if (guard)
                            guard->complete(url, headers, attributes, std::move(bytes), error,
                                            message);
                    });
                });
        });
        return result;
    }

private:
    void initialize()
    {
        if (m_disk)
            return;
        m_disk = new ImageDiskCache(m_worker);
        m_disk->setCacheDirectory(m_directory);
        m_disk->setMaximumCacheSize(m_maximumCacheSize);
        m_disk->cacheSize();
    }
    const QString m_directory;
    const qint64 m_maximumCacheSize;
    QThread m_thread;
    QObject *m_worker = nullptr;
    QObject *m_delivery = nullptr;
    QNetworkDiskCache *m_disk = nullptr;
    QNetworkAccessManager *m_network = nullptr;
};

namespace
{
class ImageDiskCacheAdapter final : public QAbstractNetworkCache
{
public:
    ImageDiskCacheAdapter(std::shared_ptr<ImageDiskCacheOwner> owner, QObject *parent)
        : QAbstractNetworkCache(parent), m_owner(std::move(owner))
    {
    }
    qint64 cacheSize() const override
    {
        return m_owner->call([](auto &disk) { return disk.cacheSize(); });
    }
    QNetworkCacheMetaData metaData(const QUrl &url) override
    {
        return m_owner->call([&](auto &disk) { return disk.metaData(url); });
    }
    QIODevice *data(const QUrl &url) override
    {
        const auto bytes = m_owner->call([&](auto &disk) {
            std::unique_ptr<QIODevice> device(disk.data(url));
            return device ? device->read(MaximumEntrySize + 1) : QByteArray{};
        });
        if (bytes.isEmpty() || bytes.size() > MaximumEntrySize)
            return nullptr;
        auto *buffer = new QBuffer;
        buffer->setData(bytes);
        buffer->open(QIODevice::ReadOnly);
        return buffer;
    }
    QIODevice *prepare(const QNetworkCacheMetaData &metadata) override
    {
        if (!metadata.isValid() || !metadata.saveToDisk() || !imageResponse(metadata))
            return nullptr;
        auto *device = new CacheDevice(metadata, this);
        m_pending.insert(device);
        return device;
    }
    void insert(QIODevice *device) override
    {
        auto *buffer = dynamic_cast<CacheDevice *>(device);
        if (!buffer || !m_pending.remove(buffer))
            return;
        if (!buffer->failed) {
            const auto metadata = buffer->metadata;
            const auto bytes = buffer->data();
            m_owner->call([&](auto &disk) {
                if (auto *output = disk.prepare(metadata)) {
                    if (output->write(bytes) == bytes.size())
                        disk.insert(output);
                    else
                        disk.remove(metadata.url());
                }
                return true;
            });
        }
        delete buffer;
    }
    bool remove(const QUrl &url) override
    {
        bool removed = false;
        const auto pending = m_pending;
        for (auto *device : pending)
            if (device->metadata.url() == url) {
                m_pending.remove(device);
                delete device;
                removed = true;
            }
        return m_owner->call([&](auto &disk) { return disk.remove(url); }) || removed;
    }
    void updateMetaData(const QNetworkCacheMetaData &metadata) override
    {
        m_owner->call([&](auto &disk) {
            disk.updateMetaData(metadata);
            return true;
        });
    }
    void clear() override
    {
        qDeleteAll(m_pending);
        m_pending.clear();
        m_owner->call([](auto &disk) {
            disk.clear();
            return true;
        });
    }

private:
    const std::shared_ptr<ImageDiskCacheOwner> m_owner;
    QSet<CacheDevice *> m_pending;
};
} // namespace
namespace
{
class ImageNetworkClient final : public QNetworkAccessManager
{
public:
    ImageNetworkClient(std::shared_ptr<ImageDiskCacheOwner> owner, qint64 maximum, QObject *parent)
        : QNetworkAccessManager(parent), m_owner(std::move(owner)), m_maximum(maximum)
    {
    }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoing) override
    {
        if (operation == GetOperation &&
            (request.url().scheme() == "http" || request.url().scheme() == "https"))
            return m_owner->download(request, m_maximum, this);
        return QNetworkAccessManager::createRequest(operation, request, outgoing);
    }

private:
    const std::shared_ptr<ImageDiskCacheOwner> m_owner;
    const qint64 m_maximum;
};
} // namespace
QmlImageNetworkFactory::QmlImageNetworkFactory(QString directory, qint64 maximumCacheSize)
    : m_cache(std::make_shared<ImageDiskCacheOwner>(std::move(directory), maximumCacheSize))
{
}
QmlImageNetworkFactory::~QmlImageNetworkFactory() = default;
QNetworkAccessManager *QmlImageNetworkFactory::create(QObject *parent)
{
    auto *manager = new QNetworkAccessManager(parent);
    // XMLHttpRequest on the GUI must never synchronously read disk. Qt Quick
    // HTTP artwork uses its background pixmap reader and receives the cache.
    if (!QThread::isMainThread())
        manager->setCache(new ImageDiskCacheAdapter(m_cache, manager));
    return manager;
}
QNetworkAccessManager *QmlImageNetworkFactory::createImageClient(QObject *parent, qint64 maximum)
{
    Q_ASSERT(QThread::isMainThread());
    return new ImageNetworkClient(m_cache, qBound<qint64>(1, maximum, MaximumEntrySize), parent);
}
} // namespace EchoMusic
