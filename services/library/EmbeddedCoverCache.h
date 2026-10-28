#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QFileSystemWatcher>
#include <memory>

namespace EchoMusic
{
/// Disk-only artwork storage. GUI retains bounded metadata; all file I/O and
/// content hashing run on one worker. Readers use ordinary asynchronous file URLs.
class EmbeddedCoverCache final : public QObject
{
    Q_OBJECT
public:
    struct Limits
    {
        qint64 bytes = 128 * 1024 * 1024;
        int entries = 512;
        qint64 ageMs = 30LL * 24 * 60 * 60 * 1000;
        qint64 fileBytes = 8 * 1024 * 1024;
    };
    EmbeddedCoverCache(QString directory, Limits limits, QObject *parent = nullptr);
    ~EmbeddedCoverCache() override;
    void setScope(quint64 account);
    QString identityKey(const QString &identity) const;
    QString lookup(const QStringList &identities);
    /// Source is the native reader's already-exported file, never an audio URL.
    void store(const QStringList &identities, const QString &source);
    void maintain();
    void shutdown();
signals:
    void changed(const QStringList &keys);
    void ready();
    void closed();

private:
    struct State;
    struct Record
    {
        QString scope, file;
        QStringList keys;
        qint64 created = 0, used = 0, bytes = 0, modified = 0;
    };
    void apply(quint64 epoch, const QList<Record> &records);
    void touch();
    void armExpiry();
    QString m_directory, m_scope;
    Limits m_limits;
    QHash<QString, Record> m_index;
    QSet<QString> m_pending;
    QHash<QString, qint64> m_touched;
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<State> m_state;
    QTimer m_expiry, m_check, m_touch;
    QFileSystemWatcher m_files;
    quint64 m_epoch = 0;
    quint64 m_checkVersion = 0;
    bool m_checkAgain = false;
    bool m_closing = false;
};
} // namespace EchoMusic
