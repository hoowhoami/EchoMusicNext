#include "StorageFence.h"
#include "ThemeImageStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
namespace EchoMusic
{
ThemeImageResult ThemeImageStore::importImage(const QString &source, const QString &directory,
                                              std::shared_ptr<std::atomic_bool> canceled)
{
    const auto interrupted = [&] { return canceled && canceled->load(); };
    if (interrupted())
        return {{}, QObject::tr("图片处理已取消"), {}, {}};
    const auto storage = StorageFence::write(directory);
    if (!storage)
        return {{}, QObject::tr("备份进行中，请稍后导入图片"), {}, {}};
    const QFileInfo file(source);
    if (!file.isFile() || file.size() <= 0 || file.size() > 20 * 1024 * 1024)
        return {{}, QObject::tr("请选择不超过 20 MB 的图片"), {}, {}};
    const auto appDirectory = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString name = "echo-theme-image-worker.exe";
#else
    const QString name = "echo-theme-image-worker";
#endif
    const QStringList candidates{QDir(appDirectory).filePath("../Helpers/" + name),
                                 QDir(appDirectory).filePath(name),
                                 QString::fromUtf8(ECHO_THEME_WORKER_PATH)};
    QString executable;
    for (const auto &candidate : candidates) {
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) {
            executable = candidate;
            break;
        }
    }
    if (executable.isEmpty())
        return {{}, QObject::tr("背景图片处理组件不可用，请重新安装应用"), {}, {}};
    // Worker-thread-owned process, fixed executable and argument vector. No
    // shell, UI IPC or account credentials; cancellation refuses the generation
    // in ThemeStore. A stalled decoder is killed/reaped within the bounded job.
    if (!QDir().mkpath(directory))
        return {{}, QObject::tr("无法创建背景图片目录"), {}, {}};
    QTemporaryDir staging(QDir(directory).filePath(".import-XXXXXX"));
    if (!staging.isValid())
        return {{}, QObject::tr("无法创建背景图片临时目录"), {}, {}};
    QProcess worker;
    worker.start(executable, {source, staging.path()}, QIODevice::ReadOnly);
    QElapsedTimer deadline;
    deadline.start();
    const bool started = worker.waitForStarted(5000);
    while (started && worker.state() != QProcess::NotRunning && !interrupted() &&
           deadline.elapsed() < 30000)
        worker.waitForFinished(100);
    if (!started || worker.state() != QProcess::NotRunning || interrupted()) {
        worker.kill();
        worker.waitForFinished(5000);
        return {{},
                interrupted() ? QObject::tr("图片处理已取消")
                              : QObject::tr("图片处理超时，请重新选择"),
                {},
                {}};
    }
    const auto bytes = worker.readAllStandardOutput();
    if (worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0 ||
        bytes.size() > 65536)
        return {{}, QObject::tr("图片处理失败，请重新选择"), {}, {}};
    QJsonParseError parse;
    const auto object = QJsonDocument::fromJson(bytes, &parse).object();
    if (parse.error != QJsonParseError::NoError || object.value("version").toInt() != 1)
        return {{}, QObject::tr("背景图片处理组件返回无效结果"), {}, {}};
    const auto error = object.value("error").toString();
    if (!error.isEmpty())
        return {{}, error, {}, {}};
    const auto id = object.value("id").toString();
    const QSize size(object.value("width").toInt(), object.value("height").toInt());
    const QColor sample(object.value("sample").toString());
    if (!validId(id) || !size.isValid() || size.width() > 2560 || size.height() > 2560 ||
        !sample.isValid() || !QFileInfo(QDir(staging.path()).filePath(id)).isFile())
        return {{}, QObject::tr("背景图片处理组件返回无效结果"), {}, {}};
    // Only a bounded normalized asset crosses into this process. The staging
    // owner also removes any QSaveFile temporary file left by a killed decoder.
    QFile normalized(QDir(staging.path()).filePath(id));
    if (!normalized.open(QIODevice::ReadOnly) || normalized.size() > 32 * 1024 * 1024)
        return {{}, QObject::tr("背景图片处理组件返回无效结果"), {}, {}};
    const auto png = normalized.readAll();
    if (interrupted())
        return {{}, QObject::tr("图片处理已取消"), {}, {}};
    if (QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()) +
            ".png" !=
        id)
        return {{}, QObject::tr("背景图片处理组件返回无效结果"), {}, {}};
    QSaveFile target(QDir(directory).filePath(id));
    if (!target.open(QIODevice::WriteOnly) || target.write(png) != png.size() || !target.commit())
        return {{}, QObject::tr("背景图片保存失败"), {}, {}};
    return {id, {}, size, sample};
}
} // namespace EchoMusic
