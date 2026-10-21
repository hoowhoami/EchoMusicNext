#include "CloudUploadService.h"
#include "RouteController.h"
#include "echo_ffi.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
namespace EchoMusic
{
namespace
{
const QSet<QString> extensions = {"aac", "aif", "aiff", "amr", "ape",  "caf", "dff",  "dsf", "flac",
                                  "m4a", "mp3", "oga",  "ogg", "opus", "wav", "wave", "wma"};
QString artists(const QJsonObject &song)
{
    QStringList names;
    for (const auto &v : song.value("artists").toArray()) {
        const auto n = v.toObject().value("name").toString();
        if (!n.isEmpty())
            names << n;
    }
    return names.join(" / ");
}
QString normalized(QString value)
{
    value = value.toLower();
    value.remove(QRegularExpression("[（(\\[【][^）)\\]】]*[）)\\]】]"));
    value.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
    return value;
}
void metadataCallback(void *context, const EchoResult *result)
{
    if (result && result->status == Success)
        *static_cast<QJsonObject *>(context) =
            QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(result->payload),
                                               qsizetype(result->payload_len)))
                .object();
}
} // namespace
CloudUploadService::CloudUploadService(ApiClient &api, bool network, QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_clock.start();
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_cooldown.setSingleShot(true);
    m_cooldown.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CloudUploadService::dispatchLookup);
    connect(&m_cooldown, &QTimer::timeout, this, &CloudUploadService::notify);
    connect(&api, &ApiClient::succeeded, this, &CloudUploadService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { reject(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        ++m_run;
        cancelPending();
        if (m_tasks)
            m_tasks->remove(m_task);
        m_task = {};
        m_cache.clear();
        m_resumeAt = 0;
        beginResetModel();
        m_files.clear();
        endResetModel();
        m_candidates = {};
        m_step = "pick";
        m_phase = "idle";
        m_error.clear();
        m_dialogRequested = false;
        emit accountInvalidated();
        notify();
    });
    connect(&api, &ApiClient::closed, this, &CloudUploadService::shutdown);
    connect(&api, &ApiClient::verificationRequired, this, [this](quint64 id, const QString &) {
        if (id == m_request)
            pause(tr("请完成账号安全验证，再确认继续上传"), 60);
    });
    connect(&m_scan, &QFutureWatcher<QList<File>>::finished, this, [this] {
        if (m_closed || m_scanRun != m_run)
            return;
        beginResetModel();
        m_files = m_scan.result();
        endResetModel();
        if (m_files.isEmpty()) {
            m_phase = "done";
            m_step = "pick";
            m_error = tr("没有可上传的音频文件");
            if (m_tasks)
                m_tasks->remove(m_task);
            m_task = {};
            notify();
            emit notice(m_error, true);
            return;
        }
        if (m_manual && m_files.first().state == "failed") {
            m_phase = "done";
            m_step = "progress";
            m_error = m_files.first().error;
            notify();
            emit notice(m_error, true);
            return;
        }
        if (m_manual) {
            m_phase = "idle";
            m_step = "manual";
            if (m_tasks)
                m_tasks->remove(m_task);
            m_task = {};
            notify();
            return;
        }
        if (failed() > 0)
            emit notice(tr("%1 个文件被跳过（超出大小限制或读取失败）").arg(failed()), true);
        m_index = 0;
        m_phase = "matching";
        notify();
        matchNext();
    });
}
CloudUploadService::~CloudUploadService()
{
    shutdown();
    m_scan.waitForFinished();
}
QVariant CloudUploadService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_files.size())
        return {};
    const auto &f = m_files[index.row()];
    switch (role) {
    case TitleRole:
        return f.title;
    case ArtistRole:
        return f.artist;
    case FileNameRole:
        return f.name;
    case SizeRole:
        return f.size;
    case StateRole:
        return f.state;
    case ErrorRole:
        return f.error;
    case MatchedRole:
        return f.match.value("audio_id").toInteger() > 0 ||
               f.match.value("id").toVariant().toLongLong() > 0;
    case InstantRole:
        return f.instant;
    case PercentRole:
        return f.size ? 100.0 * f.sent / f.size : 0;
    default:
        return {};
    }
}
QHash<int, QByteArray> CloudUploadService::roleNames() const
{
    return {{TitleRole, "trackTitle"}, {ArtistRole, "artist"},     {FileNameRole, "fileName"},
            {SizeRole, "fileSize"},    {StateRole, "uploadState"}, {ErrorRole, "uploadError"},
            {MatchedRole, "matched"},  {PercentRole, "percent"},   {InstantRole, "instantUpload"}};
}
int CloudUploadService::succeeded() const
{
    return int(std::count_if(m_files.cbegin(), m_files.cend(),
                             [](const File &f) { return f.state == "success"; }));
}
int CloudUploadService::failed() const
{
    return int(std::count_if(m_files.cbegin(), m_files.cend(),
                             [](const File &f) { return f.state == "failed"; }));
}
int CloudUploadService::instant() const
{
    return int(std::count_if(m_files.cbegin(), m_files.cend(),
                             [](const File &f) { return f.state == "success" && f.instant; }));
}
double CloudUploadService::progress() const
{
    if (m_files.isEmpty())
        return 0;
    if (m_phase == "matching" || (paused() && m_resumePhase == "matching"))
        return .2 * double(m_index) / double(m_files.size());
    double fraction = completed();
    if (m_index < m_files.size() && m_files[m_index].state == "uploading" &&
        m_files[m_index].size > 0)
        fraction += double(m_files[m_index].sent) / m_files[m_index].size;
    return std::clamp(.2 + .8 * fraction / m_files.size(), 0., 1.);
}
QString CloudUploadService::statusLabel() const
{
    if (m_phase == "scanning")
        return tr("正在读取音频文件...");
    if (paused())
        return tr("上传已暂停");
    if (m_phase == "matching")
        return tr("正在匹配歌曲信息 %1/%2").arg(m_index).arg(total());
    if (m_phase == "uploading")
        return tr("正在上传 %1/%2").arg(completed()).arg(total());
    if (m_phase == "stopped")
        return tr("上传已取消");
    if (m_phase == "done")
        return tr("上传完成，成功 %1，失败 %2，秒传 %3")
            .arg(succeeded())
            .arg(failed())
            .arg(instant());
    return tr("上传音乐");
}
QVariantMap CloudUploadService::manualFile() const
{
    if (m_files.isEmpty())
        return {};
    const auto &f = m_files.first();
    return {{"title", f.title},
            {"artist", f.artist},
            {"album", f.album},
            {"name", f.name},
            {"size", f.size}};
}
QVariantList CloudUploadService::candidates() const
{
    QVariantList result;
    for (const auto &v : m_candidates) {
        auto o = v.toObject();
        o.insert("artist", artists(o));
        result << o.toVariantMap();
    }
    return result;
}
void CloudUploadService::open(bool detail)
{
    if (m_closed || !m_api.currentAccountUserId())
        return;
    if (!detail && !busy())
        reset();
    m_dialogRequested = true;
    if (m_router)
        m_router->navigate("cloud");
    notify();
}
void CloudUploadService::dismiss()
{
    if (!busy()) {
        if (searching())
            stop();
        reset();
    }
}
void CloudUploadService::acknowledgeOpen()
{
    m_dialogRequested = false;
    emit changed();
}
void CloudUploadService::selectFiles(const QList<QUrl> &files, bool manual)
{
    scan(files, false, manual);
}
void CloudUploadService::selectFolder(const QUrl &folder)
{
    scan({folder}, true, false);
}
void CloudUploadService::scan(QList<QUrl> urls, bool folder, bool manual)
{
    if (m_closed || !m_network || !m_api.currentAccountUserId() || busy() || m_scan.isRunning() ||
        urls.isEmpty())
        return;
    reset();
    ++m_run;
    m_scanRun = m_run;
    m_session = m_api.sessionRevision();
    m_manual = manual;
    m_phase = "scanning";
    m_step = "progress";
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto run = m_run;
    if (m_tasks) {
        TaskCenterModel::Registration r;
        r.id = "cloud-upload";
        r.content.name = tr("云盘上传");
        r.policy = TaskCenterModel::Policy::actionRequired();
        r.cancel = [this] { stop(); };
        m_task = m_tasks->registerTask(*this, std::move(r));
        m_taskFinished = false;
    }
    notify();
    m_scan.setFuture(QtConcurrent::run([this, urls = std::move(urls), folder, manual, cancel, run] {
        QList<File> result;
        QStringList paths;
        QSet<QString> seen;
        auto collect = [&](const QString &path) {
            QFileInfo info(path);
            const auto canonical = info.canonicalFilePath();
            if (!canonical.isEmpty() && !seen.contains(canonical) &&
                extensions.contains(info.suffix().toLower())) {
                seen.insert(canonical);
                paths << canonical;
            }
        };
        if (folder && urls.first().isLocalFile()) {
            QDirIterator it(urls.first().toLocalFile(), QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && !cancel->load() && paths.size() < 10000)
                collect(it.next());
            if (it.hasNext() && !cancel->load())
                QMetaObject::invokeMethod(
                    this,
                    [this, run] {
                        if (!m_closed && run == m_run)
                            emit notice(tr("文件数量超过扫描上限，已截断"), true);
                    },
                    Qt::QueuedConnection);
        } else
            for (const auto &url : urls) {
                if (cancel->load())
                    break;
                if (url.isLocalFile())
                    collect(url.toLocalFile());
            }
        if (manual && paths.size() > 1)
            paths = paths.mid(0, 1);
        for (const auto &path : paths) {
            if (cancel->load())
                break;
            QFileInfo info(path);
            File row;
            row.path = path;
            row.name = info.fileName();
            row.ext = info.suffix().toLower();
            row.title = info.completeBaseName().trimmed();
            row.size = info.size();
            row.modified = info.lastModified().toMSecsSinceEpoch();
            if (row.size <= 0 || row.size > 100 * 1024 * 1024) {
                row.state = "failed";
                row.error = QObject::tr("文件为空或超过 100 MB");
                result << row;
                continue;
            }
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly)) {
                row.state = "failed";
                row.error = QObject::tr("无法读取文件");
                result << row;
                continue;
            }
            QCryptographicHash digest(QCryptographicHash::Md5);
            bool readFailed = false;
            while (!input.atEnd() && !cancel->load()) {
                const auto chunk = input.read(512 * 1024);
                if (chunk.isEmpty() && input.error() != QFile::NoError) {
                    readFailed = true;
                    break;
                }
                digest.addData(chunk);
            }
            if (cancel->load())
                break;
            QJsonObject metadata;
            const auto encoded = path.toUtf8();
            echo_audio_inspect_upload_file(reinterpret_cast<const uint8_t *>(encoded.constData()),
                                           size_t(encoded.size()), metadataCallback, &metadata);
            const QFileInfo after(path);
            if (readFailed || after.size() != row.size ||
                after.lastModified().toMSecsSinceEpoch() != row.modified) {
                row.state = "failed";
                row.error = QObject::tr("文件读取失败或读取期间发生变化");
                result << row;
                continue;
            }
            row.hash = QString::fromLatin1(digest.result().toHex());
            const auto title = metadata.value("title").toString().trimmed();
            if (!title.isEmpty())
                row.title = title;
            row.artist = metadata.value("artist").toString().trimmed();
            row.album = metadata.value("album").toString();
            row.duration = metadata.value("duration_ms").toInteger();
            if (row.artist.isEmpty() && title.isEmpty()) {
                const auto split = row.title.indexOf(" - ");
                if (split > 0 && split < row.title.size() - 3) {
                    row.artist = row.title.left(split).trimmed();
                    row.title = row.title.mid(split + 3).trimmed();
                }
            }
            result << std::move(row);
        }
        return result;
    }));
}
QStringList CloudUploadService::keywords(const File &f)
{
    QStringList result;
    for (auto v : {f.title + " " + f.artist, f.title, f.artist + " " + f.title}) {
        v = v.simplified();
        if (!v.isEmpty() && !result.contains(v))
            result << v;
    }
    return result;
}
double CloudUploadService::score(const File &f, const QJsonObject &candidate, bool *acceptable)
{
    const auto a = normalized(f.title), b = normalized(candidate.value("title").toString());
    double title = 0, extra = 0;
    if (!a.isEmpty() && !b.isEmpty()) {
        if (a == b)
            title = 1;
        else if (a.contains(b) || b.contains(a)) {
            title = .85;
            extra = double(std::abs(a.size() - b.size())) / std::min(a.size(), b.size());
        } else {
            const auto ac = a.toUcs4(), bc = b.toUcs4();
            QSet<uint> x(ac.cbegin(), ac.cend()), y(bc.cbegin(), bc.cend());
            const auto n = x;
            title = double((x & y).size()) / std::max<qsizetype>(1, (n | y).size());
        }
    }
    const auto artist = normalized(f.artist), other = normalized(artists(candidate));
    double artistScore = artist.isEmpty() ? .5 : 0;
    if (!artist.isEmpty() && !other.isEmpty()) {
        if (artist == other)
            artistScore = 1;
        else if (artist.contains(other) || other.contains(artist))
            artistScore = .9;
    }
    double duration = .5;
    const auto ms = candidate.value("duration_ms").toInteger();
    if (f.duration > 0 && ms > 0) {
        const auto delta = std::abs(f.duration - ms);
        duration = delta <= 5000 ? 1 : delta <= 15000 ? .5 : 0;
    }
    const double total = .55 * title + .3 * artistScore + .15 * duration;
    *acceptable =
        title >= .85 && extra <= .5 && (total >= .72 || (total >= .65 && artistScore >= .9));
    return total;
}
void CloudUploadService::matchNext()
{
    if (m_closed || m_phase != "matching")
        return;
    while (m_index < m_files.size() && m_files[m_index].state == "failed")
        ++m_index;
    if (m_index >= m_files.size()) {
        m_index = 0;
        m_phase = "uploading";
        notify();
        uploadNext();
        return;
    }
    auto &f = m_files[m_index];
    f.state = "matching";
    const auto keys = keywords(f);
    if (m_keyword >= keys.size()) {
        f.match = m_best;
        f.state = "pending";
        ++m_index;
        m_keyword = 0;
        m_best = {};
        m_bestScore = -1;
        notify();
        QTimer::singleShot(0, this, &CloudUploadService::matchNext);
        return;
    }
    startLookup(Automatic, keys[m_keyword]);
}
void CloudUploadService::startLookup(Lookup kind, const QString &key)
{
    m_lookupKind = kind;
    m_lookupKey = key;
    const auto cacheKey = QString::number(int(kind == Hash     ? Hash
                                              : kind == Manual ? Manual
                                                               : Automatic)) +
                          ":" + key;
    if (m_cache.contains(cacheKey)) {
        const auto run = m_run;
        QTimer::singleShot(0, this, [this, run, cacheKey] {
            if (run == m_run && !paused() && !m_closed)
                applyLookup(m_cache.value(cacheKey));
        });
        return;
    }
    const auto now = m_clock.elapsed();
    if (m_resumeAt > now)
        m_error = tr("查询冷却中，请稍后");
    m_timer.start(int(std::max<qint64>({0, 2000 - (now - m_lastLookup), m_resumeAt - now})));
    notify();
}
void CloudUploadService::dispatchLookup()
{
    if (m_closed || paused() || m_request || m_session != m_api.sessionRevision() ||
        m_lookupKind == None)
        return;
    const auto now = m_clock.elapsed();
    const auto remaining = std::max(2000 - (now - m_lastLookup), m_resumeAt - now);
    if (remaining > 0) {
        m_timer.start(int(remaining));
        return;
    }
    m_error.clear();
    m_lastLookup = m_clock.elapsed();
    m_request = m_lookupKind == Hash
                    ? m_api.cloudMatch(m_lookupKey)
                    : m_api.search(ApiClient::SearchKind::Song, m_lookupKey, 1,
                                   m_lookupKind == Manual ? 30 : 3,
                                   {AuthenticationMode::CurrentAccount, true, false});
    if (!m_request) {
        pause(tr("网络暂不可用，请稍后继续"), 0);
        return;
    }
    notify();
}
void CloudUploadService::applyLookup(const QJsonObject &response)
{
    const auto kind = m_lookupKind;
    m_lookupKind = None;
    if (kind == Manual) {
        m_candidates = response.value("page").toObject().value("items").toArray();
        notify();
        if (m_candidates.isEmpty())
            emit notice(tr("未找到匹配的歌曲，请修改搜索条件重试"), true);
        return;
    }
    if (m_index >= m_files.size())
        return;
    auto &f = m_files[m_index];
    if (kind == Automatic) {
        bool found = false;
        for (const auto &v : response.value("page").toObject().value("items").toArray()) {
            const auto c = v.toObject();
            if (c.value("hash").toString().isEmpty())
                continue;
            bool ok = false;
            const double value = score(f, c, &ok);
            if (value > m_bestScore) {
                m_bestScore = value;
                m_best = ok ? c : QJsonObject{};
            }
            if (ok) {
                m_best = c;
                found = true;
                break;
            }
        }
        if (found)
            m_keyword = keywords(f).size();
        else
            ++m_keyword;
        matchNext();
        return;
    }
    if (kind == Hash) {
        f.hashChecked = true;
        f.standardHash = f.hash;
        for (const auto &v : response.value("matches").toArray()) {
            const auto c = v.toObject();
            const auto h = c.value("audio_info").toObject().value("hash").toString();
            const auto audio = c.value("audio_id").toInteger(),
                       album = c.value("album_audio_id").toInteger();
            if (h.isEmpty() && audio <= 0 && album <= 0)
                continue;
            if (!h.isEmpty())
                f.standardHash = h.toLower();
            if (f.artist.isEmpty())
                f.artist = c.value("author_name").toString();
            if (f.match.value("audio_id").toInteger() <= 0 && audio > 0)
                f.match.insert("audio_id", audio);
            if (f.match.value("id").toVariant().toLongLong() <= 0 && album > 0)
                f.match.insert("id", QString::number(album));
            break;
        }
        uploadNext();
    }
}
void CloudUploadService::searchManual(const QString &title, const QString &artist)
{
    if (m_closed || busy() || m_step != "manual" || m_files.isEmpty() || m_request ||
        m_timer.isActive() || m_lookupKind != None)
        return;
    const auto keyword = (title + " " + artist).simplified();
    if (title.trimmed().isEmpty() || keyword.isEmpty())
        return;
    m_error.clear();
    m_candidates = {};
    startLookup(Manual, keyword);
}
void CloudUploadService::uploadMatch(int index)
{
    if (m_closed || busy() || m_step != "manual" || searching() || index < 0 ||
        index >= m_candidates.size() || m_files.isEmpty())
        return;
    if (m_files[0].state == "failed")
        return;
    const auto c = m_candidates[index].toObject();
    if (c.value("hash").toString().isEmpty())
        return;
    if (m_tasks) {
        TaskCenterModel::Registration r;
        r.id = "cloud-upload";
        r.content.name = tr("云盘上传");
        r.policy = TaskCenterModel::Policy::actionRequired();
        r.cancel = [this] { stop(); };
        m_task = m_tasks->registerTask(*this, std::move(r));
        m_taskFinished = false;
    }
    m_files[0].match = c;
    m_index = 0;
    m_phase = "uploading";
    m_step = "progress";
    notify();
    uploadNext();
}
void CloudUploadService::uploadNext()
{
    if (m_closed || m_phase != "uploading" || m_request)
        return;
    while (m_index < m_files.size() &&
           (m_files[m_index].state == "failed" || m_files[m_index].state == "success"))
        ++m_index;
    if (m_index >= m_files.size()) {
        m_phase = "done";
        m_lookupKind = None;
        notify();
        if (succeeded())
            emit cloudChanged();
        emit notice(statusLabel(), failed() > 0);
        return;
    }
    auto &f = m_files[m_index];
    const QFileInfo now(f.path);
    if (now.size() != f.size || now.lastModified().toMSecsSinceEpoch() != f.modified) {
        finishFile(false, tr("文件已变化，请重新选择"));
        return;
    }
    if (!f.hashChecked) {
        startLookup(Hash, f.hash);
        return;
    }
    f.state = "uploading";
    m_part = 0;
    m_authorization.clear();
    m_storedHash.clear();
    m_transfer = {};
    uploadStage(ApiClient::CloudUploadStage::Auth, {{"hash", f.hash}});
}
void CloudUploadService::uploadStage(ApiClient::CloudUploadStage stage, QJsonObject query)
{
    m_stage = stage;
    m_request = m_api.cloudUpload(stage, query);
    if (!m_request) {
        pause(tr("网络暂不可用，当前文件未自动重试"), 0);
        return;
    }
    notify();
}
void CloudUploadService::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_request || id != m_request || m_closed || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto result = QJsonDocument::fromJson(bytes).object();
    if (m_lookupKind != None) {
        const auto cacheKey = QString::number(int(m_lookupKind)) + ":" + m_lookupKey;
        if (m_cache.size() >= 512)
            m_cache.clear();
        m_cache.insert(cacheKey, result);
        applyLookup(result);
        return;
    }
    if (m_index >= m_files.size() || m_phase != "uploading")
        return;
    auto &f = m_files[m_index];
    auto nextPart = [this, &f] {
        ++m_part;
        uploadStage(
            ApiClient::CloudUploadStage::Part,
            {{"hash", f.hash},
             {"transfer", m_transfer},
             {"part", m_part},
             {"file",
              QJsonObject{{"path", f.path}, {"size", f.size}, {"modified_ms", f.modified}}}});
    };
    auto add = [this, &f] {
        uploadStage(
            ApiClient::CloudUploadStage::Add,
            {{"file", QJsonObject{{"name", f.title},
                                  {"ext", f.ext},
                                  {"author_name", f.artist},
                                  {"hash", m_storedHash},
                                  {"hash_std", f.standardHash},
                                  {"audio_id", f.match.value("audio_id").toInteger()},
                                  {"album_audio_id", f.match.value("id").toVariant().toLongLong()},
                                  {"size", f.size},
                                  {"timelen", f.duration}}}});
    };
    switch (m_stage) {
    case ApiClient::CloudUploadStage::Auth:
        m_authorization = result.value("authorization").toString();
        if (m_authorization.isEmpty()) {
            finishFile(false, tr("上传授权为空"));
            return;
        }
        uploadStage(ApiClient::CloudUploadStage::Initiate,
                    {{"hash", f.hash}, {"ext", f.ext}, {"authorization", m_authorization}});
        break;
    case ApiClient::CloudUploadStage::Initiate: {
        const auto uploadId = result.value("upload_id").toString();
        if (uploadId.isEmpty()) {
            if (result.value("x-bss-hash").toString().isEmpty()) {
                finishFile(false, tr("上传服务未返回有效会话"));
                return;
            }
            f.instant = true;
            f.sent = f.size;
            m_storedHash = result.value("x-bss-filename").toString();
            if (m_storedHash.isEmpty())
                m_storedHash = result.value("x-bss-hash").toString();
            add();
        } else {
            m_transfer = {{"host", result.value("external_host")},
                          {"authorization", m_authorization},
                          {"upload_id", uploadId}};
            nextPart();
        }
        break;
    }
    case ApiClient::CloudUploadStage::Part:
        f.sent = std::min<qint64>(f.size, qint64(m_part) * 1024 * 1024);
        if (f.sent < f.size)
            nextPart();
        else
            uploadStage(ApiClient::CloudUploadStage::Complete,
                        {{"hash", f.hash}, {"transfer", m_transfer}, {"parts", m_part}});
        break;
    case ApiClient::CloudUploadStage::Complete:
        m_storedHash = result.value("x-bss-filename").toString();
        if (m_storedHash.isEmpty())
            m_storedHash = f.hash;
        add();
        break;
    case ApiClient::CloudUploadStage::Add:
        // Rust has checked explicit success. Optional counters and rows must not
        // become response-shape gates; reload the cloud catalog after success.
        finishFile(true);
        break;
    }
}
void CloudUploadService::reject(quint64 id, const QByteArray &bytes)
{
    if (!m_request || id != m_request || m_closed || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto error = QJsonDocument::fromJson(bytes).object();
    const auto kind = error.value("kind").toString();
    const auto code = error.value("code").toInteger();
    QString message = error.value("message").toString(tr("上传请求失败"));
    // Read lookups may be repeated only after an explicit resume. Writes with an
    // unknown outcome are never replayed: preserve the failed row and pause the rest.
    if (m_lookupKind == None && m_index < m_files.size()) {
        if (kind == "verification_complete") {
            // This request was explicitly rejected with a challenge. Keep the
            // file queued; only the user's Continue may start it again.
            m_files[m_index].state = "pending";
            m_files[m_index].error.clear();
        } else {
            m_files[m_index].state = "failed";
            m_files[m_index].error = message;
            ++m_index;
        }
        m_authorization.clear();
        m_transfer = {};
    }
    if (kind == "file" || kind == "input") {
        m_error = message;
        notify();
        QTimer::singleShot(0, this, &CloudUploadService::uploadNext);
        return;
    }
    const bool risk = kind == "challenge" || code == 403 || code == 429 || kind == "business" ||
                      kind == "authentication";
    if (kind == "challenge")
        message = tr("需要账号安全验证，上传已暂停；处理后再继续");
    else if (code == 429)
        message = tr("请求过于频繁，上传已暂停，请稍后继续");
    else if (kind == "transport" && m_lookupKind == None)
        message += tr("；当前文件未重试，请先刷新云盘核对");
    const auto retryAfter =
        std::clamp<qint64>(error.value("retry_after_seconds").toInteger(), 0, 86400);
    pause(message, int(std::max<qint64>(risk ? 60 : 0, retryAfter)));
}
void CloudUploadService::finishFile(bool success, const QString &error)
{
    if (m_index >= m_files.size())
        return;
    auto &f = m_files[m_index];
    f.state = success ? "success" : "failed";
    f.error = error;
    if (success)
        f.sent = f.size;
    m_authorization.clear();
    m_transfer = {};
    m_storedHash.clear();
    ++m_index;
    notify();
    QTimer::singleShot(0, this, &CloudUploadService::uploadNext);
}
void CloudUploadService::pause(const QString &message, int seconds)
{
    if (!paused())
        m_resumePhase = m_phase;
    m_phase = "paused";
    m_error = message;
    const auto now = m_clock.elapsed();
    // Verification completion or a later error must not shorten an existing
    // account cooldown. It still requires an explicit Continue after expiry.
    m_resumeAt = std::max(m_resumeAt, now + qint64(seconds) * 1000);
    if (m_resumeAt > now)
        m_cooldown.start(int(m_resumeAt - now));
    notify();
}
void CloudUploadService::resume()
{
    if (m_closed || !canResume() || m_session != m_api.sessionRevision() ||
        !m_api.currentAccountUserId())
        return;
    m_phase = m_resumePhase;
    m_error.clear();
    notify();
    if (m_lookupKind != None)
        startLookup(m_lookupKind, m_lookupKey);
    else if (m_phase == "matching")
        matchNext();
    else
        uploadNext();
}
void CloudUploadService::cancelPending()
{
    m_timer.stop();
    m_cooldown.stop();
    if (m_cancel)
        m_cancel->store(true);
    const auto request = m_request;
    m_request = 0; // Cancelling a suspended verification emits failure synchronously.
    if (request)
        m_api.cancel(request);
    m_lookupKind = None;
    m_authorization.clear();
    m_transfer = {};
    m_storedHash.clear();
}
void CloudUploadService::stop()
{
    if (!busy() && !searching())
        return;
    const bool uncertainAdd = m_stage == ApiClient::CloudUploadStage::Add && m_lookupKind == None &&
                              (m_request || paused());
    ++m_run;
    cancelPending();
    m_phase = "stopped";
    m_error.clear();
    if (!m_closed)
        emit cancelled();
    for (auto &f : m_files)
        if (f.state != "success" && f.state != "failed")
            f.state = "cancelled";
    notify();
    if (succeeded() || uncertainAdd)
        emit cloudChanged();
}
void CloudUploadService::reset()
{
    if (busy() || m_scan.isRunning())
        return;
    ++m_run;
    cancelPending();
    if (m_tasks)
        m_tasks->remove(m_task);
    m_task = {};
    m_taskFinished = false;
    beginResetModel();
    m_files.clear();
    endResetModel();
    m_candidates = {};
    m_index = 0;
    m_keyword = 0;
    m_best = {};
    m_bestScore = -1;
    m_step = "pick";
    m_phase = "idle";
    m_error.clear();
    notify();
}
void CloudUploadService::attachTasks(TaskCenterModel &tasks, RouteController &router)
{
    m_tasks = &tasks;
    m_router = &router;
}
void CloudUploadService::notify()
{
    if (!m_files.isEmpty())
        emit dataChanged(index(0), index(int(m_files.size()) - 1));
    emit changed();
    publishTask();
}
void CloudUploadService::publishTask()
{
    if (!m_tasks || !m_task.generation)
        return;
    TaskCenterModel::Content content;
    content.name = tr("云盘上传");
    content.statusLabel = statusLabel();
    content.error = m_error;
    if (total())
        content.percent = progress() * 100;
    content.actions.append({"view", busy() ? tr("查看进度") : tr("查看结果"), TaskAction::Ghost,
                            true, true, [this] { open(true); }});
    if (paused())
        content.actions.append({"resume", tr("继续上传"), TaskAction::Ghost, canResume(), false,
                                [this] { resume(); }});
    if (busy())
        content.actions.append(
            {"stop", tr("取消上传"), TaskAction::Ghost, true, false, [this] { stop(); }});
    // Keep a single virtual source of file rows. Task center only gets summary;
    // 查看进度 opens that same source instead of cloning 10,000 detail objects.
    if (!busy() && m_step == "progress" && !m_taskFinished) {
        m_taskFinished = true;
        m_tasks->finish(m_task,
                        m_phase == "stopped" ? TaskCenterModel::Aborted
                        : failed()           ? TaskCenterModel::Error
                                             : TaskCenterModel::Completed,
                        std::move(content));
    } else
        m_tasks->update(m_task, std::move(content));
}
void CloudUploadService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    stop();
    ++m_run;
    cancelPending();
    if (m_tasks)
        m_tasks->remove(m_task);
    m_task = {};
}
} // namespace EchoMusic
