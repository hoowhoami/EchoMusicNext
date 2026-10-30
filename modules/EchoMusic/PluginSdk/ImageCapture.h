#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace EchoMusic
{
class ImageCapture;
struct ImageCaptureJob;
struct ImageCaptureArtifact;
struct ImageCaptureSlot;

/// One immutable encoded image. dispose() invalidates the URL and releases its file asynchronously.
class ImageCaptureLease final : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QUrl url READ url NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(qint64 encodedBytes READ encodedBytes NOTIFY changed)
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)
public:
    ~ImageCaptureLease() override;
    QString status() const
    {
        return m_status;
    }
    bool ready() const
    {
        return m_status == "ready";
    }
    QUrl url() const
    {
        return m_url;
    }
    QString errorString() const
    {
        return m_error;
    }
    qint64 encodedBytes() const;
    int width() const
    {
        return m_width;
    }
    int height() const
    {
        return m_height;
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class ImageCapture;
    explicit ImageCaptureLease(ImageCapture *, int, int);
    QPointer<ImageCapture> m_owner;
    QString m_status = "pending", m_error;
    QUrl m_url;
    int m_width = 0, m_height = 0;
    bool m_counted = false;
    std::weak_ptr<ImageCaptureJob> m_job;
    std::shared_ptr<ImageCaptureSlot> m_slot;
    std::shared_ptr<ImageCaptureArtifact> m_artifact;
};

/// One-shot scenegraph capture followed by bounded background JPEG/PNG encoding.
/// width/height always specify final physical pixels, independent of window DPR.
class ImageCapture : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int pendingJobs READ pendingJobs NOTIFY changed)
    Q_PROPERTY(int leaseCount READ leaseCount NOTIFY changed)
    Q_PROPERTY(qint64 reservedBytes READ reservedBytes NOTIFY changed)
    Q_PROPERTY(qint64 encodedBytes READ encodedBytes NOTIFY changed)
    Q_PROPERTY(bool drained READ isDrained NOTIFY changed)
    Q_PROPERTY(qulonglong readbacks READ readbacks NOTIFY changed)
    Q_PROPERTY(qulonglong encodes READ encodes NOTIFY changed)
public:
    static constexpr int MaxDimension = 4096;
    static constexpr qint64 MaxPixels = 8LL * 1024 * 1024;
    static constexpr int MaxJobs = 2;
    static constexpr int MaxLeases = 16;
    static constexpr qint64 MaxReservedBytes = 128LL * 1024 * 1024;
    static constexpr qint64 MaxEncodedBytes = 16LL * 1024 * 1024;
    static constexpr qint64 MaxTotalEncodedBytes = 64LL * 1024 * 1024;
    struct BudgetSnapshot
    {
        int pendingJobs = 0, leaseCount = 0;
        qint64 reservedBytes = 0, encodedBytes = 0, outputReservedBytes = 0;
    };
    explicit ImageCapture(QObject *parent = nullptr);
    ~ImageCapture() override;
    bool active() const;
    void setActive(bool);
    int pendingJobs() const;
    int leaseCount() const;
    qint64 reservedBytes() const;
    qint64 encodedBytes() const;
    bool isDrained() const;
    qulonglong readbacks() const;
    qulonglong encodes() const;
    static BudgetSnapshot budgetSnapshot();
    Q_INVOKABLE EchoMusic::ImageCaptureLease *encode(QQuickItem *sourceItem, int width, int height,
                                                     const QString &format = "jpeg",
                                                     int quality = 92);
signals:
    void activeChanged();
    void changed();

private:
    friend class ImageCaptureLease;
    struct Data;
    std::unique_ptr<Data> d;
    void releaseLease(ImageCaptureLease *);
    void cancel(const std::shared_ptr<ImageCaptureJob> &, const QString &error);
    void readbackReady(const std::shared_ptr<ImageCaptureJob> &);
    void complete(const std::shared_ptr<ImageCaptureJob> &,
                  const std::shared_ptr<ImageCaptureArtifact> &, const QString &error);
};
} // namespace EchoMusic
