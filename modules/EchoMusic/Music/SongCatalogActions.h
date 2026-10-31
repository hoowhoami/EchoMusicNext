#pragma once
#include "SongCatalogModel.h"
#include "PlaybackService.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Commands operate on the canonical catalog and the existing playback/library
/// writers. No queue, favorite or selection state is duplicated by page QML.
class SongCatalogActions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by an application page service")
    Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(EchoMusic::SharingCommands *sharing READ sharing NOTIFY sharingChanged)
    Q_PROPERTY(quint64 favoriteRevision READ favoriteRevision NOTIFY favoritesChanged)
    Q_PROPERTY(quint64 blacklistRevision READ blacklistRevision NOTIFY blacklistChanged)
public:
    SongCatalogActions(SongCatalogModel &, PlaybackService &, QObject *parent = nullptr);
    void setQueueContext(const QString &title, const QString &key);
    /// Standalone detail/recognition entries play one song in the active queue.
    void setPlayInCurrentQueue(bool enabled)
    {
        m_playInCurrentQueue = enabled;
    }
    quint64 favoriteRevision() const
    {
        return m_revision;
    }
    Q_INVOKABLE QVariantMap pluginMenuContext(const QString &id) const;
    Q_INVOKABLE void play(const QString &id = {}, bool replaceContext = true,
                          bool selectedOnly = false);
    QString currentTrackId() const
    {
        return m_playback.trackId();
    }
    SharingCommands *sharing() const
    {
        return m_playback.sharing();
    }
    bool playing() const
    {
        return m_playback.playing();
    }
    Q_INVOKABLE void playOrToggle(const QString &id);
    Q_INVOKABLE void appendSelected();
    Q_INVOKABLE void addSelection();
    Q_INVOKABLE bool isFavorite(const QString &id) const;
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void addSong(const QString &id);
    Q_INVOKABLE void appendSong(const QString &id, bool next = false);
    Q_INVOKABLE void playNow(const QString &id);
    Q_INVOKABLE void copySong(const QString &id);
    Q_INVOKABLE void openDetails(const QString &id);
    Q_INVOKABLE void openMv(const QString &id);
    Q_INVOKABLE void openArtist(const QString &id);
    Q_INVOKABLE void openAlbum(const QString &id);
    /// 右键菜单「分享/不感兴趣」：共享 SongCommands 唯一实现，toast 由服务发出。
    Q_INVOKABLE void shareSong(const QString &id);
    Q_INVOKABLE bool blacklistAvailable(const QString &id) const;
    Q_INVOKABLE bool blacklisted(const QString &id) const;
    Q_INVOKABLE void addToBlacklist(const QString &id);
    Q_INVOKABLE void removeFromBlacklist(const QString &id);
    Q_INVOKABLE bool blacklistBusy() const;
    quint64 blacklistRevision() const;
signals:
    void sharingChanged();
    void favoritesChanged();
    void blacklistChanged();
    void playbackChanged();

private:
    SongCatalogModel &m_model;
    PlaybackService &m_playback;
    QString m_title, m_key;
    quint64 m_revision = 0;
    bool m_playInCurrentQueue = false;
};
} // namespace EchoMusic
