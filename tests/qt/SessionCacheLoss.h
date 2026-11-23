#pragma once
#include "SessionDatabase.h"
#include <QFile>
#include <QPointer>
#include <QtTest>
#include <atomic>

// Exercise total loss on all platforms without relying on POSIX unlink of an
// open handle. Only a test-owned connection/file is closed and removed.
inline bool loseSessionCache(EchoMusic::SessionDatabase &database)
{
    const auto before = database.epoch();
    auto removed = std::make_shared<std::atomic_int>(0);
    if (!database.submit(
            &database,
            [removed](QSqlDatabase &db, const QString &error) {
                if (!error.isEmpty()) {
                    removed->store(-1);
                    return false;
                }
                const auto path = db.databaseName();
                db.close();
                const bool ok = QFile::remove(path);
                removed->store(ok ? 1 : -1);
                return ok;
            },
            [](bool) {}))
        return false;
    return QTest::qWaitFor([&] { return database.epoch() > before || removed->load() < 0; },
                           5000) &&
           removed->load() == 1 && database.epoch() > before;
}
