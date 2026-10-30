#pragma once
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class EchoPluginAssets;
/// A presentation-owned reference to asynchronously prepared image frames.
class EchoPluginAssetAnimation final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by assets.animation")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(QUrl frameSource READ frameSource NOTIFY frameChanged)
    Q_PROPERTY(int frameIndex READ frameIndex NOTIFY frameChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(int loopCount READ loopCount NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(qint64 encodedBytes READ encodedBytes NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
public:
    ~EchoPluginAssetAnimation() override;
    bool active() const
    {
        return m_active;
    }
    bool ready() const
    {
        return m_ready;
    }
    bool playing() const
    {
        return m_playing;
    }
    QUrl frameSource() const
    {
        return m_frameSource;
    }
    int frameIndex() const
    {
        return m_frameIndex;
    }
    int frameCount() const
    {
        return m_frameCount;
    }
    qint64 durationMs() const
    {
        return m_duration;
    }
    int loopCount() const
    {
        return m_loopCount;
    }
    int width() const
    {
        return m_width;
    }
    int height() const
    {
        return m_height;
    }
    qint64 encodedBytes() const
    {
        return m_encodedBytes;
    }
    QString errorString() const
    {
        return m_error;
    }
    Q_INVOKABLE void setPlaying(bool playing);
    Q_INVOKABLE void dispose();
signals:
    void changed();
    void playingChanged();
    void frameChanged();

private:
    friend class EchoPluginAssets;
    explicit EchoPluginAssetAnimation(EchoPluginAssets &assets, QString key);
    QPointer<EchoPluginAssets> m_assets;
    QString m_key, m_error;
    QUrl m_frameSource;
    bool m_active = true, m_ready = false, m_playing = true;
    int m_frameIndex = -1, m_frameCount = 0, m_width = 0, m_height = 0;
    qint64 m_encodedBytes = 0;
    qint64 m_duration = 0;
    int m_loopCount = -1;
};
} // namespace EchoMusic
