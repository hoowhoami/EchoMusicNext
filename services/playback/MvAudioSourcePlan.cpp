#include "MvAudioSourcePlan.h"
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <limits>

namespace EchoMusic
{
namespace
{
int resolution(const QJsonObject &source)
{
    const auto width = source.value("width").toInt(), height = source.value("height").toInt();
    if (width > 0 && height > 0)
        return std::min(width, height);
    if (height > 0)
        return height;
    static const QMap<QString, int> heights{
        {"fhd", 1080}, {"hd", 720}, {"qhd", 540}, {"sd", 432}, {"ld", 270}};
    return heights.value(source.value("quality").toString());
}
qint64 size(const QJsonObject &source)
{
    const auto size = source.value("size_bytes").toInteger();
    return size > 0 ? size : std::numeric_limits<qint64>::max();
}
QString audioGroup(const QJsonObject &audio)
{
    const auto codec = audio.value("codec").toString();
    return codec.isEmpty() ? QString()
                           : codec + '/' + audio.value("profile").toVariant().toString() + '/' +
                                 audio.value("channels").toVariant().toString() + '/' +
                                 audio.value("sample_rate").toVariant().toString();
}
} // namespace

QJsonArray mvAudioCandidates(const QJsonArray &videos)
{
    QJsonObject selected;
    for (const auto &value : videos) {
        const auto video = value.toObject();
        if (!video.value("audio_fallback_eligible").toBool() ||
            video.value("duration_ms").toInteger() <= 0 ||
            video.value("sources").toArray().isEmpty())
            continue;
        if (selected.isEmpty() || video.value("recommendation_code").toInt() >
                                      selected.value("recommendation_code").toInt())
            selected = video;
    }
    QMap<QString, QList<QJsonObject>> groups;
    for (const auto &value : selected.value("sources").toArray()) {
        const auto source = value.toObject();
        if (!source.value("hash").toString().trimmed().isEmpty())
            groups[source.value("codec").toString()].append(source);
    }
    auto codecs = groups.keys();
    const auto order = [](const QString &codec) {
        return codec == "H.264" ? 0 : codec == "H.265" ? 1 : codec == "MKV" ? 2 : 3;
    };
    std::stable_sort(codecs.begin(), codecs.end(),
                     [&](const auto &a, const auto &b) { return order(a) < order(b); });
    for (auto &group : groups)
        std::stable_sort(group.begin(), group.end(), [](const auto &a, const auto &b) {
            return resolution(a) == resolution(b) ? size(a) < size(b)
                                                  : resolution(a) > resolution(b);
        });
    QJsonArray result;
    QSet<QString> seen;
    const auto append = [&](const QJsonObject &source) {
        const auto identity = source.value("hash").toString().toUpper();
        if (result.size() < 12 && !seen.contains(identity)) {
            seen.insert(identity);
            result.append(source);
        }
    };
    // Compare each encoding's two extremes first. Audio often stays identical
    // across resolutions, but a low-resolution encode can have a worse track.
    for (const auto &codec : codecs) {
        const auto &group = groups[codec];
        append(group.first());
        if (group.size() > 1)
            append(group.last());
    }
    for (int index = 1; result.size() < 12; ++index) {
        bool remaining = false;
        for (const auto &codec : codecs) {
            const auto &group = groups[codec];
            if (index < group.size() - 1) {
                append(group[index]);
                remaining = true;
            }
        }
        if (!remaining)
            break;
    }
    return result;
}

QJsonArray rankedMvAudioSources(const QJsonArray &candidates)
{
    QList<QJsonObject> usable;
    QMap<QString, int> groups;
    for (const auto &value : candidates) {
        const auto source = value.toObject();
        if (source.value("urls").toArray().isEmpty())
            continue;
        const auto group = audioGroup(source.value("audio").toObject());
        if (!group.isEmpty() && !groups.contains(group))
            groups.insert(group, groups.size());
        usable.append(source);
    }
    std::stable_sort(usable.begin(), usable.end(), [&](const auto &a, const auto &b) {
        const auto left = a.value("audio").toObject(), right = b.value("audio").toObject();
        const bool leftLossless = left.value("lossless").toBool(),
                   rightLossless = right.value("lossless").toBool();
        if (leftLossless != rightLossless)
            return leftLossless;
        if (leftLossless && left.value("bits_per_sample") != right.value("bits_per_sample"))
            return left.value("bits_per_sample").toInt() > right.value("bits_per_sample").toInt();
        const auto leftRate = left.value("bit_rate").toInteger(),
                   rightRate = right.value("bit_rate").toInteger();
        const bool leftKnown = leftLossless || leftRate > 0,
                   rightKnown = rightLossless || rightRate > 0;
        if (leftKnown != rightKnown)
            return leftKnown;
        const auto leftGroup = groups.value(audioGroup(left), std::numeric_limits<int>::max());
        const auto rightGroup = groups.value(audioGroup(right), std::numeric_limits<int>::max());
        if (leftGroup != rightGroup)
            return leftGroup < rightGroup;
        if (leftRate != rightRate)
            return leftRate > rightRate;
        return leftKnown && size(a) < size(b);
    });
    QJsonArray result;
    QSet<QString> codecs;
    for (const auto &source : usable) {
        const auto codec = source.value("codec").toString();
        if (codecs.contains(codec))
            continue;
        codecs.insert(codec);
        result.append(source);
        if (result.size() == 3)
            break;
    }
    return result;
}
} // namespace EchoMusic
