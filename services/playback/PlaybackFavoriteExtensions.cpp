#include "PlaybackFavoriteExtensions.h"
#include <QJsonDocument>
#include <QTimer>
#include <utility>

namespace EchoMusic
{
PlaybackFavoriteExtensions::PlaybackFavoriteExtensions(QObject *parent) : QObject(parent) {}
PlaybackFavoriteExtensions::~PlaybackFavoriteExtensions()
{
    blockSignals(true);
    shutdown();
}
bool PlaybackFavoriteExtensions::custom(const PlaybackTrack &track)
{
    return track.hash.isEmpty() &&
           !track.catalogMetadata.value("plugin_source").toString().isEmpty();
}
QString PlaybackFavoriteExtensions::identity(const PlaybackTrack &track)
{
    return track.catalogMetadata.value("plugin_source").toString() + '\n' + track.id;
}
const PlaybackFavoriteExtensions::Provider *
PlaybackFavoriteExtensions::provider(const QString &sourceId) const
{
    for (const auto &p : m_providers)
        if (sourceId.startsWith(p.sourcePrefix) && p.valid && p.valid())
            return &p;
    return nullptr;
}
bool PlaybackFavoriteExtensions::add(Provider value)
{
    if (m_closed || m_providers.size() >= 128 || value.sourcePrefix.isEmpty() || !value.invoke)
        return false;
    // An overlapping namespace cannot have two owners; no priority takeover.
    for (const auto &p : m_providers)
        if (p.sourcePrefix.startsWith(value.sourcePrefix) ||
            value.sourcePrefix.startsWith(p.sourcePrefix))
            return false;
    m_providers.append(std::move(value));
    QTimer::singleShot(0, this, [this] { emit changed(); });
    return true;
}
void PlaybackFavoriteExtensions::cancel(const Item &entry)
{
    ++entry->generation;
    const auto job = std::exchange(entry->job, {});
    entry->state.loading = entry->state.busy = false;
    entry->toggleAfterRead = entry->dirty = false;
    if (job)
        delete job;
}
void PlaybackFavoriteExtensions::remove(const QString &key, const QString &revision)
{
    m_providers.removeIf([&](const auto &p) { return p.key == key && p.revision == revision; });
    const auto entries = m_entries.values();
    for (const auto &entry : entries)
        if (entry->providerKey == key && entry->providerRevision == revision) {
            cancel(entry);
            m_cost -= entry->cost;
            m_entries.remove(entry->key);
        }
    emit changed();
}
void PlaybackFavoriteExtensions::reset()
{
    const auto entries = std::exchange(m_entries, {});
    m_cost = 0;
    for (const auto &entry : entries)
        cancel(entry);
    // Providers are activation leases; account changes invalidate their delivery
    // through the SDK scope, not by resurrecting another account's cached state.
    emit changed();
}
void PlaybackFavoriteExtensions::shutdown()
{
    m_closed = true;
    m_providers.clear();
    reset();
}
void PlaybackFavoriteExtensions::setCurrent(const PlaybackTrack &track)
{
    const auto key = custom(track) ? identity(track) : QString();
    if (key == m_current)
        return;
    m_current = key;
    const auto entries = m_entries.values();
    for (const auto &entry : entries)
        if (entry->currentScoped && entry->key != key) {
            cancel(entry);
            entry->state.known = false;
        }
    if (custom(track))
        ensure(track);
}
PlaybackFavoriteExtensions::Item PlaybackFavoriteExtensions::ensure(const PlaybackTrack &track)
{
    if (m_closed || !custom(track) || !track.payloadLoaded)
        return {};
    const auto sourceId = track.catalogMetadata.value("plugin_source").toString();
    const auto *p = provider(sourceId);
    if (!p)
        return {};
    const auto key = identity(track);
    if (const auto entry = m_entries.value(key)) {
        entry->used = ++m_used;
        if (!entry->state.known && !entry->job && entry->state.error.isEmpty()) {
            entry->currentScoped = key == m_current;
            request(entry, false);
        }
        return entry;
    }
    auto entry = std::make_shared<Entry>();
    entry->key = key;
    entry->sourceId = sourceId;
    entry->providerKey = p->key;
    entry->providerRevision = p->revision;
    entry->track = track.publicValue();
    entry->cost = QJsonDocument(QJsonObject::fromVariantMap(entry->track))
                      .toJson(QJsonDocument::Compact)
                      .size();
    constexpr qsizetype budget = 8 * 1024 * 1024;
    if (entry->cost > budget)
        return {};
    while (m_entries.size() >= 128 || m_cost + entry->cost > budget) {
        Item oldest;
        for (const auto &candidate : std::as_const(m_entries))
            if (!candidate->state.busy && candidate->key != m_current &&
                (!oldest || candidate->used < oldest->used))
                oldest = candidate;
        if (!oldest)
            return {};
        cancel(oldest);
        m_cost -= oldest->cost;
        m_entries.remove(oldest->key);
    }
    entry->used = ++m_used;
    entry->currentScoped = key == m_current;
    entry->state.available = true;
    m_cost += entry->cost;
    m_entries.insert(key, entry);
    request(entry, false);
    return entry;
}
PlaybackFavoriteExtensions::State PlaybackFavoriteExtensions::state(const PlaybackTrack &track)
{
    if (!m_closed && custom(track) && !track.payloadLoaded) {
        State waiting;
        waiting.available = provider(track.catalogMetadata.value("plugin_source").toString());
        waiting.loading = waiting.available;
        return waiting;
    }
    if (const auto entry = ensure(track))
        return entry->state;
    return {};
}
void PlaybackFavoriteExtensions::toggle(const PlaybackTrack &track, bool requireCurrent)
{
    const auto entry = ensure(track);
    if (!entry) {
        emit notice(tr("此曲目不支持收藏"), true);
        return;
    }
    if (entry->state.busy)
        return;
    entry->currentScoped = requireCurrent;
    if (entry->state.loading) {
        entry->toggleAfterRead = true;
        emit changed();
        return;
    }
    if (!entry->state.known) {
        entry->toggleAfterRead = true;
        request(entry, false);
    } else
        request(entry, true, !entry->state.favorite);
    emit changed();
}
bool PlaybackFavoriteExtensions::invalidate(const QString &key, const QString &revision,
                                            const QString &sourceId)
{
    bool found = false;
    for (const auto &p : m_providers)
        if (p.key == key && p.revision == revision && p.valid && p.valid() &&
            (sourceId.isEmpty() || sourceId.startsWith(p.sourcePrefix)))
            found = true;
    if (!found)
        return false;
    const auto entries = m_entries.values();
    for (const auto &entry : entries)
        if (entry->providerKey == key && entry->providerRevision == revision &&
            (sourceId.isEmpty() || entry->sourceId == sourceId)) {
            if (entry->state.busy) {
                entry->dirty = true;
                continue;
            }
            cancel(entry);
            entry->currentScoped = entry->key == m_current;
            request(entry, false);
        }
    emit changed();
    return true;
}
void PlaybackFavoriteExtensions::request(const Item &entry, bool write, bool favorite)
{
    const auto *p = provider(entry->sourceId);
    if (!p || p->key != entry->providerKey || p->revision != entry->providerRevision)
        return;
    const auto invoke = p->invoke;
    auto *job = new QObject(this);
    entry->job = job;
    const auto generation = ++entry->generation;
    entry->state.loading = !write;
    entry->state.busy = write;
    entry->state.error.clear();
    // No synchronous callback or changed notification from a QML property read.
    QTimer::singleShot(0, job, [this, entry, job, generation, invoke, write, favorite] {
        const QPointer<PlaybackFavoriteExtensions> self(this);
        const QPointer<QObject> guard(job);
        const auto settle = [self, guard, entry, generation,
                             write](bool success, const QVariant &result, const QString &error) {
            if (!self || !guard || entry->job != guard || entry->generation != generation)
                return;
            entry->job = nullptr;
            guard->deleteLater();
            entry->state.loading = entry->state.busy = false;
            const auto value = result.toMap().value("favorite");
            const bool valid = success && value.metaType().id() == QMetaType::Bool;
            if (valid) {
                entry->state.known = true;
                entry->state.favorite = value.toBool();
                entry->state.error.clear();
            } else {
                entry->state.error = error.isEmpty() ? self->tr("读取或保存收藏状态失败") : error;
                if (write || entry->toggleAfterRead)
                    emit self->notice(entry->state.error, true);
            }
            const bool toggle = std::exchange(entry->toggleAfterRead, false);
            const bool dirty = std::exchange(entry->dirty, false);
            if (valid && toggle)
                self->request(entry, true, !entry->state.favorite);
            else if (dirty)
                self->request(entry, false);
            emit self->changed();
        };
        auto *timer = new QTimer(job);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, job,
                [settle] { settle(false, {}, tr("收藏操作超时，请重试")); });
        timer->start(5000);
        QVariantMap arguments{{"track", entry->track}};
        if (write)
            arguments.insert("favorite", favorite);
        invoke(write ? "setState" : "getState", arguments, job, settle);
    });
}
} // namespace EchoMusic
