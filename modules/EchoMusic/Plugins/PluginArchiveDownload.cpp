#include "PluginArchiveDownload_p.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>

namespace EchoMusic
{
namespace
{
QString tr(const char *text)
{
    return QCoreApplication::translate("PluginManager", text);
}
} // namespace
PluginArchiveDownload::PluginArchiveDownload(QString path, QString expectedHash)
    : m_path(std::move(path)), m_expectedHash(std::move(expectedHash))
{
}
bool PluginArchiveDownload::begin()
{
    cancel();
    m_received = 0;
    m_limited = m_storageFailed = false;
    m_error.clear();
    m_hash.reset();
    m_file = std::make_unique<QSaveFile>(m_path);
    // Never truncate an existing destination if atomic temporary creation fails.
    m_file->setDirectWriteFallback(false);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_storageFailed = true;
        m_error = tr("无法创建插件包临时文件：%1").arg(m_file->errorString());
        cancel();
        return false;
    }
    return true;
}
bool PluginArchiveDownload::receive(QIODevice &source, qint64 limit)
{
    if (!m_file || !m_error.isEmpty() || m_limited)
        return false;
    std::array<char, ChunkBytes> buffer;
    while (source.bytesAvailable() > 0) {
        const auto count = source.read(buffer.data(), std::min(ChunkBytes, limit - m_received + 1));
        if (count < 0) {
            m_error = tr("读取插件包下载数据失败：%1").arg(source.errorString());
            return false;
        }
        if (!count)
            break;
        m_received += count;
        if (m_received > limit) {
            m_limited = true;
            return false;
        }
        qint64 written = 0;
        while (written < count) {
            const auto size = m_file->write(buffer.data() + written, count - written);
            if (size <= 0) {
                m_storageFailed = true;
                m_error = tr("写入插件包临时文件失败：%1").arg(m_file->errorString());
                return false;
            }
            written += size;
        }
        m_hash.addData(QByteArrayView(buffer.data(), count));
    }
    return true;
}
bool PluginArchiveDownload::finish()
{
    if (!m_file || !m_error.isEmpty() || m_limited)
        return false;
    if (QString::fromLatin1(m_hash.result().toHex()) != m_expectedHash) {
        m_error = tr("下载的插件文件与作者发布记录不一致，已停止安装。请刷新插件列表后重试；"
                     "若仍失败，请联系插件作者。");
        cancel();
        return false;
    }
    if (!m_file->commit()) {
        m_storageFailed = true;
        m_error = tr("保存插件包失败：%1").arg(m_file->errorString());
        cancel();
        return false;
    }
    m_file.reset();
    return true;
}
void PluginArchiveDownload::cancel()
{
    if (m_file)
        m_file->cancelWriting();
    m_file.reset();
}
} // namespace EchoMusic
