#pragma once
#include "PlaybackTrack.h"
#include <QObject>
#include <QPointer>
namespace EchoMusic
{
class AccountService;
class SharingCommands;
class ContentBlacklistService;
/// 歌曲列表右键菜单的共享命令，由 PlaybackService 持有。
/// 分享、复制和不感兴趣动作在各列表间保持一致，反馈只发出一次。
/// 菜单状态读取 ContentBlacklistService 的歌曲索引；
/// 未接入服务或未登录时不执行写操作。
class SongCommands final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit SongCommands(QObject *parent = nullptr);
    /// 应用装配时注入账号与黑名单服务；两者生命周期之外指针自动失效。
    void attach(ContentBlacklistService &blacklist, AccountService &account);
    quint64 revision() const
    {
        return m_revision;
    }
    bool busy() const;
    /// 菜单「分享」：复制源格式分享文本；不可分享的歌曲返回 false。
    bool share(const PlaybackTrack &track);
    void attachSharing(SharingCommands &);
    SharingCommands *sharing() const;
    /// 菜单「复制信息」：复制「歌名 - 歌手」；空文本不复制。
    bool copyInfo(const PlaybackTrack &track);
    void openDetails(const PlaybackTrack &track)
    {
        emit detailsRequested(track);
    }
    void openComments(const PlaybackTrack &track)
    {
        emit commentsRequested(track);
    }
    void openMv(const PlaybackTrack &track)
    {
        emit mvRequested(track);
    }
    void openArtist(const QString &id)
    {
        const auto numeric = id.toULongLong();
        if (numeric)
            emit artistRequested(QString::number(numeric));
    }
    void openAlbum(const QString &id)
    {
        const auto numeric = id.toULongLong();
        if (numeric)
            emit albumRequested(QString::number(numeric));
    }
    bool blacklistAvailable(const PlaybackTrack &track) const;
    bool blacklisted(const PlaybackTrack &track) const;
    bool addToBlacklist(const PlaybackTrack &track);
    bool removeFromBlacklist(const PlaybackTrack &track);
signals:
    void changed();
    void sharingChanged();
    void artistRequested(const QString &id);
    void albumRequested(const QString &id);
    void detailsRequested(const EchoMusic::PlaybackTrack &track);
    void commentsRequested(const EchoMusic::PlaybackTrack &track);
    void mvRequested(const EchoMusic::PlaybackTrack &track);
    /// tone 与 ToastService::Tone 同序（0 Info / 1 Success / 2 Warning / 3 Danger）。
    void notice(const QString &message, int tone);

private:
    QString sharerName() const;
    void refresh();
    QPointer<SharingCommands> m_sharing;
    QPointer<ContentBlacklistService> m_blacklist;
    QPointer<AccountService> m_account;
    quint64 m_revision = 0;
};
} // namespace EchoMusic
