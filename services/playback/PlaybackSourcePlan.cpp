#include "PlaybackSourcePlan.h"
#include <QJsonArray>

namespace EchoMusic
{
namespace
{
bool legacyFree(const QJsonObject &file)
{
    // Observed legacy-copyright files use privilege=5 even with no payment.
    // These facts allow an authorized attempt; the tracker still decides access.
    return file.value("privilege").toInt() == 5 && file.value("payment_type").isDouble() &&
           file.value("payment_type").toInt(-1) == 0 && file.value("price_units").isDouble() &&
           file.value("price_units").toInt(-1) == 0;
}
} // namespace
QString PlaybackSourcePlan::qualityKey(ApiClient::PlaybackQuality quality)
{
    static const QStringList keys{"standard",   "higher",      "flac",       "hi_res",
                                  "viper_tape", "viper_clear", "viper_atmos"};
    return keys.value(int(quality));
}
QString PlaybackSourcePlan::sourceQuality(const QJsonObject &source,
                                          const PlaybackSourceCandidate &candidate,
                                          const QJsonObject &privileges)
{
    const auto hash = source.value("hash").toString();
    const bool automatic = candidate.quality == ApiClient::PlaybackQuality::Automatic;
    if (!automatic && (hash.isEmpty() || hash.compare(candidate.hash, Qt::CaseInsensitive) == 0))
        return candidate.resolvedQuality;
    if (hash.isEmpty())
        return {};
    for (const auto &pair :
         {qMakePair("ogg_standard", "standard"), qMakePair("ogg_high", "higher")})
        if (hash.compare(privileges.value(pair.first).toObject().value("hash").toString(),
                         Qt::CaseInsensitive) == 0)
            return pair.second;
    auto variants = privileges.value("variants").toArray();
    variants.prepend(privileges.value("primary"));
    const QStringList wire{"128",        "320",         "flac",       "high",
                           "viper_tape", "viper_clear", "viper_atmos"};
    for (const auto &value : variants) {
        const auto row = value.toObject();
        if (hash.compare(row.value("hash").toString(), Qt::CaseInsensitive) != 0)
            continue;
        const auto index = wire.indexOf(row.value("quality").toString());
        if (index >= 0)
            return qualityKey(ApiClient::PlaybackQuality(index));
    }
    return {};
}
QJsonObject PlaybackSourcePlan::qualityVariant(ApiClient::PlaybackQuality quality,
                                               const QJsonObject &privileges)
{
    const QStringList wire{"128",        "320",         "flac",       "high",
                           "viper_tape", "viper_clear", "viper_atmos"};
    const auto key = wire.value(int(quality));
    QList<int> levels;
    if (quality == ApiClient::PlaybackQuality::Standard)
        levels = {1, 2};
    else if (quality == ApiClient::PlaybackQuality::Higher)
        levels = {4};
    else if (quality == ApiClient::PlaybackQuality::Flac)
        levels = {5};
    else if (quality == ApiClient::PlaybackQuality::HiRes)
        levels = {6};
    const auto variants = privileges.value("variants").toArray();
    for (const auto level : levels)
        for (const auto &v : variants) {
            const auto row = v.toObject();
            if (row.value("quality").toString() == key && row.value("level").toInt() == level)
                return row;
        }
    for (const auto &v : variants)
        if (v.toObject().value("quality").toString() == key)
            return v.toObject();
    return {};
}
QString PlaybackSourcePlan::qualityAccessReason(ApiClient::PlaybackQuality quality,
                                                MembershipFacts membership, bool viperEnabled,
                                                const QJsonObject &privileges)
{
    const bool viper = qualityKey(quality).startsWith("viper_");
    if (viper && !viperEnabled)
        return QObject::tr("蝰蛇音质未开启");
    const auto file = qualityVariant(quality, privileges);
    if (!privileges.isEmpty() && privileges.value("variants").isArray() && file.isEmpty())
        return QObject::tr("歌曲不支持");
    if (!viper && legacyFree(file))
        return {};
    const auto right = file.value("privilege");
    if (right.isDouble()) {
        const bool paid = right.toInt() != 0;
        if (paid && quality == ApiClient::PlaybackQuality::Standard)
            return {};
        if (!paid &&
            (!file.value("service_status").isDouble() || file.value("service_status").toInt() != 0))
            return {};
        if (paid) {
            if (!membership.known)
                return QObject::tr("会员权益尚未确认");
            return (viper ? membership.superVip : membership.conceptVip) ? QString()
                   : viper ? QObject::tr("需超级 VIP")
                           : QObject::tr("需概念版 VIP");
        }
        const auto message = file.value("message").toString();
        return message.isEmpty() ? QObject::tr("当前不可用") : message;
    }
    if (!viper)
        return {};
    if (!membership.known)
        return QObject::tr("会员权益尚未确认");
    return membership.superVip ? QString() : QObject::tr("需超级 VIP");
}
QList<PlaybackSourceCandidate>
PlaybackSourcePlan::catalog(const QString &hash, const QJsonObject &privileges,
                            ApiClient::PlaybackQuality preferred, ApiClient::SoundEffect effect,
                            bool compatibility, bool viperEnabled, MembershipFacts membership)
{
    using Quality = ApiClient::PlaybackQuality;
    using Effect = ApiClient::SoundEffect;
    // Product order differs from the bridge enum: clear precedes tape.
    const QList<Quality> order{Quality::Standard,  Quality::Higher,     Quality::Flac,
                               Quality::HiRes,     Quality::ViperClear, Quality::ViperTape,
                               Quality::ViperAtmos};
    if (!viperEnabled && order.indexOf(preferred) > order.indexOf(Quality::HiRes))
        preferred = Quality::HiRes;
    if (!order.contains(preferred) || hash.isEmpty())
        return {};
    const auto variants = privileges.value("variants").toArray();
    const auto matchedHash = [&variants](const QString &quality) {
        for (const auto &value : variants) {
            const auto variant = value.toObject();
            if (variant.value("quality").toString() == quality &&
                !variant.value("hash").toString().isEmpty())
                return variant.value("hash").toString();
        }
        return QString();
    };
    QList<PlaybackSourceCandidate> result;
    if (effect != Effect::None) {
        const QStringList names{"none",      "piano",   "acappella", "acappella",
                                "subwoofer", "ancient", "surnay",    "dj"};
        const auto effectHash = matchedHash(names.value(int(effect)));
        if (!effectHash.isEmpty() && effectHash.compare(hash, Qt::CaseInsensitive) != 0)
            result.append({effectHash, preferred, effect, {}, qualityKey(preferred)});
        result.append({hash, preferred, effect, {}, qualityKey(preferred)});
    }
    // Resolver candidates follow the provider's relate-goods order. The
    // access/display selector above independently prefers the reference levels.
    const auto candidateHash = [&variants](Quality quality) {
        for (const auto &v : variants) {
            const auto row = v.toObject();
            const auto key = row.value("quality").toString().trimmed().toLower();
            const auto level = row.value("level").toInt(-1);
            bool match = false;
            switch (quality) {
            case Quality::Standard:
                match = key == "128" || level == 1 || level == 2;
                break;
            case Quality::Higher:
                match = key == "320" || key == "hq" || level == 4;
                break;
            case Quality::Flac:
                match = key == "flac" || key == "sq" || level == 5;
                break;
            case Quality::HiRes:
                match = key == "high" || key == "hires" || key == "hi-res" || key == "res" ||
                        level == 6;
                break;
            case Quality::ViperClear:
                match = key == "viper_clear";
                break;
            case Quality::ViperAtmos:
                match = key == "viper_atmos";
                break;
            case Quality::ViperTape:
                match = key == "viper_tape" || level == 101;
                break;
            default:
                break;
            }
            const auto hash = row.value("hash").toString();
            if (match && !hash.isEmpty())
                return hash;
        }
        return QString();
    };
    bool hasAccessibleCandidate = false;
    for (int i = order.indexOf(preferred); i >= 0; --i) {
        const bool accessible =
            qualityAccessReason(order[i], membership, viperEnabled, privileges).isEmpty();
        hasAccessibleCandidate = hasAccessibleCandidate || accessible;
        const auto variantHash = candidateHash(order[i]);
        if (accessible && !variantHash.isEmpty()) {
            const auto key = qualityKey(order[i]);
            result.append({variantHash, order[i], Effect::None, {}, key});
            // The default origin can reject free legacy HQ/SQ while the
            // existing compatibility origin grants the same encoded quality.
            // Keep that quality ahead of descending/Automatic candidates.
            if (!key.startsWith("viper_") && legacyFree(qualityVariant(order[i], privileges)))
                result.append({variantHash, order[i], Effect::None, "356753938", key});
        }
        if (!compatibility)
            break;
    }
    // Both original-hash fallbacks are intentional in the reference. Strict
    // mode omits descending qualities/default-page fallback, but keeps the
    // explicit compatibility-page request; it is not a lower-quality loop.
    if (compatibility && hasAccessibleCandidate)
        result.append({hash, Quality::Automatic, Effect::None, {}, {}});
    if (hasAccessibleCandidate)
        result.append({hash, Quality::Automatic, Effect::None, "356753938", {}});
    return result;
}
} // namespace EchoMusic
