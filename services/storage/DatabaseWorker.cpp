#include "DatabaseWorker.h"
#include "SqlDiagnostics.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QDateTime>

namespace EchoMusic
{
DatabaseWorker::~DatabaseWorker()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto connection = m_database.connectionName();
    m_database.close();
    m_database = QSqlDatabase();
    if (!connection.isEmpty())
        QSqlDatabase::removeDatabase(connection);
}

void DatabaseWorker::initialize(const QString &path)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // A failed attempt can leave an open/invalid connection. Retire it on its
    // owning thread before retrying; no query from the prior call survives here.
    if (m_database.isValid()) {
        const auto connection = m_database.connectionName();
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        emit failed(QStringLiteral("无法创建应用数据目录"));
        return;
    }
    m_database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QUuid::createUuid().toString());
    m_database.setDatabaseName(path);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!m_database.open()) {
        emit failed(SqlDiagnostics::failure(m_database, "open", m_database.lastError()));
        return;
    }
    QSqlQuery query(m_database);
    for (const auto &sql :
         {"PRAGMA foreign_keys=ON", "PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL"}) {
        if (!query.exec(QString::fromLatin1(sql))) {
            emit failed(
                SqlDiagnostics::failure(m_database, "initialize.configure_wal", query.lastError()));
            return;
        }
    }
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        emit failed(
            tr("无法读取数据库版本：%1")
                .arg(SqlDiagnostics::failure(m_database, "read_user_version", query.lastError())));
        return;
    }
    const int version = query.value(0).toInt();
    query.finish();
    if (version > 6) {
        SqlDiagnostics::unsupportedVersion(m_database, version, 6);
        emit failed(tr("数据库版本比当前应用更新（实际版本 %1，当前支持至 6）").arg(version));
        return;
    }
    if (version < 6) {
        if (!m_database.transaction()) {
            emit failed(
                SqlDiagnostics::failure(m_database, "initialize.begin", m_database.lastError()));
            return;
        }
        // Credential storage is intentionally replaced, never imported. Keep
        // unrelated searches/covers, but discard the former external references.
        for (const auto &sql :
             {"CREATE TABLE IF NOT EXISTS recent_searches (query TEXT PRIMARY KEY NOT NULL "
              "CHECK(length(query)>0), searched_at_ms INTEGER NOT NULL CHECK(searched_at_ms>=0))",
              "CREATE INDEX IF NOT EXISTS recent_searches_recency ON "
              "recent_searches(searched_at_ms DESC)",
              "DROP TABLE IF EXISTS account_selection", "DROP TABLE IF EXISTS accounts",
              "DROP TABLE IF EXISTS credential_cleanup",
              "CREATE TABLE accounts (user_id INTEGER PRIMARY KEY CHECK(user_id>0), "
              "nickname TEXT NOT NULL, avatar_url TEXT NOT NULL, token TEXT NOT NULL "
              "CHECK(length(token)>0), "
              "refresh_credential TEXT NOT NULL, expires_at_seconds INTEGER NOT NULL "
              "CHECK(expires_at_seconds>=0), "
              "last_used_at_ms INTEGER NOT NULL CHECK(last_used_at_ms>=0), "
              "avatar_identity_kind TEXT NOT NULL DEFAULT '' CHECK(avatar_identity_kind IN "
              "('','talent','singer')))",
              "CREATE TABLE account_selection (singleton INTEGER PRIMARY KEY CHECK(singleton=1), "
              "account_id INTEGER REFERENCES accounts(user_id) ON DELETE SET NULL)",
              "INSERT INTO account_selection(singleton,account_id) VALUES(1,NULL)",
              "CREATE TABLE IF NOT EXISTS playlist_auto_covers (account_id INTEGER NOT NULL "
              "CHECK(account_id>0), cloud_list_id INTEGER NOT NULL CHECK(cloud_list_id>0), "
              "revision INTEGER NOT NULL CHECK(revision>=0), cover_url TEXT NOT NULL, PRIMARY "
              "KEY(account_id,cloud_list_id))",
              "PRAGMA user_version=6"}) {
            if (!query.exec(QString::fromLatin1(sql))) {
                const auto message = SqlDiagnostics::failure(
                    m_database, "initialize.upgrade_schema", query.lastError());
                m_database.rollback();
                emit failed(message);
                return;
            }
        }
        if (!m_database.commit()) {
            const auto message =
                SqlDiagnostics::failure(m_database, "initialize.commit", m_database.lastError());
            m_database.rollback();
            emit failed(message);
            return;
        }
    }
    publishHistory();
    publishAccounts();
    emit initialized();
}

