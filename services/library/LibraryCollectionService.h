#pragma once
#include "UserPlaylistModel.h"
#include "PlaylistMutationService.h"
namespace EchoMusic
{
/// Collection membership is a projection of the canonical account catalog.
/// All writes remain serialized by PlaylistMutationService.
class LibraryCollectionService final : public QObject
{
    Q_OBJECT
public:
    LibraryCollectionService(ApiClient &, UserPlaylistModel &, PlaylistMutationService &,
                             QObject *parent = nullptr);
    bool available(bool album, const QJsonObject &detail) const;
    bool collected(bool album, const QJsonObject &detail) const;
    bool busy() const;
    bool writingCollection() const
    {
        return m_pending;
    }
    void ensureLoaded();
    void toggle(bool album, const QJsonObject &detail);
signals:
    void changed();
    void notice(const QString &, bool warning);

private:
    int find(bool album, const QJsonObject &detail) const;
    ApiClient &m_api;
    UserPlaylistModel &m_library;
    PlaylistMutationService &m_mutations;
    bool m_pending = false, m_removing = false, m_wanted = false;
};
} // namespace EchoMusic
