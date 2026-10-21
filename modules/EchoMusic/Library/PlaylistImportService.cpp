#include "PlaylistImportService.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <utility>
#include <algorithm>

namespace EchoMusic
{
namespace
{
struct Images
{
    QList<QByteArray> data;
    QString error;
};
Images readImages(const QList<QUrl> &urls)
{
    Images result;
    for (const auto &url : urls) {
        if (!url.isLocalFile()) {
            result.error = QObject::tr("请选择本地 JPEG / PNG 截图");
            return result;
        }
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 10 * 1024 * 1024) {
            result.error = QObject::tr("截图无法读取或超过 10 MB");
            return result;
        }
        QImageReader reader(url.toLocalFile());
        const auto format = reader.format().toLower();
        if ((format != "png" && format != "jpeg" && format != "jpg") || !reader.canRead()) {
            result.error = QObject::tr("仅支持 JPEG / PNG 截图");
            return result;
        }
        const auto bytes = file.read(10 * 1024 * 1024 + 1);
        if (bytes.size() != file.size() || bytes.size() > 10 * 1024 * 1024) {
            result.error = QObject::tr("读取截图时文件发生变化");
            return result;
        }
        result.data.append(bytes);
    }
    return result;
}
} // namespace
PlaylistImportService::PlaylistImportService(ApiClient &api, UserPlaylistModel &source,
                                             PlaylistMutationService &mutations, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_source(source), m_mutations(mutations)
{
    m_poll.setSingleShot(true);
    m_poll.setInterval(1500);
    connect(&m_poll, &QTimer::timeout, this, &PlaylistImportService::poll);
    connect(&api, &ApiClient::succeeded, this, &PlaylistImportService::succeed);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (!id || id != m_request)
            return;
        const auto message = QJsonDocument::fromJson(payload).object().value("message").toString();
        fail(message.isEmpty() ? tr("导入请求失败，请稍后重试") : message);
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        reset();
        emit accountInvalidated();
    });
    connect(&api, &ApiClient::closed, this, [this] { reset(); });
    connect(&mutations, &PlaylistMutationService::completed, this,
            [this](const QByteArray &payload) {
                if (m_stage != Creating || !busy() || m_session != m_api.sessionRevision())
                    return;
                const auto data = QJsonDocument::fromJson(payload).object();
                if (data.value("user_id").toInteger() != qint64(m_api.currentAccountUserId()) ||
                    data.value("cloud_list_id").toInteger() <= 0) {
                    fail(tr("无法确认新歌单，请刷新后重试"));
                    return;
                }
                m_target = quint64(data.value("cloud_list_id").toInteger());
                uploadNext();
            });
    connect(&mutations, &PlaylistMutationService::changed, this, [this] {
        if (m_stage == Creating && !m_mutations.errorMessage().isEmpty())
            fail(m_mutations.errorMessage());
    });
}
PlaylistImportService::~PlaylistImportService()
{
    if (m_request)
        m_api.cancel(m_request);
}
QVariant PlaylistImportService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_missedRows.size())
        return {};
    if (role != TitleRole && role != ArtistRole && role != ReasonRole)
        return {};
    const auto &row = m_missedRows[index.row()];
    return row.value(role == TitleRole    ? "title"
                     : role == ArtistRole ? "artist"
                                          : "reason")
        .toVariant();
}
QVariantList PlaylistImportService::targets() const
{
    QVariantList result;
    for (const auto &row : m_targets)
        result.append(
            QVariantMap{{"label", row.value("title").toString()},
                        {"value", QString::number(row.value("cloud_list_id").toInteger())}});
    return result;
}
QVariantList PlaylistImportService::stages() const
{
    if (m_phase == Failed)
        return {QVariantMap{{"title", tr("导入失败")}, {"artist", m_error}, {"status", "failed"}}};
    if (!m_task)
        return {QVariantMap{{"title", m_label},
                            {"artist", tr("请稍候")},
                            {"status", m_phase == Failed ? "failed" : "matching"}}};
    const bool done = m_cloudStatus == 3;
    QVariantList result{
        QVariantMap{{"title", tr("提交导入任务")}, {"artist", tr("已提交")}, {"status", "success"}},
        QVariantMap{
            {"title", tr("读取歌单信息")},
            {"artist",
             done || m_total
                 ? tr("%1 · %2 首").arg(m_title.isEmpty() ? tr("歌单") : m_title).arg(m_total)
                 : tr("正在读取歌单信息")},
            {"status", done || m_total ? "success" : "matching"}},
        QVariantMap{{"title", tr("创建新歌单")},
                    {"artist", m_target ? tr("歌单已创建") : tr("等待创建歌单")},
                    {"status", m_target || done ? "success" : "pending"}},
        QVariantMap{{"title", tr("添加歌曲")},
                    {"artist", m_total ? tr("已导入 %1 / %2").arg(m_imported).arg(m_total)
                                       : tr("等待歌曲信息")},
                    {"status", done                  ? "success"
                               : m_target || m_total ? "adding"
                                                     : "pending"}}};
    return result;
}
double PlaylistImportService::progress() const
{
    if (m_phase == Completed || m_phase == Failed || m_cloudStatus == 3)
        return 1;
    const auto total = std::max({quint64(1), m_total, m_imported + m_missed});
    return m_total ? double(std::min(total - 1, m_imported)) / double(total) : m_task ? 0.25 : 0;
}
void PlaylistImportService::reset()
{
    ++m_generation;
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    m_poll.stop();
    m_phase = Idle;
    m_enteredProgress = false;
    m_stage = None;
    m_task = m_target = m_total = m_imported = m_missed = 0;
    m_imageIndex = m_imageCount = m_cloudStatus = 0;
    m_title.clear();
    m_label.clear();
    m_error.clear();
    m_duplicate.clear();
    m_serial.clear();
    m_images.clear();
    m_targets.clear();
    m_catalogRows.clear();
    beginResetModel();
    m_missedRows.clear();
    endResetModel();
    emit changed();
}
bool PlaylistImportService::beginRun()
{
    if (busy() || !m_api.currentAccountUserId())
        return false;
    reset();
    m_session = m_api.sessionRevision();
    m_phase = Preparing;
    m_label = tr("正在准备导入");
    emit started();
    emit changed();
    return true;
}
bool PlaylistImportService::startLink(const QString &text)
{
    if (!beginRun())
        return false;
    m_title = tr("歌单导入");
    m_stage = Adding;
    m_request = m_api.importPlaylistLink(text.trimmed());
    if (!m_request)
        fail(tr("请输入歌单分享链接"));
    emit changed();
    return m_request != 0;
}
void PlaylistImportService::refreshTargets()
{
    if (busy() || m_request || !m_api.currentAccountUserId())
        return;
    m_session = m_api.sessionRevision();
    beginCatalog(false);
}
void PlaylistImportService::beginCatalog(bool create)
{
    m_newTarget = create;
    m_catalogRows.clear();
    m_page = 1;
    m_stage = Catalog;
    m_request = m_api.userPlaylists(1, 200);
    if (!m_request)
        fail(tr("无法读取目标歌单，请重试"));
    emit changed();
}
QVariantMap PlaylistImportService::prepareScreenshotSelection(const QList<QUrl> &files) const
{
    const auto selected = files.mid(0, 9);
    for (const auto &url : selected) {
        const QFileInfo file(url.toLocalFile());
        if (url.isLocalFile() && file.size() > 10 * 1024 * 1024)
            return {{"files", QVariant::fromValue(QList<QUrl>{})},
                    {"message", tr("%1 超过 10 MB").arg(file.fileName())}};
    }
    return {{"files", QVariant::fromValue(selected)},
            {"message", files.size() > 9 ? tr("一次最多选择 9 张截图") : QString()}};
}
bool PlaylistImportService::startScreenshot(const QList<QUrl> &files, const QString &id,
                                            const QString &title)
{
    const auto selection = prepareScreenshotSelection(files);
    const auto selected = selection.value("files").value<QList<QUrl>>();
    if (selected.isEmpty()) {
        m_error = selection.value("message").toString();
        if (m_error.isEmpty())
            m_error = tr("请选择截图");
        emit changed();
        return false;
    }
    bool valid = false;
    const auto target = id.toULongLong(&valid);
    if ((!valid || !target) && title.trimmed().isEmpty()) {
        m_error = tr("请选择或创建目标歌单");
        emit changed();
        return false;
    }
    if (!beginRun())
        return false;
    m_imageCount = selected.size();
    m_target = valid ? target : 0;
    m_title = title.trimmed();
    m_newTarget = m_target == 0;
    m_serial = QString::number(m_api.currentAccountUserId()) +
               QString::number(QDateTime::currentMSecsSinceEpoch());
    const auto generation = m_generation;
    auto *watcher = new QFutureWatcher<Images>(this);
    connect(watcher, &QFutureWatcher<Images>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation || m_session != m_api.sessionRevision())
            return;
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        m_images = result.data;
        beginCatalog(m_newTarget);
    });
    watcher->setFuture(QtConcurrent::run([selected] { return readImages(selected); }));
    return true;
}
void PlaylistImportService::confirmDuplicate(bool accepted, const QString &title)
{
    if (m_duplicate.isEmpty() || m_phase != Preparing)
        return;
    if (accepted && title.trimmed().isEmpty())
        return;
    m_duplicate.clear();
    if (accepted) {
        m_title = title.trimmed();
        createTarget();
    } else
        stop();
    emit changed();
}
void PlaylistImportService::createTarget()
{
    m_stage = Creating;
    m_label = tr("正在创建歌单");
    if (!m_mutations.create(m_title, false))
        fail(tr("无法创建歌单，请稍后重试"));
    emit changed();
}
void PlaylistImportService::uploadNext()
{
    if (m_imageIndex >= m_images.size()) {
        m_images.clear();
        m_stage = Adding;
        m_request = m_api.importPlaylistScreenshot(m_serial, {m_target}, m_title);
        if (!m_request)
            fail(tr("无法创建截图导入任务"));
        emit changed();
        return;
    }
    m_phase = Uploading;
    m_enteredProgress = true;
    m_stage = Image;
    m_label = tr("正在上传截图 %1 / %2").arg(m_imageIndex + 1).arg(m_images.size());
    m_request = m_api.submitPlaylistImportImage(m_serial, m_images[m_imageIndex]);
    if (!m_request)
        fail(tr("无法提交截图，请重试"));
    emit changed();
}
void PlaylistImportService::poll()
{
    if (m_phase != Cloud || m_request || m_session != m_api.sessionRevision())
        return;
    m_stage = Polling;
    m_request = m_api.playlistImportStatus(m_task);
    if (!m_request)
        fail(tr("无法读取导入进度"));
}
void PlaylistImportService::fail(const QString &message)
{
    m_request = 0;
    m_poll.stop();
    m_images.clear();
    m_stage = None;
    m_phase = Failed;
    m_error = message;
    m_label = tr("导入失败");
    emit changed();
    emit notice(message, true);
}
void PlaylistImportService::stop()
{
    ++m_generation;
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    m_poll.stop();
    m_images.clear();
    m_duplicate.clear();
    m_stage = None;
    m_phase = Stopped;
    m_label = m_task ? tr("已停止查看进度，导入任务仍会继续处理")
                     : tr("已停止导入；已提交请求的结果请刷新歌单确认");
    emit changed();
    if (m_task)
        emit notice(m_label, true);
}
void PlaylistImportService::dismiss()
{
    if (!busy())
        reset();
}
void PlaylistImportService::complete()
{
    m_stage = None;
    m_phase = Completed;
    m_label = tr("导入完成");
    m_images.clear();
    m_source.reload();
    emit changed();
    emit notice(tr("导入完成：成功 %1 / %2").arg(m_imported).arg(m_total), false);
}
void PlaylistImportService::succeed(quint64 request, const QByteArray &payload)
{
    if (!request || request != m_request || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto document = QJsonDocument::fromJson(payload);
    const auto object = document.object();
    if (m_stage == Catalog) {
        const auto page = object.value("page").toObject();
        const auto version = object.value("total_revision").toInteger(-1);
        const auto total = page.value("total").toInteger(-1);
        const auto items = page.value("items").toArray();
        if (object.value("user_id").toInteger() != qint64(m_api.currentAccountUserId()) ||
            version < 0 || !page.value("items").isArray() ||
            (m_page > 1 && quint64(version) != m_catalogVersion)) {
            fail(tr("歌单在加载期间发生变化，请重新加载"));
            return;
        }
        m_catalogVersion = quint64(version);
        m_catalogTotal = quint64(qMax<qint64>(0, total));
        const auto previousCount = m_catalogRows.size();
        QHash<quint64, qsizetype> seen;
        for (qsizetype i = 0; i < m_catalogRows.size(); ++i)
            seen.insert(quint64(m_catalogRows[i].value("cloud_list_id").toInteger()), i);
        for (const auto &row : items) {
            const auto value = row.toObject();
            const auto key = quint64(value.value("cloud_list_id").toInteger());
            const auto found = seen.constFind(key);
            if (found != seen.cend())
                m_catalogRows[*found] = value;
            else {
                seen.insert(key, m_catalogRows.size());
                m_catalogRows.append(value);
            }
        }
        // Repeated pages must not create unbounded work if total is inaccurate.
        if (m_catalogRows.size() > previousCount &&
            (total >= 0 ? quint64(m_catalogRows.size()) < m_catalogTotal : items.size() >= 200)) {
            m_request = m_api.userPlaylists(++m_page, 200);
            if (!m_request)
                fail(tr("无法继续读取歌单"));
            return;
        }
        m_targets.clear();
        for (const auto &row : m_catalogRows)
            if (row.value("kind").toString() == "owned" && row.value("source_code").toInt() == 1 &&
                row.value("creator_id").toInteger() == qint64(m_api.currentAccountUserId()))
                m_targets.append(row);
        m_catalogRows.clear();
        m_stage = None;
        if (m_phase == Preparing) {
            if (m_newTarget) {
                for (const auto &row : m_targets)
                    if (row.value("title").toString().trimmed().toLower() == m_title.toLower()) {
                        m_duplicate = m_title;
                        emit changed();
                        emit duplicateConfirmationRequested();
                        return;
                    }
                createTarget();
            } else {
                const auto found = std::find_if(
                    m_targets.begin(), m_targets.end(), [this](const QJsonObject &row) {
                        return quint64(row.value("cloud_list_id").toInteger()) == m_target;
                    });
                if (found == m_targets.end()) {
                    fail(tr("目标歌单已变化，请重新选择"));
                    return;
                }
                m_title = found->value("title").toString();
                uploadNext();
            }
        }
        emit changed();
        return;
    }
    if (m_stage == Image) {
        if (!object.value("uploaded").toBool()) {
            fail(tr("截图上传未确认"));
            return;
        }
        ++m_imageIndex;
        uploadNext();
        return;
    }
    if (m_stage == Adding) {
        if (object.value("user_id").toInteger() != qint64(m_api.currentAccountUserId()) ||
            object.value("task_id").toInteger() <= 0) {
            fail(tr("创建导入任务的响应无法确认；请刷新歌单后重试"));
            return;
        }
        m_task = quint64(object.value("task_id").toInteger());
        m_phase = Cloud;
        m_enteredProgress = true;
        m_label = tr("已提交，正在读取歌单信息");
        emit changed();
        poll();
        return;
    }
    if (m_stage == Polling) {
        const auto rows = document.array();
        if (rows.size() != 1) {
            fail(tr("未查询到导入任务"));
            return;
        }
        const auto task = rows[0].toObject();
        if (task.value("task_id").toInteger() != qint64(m_task) ||
            task.value("user_id").toInteger() != qint64(m_api.currentAccountUserId())) {
            fail(tr("导入任务响应无法确认"));
            return;
        }
        m_target = quint64(task.value("cloud_list_id").toInteger());
        m_total = quint64(task.value("total").toInteger());
        m_imported = quint64(task.value("imported").toInteger());
        m_missed = quint64(task.value("missed").toInteger());
        if (!task.value("title").toString().isEmpty())
            m_title = task.value("title").toString();
        const auto status = m_cloudStatus = task.value("status").toInt();
        m_label =
            m_total ? tr("已导入 %1 / %2").arg(m_imported).arg(m_total) : tr("正在读取歌单信息");
        if (status >= 10) {
            const auto message = task.value("message").toString();
            fail(message.isEmpty() ? tr("导入任务失败（状态 %1）").arg(status) : message);
            return;
        }
        if (status == 3) {
            if (!m_missed || !m_target) {
                complete();
                return;
            }
            m_stage = Result;
            m_page = 1;
            m_request = m_api.playlistImportResult({m_target}, 1, 100);
            if (!m_request)
                fail(tr("无法读取导入明细"));
        } else
            m_poll.start();
        emit changed();
        return;
    }
    if (m_stage == Result) {
        const auto task = object.value("task").toObject();
        const auto rows = object.value("missed_tracks").toArray();
        if (task.value("task_id").toInteger() != qint64(m_task) ||
            task.value("cloud_list_id").toInteger() != qint64(m_target)) {
            fail(tr("导入明细响应无法确认"));
            return;
        }
        if (!rows.isEmpty()) {
            const int first = m_missedRows.size();
            beginInsertRows({}, first, first + rows.size() - 1);
            for (const auto &row : rows)
                m_missedRows.append(row.toObject());
            endInsertRows();
        }
        if (quint64(m_missedRows.size()) < m_missed && rows.size() == 100) {
            m_request = m_api.playlistImportResult({m_target}, ++m_page, 100);
            if (!m_request)
                fail(tr("无法继续读取导入明细"));
            return;
        }
        complete();
    }
}
} // namespace EchoMusic
