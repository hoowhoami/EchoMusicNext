#include "PlaylistEditModel.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtConcurrentRun>
#include <utility>
namespace EchoMusic
{
namespace
{
QString normalizedTags(const QString &text)
{
    QStringList result;
    for (auto tag :
         text.split(QRegularExpression(QStringLiteral("[,，\\r\\n]+")), Qt::SkipEmptyParts)) {
        tag = tag.trimmed();
        if (!tag.isEmpty() && !result.contains(tag))
            result.append(tag);
    }
    return result.join(u',');
}
bool editable(const QJsonObject &row)
{
    const auto kind = row["kind"].toString();
    return kind == "owned" && row["source_code"].toInt() != 2 && row["default_kind"].toInt() == 0;
}
} // namespace
PlaylistEditModel::PlaylistEditModel(ApiClient &api, UserPlaylistModel &source,
                                     PlaylistMutationService &mutations, QObject *parent)
    : QObject(parent), m_api(api), m_source(source), m_mutations(mutations)
{
    connect(&api, &ApiClient::succeeded, this, &PlaylistEditModel::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (id && id == m_request)
            fail(tr("读取歌单失败，请重新加载"));
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        close();
        emit invalidated();
    });
    connect(&mutations, &PlaylistMutationService::completed, this, [this](const QByteArray &) {
        if (!m_saving)
            return;
        m_saving = false;
        m_snapshot["title"] = m_title.trimmed();
        m_snapshot["tags"] = normalizedTags(m_tags);
        m_snapshot["description"] = m_description;
        m_png.clear();
        emit changed();
        emit saved();
        emit notice(tr("歌单已更新"), false);
    });
    connect(&mutations, &PlaylistMutationService::changed, this, [this] {
        if (m_saving && !m_mutations.errorMessage().isEmpty()) {
            m_saving = false;
            m_error = m_mutations.errorMessage();
            emit changed();
        }
    });
}
PlaylistEditModel::~PlaylistEditModel()
{
    close();
}
bool PlaylistEditModel::canEdit(quint64 id) const
{
    if (!m_api.currentAccountUserId())
        return false;
    for (int row = 0; row < m_source.rowCount(); ++row) {
        const auto index = m_source.index(row);
        if (index.data(UserPlaylistModel::CloudListIdRole).toULongLong() == id) {
            const auto kind = index.data(UserPlaylistModel::KindRole).toString();
            return kind == "owned" && index.data(UserPlaylistModel::SourceCodeRole).toUInt() != 2 &&
                   !index.data(UserPlaylistModel::DefaultKindRole).toUInt();
        }
    }
    return false;
}
bool PlaylistEditModel::dirty() const
{
    return m_loaded && (!m_png.isEmpty() || m_title.trimmed() != m_snapshot["title"].toString() ||
                        normalizedTags(m_tags) != normalizedTags(m_snapshot["tags"].toString()) ||
                        m_description != m_snapshot["description"].toString());
}
void PlaylistEditModel::setTitle(const QString &value)
{
    if (!m_saving) {
        m_title = value;
        emit changed();
    }
}
void PlaylistEditModel::setTags(const QString &value)
{
    if (!m_saving) {
        m_tags = value;
        emit changed();
    }
}
void PlaylistEditModel::setDescription(const QString &value)
{
    if (!m_saving) {
        m_description = value;
        emit changed();
    }
}
void PlaylistEditModel::close()
{
    ++m_generation;
    ++m_selection;
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    m_id = 0;
    m_loaded = m_saving = m_detailRequest = false;
    // Keep a single bounded worker across close/reopen; its result is discarded
    // by generation, but it must finish before another image decode is queued.
    m_reading = m_coverWorker != nullptr;
    m_unknownRefreshed = false;
    m_snapshot = {};
    m_update = {};
    m_png.clear();
    m_title.clear();
    m_tags.clear();
    m_description.clear();
    m_cover.clear();
    m_coverName.clear();
    m_error.clear();
    emit changed();
}
void PlaylistEditModel::open(quint64 id)
{
    if (m_saving)
        return;
    close();
    if (!canEdit(id)) {
        fail(tr("该歌单不支持编辑"));
        return;
    }
    m_id = id;
    reload();
}
void PlaylistEditModel::reload()
{
    if (!m_id || m_saving)
        return;
    const auto id = m_id;
    close();
    m_id = id;
    m_page = 1;
    // Explicit reload may retry an acknowledged readback, never the write.
    if (m_mutations.state() == PlaylistMutationService::State::RefreshRequired)
        m_mutations.reconcile();
    m_request = m_api.userPlaylists(1, 200);
    if (!m_request)
        fail(tr("无法加载歌单，请重试"));
    emit changed();
}
void PlaylistEditModel::fail(const QString &message)
{
    m_request = 0;
    m_error = message;
    emit changed();
}
void PlaylistEditModel::ready()
{
    m_title = m_snapshot["title"].toString();
    m_tags = normalizedTags(m_snapshot["tags"].toString());
    m_description = m_snapshot["description"].toString();
    m_cover =
        m_source.coverFor(m_snapshot["global_id"].toString(), m_snapshot["cover_url"].toString());
    m_unknownRefreshed = m_mutations.state() == PlaylistMutationService::State::OutcomeUnknown;
    m_loaded = true;
    emit changed();
}
void PlaylistEditModel::accept(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request)
        return;
    m_request = 0;
    const auto document = QJsonDocument::fromJson(bytes);
    if (m_detailRequest) {
        m_detailRequest = false;
        const auto rows = document.array();
        if (rows.size() != 1) {
            fail(tr("无法读取完整歌单信息，请重新加载"));
            return;
        }
        const auto detail = rows.first().toObject();
        if (!m_snapshot["description_present"].toBool())
            m_snapshot["description"] = detail["description"];
        if (!m_snapshot["tags_present"].toBool())
            m_snapshot["tags"] = detail["tags"].toString();
        ready();
        return;
    }
    const auto object = document.object();
    const auto version = object["total_revision"].toInteger(-1);
    const auto page = object["page"].toObject();
    if (object["user_id"].toInteger() != qint64(m_api.currentAccountUserId()) || version < 0 ||
        (m_page > 1 && quint64(version) != m_version) || !page["items"].isArray()) {
        fail(tr("歌单列表已变化，请重新加载"));
        return;
    }
    m_version = quint64(version);
    const auto rows = page["items"].toArray();
    for (const auto &value : rows) {
        const auto row = value.toObject();
        if (row["cloud_list_id"].toInteger() != qint64(m_id))
            continue;
        if (!editable(row) || !row["sort_index"].isDouble() ||
            row["sort_index"].toInteger(-1) < 0) {
            fail(tr("无法确认歌单编辑信息，请重新加载"));
            return;
        }
        m_snapshot = row;
        m_update = {{m_id}, m_version, row["sort_index"].toInteger(), {}, {}, {}};
        m_update.kind = ApiClient::PlaylistKind::Owned;
        if (!row["description_present"].toBool() || !row["tags_present"].toBool()) {
            const auto global = row["global_id"].toString();
            if (global.isEmpty()) {
                fail(tr("缺少完整歌单信息，请重新加载"));
                return;
            }
            m_detailRequest = true;
            m_request = m_api.playlistDetails({{global}});
            if (!m_request)
                fail(tr("无法读取完整歌单信息"));
            emit changed();
            return;
        }
        ready();
        return;
    }
    if (rows.size() == 200 && m_page < 500) {
        m_request = m_api.userPlaylists(++m_page, 200);
        if (!m_request)
            fail(tr("无法继续读取歌单"));
        emit changed();
        return;
    }
    fail(tr("歌单已不存在或不在当前账号中"));
}
void PlaylistEditModel::selectCover(const QUrl &file)
{
    if (!m_loaded || m_saving || m_reading || !file.isLocalFile())
        return;
    struct Result
    {
        QByteArray png;
        QString error;
    };
    m_reading = true;
    m_error.clear();
    const auto generation = m_generation, selection = ++m_selection;
    auto *watcher = new QFutureWatcher<Result>(this);
    m_coverWorker = watcher;
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, generation, selection, file] {
                auto result = watcher->result();
                watcher->deleteLater();
                m_coverWorker = nullptr;
                m_reading = false;
                if (m_generation != generation || m_selection != selection) {
                    emit changed();
                    return;
                }
                m_error = result.error;
                if (m_error.isEmpty()) {
                    m_png = std::move(result.png);
                    m_coverName = QFileInfo(file.toLocalFile()).fileName();
                    m_cover = QStringLiteral("data:image/png;base64,") +
                              QString::fromLatin1(m_png.toBase64());
                }
                emit changed();
            });
    watcher->setFuture(QtConcurrent::run([path = file.toLocalFile()] {
        QFile file(path);
        constexpr qint64 limit = 8 * 1024 * 1024;
        if (!file.open(QIODevice::ReadOnly))
            return Result{{}, QObject::tr("图片读取失败")};
        if (file.size() <= 0 || file.size() > limit)
            return Result{{}, QObject::tr("封面图片不能为空或超过 8 MB")};
        auto bytes = file.read(limit + 1);
        if (bytes.size() > limit)
            return Result{{}, QObject::tr("封面图片不能超过 8 MB")};
        QBuffer input(&bytes);
        input.open(QIODevice::ReadOnly);
        QImageReader reader(&input);
        reader.setAutoTransform(true);
        const auto format = reader.format().toLower();
        if (format != "jpeg" && format != "png" && format != "webp")
            return Result{{}, QObject::tr("请选择 JPG、PNG 或 WebP 图片")};
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 64 * 1024 * 1024)
            return Result{{}, QObject::tr("图片尺寸过大或无法读取")};
        const auto image = reader.read();
        if (image.isNull())
            return Result{{}, QObject::tr("图片无法读取，请重新选择")};
        const int side = qMin(image.width(), image.height());
        const auto cropped =
            image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                .scaled(400, 400, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        Result result;
        QBuffer output(&result.png);
        output.open(QIODevice::WriteOnly);
        if (!cropped.save(&output, "PNG"))
            return Result{{}, QObject::tr("图片转换失败")};
        return result;
    }));
    emit changed();
}
void PlaylistEditModel::save()
{
    if (!m_loaded || m_saving || m_reading || !dirty())
        return;
    if (m_title.trimmed().isEmpty()) {
        fail(tr("歌单名称不能为空"));
        return;
    }
    auto update = m_update;
    if (m_title.trimmed() != m_snapshot["title"].toString())
        update.title = m_title.trimmed();
    update.description = m_description;
    update.tags = normalizedTags(m_tags);
    m_error.clear();
    // A fresh snapshot plus an explicit save releases an unknown outcome; no
    // automatic replay occurs on failure, reopen or reload.
    if (m_unknownRefreshed) {
        m_mutations.acceptRefreshedUnknownOutcome();
        m_unknownRefreshed = false;
    }
    m_saving = true;
    if (!m_mutations.saveEdit(update, m_png)) {
        m_saving = false;
        m_error = m_mutations.errorMessage().isEmpty() ? tr("歌单操作正在进行，请稍后重试")
                                                       : m_mutations.errorMessage();
    }
    emit changed();
}
} // namespace EchoMusic
