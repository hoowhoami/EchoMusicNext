#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QSize>
#include <QSharedPointer>
#include <QUrl>
#include <QTimer>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class EchoItemObserver;
// Artwork density follows the native item scale chain, not scrolling positions.
// Keep one bounded high-water resolution per visible source to avoid decoding
// smaller copies at every step of a collapsing header.
class EchoImageMetrics : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QQuickItem *item READ item WRITE setItem NOTIFY itemChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QSizeF minimumSize READ minimumSize WRITE setMinimumSize NOTIFY minimumSizeChanged)
    Q_PROPERTY(int maximumDecodeSize READ maximumDecodeSize WRITE setMaximumDecodeSize NOTIFY
                   maximumDecodeSizeChanged)
    Q_PROPERTY(
        bool deferRequests READ deferRequests WRITE setDeferRequests NOTIFY deferRequestsChanged)
    Q_PROPERTY(QUrl requestSource READ requestSource NOTIFY requestChanged)
    Q_PROPERTY(QSize pixelSize READ pixelSize NOTIFY metricsChanged)
    Q_PROPERTY(QSize decodeSize READ decodeSize NOTIFY decodeSizeChanged)
    Q_PROPERTY(QSize deviceSize READ deviceSize NOTIFY metricsChanged)
    Q_PROPERTY(QUrl resolvedSource READ resolvedSource NOTIFY requestChanged)
public:
    explicit EchoImageMetrics(QObject *parent = nullptr);
    void classBegin() override
    {
        m_complete = false;
    }
    void componentComplete() override;
    ~EchoImageMetrics() override;
    QQuickItem *item() const
    {
        return m_item;
    }
    bool active() const
    {
        return m_active;
    }
    QUrl source() const
    {
        return m_source;
    }
    QSize pixelSize() const
    {
        return m_pixels;
    }
    QSize deviceSize() const
    {
        return m_deviceSize;
    }
    QSizeF minimumSize() const
    {
        return m_minimumSize;
    }
    QSize decodeSize() const
    {
        return m_decodePixels;
    }
    QUrl resolvedSource() const
    {
        return m_resolved;
    }
    QUrl requestSource() const
    {
        return m_requestSource;
    }
    int maximumDecodeSize() const
    {
        return m_maximumDecodeSize;
    }
    bool deferRequests() const
    {
        return m_deferRequests;
    }
    void setMaximumDecodeSize(int size);
    void setDeferRequests(bool defer);
    void setItem(QQuickItem *item);
    void setActive(bool active);
    void setSource(const QUrl &source);
    void setMinimumSize(const QSizeF &size);

signals:
    void itemChanged();
    void activeChanged();
    void sourceChanged();
    void minimumSizeChanged();
    void metricsChanged();
    void requestChanged();
    void decodeSizeChanged();
    void maximumDecodeSizeChanged();
    void deferRequestsChanged();

private:
    void rebuild();
    void updateSize();
    void scheduleRequest();
    void publishRequest();
    QUrl resolveSource(const QUrl &source, const QSize &size) const;
    QPointer<QQuickItem> m_item;
    QSharedPointer<EchoItemObserver> m_observer;
    QList<QMetaObject::Connection> m_connections;
    QUrl m_source, m_requestSource, m_resolved;
    QTimer m_requestTimer;
    QSize m_pendingDecode;
    int m_maximumDecodeSize = 1600;
    bool m_deferRequests = false, m_complete = true, m_resetDecode = true;
    QSize m_pixels, m_decodePixels, m_deviceSize;
    QSizeF m_minimumSize;
    bool m_active = true;
};
} // namespace EchoMusic
