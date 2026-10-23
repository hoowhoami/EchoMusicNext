#include "LibraryController.h"
namespace EchoMusic
{
LibraryController::LibraryController(ApiClient &api, UserPlaylistModel &source,
                                     PlaylistMutationService &mutations, QObject *parent)
    : QObject(parent), m_api(api), m_source(source), m_mutations(mutations),
      m_projection(source, this), m_order(api, mutations, this),
      m_editor(api, source, mutations, this), m_importer(api, source, mutations, this)
{
    connect(&m_editor, &PlaylistEditModel::notice, this, &LibraryController::notice);
    connect(&source, &UserPlaylistModel::changed, this, &LibraryController::changed);
    connect(&mutations, &PlaylistMutationService::changed, this, [this] {
        const auto state = m_mutations.state();
        if (m_command != Command::None && !m_mutations.errorMessage().isEmpty() &&
            (state == PlaylistMutationService::State::Ready ||
             state == PlaylistMutationService::State::OutcomeUnknown ||
             state == PlaylistMutationService::State::RefreshRequired)) {
            m_command = Command::None;
            m_removing.clear();
            emit notice(m_mutations.errorMessage(), true);
        }
        emit changed();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        m_command = Command::None;
        m_error.clear();
        m_removing.clear();
        emit changed();
    });
    connect(&mutations, &PlaylistMutationService::completed, this, [this](const QByteArray &) {
        const auto command = m_command;
        m_command = Command::None;
        if (command == Command::Create) {
            setTab(0);
            emit created();
            emit notice(tr("创建歌单成功"), false);
        } else if (command == Command::Remove) {
            const auto id = m_removing;
            m_removing.clear();
            emit removed(id);
            emit notice(m_removingOwned ? tr("已删除歌单") : tr("已取消收藏"), false);
        }
    });
}
QVariantList LibraryController::pinnedLayoutEntries() const
{
    QVariantList entries;
    QSet<quint32> included;
    for (int row = 0; row < m_source.rowCount(); ++row) {
        const auto index = m_source.index(row);
        const auto kind = index.data(UserPlaylistModel::DefaultKindRole).toUInt();
        if ((kind != 1 && kind != 2) || included.contains(kind) ||
            index.data(UserPlaylistModel::KindRole).toString() != "owned" ||
            index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2)
            continue;
        included.insert(kind);
        // default_kind retains provider is_def values 1 and 2.
        entries.append(
            QVariantMap{{"key", kind == 1 ? "playlist:created:default" : "playlist:created:liked"},
                        {"title", index.data(UserPlaylistModel::TitleRole)},
                        {"cover", index.data(UserPlaylistModel::CoverUrlRole)},
                        {"glyph", "playlist-add"},
                        {"lockedOrder", true}});
    }
    return entries;
}
void LibraryController::setHiddenDefaultKinds(const QSet<quint32> &kinds)
{
    m_projection.setHiddenDefaultKinds(kinds);
}
void LibraryController::setTab(int tab)
{
    if (tab < 0 || tab > 1 || tab == m_tab)
        return;
    m_tab = tab;
    m_projection.setCollected(tab == 1);
    emit changed();
}
QString LibraryController::errorMessage() const
{
    return !m_error.isEmpty()                      ? m_error
           : !m_mutations.errorMessage().isEmpty() ? m_mutations.errorMessage()
                                                   : m_source.errorMessage();
}
void LibraryController::reload()
{
    if (!busy()) {
        m_error.clear();
        m_source.reload();
    }
}
bool LibraryController::create(const QString &title, bool isPrivate)
{
    if (!signedIn() || busy())
        return false;
    if (title.trimmed().isEmpty()) {
        m_error = tr("请输入歌单名称");
        emit changed();
        return false;
    }
    m_error.clear();
    m_command = Command::Create;
    if (!m_mutations.create(title.trimmed(), isPrivate)) {
        m_command = Command::None;
        emit notice(m_mutations.errorMessage().isEmpty() ? tr("歌单操作未能开始，请稍后重试")
                                                         : m_mutations.errorMessage(),
                    true);
        return false;
    }
    return true;
}
bool LibraryController::remove(quint64 cloudId)
{
    if (!signedIn() || busy() || loading())
        return false;
    const int row = m_projection.sourceRow(cloudId);
    const auto index = m_source.index(row, 0);
    if (!index.isValid() || index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2 ||
        index.data(UserPlaylistModel::DefaultKindRole).toUInt() > 0)
        return false;
    const auto id = m_source.libraryIdAt(row);
    if (!id)
        return false;
    const bool owned =
        index.data(UserPlaylistModel::KindRole).toString() == QStringLiteral("owned");
    m_removingOwned = owned;
    m_removing =
        index.data(owned ? UserPlaylistModel::GlobalIdRole : UserPlaylistModel::CreatorGlobalIdRole)
            .toString();
    m_command = Command::Remove;
    m_error.clear();
    if (!m_mutations.remove(*id)) {
        m_command = Command::None;
        emit notice(m_mutations.errorMessage().isEmpty() ? tr("歌单操作未能开始，请稍后重试")
                                                         : m_mutations.errorMessage(),
                    true);
        return false;
    }
    return true;
}
bool LibraryController::openPlaylist(quint64 cloudId)
{
    const int row = m_projection.sourceRow(cloudId);
    const auto index = m_source.index(row, 0);
    if (!index.isValid() || index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2)
        return false;
    const bool owned =
        index.data(UserPlaylistModel::KindRole).toString() == QStringLiteral("owned");
    const auto id =
        index.data(owned ? UserPlaylistModel::GlobalIdRole : UserPlaylistModel::CreatorGlobalIdRole)
            .toString();
    if (id.isEmpty())
        return false;
    emit playlistRequested(id, cloudId, owned);
    return true;
}
} // namespace EchoMusic
