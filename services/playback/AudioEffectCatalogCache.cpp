#include "AudioEffectCatalogCache.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QTimer>
#include <utility>

namespace EchoMusic
{
struct AudioEffectCatalogCache::State
{
    QSqlDatabase db;
    QString path;
    QString connection = QUuid::createUuid().toString();
    bool valid = false;
};
AudioEffectCatalogCache::AudioEffectCatalogCache(QString path, QObject *parent)
    : QObject(parent), m_worker(new QObject(this)), m_state(std::make_shared<State>())
{
    m_state->path = std::move(path);
    m_thread.setObjectName("echo-effect-catalog-cache");
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &AudioEffectCatalogCache::closed);
}
void AudioEffectCatalogCache::load()
{
    if (m_closing || m_thread.isRunning())
        return;
    m_worker->setParent(nullptr);
    m_worker->moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this, state = m_state, path = m_state->path] {
        const auto fail = [&](QString detail = {}) {
            if (detail.isEmpty())
                detail = state->db.lastError().text();
            emit failed(tr("音效目录缓存不可用：%1")
                            .arg(detail.isEmpty() ? tr("无法读取目录数据库") : detail));
        };
        if (!path.isEmpty())
            QDir().mkpath(QFileInfo(path).absolutePath());
        state->db = QSqlDatabase::addDatabase("QSQLITE", state->connection);
        state->db.setDatabaseName(path.isEmpty() ? ":memory:" : path);
        if (!state->db.open()) {
            fail();
            emit loaded({});
            return;
        }
        QSqlQuery query(state->db);
        if (!query.exec("PRAGMA user_version") || !query.next()) {
            fail(query.lastError().text());
            emit loaded({});
            return;
        }
        if (query.value(0).toInt() > 1) {
            fail(tr("目录数据库版本高于当前应用支持的版本"));
            emit loaded({});
            return;
        }
        for (const auto *sql : {"PRAGMA journal_mode=WAL", "PRAGMA max_page_count=16384",
                                "CREATE TABLE IF NOT EXISTS effect_scopes(scope TEXT PRIMARY KEY, "
                                "metadata BLOB NOT NULL)",
                                "CREATE TABLE IF NOT EXISTS effect_entries(scope TEXT NOT NULL, "
                                "identity TEXT NOT NULL, generation TEXT NOT NULL, ordinal INTEGER "
                                "NOT NULL, metadata BLOB NOT NULL, PRIMARY KEY(scope,identity))",
                                "PRAGMA user_version=1"}) {
            if (!query.exec(sql)) {
                fail(query.lastError().text());
                emit loaded({});
                return;
            }
        }
        state->valid = true;
        QJsonObject scopes;
        if (!query.exec("SELECT scope,metadata FROM effect_scopes ORDER BY scope LIMIT 64")) {
            fail(query.lastError().text());
            emit loaded({});
            return;
        }
        while (query.next()) {
            auto metadata = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
            metadata.insert("rows", QJsonArray());
            scopes.insert(query.value(0).toString(), metadata);
        }
        QSqlQuery rows(state->db);
        if (!rows.exec("SELECT scope,identity,generation,ordinal,metadata FROM effect_entries "
                       "ORDER BY scope,ordinal LIMIT 20000")) {
            fail(rows.lastError().text());
            emit loaded({});
            return;
        }
        QHash<QString, QJsonArray> entries;
        while (rows.next()) {
            const auto scope = rows.value(0).toString();
            if (!scopes.contains(scope))
                continue;
            auto row = QJsonDocument::fromJson(rows.value(4).toByteArray()).object();
            if (row.isEmpty())
                continue;
            row.insert("_generation", rows.value(2).toString());
            row.insert("_ordinal", rows.value(3).toInt());
            entries[scope].append(row);
        }
        for (auto it = scopes.begin(); it != scopes.end(); ++it) {
            auto metadata = it.value().toObject();
            const auto values = entries.value(it.key());
            if (metadata.value("count").toInt(-1) != values.size()) {
                metadata.insert("complete", false);
                metadata.insert("limited", false);
                metadata.insert("nextPage", 1);
                metadata.insert("generation", QUuid::createUuid().toString());
                metadata.insert("updated", 0);
            }
            metadata.insert("rows", values);
            it.value() = metadata;
        }
        emit loaded(scopes);
    });
}
AudioEffectCatalogCache::~AudioEffectCatalogCache()
{
    shutdown();
    m_thread.wait();
}
void AudioEffectCatalogCache::save(const QString &scope, const QJsonObject &metadata,
                                   const QJsonArray &rows, bool complete)
{
    if (m_closing)
        return;
    load();
    QMetaObject::invokeMethod(m_worker, [this, state = m_state, scope, metadata, rows, complete] {
        if (!state->valid)
            return;
        bool ok = state->db.transaction();
        QSqlQuery query(state->db);
        if (ok) {
            query.prepare("INSERT OR REPLACE INTO effect_scopes VALUES(?,?)");
            query.addBindValue(scope);
            query.addBindValue(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
            ok = query.exec();
        }
        for (const auto &value : rows) {
            if (!ok)
                break;
            auto row = value.toObject();
            query.prepare("INSERT OR REPLACE INTO effect_entries VALUES(?,?,?,?,?)");
            query.addBindValue(scope);
            query.addBindValue(row.take("_identity").toString());
            query.addBindValue(row.take("_generation").toString());
            query.addBindValue(row.take("_ordinal").toInt());
            query.addBindValue(QJsonDocument(row).toJson(QJsonDocument::Compact));
            ok = query.exec();
        }
        if (ok && complete) {
            query.prepare("DELETE FROM effect_entries WHERE scope=? AND generation<>?");
            query.addBindValue(scope);
            query.addBindValue(metadata.value("generation").toString());
            ok = query.exec();
        }
        if (!ok || !state->db.commit()) {
            state->db.rollback();
            emit failed(tr("音效目录缓存写入失败"));
        }
    });
}
void AudioEffectCatalogCache::remove(const QString &scope)
{
    if (m_closing)
        return;
    QMetaObject::invokeMethod(m_worker, [state = m_state, scope] {
        if (!state->valid || !state->db.transaction())
            return;
        QSqlQuery query(state->db);
        bool ok = true;
        for (const auto *sql : {"DELETE FROM effect_entries WHERE scope=?",
                                "DELETE FROM effect_scopes WHERE scope=?"}) {
            query.prepare(sql);
            query.addBindValue(scope);
            ok = query.exec() && ok;
        }
        if (ok)
            state->db.commit();
        else
            state->db.rollback();
    });
}
void AudioEffectCatalogCache::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    if (!m_thread.isRunning()) {
        QTimer::singleShot(0, this, [this] { emit closed(); });
        return;
    }
    QMetaObject::invokeMethod(m_worker, [this, state = m_state] {
        state->db.close();
        state->db = {};
        QSqlDatabase::removeDatabase(state->connection);
        m_thread.quit();
    });
}
} // namespace EchoMusic
