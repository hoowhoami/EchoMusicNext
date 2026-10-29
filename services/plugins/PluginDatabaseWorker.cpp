#include "StorageSnapshot.h"
#include "PluginDatabaseWorker.h"
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QSet>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace EchoMusic
{
namespace
{
constexpr qsizetype MaxBytes = 8 * 1024 * 1024;
QMutex openedMutex;
QHash<QString, int> openedPaths;
void require(bool valid, const QString &error)
{
    if (!valid)
        throw std::runtime_error(error.toStdString());
}
QString migrationSql(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QString)
        return value.toString();
    require(value.metaType() == QMetaType::fromType<QVariantList>(),
            "Migration SQL must be a string or string array");
    QStringList sql;
    for (const auto &part : value.toList()) {
        require(part.metaType().id() == QMetaType::QString, "Migration SQL array requires strings");
        sql.append(part.toString());
    }
    return sql.join("\n;\n");
}
QString fileKey(const QString &path)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    auto key =
        canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : canonical;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    key = key.toCaseFolded();
#endif
    return key;
}
QString nameOf(const QVariant &value)
{
    require(!value.isValid() || value.metaType().id() == QMetaType::QString,
            "Database name must be a string");
    const auto name = value.isValid() ? value.toString() : QStringLiteral("main");
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$");
    require(pattern.match(name).hasMatch(),
            "Database name must contain 1..64 ASCII letters, digits, dots, underscores or hyphens");
    return name;
}
void fields(const QVariantMap &map, const QStringList &allowed)
{
    for (auto it = map.begin(); it != map.end(); ++it)
        require(allowed.contains(it.key()), "Unknown SQLite option: " + it.key());
}
int integer(const QVariantMap &map, const QString &key, int fallback, int minimum, int maximum)
{
    if (!map.contains(key))
        return fallback;
    bool ok = false;
    const double value = map.value(key).toDouble(&ok);
    require(ok && std::isfinite(value) && std::floor(value) == value && value >= minimum &&
                value <= maximum,
            "Invalid SQLite integer option: " + key);
    return static_cast<int>(value);
}
void checkCanceled(const std::atomic_bool &canceled)
{
    require(!canceled.load(), "SQLite operation canceled");
}
// Tokenize comments/quoted strings before classifying statements. In particular,
// semicolons in strings or trigger bodies must not escape the transaction owner.
QStringList statements(const QString &sql)
{
    require(!sql.trimmed().isEmpty() && sql.toUtf8().size() <= 256 * 1024,
            "SQL must contain 1..256 KiB");
    QStringList result, tokens;
    qsizetype start = 0;
    int triggerDepth = 0;
    bool trigger = false;
    auto flush = [&](qsizetype end) {
        if (!tokens.isEmpty()) {
            static const QStringList heads{"SELECT", "WITH",    "INSERT", "UPDATE",
                                           "DELETE", "REPLACE", "CREATE", "ALTER",
                                           "DROP",   "EXPLAIN", "PRAGMA"};
            require(heads.contains(tokens.first()), "SQL statement is not allowed");
            static const QStringList blocked{"ATTACH",         "DETACH",       "VACUUM",
                                             "LOAD_EXTENSION", "READFILE",     "WRITEFILE",
                                             "SQLITE_DBPAGE",  "SQLITE_DBPTR", "FTS3_TOKENIZER"};
            for (const auto &token : tokens)
                require(!blocked.contains(token),
                        "SQL contains an external-access statement or function");
            if (tokens.first() == "PRAGMA") {
                static const QStringList pragmas{
                    "USER_VERSION",    "TABLE_INFO", "TABLE_XINFO",      "INDEX_INFO",
                    "INDEX_XINFO",     "INDEX_LIST", "FOREIGN_KEY_LIST", "FOREIGN_KEYS",
                    "INTEGRITY_CHECK", "QUICK_CHECK"};
                require(tokens.size() >= 2 && pragmas.contains(tokens[1]), "PRAGMA is not public");
                const auto text = sql.mid(start, end - start);
                require(!text.contains('=') && (tokens[1] != "USER_VERSION" ||
                                                (tokens.size() == 2 && !text.contains('('))),
                        "PRAGMA assignment is reserved to the host");
                require(tokens[1] != "FOREIGN_KEYS" || (tokens.size() == 2 && !text.contains('(')),
                        "Foreign key enforcement is owned by the host");
            }
            result.append(sql.mid(start, end - start).trimmed());
        }
        tokens.clear();
        trigger = false;
        triggerDepth = 0;
        start = end + 1;
    };
    for (qsizetype i = 0; i < sql.size();) {
        const auto c = sql[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == '-' && sql.mid(i, 2) == "--") {
            while (i < sql.size() && sql[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && sql.mid(i, 2) == "/*") {
            const auto end = sql.indexOf("*/", i + 2);
            require(end >= 0, "Unterminated SQL comment");
            i = end + 2;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`' || c == '[') {
            const QChar end = c == '[' ? QChar(']') : c;
            QString word;
            bool closed = false;
            for (++i; i < sql.size(); ++i) {
                if (sql[i] == end) {
                    if (i + 1 < sql.size() && sql[i + 1] == end) {
                        word += end;
                        ++i;
                        continue;
                    }
                    ++i;
                    closed = true;
                    break;
                }
                word += sql[i];
            }
            require(closed, "Unterminated SQL quote");
            // Quoted identifiers/functions can select extensions too. Strings
            // are data and are deliberately absent from authorization tokens.
            if (c != '\'')
                tokens.append(word.toUpper());
            continue;
        }
        if (c.isLetter() || c == '_') {
            const auto wordStart = i++;
            while (i < sql.size() && (sql[i].isLetterOrNumber() || sql[i] == '_'))
                ++i;
            const auto word = sql.mid(wordStart, i - wordStart).toUpper();
            tokens.append(word);
            if (tokens.first() == "CREATE" && word == "TRIGGER")
                trigger = true;
            if (trigger && (word == "BEGIN" || word == "CASE"))
                ++triggerDepth;
            if (trigger && word == "END")
                --triggerDepth;
            continue;
        }
        if (c == ';' && triggerDepth == 0)
            flush(i);
        ++i;
    }
    flush(sql.size());
    require(!result.isEmpty(), "SQL is empty");
    return result;
}
QVariant bindValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return QVariant(QMetaType::fromType<QString>());
    const auto type = value.metaType().id();
    if (type == QMetaType::Bool)
        return value.toBool() ? 1 : 0;
    if (type == QMetaType::QString)
        return value;
    if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        const auto map = value.toMap();
        fields(map, {"type", "data"});
        const auto encoding = map.value("type").toString();
        require(map.value("data").metaType().id() == QMetaType::QString,
                "SQLite typed value requires string data");
        const auto text = map.value("data").toString();
        if (encoding == "int64") {
            static const QRegularExpression decimal("^-?(0|[1-9][0-9]*)$");
            bool ok = false;
            const auto n = text.toLongLong(&ok);
            require(ok && decimal.match(text).hasMatch(), "Invalid signed int64");
            return n;
        }
        require(encoding == "hex" || encoding == "base64", "Invalid SQLite blob encoding");
        require(text.size() <= MaxBytes * 2, "SQLite blob exceeds 8 MiB");
        const auto bytes = text.toLatin1();
        QByteArray decoded;
        if (encoding == "hex") {
            static const QRegularExpression hex("^[0-9a-fA-F]*$");
            require(text.size() % 2 == 0 && hex.match(text).hasMatch(), "Invalid hex blob");
            decoded = QByteArray::fromHex(bytes);
        } else {
            const auto result =
                QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
            require(bool(result), "Invalid base64 blob");
            decoded = result.decoded;
        }
        require(decoded.size() <= MaxBytes, "SQLite blob exceeds 8 MiB");
        if (decoded.isNull())
            decoded = QByteArray("");
        return decoded;
    }
    bool ok = false;
    const auto number = value.toDouble(&ok);
    require(ok && std::isfinite(number) &&
                (std::floor(number) != number || std::abs(number) <= 9007199254740991.),
            "Use {type:'int64',data:'...'} for exact large integers");
    if (std::floor(number) == number)
        return static_cast<qlonglong>(number);
    return number;
}
QVariant publicValue(const QVariant &value, qsizetype &bytes)
{
    bytes += 32;
    if (value.isNull())
        return QVariant::fromValue(nullptr);
    if (value.metaType().id() == QMetaType::QByteArray) {
        const auto hex = value.toByteArray().toHex();
        bytes += hex.size();
        return QVariantMap{{"type", "hex"}, {"data", QString::fromLatin1(hex)}};
    }
    if (value.metaType().id() == QMetaType::LongLong ||
        value.metaType().id() == QMetaType::ULongLong) {
        const auto n = value.toLongLong();
        if (n < -9007199254740991ll || n > 9007199254740991ll)
            return QVariantMap{{"type", "int64"}, {"data", QString::number(n)}};
    }
    if (value.metaType().id() == QMetaType::QString)
        bytes += value.toString().toUtf8().size();
    return value;
}
QVariant query(QSqlDatabase &db, const QString &mode, const QVariantMap &args,
               const std::atomic_bool &canceled)
{
    fields(args, {"sql", "params", "limit"});
    const auto sqls = statements(args.value("sql").toString());
    require(mode == "exec" || sqls.size() == 1, "Query/run accepts exactly one statement");
    require(!args.contains("params") ||
                args.value("params").metaType() == QMetaType::fromType<QVariantList>(),
            "SQLite params must be an array");
    const auto params = args.value("params").toList();
    require(params.size() <= 32766 && (mode != "exec" || params.isEmpty()),
            "Invalid SQLite bind count");
    const int limit = integer(args, "limit", mode == "get" ? 1 : 1000, 1, 5000);
    QVariant result;
    for (const auto &sql : sqls) {
        checkCanceled(canceled);
        QSqlQuery q(db);
        q.setForwardOnly(true);
        require(q.prepare(sql), q.lastError().text());
        qsizetype paramBytes = 0;
        for (const auto &param : params) {
            const auto value = bindValue(param);
            paramBytes +=
                (value.metaType().id() == QMetaType::QByteArray ? value.toByteArray().size()
                                                                : value.toString().toUtf8().size());
            require(paramBytes <= MaxBytes, "SQLite parameters exceed budget");
            q.addBindValue(value);
        }
        require(q.exec(), q.lastError().text());
        if (mode == "exec") {
            require(!q.isSelect(), "exec accepts statements without result rows; use all/get");
            result = QVariantMap{};
        } else if (mode == "run") {
            require(!q.isSelect(), "run accepts statements without result rows; use all/get");
            const auto rowId = q.lastInsertId();
            qsizetype bytes = 0;
            result = QVariantMap{{"changes", q.numRowsAffected()},
                                 {"lastInsertRowid", publicValue(rowId, bytes)}};
        } else {
            require(q.isSelect(), "Query must return rows");
            QVariantList rows;
            qsizetype bytes = 0;
            bool truncated = false;
            const auto record = q.record();
            while (q.next()) {
                checkCanceled(canceled);
                if (rows.size() == limit) {
                    truncated = true;
                    break;
                }
                QVariantMap row;
                for (int column = 0; column < record.count(); ++column) {
                    const auto key = record.fieldName(column);
                    bytes += key.toUtf8().size();
                    require(!row.contains(key), "Duplicate result column; provide a unique alias");
                    row.insert(key, publicValue(q.value(column), bytes));
                    require(bytes <= MaxBytes, "SQLite result exceeds 8 MiB");
                }
                rows.append(row);
            }
            require(!q.lastError().isValid(), q.lastError().text());
            result = mode == "get" ? (rows.isEmpty() ? QVariant::fromValue(nullptr) : rows.first())
                                   : QVariant(QVariantMap{{"rows", rows},
                                                          {"rowCount", rows.size()},
                                                          {"truncated", truncated}});
        }
    }
    checkCanceled(canceled);
    return result;
}
bool linked(const QString &directory, const QString &path)
{
    return QFileInfo(directory).isSymLink() ||
           QFileInfo(QFileInfo(directory).absolutePath()).isSymLink() ||
           QFileInfo(path).isSymLink();
}
} // namespace
PluginDatabaseWorker::PluginDatabaseWorker(QString directory) : m_directory(std::move(directory)) {}
PluginDatabaseWorker::~PluginDatabaseWorker()
{
    closeAll();
}
bool PluginDatabaseWorker::close(const QString &id)
{
    auto it = m_databases.find(id);
    if (it == m_databases.end())
        return false;
    const auto path = fileKey(it->path), connectionName = it->connection.connectionName();
    it->connection.close();
    m_databases.erase(it);
    QSqlDatabase::removeDatabase(connectionName);
    QMutexLocker lock(&openedMutex);
    if (--openedPaths[path] == 0)
        openedPaths.remove(path);
    return true;
}
void PluginDatabaseWorker::closeAll()
{
    const auto keys = m_databases.keys();
    for (const auto &id : keys)
        close(id);
}
PluginDatabaseResult PluginDatabaseWorker::open(const QString &id, const QVariantMap &args,
                                                const std::atomic_bool &canceled)
{
    fields(args, {"name", "readOnly", "busyTimeoutMs", "migrations"});
    const auto name = nameOf(args.value("name"));
    require(m_databases.size() < 16, "At most 16 databases may be open per scope");
    for (const auto &db : std::as_const(m_databases))
        require(db.name != name, "Database is already open in this scope");
    require(!args.contains("readOnly") || args.value("readOnly").metaType().id() == QMetaType::Bool,
            "readOnly must be boolean");
    const bool readOnly = args.value("readOnly", false).toBool();
    const int busy = integer(args, "busyTimeoutMs", 1000, 0, 5000);
    require(!args.contains("migrations") ||
                args.value("migrations").metaType() == QMetaType::fromType<QVariantList>(),
            "Migrations must be an array");
    auto migrations = args.value("migrations").toList();
    require(migrations.size() <= 500, "At most 500 migrations");
    QSet<int> versions;
    qsizetype migrationBytes = 0;
    for (const auto &value : migrations) {
        const auto migration = value.toMap();
        fields(migration, {"version", "sql"});
        const int version = integer(migration, "version", -1, 1, 999999);
        require(version > 0 && !versions.contains(version),
                "Migration versions must be positive and unique");
        versions.insert(version);
        migrationBytes += migrationSql(migration.value("sql")).toUtf8().size();
        require(migrationBytes <= 256 * 1024, "Migration SQL exceeds 256 KiB");
        statements(migrationSql(migration.value("sql")));
    }
    std::sort(migrations.begin(), migrations.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("version").toInt() < b.toMap().value("version").toInt();
    });
    const auto path = m_directory + '/' + name + ".sqlite";
    require(!linked(m_directory, path), "Linked database paths are not supported");
    require(readOnly ? QFileInfo::exists(path) : QDir().mkpath(m_directory),
            "Database directory is unavailable");
    const auto connectionName =
        "PluginDatabase-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(path);
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=" + QString::number(busy) +
                         (readOnly ? ";QSQLITE_OPEN_READONLY" : ""));
    // Open/delete across presentation scopes must agree on live-file ownership.
    {
        QMutexLocker lock(&openedMutex);
        if (!QFileInfo::exists(path) &&
            QDir(m_directory).entryList({"*.sqlite"}, QDir::Files).size() >= 256) {
            db = {};
            QSqlDatabase::removeDatabase(connectionName);
            require(false, "At most 256 named databases per installation");
        }
        if (!db.open()) {
            const auto error = db.lastError().text();
            db = {};
            QSqlDatabase::removeDatabase(connectionName);
            throw std::runtime_error(error.toStdString());
        }
        if (!readOnly) {
            const auto error = StorageSnapshot::claimSqlite(path);
            if (!error.isEmpty()) {
                db.close();
                db = QSqlDatabase();
                QSqlDatabase::removeDatabase(connectionName);
                require(false, error);
            }
        }
        ++openedPaths[fileKey(path)];
    }
    m_databases.insert(id, {db, path, name, readOnly});
    try {
        int version = 0;
        {
            QSqlQuery q(db);
            require(q.exec("PRAGMA foreign_keys=ON"), q.lastError().text());
            require(q.exec("PRAGMA trusted_schema=OFF"), q.lastError().text());
            require(q.exec("PRAGMA user_version") && q.next(), q.lastError().text());
            version = q.value(0).toInt();
        }
        bool transaction = false;
        try {
            for (const auto &value : migrations) {
                const auto migration = value.toMap();
                const int next = migration.value("version").toInt();
                if (next <= version)
                    continue;
                require(!readOnly, "Pending migrations require a writable database");
                if (!transaction) {
                    require(db.transaction(), db.lastError().text());
                    transaction = true;
                }
                query(db, "exec", {{"sql", migrationSql(migration.value("sql"))}}, canceled);
                QSqlQuery q(db);
                require(q.exec("PRAGMA user_version=" + QString::number(next)),
                        q.lastError().text());
                version = next;
            }
            checkCanceled(canceled);
            if (transaction)
                require(db.commit(), db.lastError().text());
        } catch (...) {
            if (transaction)
                db.rollback();
            throw;
        }
        return {QVariantMap{{"name", name}, {"version", version}, {"readOnly", readOnly}}, {}, {}};
    } catch (...) {
        db = {};
        close(id);
        throw;
    }
}
PluginDatabaseResult PluginDatabaseWorker::execute(const QString &command, const QString &id,
                                                   const QVariantMap &args,
                                                   const std::atomic_bool &canceled)
{
    try {
        checkCanceled(canceled);
        if (command == "open")
            return open(id, args, canceled);
        if (command == "close")
            return {close(id), {}, {}};
        if (command == "list") {
            QVariantList databases;
            for (const auto &entry :
                 QDir(m_directory)
                     .entryInfoList({"*.sqlite"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
                if (databases.size() >= 256)
                    break;
                databases.append(
                    QVariantMap{{"name", entry.completeBaseName()},
                                {"size", entry.size()},
                                {"modifiedAt", entry.lastModified().toMSecsSinceEpoch()}});
            }
            return {databases, {}, {}};
        }
        if (command == "delete") {
            const auto name = nameOf(args.value("name")),
                       path = m_directory + '/' + name + ".sqlite";
            require(!linked(m_directory, path), "Linked database paths are not supported");
            QMutexLocker lock(&openedMutex);
            require(openedPaths.value(fileKey(path)) == 0,
                    "Close every database handle before deleting");
            if (!QFileInfo::exists(path)) {
                require(StorageSnapshot::forgetSqlite(path), "Cannot delete database descriptor");
                return {false, {}, {}};
            }
            require(QFile::remove(path), "Cannot delete database");
            require(StorageSnapshot::forgetSqlite(path), "Cannot delete database descriptor");
            for (const auto &suffix : {"-wal", "-shm", "-journal"})
                QFile::remove(path + suffix);
            return {true, {}, {}};
        }
        auto it = m_databases.find(id);
        require(it != m_databases.end(), "Database handle is closed");
        auto &db = it->connection;
        require(!it->readOnly || command == "all" || command == "get", "Database is read-only");
        require(db.transaction(), db.lastError().text());
        try {
            QVariant result;
            if (command == "transaction") {
                const auto list = args.value("statements").toList();
                require(!list.isEmpty() && list.size() <= 500,
                        "Transaction requires 1..500 statements");
                QVariantList results;
                for (const auto &value : list)
                    results.append(query(db, "run", value.toMap(), canceled));
                result = results;
            } else
                result = query(db, command, args, canceled);
            checkCanceled(canceled);
            require(db.commit(), db.lastError().text());
            return {result, {}, {}};
        } catch (...) {
            db.rollback();
            throw;
        }
    } catch (const std::exception &error) {
        return {{}, canceled.load() ? "Canceled" : "SqliteError", QString::fromUtf8(error.what())};
    }
}
} // namespace EchoMusic
