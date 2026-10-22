#pragma once
#include <QQuickItem>
#include <QImage>
#include <atomic>
#include <QTextLayout>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Native shaped-text renderer. QTextLayout owns wrapping/fallback/kerning;
/// cached word masks own no glyph metrics and are bounded to visible delegates.
/// Frame ticks change GPU uniforms/transforms, never reshape or upload glyphs.
class LyricLine : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Qt::Alignment alignment READ alignment WRITE setAlignment NOTIFY layoutChanged)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY layoutChanged)
    Q_PROPERTY(qreal textWidth READ textWidth NOTIFY layoutChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList glyphs READ glyphs NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList words READ words WRITE setWords NOTIFY layoutChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY layoutChanged)
    Q_PROPERTY(qreal timeMs READ timeMs WRITE setTimeMs NOTIFY timeChanged)
    Q_PROPERTY(qint64 startMs READ startMs WRITE setStartMs NOTIFY layoutChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs WRITE setDurationMs NOTIFY layoutChanged)
    Q_PROPERTY(qreal textHeight READ textHeight NOTIFY layoutChanged)
    Q_PROPERTY(qreal paintPadding READ paintPadding NOTIFY layoutChanged)
    Q_PROPERTY(bool current READ current WRITE setCurrent NOTIFY timeChanged)
    Q_PROPERTY(QColor playedColor READ playedColor WRITE setPlayedColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor unplayedColor READ unplayedColor WRITE setUnplayedColor NOTIFY colorsChanged)
    Q_PROPERTY(qreal fadeWidth READ fadeWidth WRITE setFadeWidth NOTIFY colorsChanged)
    Q_PROPERTY(
        qreal highlightAmount READ highlightAmount WRITE setHighlightAmount NOTIFY colorsChanged)
    Q_PROPERTY(bool animateWords READ animateWords WRITE setAnimateWords NOTIFY colorsChanged)
public:
    explicit LyricLine(QQuickItem *parent = nullptr);
    ~LyricLine() override
    {
        disconnect(this, nullptr, this, nullptr);
    }
    // CPU snapshot helper for typography tests; production uses the scene graph.
    void paint(QPainter *);
    quint64 textureUploads() const
    {
        return m_textureUploads.load(std::memory_order_relaxed);
    }
    int renderBatches() const
    {
        return m_renderBatches.load(std::memory_order_relaxed);
    }
    quint64 maskRevision() const
    {
        return m_maskRevision;
    }

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

public:
    Qt::Alignment alignment() const
    {
        return m_alignment;
    }
    void setAlignment(Qt::Alignment);
    bool wrap() const
    {
        return m_wrap;
    }
    void setWrap(bool);
    qreal textWidth() const
    {
        return m_textWidth;
    }
    Q_INVOKABLE qreal readingPosition(qreal timeMs) const;
    QString text() const
    {
        return m_text;
    }
    void setText(const QString &);
    QVariantList glyphs() const;
    QVariantList words() const
    {
        return m_words;
    }
    void setWords(const QVariantList &);
    QFont font() const
    {
        return m_font;
    }
    void setFont(const QFont &);
    qreal timeMs() const
    {
        return m_time;
    }
    void setTimeMs(qreal);
    qint64 startMs() const
    {
        return m_start;
    }
    void setStartMs(qint64);
    qint64 durationMs() const
    {
        return m_duration;
    }
    void setDurationMs(qint64);
    qreal textHeight() const
    {
        return m_textHeight;
    }
    qreal paintPadding() const
    {
        return m_paintPadding;
    }
    bool current() const
    {
        return m_current;
    }
    void setCurrent(bool);
    QColor playedColor() const
    {
        return m_played;
    }
    void setPlayedColor(const QColor &);
    QColor unplayedColor() const
    {
        return m_unplayed;
    }
    void setUnplayedColor(const QColor &);
    qreal fadeWidth() const
    {
        return m_fadeWidth;
    }
    void setFadeWidth(qreal);
    qreal highlightAmount() const
    {
        return m_highlightAmount;
    }
    void setHighlightAmount(qreal);
    bool animateWords() const
    {
        return m_animateWords;
    }
    void setAnimateWords(bool);
signals:
    void layoutChanged();
    void timeChanged();
    void colorsChanged();

private:
    friend class LyricLineTestAccess;
    void layout();
    void rebuildTimeline();
    void rebuildMasks();
    struct Word
    {
        int start, length;
        qint64 time, duration;
    };
    struct Fragment
    {
        QRectF rect;
        // Temporary atlas staging; released after the page is assembled.
        QImage packed;
        int atlas = 0;
        QRect atlasPixels;
        Word word;
        bool rtl;
        int character = 0, characters = 1;
        bool emphasis = false, last = false;
        qreal readingOrigin = 0;
    };
    struct Pose
    {
        qreal edge, fade, lift, scale, push, halo;
    };
    Pose pose(const Fragment &) const;
    struct SweepWord
    {
        Word word;
        qreal width;
    };
    qreal revealDistance(qreal fade) const;
    QList<SweepWord> m_sweep;
    std::atomic<quint64> m_textureUploads{0};
    std::atomic<int> m_renderBatches{0};
    QList<QImage> m_atlases;
    mutable QVariantList m_publicGlyphs;
    mutable quint64 m_publicGlyphRevision = ~quint64(0);
    quint64 m_maskRevision = 0;
    Qt::Alignment m_alignment = Qt::AlignLeft;
    QString m_text;
    QVariantList m_words;
    bool m_hasWordTiming = false;
    QList<Word> m_timeline;
    QList<Fragment> m_fragments;
    QFont m_font;
    QTextLayout m_layout;
    qreal m_maskDevicePixelRatio = 1;
    qreal m_time = -1;
    qint64 m_start = 0, m_duration = 0;
    bool m_current = false, m_animateWords = true, m_wrap = true;
    qreal m_textWidth = 0;
    qreal m_fadeWidth = .5, m_textHeight = 0, m_paintPadding = 0, m_highlightAmount = 1;
    QColor m_played{Qt::white}, m_unplayed{255, 255, 255, 102};
};
} // namespace EchoMusic
