#include "UiTypography.h"
#include <QFontDatabase>
#include <QFontInfo>
#include <cmath>
#include <QGuiApplication>

namespace EchoMusic
{
QFont styledUiFont(const QFont &base, qreal pixelSize, int weight, qreal letterSpacing, bool italic,
                   bool underline)
{
    auto font = base;
    if (pixelSize == std::floor(pixelSize)) {
        font.setPixelSize(int(pixelSize));
    } else {
        // QFont's pixel setter takes int. Retain fractional sizes through its
        // point-size setter using Qt's own conversion. A large reference size
        // avoids minimum-size rounding at 1px.
        font.setPixelSize(100);
        // A platform that cannot resolve the probe reports a non-positive point
        // size (offscreen yields -0.75); the setter would then reject the result
        // and silently keep the 100px probe, inflating every metric downstream.
        const auto pointsPerPixel = QFontInfo(font).pointSizeF() / 100;
        if (pointsPerPixel > 0)
            font.setPointSizeF(pixelSize * pointsPerPixel);
        else
            font.setPixelSize(qMax(1, qRound(pixelSize)));
    }
    font.setWeight(static_cast<QFont::Weight>(weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    font.setItalic(italic);
    font.setUnderline(underline);
    return font;
}

QFont uiFont(const QString &family)
{
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (family != QStringLiteral("system-ui") && QFontDatabase::hasFamily(family)) {
        // EchoMusic's user font precedes system-ui, rather than replacing its
        // fallback. Retain that chain even for installed Latin-only families.
        auto families = font.families();
        families.removeAll(family);
        families.prepend(family);
        font.setFamilies(families);
    }
    font.setPixelSize(13);
    font.setWeight(QFont::Normal);
    font.setStyleHint(QFont::SansSerif);
#ifdef Q_OS_MACOS
    // Qt's CoreText cascade selects .AppleSimplifiedChineseFont Regular even
    // for a 600 request (QRawFont::weight() still reports 600). An explicit
    // public family lets Qt select PingFang's Medium/Semibold faces, including
    // CJK punctuation. Keep system-ui first so Latin retains the platform face.
    // A Han-only script fallback misses punctuation in mixed-script titles.
    const auto chineseFamily = QStringLiteral("PingFang SC");
    if (QFontDatabase::hasFamily(chineseFamily)) {
        auto families = font.families();
        if (!families.contains(chineseFamily))
            families.append(chineseFamily);
        font.setFamilies(families);
    }
#endif
    // Keep Qt's remaining glyph fallback for other scripts and platforms.
    return font;
}

void configureUiTypography(const QString &family)
{
    QGuiApplication::setFont(uiFont(family));
}
} // namespace EchoMusic
