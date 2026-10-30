#pragma once
#include "LyricParser.h"
#include <QStringView>
namespace EchoMusic::LyricCodecs
{
bool isTtml(QStringView text);
/// Value-only, bounded TTML interchange codec, called on LyricParser's worker.
/// Decimal parent times remain exact until the final public millisecond value.
LyricParser::Result parseTtml(const QString &text, const std::function<bool()> &canceled);
} // namespace EchoMusic::LyricCodecs
