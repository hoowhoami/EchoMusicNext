#include "PlaybackArtwork.h"
#include <QImageReader>
#include <QBuffer>
#include <QFile>
#include <QtConcurrentRun>
#include <QRegularExpression>
#include <cmath>
#include <array>
#include <algorithm>
namespace EchoMusic
{
static ArtworkResult decode(const QByteArray &bytes)
{
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
        return {};
    reader.setScaledSize(size.scaled(300, 300, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    ArtworkResult result;
    QBuffer output(&result.png);
    output.open(QIODevice::WriteOnly);
    image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&output, "PNG");
    // Use 18 hue buckets, excluding near-black, white and gray. Score by
    // saturation times lightness, then take the winning bucket's weighted mean.
    struct Bucket
    {
        double weight = 0, r = 0, g = 0, b = 0;
    };
    std::array<Bucket, 18> buckets;
    const QImage sample = image.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < sample.height(); ++y)
        for (int x = 0; x < sample.width(); ++x) {
            const QColor c = sample.pixelColor(x, y);
            if (c.alpha() < 200 || c.lightnessF() < .15 || c.lightnessF() > .92 ||
                c.hslSaturationF() < .18)
                continue;
            const double score =
                c.hslSaturationF() * std::max(.15, 1 - std::abs(c.lightnessF() - .5) * 1.2);
            auto &b = buckets[size_t(std::clamp(int(c.hslHueF() * 18), 0, 17))];
            b.weight += score;
            b.r += c.red() * score;
            b.g += c.green() * score;
            b.b += c.blue() * score;
        }
    const auto best =
        std::max_element(buckets.begin(), buckets.end(),
                         [](const auto &a, const auto &b) { return a.weight < b.weight; });
    if (best->weight > 0)
        result.dominant = QColor(qRound(best->r / best->weight), qRound(best->g / best->weight),
                                 qRound(best->b / best->weight));
    return result;
}
PlaybackArtwork::PlaybackArtwork(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}
PlaybackArtwork::~PlaybackArtwork()
{
    cancel();
    m_pool.waitForDone();
}
void PlaybackArtwork::cancel()
{
    ++m_generation;
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_pool.clear();
}
void PlaybackArtwork::load(const QString &source, quint64 sequence)
{
    cancel();
    m_sequence = sequence;
    const auto generation = m_generation;
    QString url = source;
    url.replace("{size}", "300");
    url.replace(QRegularExpression("/\\d+(?=/\\d{8}/)"), "/300");
    const QUrl u(url);
    if (u.isLocalFile()) {
        auto *watcher = new QFutureWatcher<ArtworkResult>(this);
        connect(watcher, &QFutureWatcher<ArtworkResult>::finished, this,
                [this, watcher, sequence, generation] {
                    watcher->deleteLater();
                    if (m_sequence != sequence || m_generation != generation ||
                        watcher->future().isCanceled())
                        return;
                    const auto result = watcher->result();
                    emit ready(sequence, result.png, result.dominant);
                });
        watcher->setFuture(QtConcurrent::run(&m_pool, [path = u.toLocalFile()] {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
                return ArtworkResult{};
            return decode(file.readAll());
        }));
        return;
    }
    if (u.scheme() != "https" && u.scheme() != "http") {
        // load() may run in the middle of track adoption. Deliver even an empty
        // source asynchronously, and let a replacement/cancel retire this result.
        QMetaObject::invokeMethod(
            this,
            [this, sequence, generation] {
                if (m_sequence == sequence && m_generation == generation)
                    emit ready(sequence, {}, {});
            },
            Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(u);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > 2 * 1024 * 1024 || total > 2 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, sequence, generation] {
        reply->deleteLater();
        if (m_sequence != sequence || m_generation != generation || m_reply != reply)
            return;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            emit ready(sequence, {}, {});
            return;
        }
        const auto bytes = reply->readAll();
        if (bytes.size() > 2 * 1024 * 1024) {
            emit ready(sequence, {}, {});
            return;
        }
        auto *watcher = new QFutureWatcher<ArtworkResult>(this);
        connect(watcher, &QFutureWatcher<ArtworkResult>::finished, this,
                [this, watcher, sequence, generation] {
                    watcher->deleteLater();
                    if (m_sequence != sequence || m_generation != generation ||
                        watcher->future().isCanceled())
                        return;
                    const auto result = watcher->result();
                    emit ready(sequence, result.png, result.dominant);
                });
        watcher->setFuture(QtConcurrent::run(&m_pool, [bytes] { return decode(bytes); }));
    });
}
} // namespace EchoMusic