void DatabaseWorker::loadPlaylistCovers(quint64 request, quint64 account)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QSqlQuery q(m_database);
    q.prepare(
        "SELECT cloud_list_id,revision,cover_url FROM playlist_auto_covers WHERE account_id=?");
    q.addBindValue(QVariant::fromValue(account));
    if (!q.exec()) {
        SqlDiagnostics::failure(m_database, "load_playlist_covers", q.lastError());
        emit playlistCoversFailed(QStringLiteral("无法读取歌单封面缓存"));
        return;
    }
    StoredPlaylistCovers covers;
    while (q.next())
        covers.append({q.value(0).toULongLong(), q.value(1).toULongLong(), q.value(2).toString()});
    if (q.lastError().isValid()) {
        SqlDiagnostics::failure(m_database, "load_playlist_covers.fetch", q.lastError());
        emit playlistCoversFailed(QStringLiteral("无法读取歌单封面缓存"));
        return;
    }
    emit playlistCoversReady(request, account, covers);
}
void DatabaseWorker::savePlaylistCover(quint64 account, const StoredPlaylistCover &cover)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QSqlQuery q(m_database);
    q.prepare("INSERT INTO playlist_auto_covers(account_id,cloud_list_id,revision,cover_url) "
              "VALUES(?,?,?,?) ON CONFLICT(account_id,cloud_list_id) DO UPDATE SET "
              "revision=excluded.revision,cover_url=excluded.cover_url WHERE "
              "playlist_auto_covers.revision<=excluded.revision");
    q.addBindValue(QVariant::fromValue(account));
    q.addBindValue(QVariant::fromValue(cover.listId));
    q.addBindValue(QVariant::fromValue(cover.revision));
    q.addBindValue(cover.url);
    if (!q.exec()) {
        SqlDiagnostics::failure(m_database, "save_playlist_cover", q.lastError());
        emit playlistCoversFailed(QStringLiteral("无法保存歌单封面缓存"));
    }
}
void DatabaseWorker::publishHistory()
{
    Q_ASSERT(QThread::currentThread() == thread());
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT query FROM recent_searches ORDER BY searched_at_ms DESC LIMIT 20"))) {
        SqlDiagnostics::failure(m_database, "load_search_history", query.lastError());
        emit historyFailed(QStringLiteral("无法读取搜索历史"));
        return;
    }
    QStringList history;
    while (query.next())
        history.append(query.value(0).toString());
    if (query.lastError().isValid()) {
        SqlDiagnostics::failure(m_database, "load_search_history.fetch", query.lastError());
        emit historyFailed(QStringLiteral("无法读取搜索历史"));
        return;
    }
    emit historyReady(history);
}
void DatabaseWorker::recordSearch(const QString &text)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_database.isOpen() || text.trimmed().isEmpty())
        return;
    if (!m_database.transaction()) {
        SqlDiagnostics::failure(m_database, "record_search.begin", m_database.lastError());
        emit historyFailed(QStringLiteral("无法保存搜索历史"));
        return;
    }
    QSqlQuery query(m_database);
    // Clock milliseconds can collide or move backwards. Strictly increasing
    // stored recency guarantees that a duplicate term always moves first.
    bool ok =
        query.exec(QStringLiteral("SELECT COALESCE(MAX(searched_at_ms),0) FROM recent_searches")) &&
        query.next();
    if (!ok)
        SqlDiagnostics::failure(m_database, "record_search.read_recency", query.lastError());
    const qint64 timestamp =
        qMax(QDateTime::currentMSecsSinceEpoch(), query.value(0).toLongLong() + 1);
    query.finish();
    if (ok) {
        ok = query.prepare(
            QStringLiteral("INSERT INTO recent_searches(query,searched_at_ms) VALUES(?,?) ON "
                           "CONFLICT(query) DO UPDATE SET searched_at_ms=excluded.searched_at_ms"));
        query.addBindValue(text.trimmed());
        query.addBindValue(timestamp);
        ok = ok && query.exec();
        if (!ok)
            SqlDiagnostics::failure(m_database, "record_search.write", query.lastError());
    }
    query.finish();
    if (ok) {
        ok = query.exec(
            QStringLiteral("DELETE FROM recent_searches WHERE query NOT IN (SELECT query "
                           "FROM recent_searches ORDER BY searched_at_ms DESC LIMIT 20)"));
        if (!ok)
            SqlDiagnostics::failure(m_database, "record_search.trim", query.lastError());
    }
    query.finish();
    if (ok && !m_database.commit()) {
        SqlDiagnostics::failure(m_database, "record_search.commit", m_database.lastError());
        ok = false;
    }
    if (!ok) {
        m_database.rollback();
        emit historyFailed(QStringLiteral("无法保存搜索历史"));
        return;
    }
    publishHistory();
}
void DatabaseWorker::removeSearch(const QString &text)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_database.isOpen())
        return;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM recent_searches WHERE query=?"));
    query.addBindValue(text);
    if (!query.exec()) {
        SqlDiagnostics::failure(m_database, "remove_search", query.lastError());
        emit historyFailed(QStringLiteral("无法删除搜索历史"));
        return;
    }
    query.finish();
    publishHistory();
}
void DatabaseWorker::clearSearches()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_database.isOpen())
        return;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("DELETE FROM recent_searches"))) {
        SqlDiagnostics::failure(m_database, "clear_searches", query.lastError());
        emit historyFailed(QStringLiteral("无法清空搜索历史"));
        return;
    }
    query.finish();
    publishHistory();
}
bool DatabaseWorker::readAccounts(StoredAccounts &accounts, quint64 &active)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT "
                                   "user_id,nickname,avatar_url,token,last_used_at_ms,avatar_"
                                   "identity_kind,refresh_credential,expires_at_seconds "
                                   "FROM accounts ORDER BY last_used_at_ms DESC,user_id"))) {
        SqlDiagnostics::failure(m_database, "read_accounts", query.lastError());
        return false;
    }
    while (query.next())
        accounts.append({query.value(0).toULongLong(), query.value(1).toString(),
                         query.value(2).toString(), query.value(3).toString(),
                         query.value(4).toLongLong(), query.value(5).toString(),
                         query.value(6).toString(), query.value(7).toLongLong()});
    if (query.lastError().isValid()) {
        SqlDiagnostics::failure(m_database, "read_accounts.fetch", query.lastError());
        return false;
    }
    query.finish();
    if (!query.exec(QStringLiteral("SELECT account_id FROM account_selection WHERE singleton=1")) ||
        !query.next()) {
        SqlDiagnostics::failure(m_database, "read_account_selection", query.lastError());
        return false;
    }
    active = query.value(0).toULongLong();
    return true;
}
void DatabaseWorker::publishAccounts(quint64 operation)
{
    StoredAccounts accounts;
    quint64 active = 0;
    if (!readAccounts(accounts, active)) {
        emit accountsFailed(operation);
        return;
    }
    emit accountsReady(operation, accounts, active);
}
void DatabaseWorker::commitAccounts(quint64 operation, bool statementsSucceeded)
{
    // Publish only after both the account data and selection commit. A failed
    // readback rolls back credentials and catalog together.
    StoredAccounts accounts;
    quint64 active = 0;
    bool ok = statementsSucceeded && readAccounts(accounts, active);
    if (ok && !m_database.commit()) {
        SqlDiagnostics::failure(m_database, "accounts.commit", m_database.lastError());
        ok = false;
    }
    if (!ok) {
        m_database.rollback();
        emit accountsFailed(operation);
        return;
    }
    emit accountsReady(operation, accounts, active);
}
void DatabaseWorker::saveAccount(quint64 operation, const StoredAccount &account, bool active)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!account.userId || account.token.isEmpty()) {
        emit accountsFailed(operation);
        return;
    }
    if (!m_database.transaction()) {
        SqlDiagnostics::failure(m_database, "save_account.begin", m_database.lastError());
        emit accountsFailed(operation);
        return;
    }
    QSqlQuery query(m_database);
    bool ok = query.prepare(QStringLiteral(
        "INSERT INTO "
        "accounts(user_id,nickname,avatar_url,token,refresh_credential,expires_at_seconds,last_"
        "used_at_ms) "
        "VALUES(?,?,?,?,?,?,?) ON CONFLICT(user_id) DO UPDATE SET "
        "nickname=excluded.nickname,avatar_url=excluded.avatar_url,token=excluded.token,"
        "refresh_credential=excluded.refresh_credential,expires_at_seconds=excluded.expires_at_"
        "seconds,"
        "last_used_at_ms=excluded.last_used_at_ms"));
    query.addBindValue(QVariant::fromValue(account.userId));
    query.addBindValue(account.nickname.isNull() ? QStringLiteral("") : account.nickname);
    query.addBindValue(account.avatarUrl.isNull() ? QStringLiteral("") : account.avatarUrl);
    query.addBindValue(account.token);
    query.addBindValue(account.refreshCredential.isNull() ? QStringLiteral("")
                                                          : account.refreshCredential);
    query.addBindValue(account.expiresAtSeconds);
    query.addBindValue(account.lastUsedAt);
    ok = ok && query.exec();
    if (!ok)
        SqlDiagnostics::failure(m_database, "save_account.write", query.lastError());
    query.finish();
    if (active && ok) {
        ok = ok && query.prepare(QStringLiteral(
                       "UPDATE account_selection SET account_id=? WHERE singleton=1"));
        query.addBindValue(QVariant::fromValue(account.userId));
        ok = ok && query.exec();
        if (!ok)
            SqlDiagnostics::failure(m_database, "save_account.select", query.lastError());
        query.finish();
    }
    commitAccounts(operation, ok);
}
void DatabaseWorker::selectAccount(quint64 operation, quint64 userId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_database.transaction()) {
        SqlDiagnostics::failure(m_database, "select_account.begin", m_database.lastError());
        emit accountsFailed(operation);
        return;
    }
    QSqlQuery query(m_database);
    bool ok = query.prepare(
        QStringLiteral("UPDATE account_selection SET account_id=? WHERE singleton=1"));
    query.addBindValue(userId ? QVariant::fromValue(userId) : QVariant());
    ok = ok && query.exec();
    if (!ok)
        SqlDiagnostics::failure(m_database, "select_account.write", query.lastError());
    query.finish();
    commitAccounts(operation, ok);
}
void DatabaseWorker::removeAccount(quint64 operation, quint64 userId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_database.transaction()) {
        SqlDiagnostics::failure(m_database, "remove_account.begin", m_database.lastError());
        emit accountsFailed(operation);
        return;
    }
    QSqlQuery query(m_database);
    bool ok = query.prepare(QStringLiteral("DELETE FROM accounts WHERE user_id=?"));
    query.addBindValue(QVariant::fromValue(userId));
    ok = ok && query.exec();
    if (!ok)
        SqlDiagnostics::failure(m_database, "remove_account.write", query.lastError());
    query.finish();
    commitAccounts(operation, ok);
}
void DatabaseWorker::updateAccountIdentity(quint64 userId, const QString &kind)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!userId || (kind != "" && kind != "talent" && kind != "singer"))
        return;
    if (!m_database.transaction()) {
        SqlDiagnostics::failure(m_database, "update_account_identity.begin",
                                m_database.lastError());
        emit accountsFailed(0);
        return;
    }
    QSqlQuery query(m_database);
    bool ok =
        query.prepare(QStringLiteral("UPDATE accounts SET avatar_identity_kind=? WHERE user_id=?"));
    query.addBindValue(kind.isNull() ? QStringLiteral("") : kind);
    query.addBindValue(QVariant::fromValue(userId));
    ok = ok && query.exec();
    if (!ok)
        SqlDiagnostics::failure(m_database, "update_account_identity.write", query.lastError());
    query.finish();
    commitAccounts(0, ok);
}
void DatabaseWorker::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_database.close();
    emit drained();
}
} // namespace EchoMusic
