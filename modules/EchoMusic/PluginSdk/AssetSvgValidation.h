#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

namespace EchoMusic
{
/// Worker-process only. Validates a finite static SVG language before QtSvg sees it.
struct AssetSvgValidation
{
    static bool render(const QByteArray &source, QImage &image, QString &error);
};
} // namespace EchoMusic
