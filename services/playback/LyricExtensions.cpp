#include "LyricExtensions.h"
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <utility>
#include <memory>
namespace EchoMusic
{
namespace
{
class ResolveJob final : public QObject
{
public:
    ResolveJob(LyricExtensions &registry, QList<LyricExtensions::Resolver> entries,
               QVariantMap context, QString only, QObject *owner,
               std::function<void(LyricExtensions::Result)> done)
        : QObject(owner), registry(registry), entries(std::move(entries)),
          context(std::move(context)), only(std::move(only)), done(std::move(done))
    {
        QTimer::singleShot(0, this, [this] { next(); });
    }

private:
    void finish(LyricExtensions::Result result)
    {
        auto callback = std::exchange(done, {});
        deleteLater();
        if (callback)
            callback(std::move(result));
    }
    void next()
    {
        while (index < entries.size()) {
            const auto entry = entries[index++];
            if (!entry.valid() || (!only.isEmpty() && only != entry.key))
                continue;
            if (entry.match)
                invoke(entry, "match", [this, entry](bool ok, const QVariant &value) {
                    if (!ok || !entry.valid()) {
                        next();
                        return;
                    }
                    if (value.metaType().id() != QMetaType::Bool) {
                        const QPointer<ResolveJob> guard(this);
                        entry.report("Lyric match must return a boolean");
                        if (guard)
                            next();
                    } else if (value.toBool())
                        run(entry);
                    else
                        next();
                });
            else
                run(entry);
            return;
        }
        finish({});
    }
    void invoke(const LyricExtensions::Resolver &entry, const QString &method,
                LyricExtensions::Reply callback)
    {
        auto *attempt = new QObject(this);
        auto *timer = new QTimer(attempt);
        timer->setSingleShot(true);
        const QPointer<ResolveJob> guard(this);
        const QPointer<QObject> lifetime(attempt);
        auto settled = std::make_shared<bool>(false);
        const auto complete = [guard, lifetime, settled,
                               callback = std::move(callback)](bool ok, const QVariant &value) {
            if (!guard || !lifetime || std::exchange(*settled, true))
                return;
            lifetime->deleteLater();
            callback(ok, value);
        };
        connect(timer, &QTimer::timeout, attempt, [entry, complete] {
            if (entry.valid())
                entry.report("Lyric resolver callback timed out");
            complete(false, {});
        });
        timer->start(2500);
        entry.invoke(method, context, attempt, complete);
    }
    void run(const LyricExtensions::Resolver &entry)
    {
        invoke(entry, "resolve", [this, entry](bool ok, const QVariant &value) {
            if (!ok || !entry.valid() || !value.isValid() || value.isNull() ||
                (value.metaType().id() == QMetaType::Bool && !value.toBool())) {
                next();
                return;
            }
            if (value.metaType().id() != QMetaType::QVariantMap) {
                const QPointer<ResolveJob> guard(this);
                entry.report("Lyric resolver must return an explicit format/content document");
                if (guard)
                    next();
                return;
            }
            const auto document = value.toMap();
            registry.parse(document, this,
                           [this, entry, document](QJsonObject lyrics, QString error) {
                               if (!entry.valid()) {
                                   next();
                                   return;
                               }
                               if (!error.isEmpty()) {
                                   const QPointer<ResolveJob> guard(this);
                                   entry.report(error);
                                   if (guard)
                                       next();
                               } else
                                   finish({entry.key, entry.revision,
                                           document.value("source", entry.key).toString(), lyrics});
                           });
        });
    }
    LyricExtensions &registry;
    QList<LyricExtensions::Resolver> entries;
    QVariantMap context;
    QString only;
    std::function<void(LyricExtensions::Result)> done;
    qsizetype index = 0;
};
} // namespace
LyricExtensions::LyricExtensions(QObject *parent) : QObject(parent), m_parser(this) {}
bool LyricExtensions::add(Resolver resolver)
{
    if (m_resolvers.size() >= 128)
        return false;
    m_resolvers.append(std::move(resolver));
    std::sort(m_resolvers.begin(), m_resolvers.end(), [](const auto &a, const auto &b) {
        return a.order != b.order ? a.order < b.order : a.key < b.key;
    });
    emit changed();
    return true;
}
void LyricExtensions::remove(const QString &key, const QString &revision)
{
    if (m_resolvers.removeIf([&](const auto &e) { return e.key == key && e.revision == revision; }))
        emit changed();
}
bool LyricExtensions::contains(const QString &key, const QString &revision) const
{
    return std::any_of(m_resolvers.begin(), m_resolvers.end(), [&](const auto &e) {
        return e.key == key && (revision.isEmpty() || e.revision == revision) && e.valid();
    });
}
QObject *LyricExtensions::resolve(const PlaybackTrack &track, bool force, bool preserveCurrent,
                                  const QString &only, QObject *owner,
                                  std::function<void(Result)> done)
{
    // An empty registry has no asynchronous work or resources to own. Preserve
    // the normal host lookup path without manufacturing a resolver job.
    if (m_resolvers.isEmpty()) {
        done({});
        return nullptr;
    }
    QVariantMap context{
        {"track",
         QVariantMap{
             {"id", track.id},
             {"hash", track.hash},
             {"title", track.title},
             {"artist", track.artist},
             {"album", track.album},
             {"durationMs", double(track.durationMs)},
             {"sourceId", track.catalogMetadata.value("plugin_source").toString()},
             {"data", track.catalogMetadata.value("plugin_data").toObject().toVariantMap()}}},
        {"hash", track.hash},
        {"durationMs", double(track.durationMs)},
        {"force", force},
        {"preserveCurrent", preserveCurrent}};
    return new ResolveJob(*this, m_resolvers, context, only, owner, std::move(done));
}
QString LyricExtensions::validate(const QVariantMap &document)
{
    const QStringList keys{"format", "content",     "encoding",
                           "source", "translation", "romanization"};
    for (auto it = document.begin(); it != document.end(); ++it)
        if (!keys.contains(it.key()) || it.value().metaType().id() != QMetaType::QString ||
            it.value().toString().toUtf8().size() > (it.key() == "source" ? 1024 : 1024 * 1024))
            return "Invalid or oversized lyric document field";
    if (!document.contains("content") ||
        !QStringList{"lrc", "krc", "yrc"}.contains(document.value("format").toString()) ||
        !QStringList{"text", "base64"}.contains(document.value("encoding", "text").toString()) ||
        (document.value("encoding").toString() == "base64" &&
         document.value("format").toString() != "krc"))
        return "Explicit lyric format/content or encoding is invalid";
    if (QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact).size() >
        4 * 1024 * 1024)
        return "Lyric document exceeds the worker request budget";
    return {};
}
QObject *LyricExtensions::parse(const QVariantMap &document, QObject *owner, Parsed done)
{
    return m_parser.submit(LyricParser::Source::Document, document, owner,
                           [done = std::move(done)](LyricParser::Result result) {
                               done(std::move(result.lyrics), std::move(result.error));
                           });
}
QObject *LyricExtensions::parseDownloaded(const QVariantMap &document, QObject *owner, Parsed done)
{
    return m_parser.submit(LyricParser::Source::Downloaded, document, owner,
                           [done = std::move(done)](LyricParser::Result result) {
                               done(std::move(result.lyrics), std::move(result.error));
                           });
}
QObject *LyricExtensions::parseEmbedded(const QString &text, QObject *owner, Parsed done)
{
    return m_parser.submit(LyricParser::Source::Embedded, {{"content", text}}, owner,
                           [done = std::move(done)](LyricParser::Result result) {
                               done(std::move(result.lyrics), std::move(result.error));
                           });
}
} // namespace EchoMusic
