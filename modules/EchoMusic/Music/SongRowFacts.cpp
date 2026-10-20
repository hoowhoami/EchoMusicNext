#include "SongRowFacts.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>

namespace EchoMusic
{
QVariantList songPrivilegeTags(const PlaybackTrack &track)
{
    QVariantList tags;
    const auto add = [&](const QString &label, const QString &color) {
        tags.append(QVariantMap{{"label", label}, {"color", color}});
    };
    const int payment = track.catalogMetadata.value("payment_type").toInt();
    if (track.catalogPrivilege == 10 && payment == 2)
        add(QObject::tr("付费"), "#EF4444");
    if (track.catalogPrivilege == 10 && payment == 3)
        add("VIP", "#F59E0B");
    if (track.catalogPrivilege == 5 &&
        track.catalogMetadata.value("old_copyright_code").toInt() != 1)
        add(QObject::tr("版权"), "#8B5CF6");
    if (track.catalogPrivilege == 40)
        add(QObject::tr("音源"), "#6B7280");
    return tags;
}
QString songQualityLabel(const PlaybackTrack &track)
{
    const auto resources = track.catalogMetadata.value("resources").toArray();
    const auto bits = quint64(track.catalogMetadata.value("rights")
                                  .toObject()
                                  .value("quality_map")
                                  .toObject()
                                  .value("attr0")
                                  .toInteger());
    const auto has = [&](const QString &quality, int bit) {
        for (const auto &value : resources)
            if (value.toObject()["quality"] == quality &&
                !value.toObject().value("hash").toString().isEmpty())
                return true;
        return bool(bits & (quint64(1) << bit));
    };
    return has("high", 6)   ? QStringLiteral("Hi-Res")
           : has("flac", 5) ? QStringLiteral("SQ")
           : has("320", 4)  ? QStringLiteral("HQ")
                            : QString();
}
QVariantList songQualityTags(const PlaybackTrack &track)
{
    const auto resources = track.catalogMetadata.value("resources").toArray();
    const auto labelFor = [](const QString &quality) -> QString {
        if (quality == "high")
            return QStringLiteral("Hi-Res");
        if (quality == "flac")
            return QStringLiteral("SQ");
        if (quality == "320")
            return QStringLiteral("HQ");
        return {};
    };
    static const QSet<QString> effects{"piano",  "vocal", "accompaniment", "subwoofer", "ancient",
                                       "surnay", "dj"};
    QVariantList tags;
    QSet<QString> seen;
    for (const auto &value : resources) {
        const auto entry = value.toObject();
        const auto quality = entry.value("quality").toString();
        const auto label = labelFor(quality);
        // A quality label requires a non-empty resource hash. Size alone is insufficient.
        if (label.isEmpty() || effects.contains(quality) || seen.contains(label) ||
            entry.value("hash").toString().isEmpty())
            continue;
        seen.insert(label);
        tags.append(label);
    }
    return tags;
}
} // namespace EchoMusic
