#include "PlaybackSourceExtensions.h"
#include "PlaybackSourceRequest.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <utility>

namespace EchoMusic
{
QStringList PlaybackSourceExtensions::stages()
{
    return {"before-catalog", "catalog", "after-catalog", "cloud", "final-fallback"};
}
bool PlaybackSourceExtensions::add(Resolver resolver)
{
    if (m_resolvers.size() >= 128 || !resolver.valid || !resolver.invoke)
        return false;
    m_resolvers.append(std::move(resolver));
    std::sort(m_resolvers.begin(), m_resolvers.end(), [](const auto &a, const auto &b) {
        return a.order != b.order ? a.order < b.order : a.key < b.key;
    });
    emit changed();
    return true;
}
void PlaybackSourceExtensions::remove(const QString &key, const QString &revision)
{
    if (m_resolvers.removeIf([&](const auto &v) { return v.key == key && v.revision == revision; }))
        emit changed();
}
qint64 PlaybackSourceExtensions::audioTrack(const QJsonObject &source, const QString &url)
{
    for (const auto &value : source.value("sources").toArray()) {
        const auto item = value.toObject();
        if (item.value("url").toString() == url)
            return item.value("audioTrackId").toInteger();
    }
    return 0;
}
QVariantMap PlaybackSourceExtensions::publicSource(const QJsonObject &source)
{
    auto sources = source.value("sources").toArray();
    if (sources.isEmpty())
        for (const auto &url : PlaybackSourceRequest::usableUrls(source))
            sources.append(QJsonObject{{"url", url}, {"audioTrackId", 0}});
    return {{"urls", PlaybackSourceRequest::usableUrls(source)},
            {"sources", sources.toVariantList()},
            {"quality", source.value("quality").toString()},
            {"effect", source.value("effect").toString("none")},
            {"durationMs", source.value("duration_ms").toDouble()},
            {"sourceKind", source.value("source_kind").toString("catalog")},
            {"noticeCode", source.value("notice_code").toString()},
            {"loudness", QVariantMap{{"lufs", source.value("loudness").toVariant()},
                                     {"gain", source.value("gain").toVariant()},
                                     {"peak", source.value("peak").toVariant()}}}};
}
QJsonObject PlaybackSourceExtensions::normalize(const QVariant &value, const QJsonObject &current)
{
    const auto number = [](const QVariant &v) {
        return v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Int ||
               v.metaType().id() == QMetaType::LongLong || v.metaType().id() == QMetaType::UInt ||
               v.metaType().id() == QMetaType::ULongLong;
    };
    QVariantMap patch;
    if (value.metaType().id() == QMetaType::QString)
        patch = {{"urls", QVariantList{value.toString()}}, {"sources", QVariantList{}}};
    else if (value.metaType().id() == QMetaType::QVariantMap)
        patch = value.toMap();
    else
        return {};
    static const QStringList fields{"url",        "urls",       "sources",    "quality", "effect",
                                    "durationMs", "sourceKind", "noticeCode", "loudness"};
    for (auto i = patch.begin(); i != patch.end(); ++i)
        if (!fields.contains(i.key()) ||
            (QStringList{"url", "quality", "effect", "sourceKind", "noticeCode"}.contains(
                 i.key()) &&
             i.value().metaType().id() != QMetaType::QString) ||
            (i.key() == "durationMs" && !number(i.value())) ||
            (i.key() == "loudness" && !i.value().isNull() &&
             i.value().metaType().id() != QMetaType::QVariantMap))
            return {};
    auto values = current.isEmpty() ? QVariantMap{} : publicSource(current);
    for (auto i = patch.begin(); i != patch.end(); ++i)
        values.insert(i.key(), i.value());
    if (patch.contains("url")) {
        if (patch.value("url").metaType().id() != QMetaType::QString)
            return {};
        values["urls"] = QVariantList{patch.value("url")};
        if (!patch.contains("sources"))
            values["sources"] = QVariantList{};
    } else if (patch.contains("urls") && !patch.contains("sources"))
        values["sources"] = QVariantList{};
    else if (patch.contains("sources") && !patch.contains("urls"))
        values["urls"] = QVariantList{};
    const auto validList = [](const QVariant &v) {
        return !v.isValid() || v.metaType().id() == QMetaType::QVariantList ||
               v.metaType().id() == QMetaType::QStringList;
    };
    if (!validList(values.value("urls")) || !validList(values.value("sources")))
        return {};
    QJsonArray urls, sources;
    const auto append = [&](const QString &text, qint64 track) {
        const QUrl url(text);
        if (text.size() > 16384 || !url.isValid() ||
            (url.scheme() != "https" && url.scheme() != "http" && url.scheme() != "file"))
            return false;
        const QJsonObject item{{"url", text}, {"audioTrackId", track}};
        if (!sources.contains(item))
            sources.append(item);
        if (!urls.contains(text))
            urls.append(text);
        return true;
    };
    const auto list = values.value("sources").toList();
    if (list.size() > 32 || values.value("urls").toList().size() > 32)
        return {};
    for (const auto &entry : list) {
        if (entry.metaType().id() != QMetaType::QVariantMap)
            return {};
        const auto item = entry.toMap();
        const auto ordinal = item.value("audioTrackId", 0).toDouble();
        if ((item.contains("audioTrackId") && !number(item.value("audioTrackId"))) ||
            !std::isfinite(ordinal) || ordinal != std::floor(ordinal) || ordinal < 0 ||
            ordinal > 256 || item.value("url").metaType().id() != QMetaType::QString ||
            !append(item.value("url").toString(), qint64(ordinal)))
            return {};
    }
    for (const auto &entry : values.value("urls").toList()) {
        if (entry.metaType().id() != QMetaType::QString)
            return {};
        if (!urls.contains(entry.toString()) && !append(entry.toString(), 0))
            return {};
    }
    if (urls.isEmpty() || sources.size() > 32)
        return {};
    const auto quality = values.value("quality").toString();
    const auto effect = values.value("effect", "none").toString();
    const auto kind = values.value("sourceKind", "plugin").toString();
    if ((!quality.isEmpty() && !QStringList{"standard", "higher", "flac", "hi_res", "viper_tape",
                                            "viper_clear", "viper_atmos"}
                                    .contains(quality)) ||
        !QStringList{"none", "piano", "vocal", "accompaniment", "subwoofer", "ancient", "surnay",
                     "dj", "viper_atmos", "viper_clear"}
             .contains(effect) ||
        !QStringList{"plugin", "catalog", "cloud", "local", "mv"}.contains(kind))
        return {};
    const auto duration = values.value("durationMs", 0).toDouble();
    if (!std::isfinite(duration) || duration < 0 || duration > 86400000 ||
        values.value("noticeCode").toString().size() > 256)
        return {};
    auto result = current;
    result["urls"] = urls;
    result["backup_urls"] = QJsonArray{};
    result["sources"] = sources;
    result["quality"] = quality;
    result["effect"] = effect;
    result["source_kind"] = kind;
    result["duration_ms"] = duration;
    result["notice_code"] = values.value("noticeCode").toString();
    if (values.contains("loudness")) {
        const auto volume = values.value("loudness").toMap();
        for (const auto &key : {"lufs", "gain", "peak"}) {
            const auto v = volume.value(key);
            const auto target = QString(key) == "lufs" ? QString("loudness") : QString(key);
            if (!v.isValid() || v.isNull())
                result.remove(target);
            else if (!number(v) || !std::isfinite(v.toDouble()))
                return {};
            else
                result[target] = v.toDouble();
        }
    }
    return result;
}
namespace
{
class SourceJob final : public QObject
{
public:
    using Resolver = PlaybackSourceExtensions::Resolver;
    SourceJob(QList<Resolver> entries, QString stage, QVariantMap context, QJsonObject source,
              bool transforming, QObject *owner, std::function<void(QJsonObject)> done)
        : QObject(owner), entries(std::move(entries)), stage(std::move(stage)),
          context(std::move(context)), current(std::move(source)), transforming(transforming),
          done(std::move(done))
    {
        QTimer::singleShot(0, this, [this] { next(); });
    }

private:
    void finish(QJsonObject source)
    {
        auto callback = std::exchange(done, {});
        deleteLater();
        if (callback)
            callback(std::move(source));
    }
    void invoke(const Resolver &entry, const QString &method, QVariantMap args,
                std::function<void(bool, const QVariant &)> callback)
    {
        auto *attempt = new QObject(this);
        auto *deadline = new QTimer(attempt);
        deadline->setSingleShot(true);
        auto completed = std::make_shared<bool>(false);
        const QPointer<SourceJob> guard(this);
        const QPointer<QObject> lifetime(attempt);
        const auto settle = [guard, lifetime, completed,
                             callback = std::move(callback)](bool ok, const QVariant &value) {
            if (!guard || !lifetime || std::exchange(*completed, true))
                return;
            lifetime->deleteLater();
            callback(ok, value);
        };
        connect(deadline, &QTimer::timeout, attempt, [entry, settle] {
            if (entry.valid())
                entry.report("Audio source callback timed out");
            settle(false, {});
        });
        deadline->start(2500);
        entry.invoke(method, args, attempt, settle);
    }
    void next()
    {
        while (index < entries.size()) {
            const auto entry = entries[index++];
            if (!entry.valid() || (transforming ? !entry.transform || !entry.stages.contains(stage)
                                                : !entry.resolve || entry.position != stage))
                continue;
            auto args = context;
            args.insert(transforming ? "stage" : "position", stage);
            if (transforming)
                args["source"] = PlaybackSourceExtensions::publicSource(current);
            if (entry.match)
                invoke(entry, "match", args, [this, entry, args](bool ok, const QVariant &value) {
                    const QPointer<SourceJob> guard(this);
                    if (ok && value.metaType().id() != QMetaType::Bool && entry.valid())
                        entry.report("Audio source match must return a boolean");
                    if (!guard)
                        return;
                    if (!ok || value.metaType().id() != QMetaType::Bool || !value.toBool() ||
                        !entry.valid())
                        next();
                    else
                        run(entry, args);
                });
            else
                run(entry, args);
            return;
        }
        finish(transforming ? current : QJsonObject{});
    }
    void run(const Resolver &entry, const QVariantMap &args)
    {
        invoke(entry, transforming ? "transform" : "resolve", args,
               [this, entry](bool ok, const QVariant &value) {
                   if (!ok || !entry.valid()) {
                       next();
                       return;
                   }
                   if (!value.isValid() || value.isNull()) {
                       next();
                       return;
                   }
                   if (value.metaType().id() == QMetaType::Bool && !value.toBool()) {
                       if (transforming)
                           finish({});
                       else
                           next();
                       return;
                   }
                   const auto candidate = PlaybackSourceExtensions::normalize(
                       value, transforming ? current : QJsonObject{});
                   if (candidate.isEmpty()) {
                       const QPointer<SourceJob> guard(this);
                       entry.report("Audio source callback returned an invalid candidate");
                       if (guard)
                           next();
                       return;
                   }
                   if (transforming) {
                       current = candidate;
                       next();
                   } else {
                       // Every resolved candidate runs the complete transform chain.
                       auto *child = new SourceJob(entries, stage, context, candidate, true, this,
                                                   [this](QJsonObject result) {
                                                       if (result.isEmpty())
                                                           next();
                                                       else
                                                           finish(std::move(result));
                                                   });
                       Q_UNUSED(child);
                   }
               });
    }
    QList<Resolver> entries;
    QString stage;
    QVariantMap context;
    QJsonObject current;
    bool transforming;
    std::function<void(QJsonObject)> done;
    qsizetype index = 0;
};
} // namespace
QObject *PlaybackSourceExtensions::resolve(const QString &stage, const QVariantMap &context,
                                           QObject *owner, std::function<void(QJsonObject)> done)
{
    return new SourceJob(m_resolvers, stage, context, {}, false, owner, std::move(done));
}
QObject *PlaybackSourceExtensions::transform(const QString &stage, const QVariantMap &context,
                                             const QJsonObject &source, QObject *owner,
                                             std::function<void(QJsonObject)> done)
{
    return new SourceJob(m_resolvers, stage, context, source, true, owner, std::move(done));
}
} // namespace EchoMusic
