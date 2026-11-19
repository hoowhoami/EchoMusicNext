#pragma once
#include <QCryptographicHash>
#include <QSaveFile>
#include <memory>

class PluginRuntimeTest;
namespace EchoMusic
{
/// GUI-thread, bounded receiver for one archive attempt. QSaveFile owns the
/// partial file; only a verified, closed archive is handed to extraction.
class PluginArchiveDownload final
{
public:
    static constexpr qint64 ChunkBytes = 64 * 1024;
    PluginArchiveDownload(QString path, QString expectedHash);
    bool begin();
    bool receive(QIODevice &source, qint64 limit);
    bool finish();
    void cancel();
    qint64 received() const
    {
        return m_received;
    }
    bool limited() const
    {
        return m_limited;
    }
    bool storageFailed() const
    {
        return m_storageFailed;
    }
    QString error() const
    {
        return m_error;
    }

private:
    friend class ::PluginRuntimeTest;
    QString m_path, m_expectedHash, m_error;
    std::unique_ptr<QSaveFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    bool m_limited = false, m_storageFailed = false;
};
} // namespace EchoMusic
