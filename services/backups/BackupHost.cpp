#include "BackupHost.h"
#include <QDateTime>
#include <QSet>
#include <algorithm>
namespace EchoMusic
{
BackupHost::BackupHost(QObject *parent) : QObject(parent)
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(120000);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        cancelProvider();
        emit providerNotice(tr("备份存储请求超时"), true);
    });
}
QVariantList BackupHost::providers() const
{
    QStringList keys = m_providers.keys();
    std::sort(keys.begin(), keys.end());
    QVariantList result;
    for (const auto &key : keys) {
        auto metadata = m_providers[key].metadata;
        metadata.insert("key", key);
        result.append(metadata);
    }
    return result;
}
bool BackupHost::addProvider(const QString &key, quint64 generation, const QString &owner,
                             QVariantMap metadata, Invoke callback)
{
    if (m_providers.size() >= 64 && !m_providers.contains(key))
        return false;
    if (m_providers.contains(key))
        removeProvider(key, m_providers[key].generation);
    m_providers.insert(key, {generation, owner, std::move(metadata), std::move(callback)});
    emit providersChanged();
    return true;
}
void BackupHost::removeProvider(const QString &key, quint64 generation)
{
    if (!m_providers.contains(key) || m_providers[key].generation != generation)
        return;
    const auto owner = m_providers.take(key).owner;
    if (m_selected == key) {
        cancelProvider();
        m_entries.clear();
        m_selected.clear();
        emit providerEntriesChanged();
    }
    releaseOwner(owner);
    emit providersChanged();
}
bool BackupHost::begin(const QString &key)
{
    if (providerBusy() || !m_providers.contains(key)) {
        emit providerNotice(tr("备份存储不可用或仍在处理中"), true);
        return false;
    }
    if (m_selected != key)
        m_entries.clear();
    m_error.clear();
    m_selected = key;
    m_providerCancel = std::make_shared<std::atomic_bool>(false);
    ++m_run;
    m_deadline.start();
    emit providerEntriesChanged();
    return true;
}
void BackupHost::finish(BackupCancel cancel, const BackupResult &result)
{
    if (cancel != m_providerCancel)
        return;
    m_deadline.stop();
    m_error = result.error;
    m_providerCancel.reset();
    emit providerEntriesChanged();
    if (!cancel->load() && !result.error.isEmpty())
        emit providerNotice(result.error, true);
}
void BackupHost::invoke(const QString &key, const QString &method, const QVariantMap &args,
                        BackupCancel canceled, BackupCompletion done)
{
    if (!m_providers.contains(key) || canceled->load()) {
        done({{}, "Unavailable", tr("备份存储已停用")});
        return;
    }
    const auto provider = m_providers[key];
    provider.invoke(
        method, args, canceled,
        [this, key, generation = provider.generation, canceled, done](BackupResult result) {
            if (canceled->load() || !m_providers.contains(key) ||
                m_providers[key].generation != generation)
                return;
            done(std::move(result));
        });
}
void BackupHost::refreshProvider(const QString &key)
{
    if (!begin(key))
        return;
    const auto cancel = m_providerCancel;
    invoke(key, "list", {}, cancel, [this, cancel](BackupResult result) {
        if (result.error.isEmpty()) {
            if (result.value.metaType() != QMetaType::fromType<QVariantList>())
                result.error = tr("备份存储返回了无效列表");
            else {
                QVariantList entries;
                QSet<QString> seen;
                const auto list = result.value.toList();
                for (const auto &item : list.mid(0, 500)) {
                    const auto entry = item.toMap();
                    const auto id = entry.value("id").toString().trimmed();
                    if (id.isEmpty() || id.size() > 512 || seen.contains(id))
                        continue;
                    seen.insert(id);
                    QVariantMap row{
                        {"id", id},
                        {"name", entry.value("name", id).toString().left(200)},
                        {"createdAt", entry.value("createdAt").toString().left(80)},
                        {"description", entry.value("description").toString().left(300)}};
                    bool ok = false;
                    const auto size = entry.value("size").toLongLong(&ok);
                    if (ok && size >= 0)
                        row.insert("size", size);
                    entries.append(row);
                }
                m_entries = entries;
            }
        }
        finish(cancel, result);
    });
}
void BackupHost::saveToProvider(const QString &key, bool settings, bool plugins)
{
    if (!begin(key))
        return;
    const auto cancel = m_providerCancel;
    const auto owner = m_providers.value(key).owner;
    createArchive(owner, {{"settings", settings}, {"plugins", plugins}}, cancel,
                  [this, key, cancel](BackupResult result) {
                      if (cancel->load())
                          return;
                      if (!result.error.isEmpty()) {
                          finish(cancel, result);
                          return;
                      }
                      invoke(key, "save", result.value.toMap(), cancel,
                             [this, key, cancel](BackupResult saved) {
                                 finish(cancel, saved);
                                 if (saved.error.isEmpty() && !cancel->load()) {
                                     emit providerNotice(tr("备份已保存"), false);
                                     emit providerSaved();
                                 }
                             });
                  });
}
void BackupHost::loadFromProvider(const QString &key, const QString &id)
{
    if (id.isEmpty() || id.size() > 512 || !begin(key))
        return;
    const auto cancel = m_providerCancel;
    const auto owner = m_providers.value(key).owner;
    invoke(key, "load", {{"id", id}}, cancel, [this, owner, cancel](BackupResult result) {
        if (!result.error.isEmpty()) {
            finish(cancel, result);
            return;
        }
        if (result.value.metaType().id() != QMetaType::QByteArray) {
            finish(cancel, {{}, "InvalidData", tr("备份存储没有返回二进制数据")});
            return;
        }
        inspectArchive(owner, result.value.toByteArray(), cancel,
                       [this, cancel](BackupResult inspected) {
                           finish(cancel, inspected);
                           if (inspected.error.isEmpty() && !cancel->load())
                               emit providerInspection(inspected.value.toMap());
                       });
    });
}
void BackupHost::removeFromProvider(const QString &key, const QString &id)
{
    if (!m_providers.value(key).metadata.value("removable").toBool() || id.isEmpty() ||
        id.size() > 512 || !begin(key))
        return;
    const auto cancel = m_providerCancel;
    invoke(key, "remove", {{"id", id}}, cancel, [this, key, cancel](BackupResult result) {
        finish(cancel, result);
        if (result.error.isEmpty() && !cancel->load())
            refreshProvider(key);
    });
}
void BackupHost::cancelProvider()
{
    if (!m_providerCancel)
        return;
    m_providerCancel->store(true);
    m_providerCancel.reset();
    ++m_run;
    m_deadline.stop();
    emit providerEntriesChanged();
}
void BackupHost::invalidateOwner(const QString &owner)
{
    const auto providers = m_providers;
    for (const auto &provider : providers) {
        if (provider.owner != owner && !provider.owner.startsWith(owner + ":provider:"))
            continue;
        if (m_providers.contains(m_selected) &&
            m_providers.value(m_selected).owner == provider.owner) {
            cancelProvider();
            m_entries.clear();
            m_error.clear();
            emit providerEntriesChanged();
        }
        releaseOwner(provider.owner);
    }
    releaseOwner(owner);
}
void BackupHost::shutdownProviders()
{
    cancelProvider();
    m_providers.clear();
    emit providersChanged();
}
} // namespace EchoMusic
