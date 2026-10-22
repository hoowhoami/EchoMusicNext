#include "LyricLine.h"
#include <QTextBoundaryFinder>
#include "LyricMaterial.h"
#include <QSGGeometryNode>
#include <QEasingCurve>
#include <QTextBoundaryFinder>
#include <QPainter>
#include <QGlyphRun>
#include <QQuickWindow>
#include <QFontInfo>
#include <QGuiApplication>
#include <algorithm>
#include <numeric>
#include <cmath>
namespace EchoMusic
{
namespace
{
// Cache a soft alpha halo once per shaped word. No glyph geometry is inferred;
// frame painting only composites this image alongside that word's native mask.
QImage wordHalo(const QImage &mask, const QColor &color, int radius)
{
    const int width = mask.width(), height = mask.height();
    QVector<int> alpha(width * height), scratch(alpha.size());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            alpha[y * width + x] = qAlpha(mask.pixel(x, y));
    const int kernel = radius * 2 + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height; ++y) {
            int sum = 0;
            for (int x = 0; x <= radius && x < width; ++x)
                sum += alpha[y * width + x];
            for (int x = 0; x < width; ++x) {
                scratch[y * width + x] = sum / kernel;
                if (x - radius >= 0)
                    sum -= alpha[y * width + x - radius];
                if (x + radius + 1 < width)
                    sum += alpha[y * width + x + radius + 1];
            }
        }
        for (int x = 0; x < width; ++x) {
            int sum = 0;
            for (int y = 0; y <= radius && y < height; ++y)
                sum += scratch[y * width + x];
            for (int y = 0; y < height; ++y) {
                alpha[y * width + x] = sum / kernel;
                if (y - radius >= 0)
                    sum -= scratch[(y - radius) * width + x];
                if (y + radius + 1 < height)
                    sum += scratch[(y + radius + 1) * width + x];
            }
        }
    }
    QImage result(mask.size(), QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(mask.devicePixelRatio());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            result.setPixel(x, y,
                            qPremultiply(qRgba(color.red(), color.green(), color.blue(),
                                               alpha[y * width + x] * color.alpha() / 255)));
    return result;
}
} // namespace
LyricLine::LyricLine(QQuickItem *parent) : QQuickItem(parent)
{
    m_font = QGuiApplication::font();
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::widthChanged, this, &LyricLine::layout);
    connect(this, &QQuickItem::windowChanged, this, &LyricLine::rebuildMasks);
}
void LyricLine::setAlignment(Qt::Alignment value)
{
    value &= Qt::AlignHorizontal_Mask;
    if (value == m_alignment)
        return;
    m_alignment = value;
    layout();
}
void LyricLine::setWrap(bool value)
{
    if (m_wrap == value)
        return;
    m_wrap = value;
    layout();
}
qreal LyricLine::readingPosition(qreal timeMs) const
{
    if (m_timeline.isEmpty())
        return m_paintPadding;
    const Word *active = &m_timeline.first();
    for (const auto &word : m_timeline) {
        if (timeMs < word.time)
            break;
        active = &word;
    }
    const auto shaped = m_layout.lineForTextPosition(active->start);
    if (!shaped.isValid())
        return m_paintPadding;
    const qreal progress = active->duration > 0
                               ? qBound(0., (timeMs - active->time) / active->duration, 1.)
                           : timeMs >= active->time ? 1.
                                                    : 0.;
    // Native cursor positions preserve contextual shaping, bidi and fallback.
    // This is a read-only sweep coordinate, never a new upstream word time.
    const qreal from = shaped.cursorToX(active->start),
                to = shaped.cursorToX(active->start + active->length);
    return from + (to - from) * progress;
}
void LyricLine::setText(const QString &value)
{
    if (m_text == value)
        return;
    m_text = value;
    rebuildTimeline();
    layout();
}
void LyricLine::setWords(const QVariantList &value)
{
    if (m_words == value)
        return;
    m_words = value;
    rebuildTimeline();
    rebuildMasks();
    emit layoutChanged();
}
void LyricLine::rebuildTimeline()
{
    m_timeline.clear();
    int cursor = 0;
    for (const auto &entry : m_words) {
        const auto word = entry.toMap();
        const auto text = word.value("text").toString();
        if (text.isEmpty() || m_text.mid(cursor, text.size()) != text) {
            m_timeline.clear();
            break;
        }
        m_timeline.append({cursor, int(text.size()), word.value("start_ms").toLongLong(),
                           word.value("duration_ms").toLongLong()});
        cursor += int(text.size());
    }
    // Partial or mismatched timing must fall back to a complete line, never
    // disappear or assign another word's timing to an unmatched suffix.
    if (cursor != m_text.size())
        m_timeline.clear();
    m_hasWordTiming = std::any_of(m_timeline.cbegin(), m_timeline.cend(),
                                  [](const Word &word) { return word.duration > 0; });
    // Line timing remains useful for scrolling long lines. It never authorizes
    // word coloring or syllable motion when the source has no real word times.
    if (m_timeline.isEmpty() && !m_text.isEmpty() && m_duration > 0)
        m_timeline.append({0, int(m_text.size()), m_start, m_duration});
}
void LyricLine::setFont(const QFont &value)
{
    if (m_font == value)
        return;
    m_font = value;
    layout();
}
void LyricLine::setTimeMs(qreal value)
{
    if (!std::isfinite(value) || m_time == value)
        return;
    m_time = value;
    update();
    emit timeChanged();
}
void LyricLine::setStartMs(qint64 value)
{
    if (m_start == value)
        return;
    m_start = value;
    rebuildTimeline();
    rebuildMasks();
    emit layoutChanged();
}
void LyricLine::setDurationMs(qint64 value)
{
    value = std::max<qint64>(0, value);
    if (m_duration == value)
        return;
    m_duration = value;
    rebuildTimeline();
    rebuildMasks();
    emit layoutChanged();
}
void LyricLine::setCurrent(bool value)
{
    if (m_current == value)
        return;
    m_current = value;
    update();
    emit timeChanged();
}
void LyricLine::setPlayedColor(const QColor &value)
{
    if (m_played == value)
        return;
    m_played = value;
    update();
    emit colorsChanged();
}
void LyricLine::setUnplayedColor(const QColor &value)
{
    if (m_unplayed == value)
        return;
    m_unplayed = value;
    update();
    emit colorsChanged();
}
void LyricLine::setFadeWidth(qreal value)
{
    if (!std::isfinite(value))
        return;
    value = qBound(.05, value, 1.);
    if (qFuzzyCompare(m_fadeWidth, value))
        return;
    m_fadeWidth = value;
    update();
    emit colorsChanged();
}
void LyricLine::setHighlightAmount(qreal value)
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, 0., 1.);
    if (qFuzzyCompare(value, m_highlightAmount))
        return;
    m_highlightAmount = value;
    update();
    emit colorsChanged();
}
void LyricLine::setAnimateWords(bool value)
{
    if (m_animateWords == value)
        return;
    m_animateWords = value;
    update();
    emit colorsChanged();
}
QVariantList LyricLine::glyphs() const
{
    if (m_publicGlyphRevision == m_maskRevision)
        return m_publicGlyphs;
    m_publicGlyphRevision = m_maskRevision;
    m_publicGlyphs.clear();
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, m_text);
    int start = 0;
    while (start < m_text.size()) {
        boundaries.setPosition(start);
        int end = boundaries.toNextBoundary();
        if (end < 0)
            end = m_text.size();
        while (end < m_text.size()) {
            const auto runs = m_layout.glyphRuns(start, end - start);
            const bool split = !m_layout.isValidCursorPosition(end) ||
                               std::any_of(runs.cbegin(), runs.cend(), [](const QGlyphRun &r) {
                                   return r.flags().testFlag(QGlyphRun::SplitLigature);
                               });
            if (!split)
                break;
            boundaries.setPosition(end);
            const int next = boundaries.toNextBoundary();
            if (next <= end)
                break;
            end = next;
        }
        QRectF bounds;
        for (int i = 0; i < m_layout.lineCount(); ++i) {
            auto line = m_layout.lineAt(i);
            if (start >= line.textStart() + line.textLength() || end <= line.textStart())
                continue;
            const int a = qMax(start, line.textStart()),
                      b = qMin(end, line.textStart() + line.textLength());
            const qreal x1 = line.cursorToX(a), x2 = line.cursorToX(b);
            bounds = bounds.united(QRectF(line.x() + qMin(x1, x2), line.y(),
                                          qMax<qreal>(.01, qAbs(x2 - x1)), line.height()));
        }
        QRectF ink;
        bool rtl = false;
        for (const auto &run : m_layout.glyphRuns(start, end - start)) {
            ink = ink.united(run.boundingRect());
            rtl = rtl || run.flags().testFlag(QGlyphRun::RightToLeft);
        }
        QVariantList wordIndices;
        int word = -1;
        qint64 time = m_start, duration = m_duration;
        for (int i = 0; i < m_timeline.size(); ++i) {
            const auto &w = m_timeline[i];
            if (start < w.start + w.length && end > w.start) {
                wordIndices.append(i);
                if (word < 0) {
                    word = i;
                    time = w.time;
                    duration = w.duration;
                } else {
                    const auto finish = qMax(time + duration, w.time + w.duration);
                    time = qMin(time, w.time);
                    duration = finish - time;
                }
            }
        }
        m_publicGlyphs.append(QVariantMap{{"text", m_text.mid(start, end - start)},
                                          {"utf16Start", start},
                                          {"utf16Length", end - start},
                                          {"rect", bounds},
                                          {"wordIndex", word},
                                          {"wordIndices", wordIndices},
                                          {"inkRect", ink},
                                          {"rtl", rtl},
                                          {"startMs", time},
                                          {"durationMs", duration}});
        start = end;
    }
    return m_publicGlyphs;
}
void LyricLine::layout()
{
    m_layout.setText(m_text);
    m_layout.setFont(m_font);
    QTextOption option;
    option.setWrapMode(m_wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(m_alignment);
    m_layout.setTextOption(option);
    m_layout.beginLayout();
    const qreal em = QFontInfo(m_font).pixelSize();
    m_paintPadding = em;
    const qreal lineHeight = em * 1.2;
    qreal y = m_paintPadding;
    m_textWidth = 0;
    while (true) {
        auto line = m_layout.createLine();
        if (!line.isValid())
            break;
        const qreal available = std::max<qreal>(1, width() - 2 * m_paintPadding);
        line.setLineWidth(available);
        const qreal natural = line.naturalTextWidth();
        m_textWidth = qMax(m_textWidth, natural);
        // An overflowing unwrapped line begins at its reading edge; alignment
        // still applies when the entire line fits the viewport.
        if (!m_wrap && natural > available)
            line.setLineWidth(natural);
        // CSS line-height is a font-size multiplier, not a multiplier of the
        // fallback face's natural line box (which is taller for CJK).
        line.setPosition(QPointF(m_paintPadding, y + (lineHeight - line.height()) / 2));
        y += lineHeight;
    }
    m_layout.endLayout();
    m_textHeight = y - m_paintPadding;
    setImplicitHeight(m_textHeight + 2 * m_paintPadding);
    rebuildMasks();
    emit layoutChanged();
}
void LyricLine::rebuildMasks()
{
    m_fragments.clear();
    m_atlases.clear();
    m_sweep.clear();
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.;
    m_maskDevicePixelRatio = dpr;
    // A cursor rectangle is not an ink boundary: an italic bearing, a CJK
    // stroke or a ligature may cross it. Render Qt's shaped glyph runs instead
    // of slicing copies of the entire line at cursor positions.
    auto timeline = m_timeline;
    for (int i = 0; i + 1 < timeline.size();) {
        const auto word = timeline[i];
        const int boundary = word.start + word.length;
        const auto runs = m_layout.glyphRuns(word.start, word.length);
        const bool split = !m_layout.isValidCursorPosition(boundary) ||
                           std::any_of(runs.cbegin(), runs.cend(), [](const QGlyphRun &run) {
                               return run.flags().testFlag(QGlyphRun::SplitLigature);
                           });
        if (split) {
            // One shaped cluster cannot move in two directions. Keep its
            // original contextual shaping and animate the joined timing span.
            const auto next = timeline.takeAt(i + 1);
            timeline[i].length += next.length;
            timeline[i].duration =
                std::max(word.time + word.duration, next.time + next.duration) - word.time;
        } else {
            ++i;
        }
    }
    const qreal padding = QFontInfo(m_font).pixelSize() * .4 + 2;
    if (timeline.isEmpty() && !m_text.isEmpty())
        timeline.append({0, int(m_text.size()), m_start, m_duration});
    qreal prefix = 0;
    for (const auto &word : timeline) {
        qreal advance = 0;
        for (int n = 0; n < m_layout.lineCount(); ++n) {
            const auto line = m_layout.lineAt(n);
            const int begin = std::max(word.start, line.textStart());
            const int end =
                std::min(word.start + word.length, line.textStart() + line.textLength());
            if (end > begin)
                advance += std::abs(line.cursorToX(end) - line.cursorToX(begin));
        }
        m_sweep.append({word, advance});
        QList<int> boundaries{word.start};
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, m_text);
        finder.setPosition(word.start);
        while (finder.toNextBoundary() > 0 && finder.position() < word.start + word.length) {
            const auto runs =
                m_layout.glyphRuns(boundaries.last(), finder.position() - boundaries.last());
            if (m_layout.isValidCursorPosition(finder.position()) &&
                std::none_of(runs.begin(), runs.end(), [](const QGlyphRun &r) {
                    return r.flags().testFlag(QGlyphRun::SplitLigature);
                }))
                boundaries.append(finder.position());
        }
        boundaries.append(word.start + word.length);
        const QString part = m_text.mid(word.start, word.length).trimmed();
        const bool cjk =
            std::all_of(part.begin(), part.end(), [](QChar c) { return c.unicode() >= 0x2e80; });
        const bool emphasize = !m_words.isEmpty() && word.duration >= 1000 &&
                               (cjk || (part.size() >= 2 && part.size() <= 7));
        if (!emphasize)
            boundaries = {word.start, word.start + word.length};
        for (int character = 0; character + 1 < boundaries.size(); ++character) {
            for (int n = 0; n < m_layout.lineCount(); ++n) {
                const auto line = m_layout.lineAt(n);
                const int start = std::max(boundaries[character], line.textStart());
                const int end =
                    std::min(boundaries[character + 1], line.textStart() + line.textLength());
                if (end <= start)
                    continue;
                const auto runs = line.glyphRuns(start, end - start);
                QRectF bounds;
                for (const auto &run : runs)
                    bounds |= run.boundingRect();
                if (bounds.isEmpty())
                    continue;
                Fragment fragment;
                const auto padded = bounds.adjusted(-padding, -padding, padding, padding);
                fragment.rect = QRectF(QPointF(std::floor(padded.left() * dpr) / dpr,
                                               std::floor(padded.top() * dpr) / dpr),
                                       QPointF(std::ceil(padded.right() * dpr) / dpr,
                                               std::ceil(padded.bottom() * dpr) / dpr));
                fragment.word = word;
                fragment.character = character;
                fragment.characters = boundaries.size() - 1;
                fragment.emphasis = emphasize;
                fragment.last = word.start + word.length == m_text.size();
                const int wordStart = std::max(word.start, line.textStart());
                const int wordEnd =
                    std::min(word.start + word.length, line.textStart() + line.textLength());
                const qreal left = std::min(line.cursorToX(wordStart), line.cursorToX(wordEnd));
                const qreal right = std::max(line.cursorToX(wordStart), line.cursorToX(wordEnd));
                qreal beforeLine = 0;
                for (int previous = 0; previous < n; ++previous) {
                    const auto prior = m_layout.lineAt(previous);
                    const int a = std::max(word.start, prior.textStart());
                    const int b =
                        std::min(word.start + word.length, prior.textStart() + prior.textLength());
                    if (b > a)
                        beforeLine += std::abs(prior.cursorToX(b) - prior.cursorToX(a));
                }
                fragment.readingOrigin = prefix + beforeLine;
                fragment.readingOrigin += line.cursorToX(end) < line.cursorToX(start)
                                              ? right - fragment.rect.right()
                                              : fragment.rect.left() - left;
                fragment.rtl = line.cursorToX(end) < line.cursorToX(start);
                const QSize pixels(qCeil(fragment.rect.width() * dpr),
                                   qCeil(fragment.rect.height() * dpr));
                QImage mask(pixels, QImage::Format_ARGB32_Premultiplied);
                mask.setDevicePixelRatio(dpr);
                mask.fill(Qt::transparent);
                QImage glow;
                {
                    QPainter painter(&mask);
                    painter.setPen(Qt::white);
                    for (const auto &run : runs)
                        painter.drawGlyphRun(-fragment.rect.topLeft(), run);
                }
                if (emphasize) {
                    const qreal ratio = qreal(word.duration) / 3000.;
                    const qreal strength =
                        std::min(.8, (ratio < 1 ? ratio * ratio * ratio : std::sqrt(ratio)) * .5 *
                                         (fragment.last ? 1.5 : 1.));
                    glow = wordHalo(
                        mask, Qt::white,
                        std::max(1, qCeil(QFontInfo(m_font).pixelSize() * dpr * strength * .15)));
                }
                fragment.packed = QImage(pixels, QImage::Format_RGBA8888);
                for (int py = 0; py < pixels.height(); ++py) {
                    auto *dst = fragment.packed.scanLine(py);
                    for (int px = 0; px < pixels.width(); ++px) {
                        dst[px * 4] = qAlpha(mask.pixel(px, py));
                        dst[px * 4 + 1] = glow.isNull() ? 0 : qAlpha(glow.pixel(px, py));
                        dst[px * 4 + 2] = 0;
                        dst[px * 4 + 3] = 255;
                    }
                }
                m_fragments.append(std::move(fragment));
            }
        }
        prefix += advance;
    }
    // Pack immutable masks once. Separate textures/uniforms per fragment force
    // one draw call per word/character, including every dimmed background row.
    // Shelf pages keep texture dimensions bounded independently of lyric length;
    // a single very wide fragment has the same size limit as its original mask.
    const int pageWidth = std::accumulate(
        m_fragments.cbegin(), m_fragments.cend(), 1024,
        [](int width, const Fragment &f) { return std::max(width, f.packed.width() + 2); });
    QList<QSize> sizes;
    int x = 0, y = 0, shelf = 0, page = 0;
    QSize used;
    for (auto &fragment : m_fragments) {
        const QSize size = fragment.packed.size() + QSize(2, 2);
        if (x + size.width() > pageWidth) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        if (y > 0 && y + size.height() > 2048) {
            sizes.append(used);
            ++page;
            x = y = shelf = 0;
            used = {};
        }
        fragment.atlas = page;
        fragment.atlasPixels = QRect(QPoint(x + 1, y + 1), fragment.packed.size());
        x += size.width();
        shelf = std::max(shelf, size.height());
        used = used.expandedTo(QSize(x, y + shelf));
    }
    if (!m_fragments.isEmpty())
        sizes.append(used);
    for (const auto &size : sizes) {
        QImage atlas(size, QImage::Format_RGBA8888);
        atlas.fill(Qt::transparent);
        m_atlases.append(std::move(atlas));
    }
    for (auto &fragment : m_fragments) {
        {
            QPainter painter(&m_atlases[fragment.atlas]);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(fragment.atlasPixels.topLeft(), fragment.packed);
        }
        // The atlas owns the immutable R/G masks now. Retaining per-fragment
        // coverage, halo and packed copies nearly triples the live CPU cache.
        fragment.packed = {};
    }
    ++m_maskRevision;
    update();
}
qreal LyricLine::revealDistance(qreal fade) const
{
    // One continuous brightness frontier follows native typographic advances.
    // It rests during timing gaps; a word boundary never restarts its gradient.
    qreal position = -1.5 * fade;
    for (int i = 0; i < m_sweep.size(); ++i) {
        const auto &entry = m_sweep[i];
        const qreal progress =
            entry.word.duration > 0
                ? std::clamp((m_time - entry.word.time) / entry.word.duration, 0., 1.)
                : (m_time >= entry.word.time ? 1. : 0.);
        const qreal travel =
            entry.width + (i == 0 ? 1.5 * fade : 0) + (i + 1 == m_sweep.size() ? .5 * fade : 0);
        position += travel * progress;
    }
    return position;
}
// The timeline is evaluated directly, including seek/pause. No animation is
// started by a telemetry event, and no character owns a second mutable clock.
LyricLine::Pose LyricLine::pose(const Fragment &f) const
{
    const qreal em = QFontInfo(m_font).pixelSize();
    const qreal duration = std::max<qint64>(1, f.word.duration);
    const qreal fade = std::max(1., em * 1.2 * m_fadeWidth);
    Pose p{revealDistance(fade) - f.readingOrigin, fade, 0, 1, 0, 0};
    if (!m_current || m_highlightAmount <= 0) {
        p.edge = -2 * fade;
        return p;
    }
    if (!m_hasWordTiming) {
        p.edge = m_layout.boundingRect().width() + m_layout.boundingRect().height() + 4 * fade;
        return p;
    }
    if (!m_animateWords)
        return p;
    const qreal t = qreal(m_time - f.word.time);
    QEasingCurve ease(QEasingCurve::BezierSpline);
    ease.addCubicBezierSegment({0, 0}, {.58, 1}, {1, 1});
    p.lift = -em * .05 * ease.valueForProgress(std::clamp(t / std::max(1000., duration), 0., 1.)) *
             m_highlightAmount;
    if (!f.emphasis)
        return p;
    const qreal length = std::max(1000., duration) * (f.last ? 1.2 : 1.);
    const qreal stagger = length / 2.5 / std::max(1, f.characters) * f.character;
    const qreal phase = std::clamp((t - stagger) / length, 0., 1.);
    QEasingCurve rise(QEasingCurve::BezierSpline), fall(QEasingCurve::BezierSpline);
    rise.addCubicBezierSegment({.2, .4}, {.58, 1}, {1, 1});
    fall.addCubicBezierSegment({.3, 0}, {.58, 1}, {1, 1});
    const qreal envelope =
        phase < .5 ? rise.valueForProgress(phase * 2) : 1 - fall.valueForProgress((phase - .5) * 2);
    auto intensity = [](qreal x) { return x < 1 ? x * x * x : std::sqrt(x); };
    const qreal amount = std::min(1.2, intensity(duration / 2000.) * .6 * (f.last ? 1.6 : 1.)) *
                         envelope * m_highlightAmount;
    p.scale = 1 + .1 * amount;
    p.push = -em * .03 * amount * (qreal(f.characters) / 2 - f.character) * p.scale;
    const qreal floatPhase = std::clamp((t - stagger + 400) / (length * 1.4), 0., 1.);
    p.lift =
        -em * (.05 * std::sin(floatPhase * M_PI) * m_highlightAmount + .025 * amount * p.scale);
    p.halo = std::min(.8, intensity(duration / 3000.) * .5 * (f.last ? 1.5 : 1.)) * envelope *
             m_highlightAmount;
    return p;
}
namespace
{
struct MaskVertex
{
    float x, y, u, v;
    float localX, edge, fade;
    float rtl, halo;
};
const QSGGeometry::AttributeSet &maskAttributes()
{
    static const QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
        QSGGeometry::Attribute::create(1, 2, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2, 3, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(3, 2, QSGGeometry::FloatType)};
    static const QSGGeometry::AttributeSet set{4, sizeof(MaskVertex), attributes};
    return set;
}
class MaskRoot final : public QSGNode
{
public:
    quint64 revision = ~quint64(0);
    QList<QSGGeometryNode *> pages;
};
QVector4D rgba(const QColor &c)
{
    return {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}
} // namespace
QSGNode *LyricLine::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *root = static_cast<MaskRoot *>(old);
    if (!root)
        root = new MaskRoot;
    if (root->revision != m_maskRevision) {
        while (auto *child = root->firstChild()) {
            root->removeChildNode(child);
            delete child;
        }
        root->pages.clear();
        for (int page = 0; page < m_atlases.size(); ++page) {
            const auto count = std::count_if(m_fragments.cbegin(), m_fragments.cend(),
                                             [page](const Fragment &f) { return f.atlas == page; });
            auto *node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(maskAttributes(), int(count) * 6);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            auto *material = new LyricMaterial;
            material->texture = window()->createTextureFromImage(m_atlases[page]);
            m_textureUploads.fetch_add(1, std::memory_order_relaxed);
            material->texture->setFiltering(QSGTexture::Linear);
            node->setMaterial(material);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
            root->pages.append(node);
        }
        m_renderBatches.store(int(root->pages.size()), std::memory_order_relaxed);
        root->revision = m_maskRevision;
    }
    const qreal amount = m_current ? m_highlightAmount : 0;
    QColor bright =
        QColor::fromRgbF(m_unplayed.redF() + (m_played.redF() - m_unplayed.redF()) * amount,
                         m_unplayed.greenF() + (m_played.greenF() - m_unplayed.greenF()) * amount,
                         m_unplayed.blueF() + (m_played.blueF() - m_unplayed.blueF()) * amount,
                         m_unplayed.alphaF() + (m_played.alphaF() - m_unplayed.alphaF()) * amount);
    auto dim = m_unplayed;
    dim.setAlphaF(std::min(1., dim.alphaF() * (1 + amount)));
    QList<MaskVertex *> vertices;
    for (auto *node : root->pages) {
        vertices.append(static_cast<MaskVertex *>(node->geometry()->vertexData()));
        auto *material = static_cast<LyricMaterial *>(node->material());
        material->bright = rgba(bright);
        material->dim = rgba(dim);
    }
    for (const auto &f : m_fragments) {
        const auto p = pose(f);
        QRectF rect = f.rect;
        rect.setSize(rect.size() * p.scale);
        rect.moveCenter(f.rect.center() + QPointF(p.push, p.lift));
        const auto size = m_atlases[f.atlas].size();
        const QRectF uv(qreal(f.atlasPixels.x()) / size.width(),
                        qreal(f.atlasPixels.y()) / size.height(),
                        qreal(f.atlasPixels.width()) / size.width(),
                        qreal(f.atlasPixels.height()) / size.height());
        const auto vertex = [&](qreal x, qreal y, qreal u, qreal v, float local) {
            return MaskVertex{float(x),
                              float(y),
                              float(u),
                              float(v),
                              local,
                              float(p.edge / f.rect.width()),
                              float(p.fade / f.rect.width()),
                              f.rtl ? 1.f : 0.f,
                              float(p.halo)};
        };
        auto *&v = vertices[f.atlas];
        const auto topLeft = vertex(rect.left(), rect.top(), uv.left(), uv.top(), 0);
        const auto topRight = vertex(rect.right(), rect.top(), uv.right(), uv.top(), 1);
        const auto bottomLeft = vertex(rect.left(), rect.bottom(), uv.left(), uv.bottom(), 0);
        const auto bottomRight = vertex(rect.right(), rect.bottom(), uv.right(), uv.bottom(), 1);
        *v++ = topLeft;
        *v++ = bottomLeft;
        *v++ = topRight;
        *v++ = topRight;
        *v++ = bottomLeft;
        *v++ = bottomRight;
    }
    for (auto *node : root->pages) {
        node->geometry()->markVertexDataDirty();
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }
    return root;
}
void LyricLine::paint(QPainter *painter)
{
    if (!m_current || m_highlightAmount <= 0) {
        painter->setPen(m_unplayed);
        m_layout.draw(painter, QPointF());
        return;
    }
    // Diagnostic CPU snapshot shares the production pose. It is never called
    // by Qt Quick, and keeps typography tests independent of GPU availability.
    const qreal amount = m_current ? m_highlightAmount : 0;
    auto bright = m_played;
    auto dim = m_unplayed;
    bright.setAlphaF(dim.alphaF() + (bright.alphaF() - dim.alphaF()) * amount);
    dim.setAlphaF(std::min(1., dim.alphaF() * (1 + amount)));
    for (const auto &f : m_fragments) {
        const auto p = pose(f);
        // Reconstruct coverage only for this diagnostic CPU snapshot. The
        // production path keeps one atlas, rather than duplicate word bitmaps.
        QImage coverage(f.atlasPixels.size(), QImage::Format_ARGB32_Premultiplied);
        coverage.setDevicePixelRatio(m_maskDevicePixelRatio);
        const auto &atlas = m_atlases[f.atlas];
        for (int y = 0; y < coverage.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(coverage.scanLine(y));
            for (int x = 0; x < coverage.width(); ++x) {
                const auto alpha = qRed(atlas.pixel(f.atlasPixels.x() + x, f.atlasPixels.y() + y));
                row[x] = qRgba(alpha, alpha, alpha, alpha);
            }
        }
        QImage image(coverage.size(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(m_maskDevicePixelRatio);
        image.fill(Qt::transparent);
        QPainter mask(&image);
        QLinearGradient gradient;
        const qreal edge = f.rtl ? f.rect.width() - p.edge : p.edge;
        gradient.setStart(edge - p.fade / 2, 0);
        gradient.setFinalStop(edge + p.fade / 2, 0);
        gradient.setColorAt(0, f.rtl ? dim : bright);
        gradient.setColorAt(1, f.rtl ? bright : dim);
        mask.fillRect(QRectF(QPointF(), f.rect.size()), gradient);
        mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mask.drawImage(QPointF(), coverage);
        mask.end();
        painter->save();
        painter->translate(f.rect.center() + QPointF(p.push, p.lift));
        painter->scale(p.scale, p.scale);
        painter->translate(-f.rect.center());
        painter->drawImage(f.rect.topLeft(), image);
        painter->restore();
    }
}
} // namespace EchoMusic
