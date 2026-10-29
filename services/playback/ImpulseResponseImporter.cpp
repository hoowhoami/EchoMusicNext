#include "StorageFence.h"
#include "ImpulseResponseImporter.h"
#include <QtConcurrent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <utility>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QCryptographicHash>

namespace EchoMusic
{
ImpulseResponseImporter::ImpulseResponseImporter(NativeBridge &native, QString directory,
                                                 QObject *parent)
    : QObject(parent), m_native(native), m_directory(std::move(directory))
{
    connect(&m_copy, &QFutureWatcher<Result>::finished, this, [this] {
        if (m_closing) {
            closeIfIdle();
            return;
        }
        const auto result = m_copy.result();
        if (!result.error.isEmpty())
            m_errors.append(result.error);
        else if (!result.path.isEmpty()) {
            ++m_count;
            emit imported(result.path, result.name);
        }
        next();
    });
    connect(&native, &NativeBridge::completed, this,
            [this](quint64 id, quint64, bool success, const QByteArray &bytes) {
                if (!m_probe || id != m_probe || m_closing)
                    return;
                m_probe = 0;
                const auto tracks = QJsonDocument::fromJson(bytes).array();
                bool audio = false;
                for (const auto &track : tracks)
                    audio |= track.toObject().value("type").toString() == "audio";
                if (!success || !audio) {
                    m_errors.append(
                        tr("%1：该文件不是可识别的音频文件").arg(QFileInfo(m_source).fileName()));
                    next();
                    return;
                }
                const auto source = m_source, directory = m_directory;
                const auto cancel = m_cancel;
                m_copy.setFuture(QtConcurrent::run([source, directory, cancel] {
                    Result result;
                    const auto storage = StorageFence::write(directory);
                    if (!storage) {
                        result.error = tr("备份进行中，请稍后导入音效");
                        return result;
                    }
                    QFile input(source);
                    if (!input.open(QIODevice::ReadOnly) || !QFileInfo(input).isFile() ||
                        !QDir().mkpath(directory)) {
                        result.error = tr("%1：音效文件导入失败").arg(QFileInfo(source).fileName());
                        return result;
                    }
                    // A temporary atomic destination holds the same bytes we hash.
                    // Catalog publication only follows commit; cancellation aborts it.
                    const auto temporary = directory + "/.import-" +
                                           QUuid::createUuid().toString(QUuid::WithoutBraces);
                    QSaveFile output(temporary);
                    if (!output.open(QIODevice::WriteOnly)) {
                        result.error = tr("无法保存音效文件");
                        return result;
                    }
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    while (!input.atEnd()) {
                        if (cancel->load())
                            return result;
                        const auto data = input.read(65536);
                        if (data.isEmpty() || output.write(data) != data.size()) {
                            result.error = tr("读取或保存音效文件失败");
                            return result;
                        }
                        hash.addData(data);
                    }
                    if (cancel->load() || !output.commit())
                        return result;
                    const auto extension = QFileInfo(source).suffix().toLower();
                    const QStringList formats{"irs", "wav", "wave", "flac", "aif", "aiff", "caf",
                                              "ogg", "oga", "mp3",  "m4a",  "aac", "opus"};
                    const auto destination = directory + "/" +
                                             QString::fromLatin1(hash.result().toHex()) + "." +
                                             (formats.contains(extension) ? extension : "irs");
                    if (cancel->load()) {
                        QFile::remove(temporary);
                        return result;
                    }
                    if (QFileInfo::exists(destination))
                        QFile::remove(temporary);
                    else if (!QFile::rename(temporary, destination)) {
                        QFile::remove(temporary);
                        result.error = tr("无法保存音效文件");
                        return result;
                    }
                    result.path = destination;
                    result.name = QFileInfo(source).fileName();
                    return result;
                }));
            });
}
ImpulseResponseImporter::~ImpulseResponseImporter()
{
    shutdown();
    m_copy.waitForFinished();
}
void ImpulseResponseImporter::importFiles(const QList<QUrl> &urls)
{
    if (m_busy || m_closing || urls.isEmpty())
        return;
    m_busy = true;
    m_count = 0;
    m_errors.clear();
    m_urls = urls;
    emit changed();
    next();
}
void ImpulseResponseImporter::next()
{
    if (m_closing) {
        closeIfIdle();
        return;
    }
    while (!m_urls.isEmpty()) {
        const auto url = m_urls.takeFirst();
        m_source = url.isLocalFile() ? url.toLocalFile() : QString();
        if (!m_source.isEmpty() && QFileInfo(m_source).isFile()) {
            m_probe = m_native.submit(NativeBridge::AudioTracks, m_source, 0);
            if (m_probe)
                return;
        }
        m_errors.append(tr("请选择有效的本地音频文件"));
    }
    m_busy = false;
    emit changed();
    // EchoMusic accepts successful members of a partially valid batch.
    emit finished(m_count, m_count > 0 ? QString() : m_errors.value(0, tr("音效文件导入失败")));
}
void ImpulseResponseImporter::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    m_cancel->store(true);
    m_probe = 0;
    m_urls.clear();
    closeIfIdle();
}
void ImpulseResponseImporter::closeIfIdle()
{
    if (m_closing && !m_copy.isRunning() && !std::exchange(m_closed, true))
        emit closed();
}
} // namespace EchoMusic
