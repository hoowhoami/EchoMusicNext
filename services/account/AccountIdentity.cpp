#include "AccountIdentity.h"
#include <QJsonArray>
#include <QSet>
#include <QCoreApplication>
namespace EchoMusic
{
AccountIdentity AccountIdentity::fromDomain(const QJsonObject &object)
{
    AccountIdentity result;
    result.talentBits = quint32(object.value("talent_bits").toInteger());
    result.actor = object.value("actor").toBool();
    result.business = object.value("business").toBool();
    result.star = object.value("star").toBool();
    result.singer = object.value("singer").toBool();
    const auto labels = [&object](const char *name) {
        QStringList values;
        for (const auto &value : object.value(name).toArray())
            values.append(value.toString());
        return values;
    };
    result.authorizations = labels("authorizations");
    result.talentAuthorizations = labels("talent_authorizations");
    result.singerAuthorizations = labels("singer_authorizations");
    return result;
}
QVariantList AccountIdentity::badges(bool student) const
{
    QVariantList result;
    QSet<QString> seen;
    const auto add = [&result, &seen](const QString &label, const QString &kind) {
        if (label.isEmpty() || seen.contains(label))
            return;
        seen.insert(label);
        result.append(QVariantMap{{"label", label}, {"kind", kind}});
    };
    const auto translated = [](const char *label) {
        return QCoreApplication::translate("AccountIdentity", label);
    };
    if (student)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "学生")), "student");
    if (actor)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "演员")), "identity");
    if (business)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "认证")), "identity");
    if (star)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "明星")), "identity");
    if (singer)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "歌手")), "identity");
    bool hasTalentDescription = !talentAuthorizations.isEmpty();
    for (const auto &label : authorizations)
        for (const auto &term : {"达人", "主播", "调音师", "酷狗号", "专栏作家"})
            hasTalentDescription |= label.contains(QString::fromUtf8(term));
    for (const auto &labels : {authorizations, talentAuthorizations, singerAuthorizations})
        for (const auto &label : labels)
            add(label, "auth");
    // Official category bit order; 11 and 12 are reserved. Descriptive
    // authorization replaces category expansion, as in the reference UI.
    constexpr const char *categories[]{QT_TRANSLATE_NOOP("AccountIdentity", "歌单达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "评论达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "调音师"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "酷狗号"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "问答达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "歌词制作达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "音频主播"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "直播达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "K歌达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "短视频达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "鱼声主播"),
                                       "",
                                       "",
                                       QT_TRANSLATE_NOOP("AccountIdentity", "专栏作家"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "视频达人"),
                                       QT_TRANSLATE_NOOP("AccountIdentity", "音乐画报达人")};
    if (!hasTalentDescription)
        for (int bit = 0; bit < 16; ++bit)
            if (talentBits & (1u << bit))
                add(translated(categories[bit]), "talent");
    bool hasTalentBadge = false;
    for (const auto &value : result) {
        const auto badge = value.toMap();
        hasTalentBadge |= badge["kind"] == "talent" ||
                          badge["label"].toString().contains(
                              translated(QT_TRANSLATE_NOOP("AccountIdentity", "达人")));
    }
    if (talentBits && !hasTalentBadge)
        add(translated(QT_TRANSLATE_NOOP("AccountIdentity", "达人")), "talent");
    return result;
}
QString AccountIdentity::avatarKind() const
{
    // Account detail lacks comment-specific vinfo9 data.
    // Its singer state takes precedence over the talent bitset.
    return singer ? QStringLiteral("singer") : talentBits ? QStringLiteral("talent") : QString();
}
QString AccountIdentity::avatarIcon(const QString &kind)
{
    if (kind == "singer")
        return QStringLiteral(
            "https://imge.kugou.com/commendpic/20180627/20180627153852371280.png");
    if (kind == "talent")
        return QStringLiteral(
            "https://imge.kugou.com/commendpic/20180627/20180627153930257837.png");
    return {};
}
QString AccountIdentity::avatarLabel(const QString &kind)
{
    return kind == "singer"   ? QCoreApplication::translate("AccountIdentity", "歌手认证")
           : kind == "talent" ? QCoreApplication::translate("AccountIdentity", "达人")
                              : QString();
}
} // namespace EchoMusic
