#pragma once
#include "SidebarPlaylistModel.h"
#include "PlaylistMutationService.h"
#include "PlaylistOrderModel.h"
#include "PlaylistImportService.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Sidebar typed commands use catalog-local cloud IDs. QML cannot send protocol
/// parameters, choose arbitrary endpoints or substitute a creator's public ID.
class LibraryController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned library")
    Q_PROPERTY(EchoMusic::SidebarPlaylistModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(EchoMusic::PlaylistOrderModel *order READ order CONSTANT)
    Q_PROPERTY(EchoMusic::PlaylistImportService *importer READ importer CONSTANT)
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    LibraryController(ApiClient &api, UserPlaylistModel &source, PlaylistMutationService &mutations,
                      QObject *parent = nullptr);
    SidebarPlaylistModel *playlists()
    {
        return &m_projection;
    }
    PlaylistOrderModel *order()
    {
        return &m_order;
    }
    PlaylistImportService *importer()
    {
        return &m_importer;
    }
    /// Read-only, bounded projection of actual default/liked playlists for the editor.
    QVariantList pinnedLayoutEntries() const;
    void setHiddenDefaultKinds(const QSet<quint32> &kinds);
    int tab() const
    {
        return m_tab;
    }
    void setTab(int tab);
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    bool loading() const
    {
        return m_source.state() == UserPlaylistModel::State::Loading;
    }
    bool busy() const
    {
        return m_mutations.state() != PlaylistMutationService::State::Ready &&
               m_mutations.state() != PlaylistMutationService::State::SignedOut;
    }
    QString errorMessage() const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool create(const QString &title, bool isPrivate);
    Q_INVOKABLE bool remove(quint64 cloudId);
    Q_INVOKABLE bool openPlaylist(quint64 cloudId);
signals:
    void changed();
    void notice(const QString &message, bool warning);
    void created();
    void removed(const QString &resourceId);
    void playlistRequested(const QString &resourceId, quint64 cloudId, bool owned);

private:
    enum class Command
    {
        None,
        Create,
        Remove
    };
    ApiClient &m_api;
    UserPlaylistModel &m_source;
    PlaylistMutationService &m_mutations;
    SidebarPlaylistModel m_projection;
    PlaylistOrderModel m_order;
    PlaylistImportService m_importer;
    int m_tab = 0;
    Command m_command = Command::None;
    QString m_error, m_removing;
    bool m_removingOwned = false;
};
} // namespace EchoMusic
