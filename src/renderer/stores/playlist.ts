import { defineStore } from 'pinia';
import {
  getUserPlaylists,
  addPlaylistTrack,
  deletePlaylistTrack,
  addPlaylist,
  deletePlaylist,
} from '@/api/playlist';
import { uploadPlayHistory } from '@/api/user';
import logger from '@/utils/logger';
import { mapPlaylistMeta, type PlaylistMeta } from '@/utils/mappers';

export interface SongRelateGood {
  hash?: string;
  quality?: string;
  level?: number;
}

export interface SongArtist {
  id?: string | number;
  name: string;
}

export interface Song {
  id: string;
  title: string;
  artist: string;
  artists?: SongArtist[];
  album?: string;
  albumId?: string | number;
  duration: number;
  coverUrl: string;
  audioUrl: string;
  hash: string;
  mixSongId: string | number;
  source?: string;
  lyric?: string;
  privilege?: number;
  payType?: number;
  oldCpy?: number;
  relateGoods?: SongRelateGood[];
}

export interface PlaylistInfo {
  id: string;
  name: string;
  coverUrl: string;
  songCount: number;
  userId: number;
  isDefault?: boolean;
  type?: number;
  source?: number;
}

const sampleLrc = `[00:00.00]梦中的婚礼
[00:02.00]作曲 : Paul de Senneville
[00:04.00]编曲 : Olivier Toussaint
[00:06.00]演奏 : Richard Clayderman
[00:08.00]
[00:10.00](这是一首纯音乐，请欣赏)
[00:40.00]音符在指尖跳跃
[01:10.00]旋律在心中回荡
[01:40.00]梦境与现实交织
[02:10.00]在这浪漫的夜晚
[02:40.00]EchoMusic 陪伴着你`;

export const usePlaylistStore = defineStore('playlist', {
  state: () => ({
    // 默认内置列表 (写死模拟数据)
    defaultList: [
      {
        id: '1',
        title: '梦中的婚礼',
        artist: 'Richard Clayderman',
        artists: [{ id: '0', name: 'Richard Clayderman' }],
        duration: 167,
        coverUrl: 'https://p2.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=431795921.mp3',
        hash: 'A1B2C3D4E5F6G7H8I9J0',
        mixSongId: '123456',
        lyric: sampleLrc,
        privilege: 10,
        payType: 3,
        oldCpy: 1,
        relateGoods: [{ quality: 'high', hash: 'A1B2C3D4E5F6G7H8I9J0' }],
      },
    ] as Song[],
    favorites: [] as Song[],
    history: [] as Song[],
    userPlaylists: [] as PlaylistMeta[],
  }),
  getters: {
    /**
     * 获取“我喜欢”歌单
     */
    likedPlaylist(state) {
      return state.userPlaylists.find(
        (p) =>
          p.name === '我喜欢' ||
          p.name === '我喜欢的音乐' ||
          (p.name?.includes('喜欢') ?? false) ||
          p.type === 1 ||
          p.isDefault === true,
      );
    },
    likedPlaylistId(): string | number | null {
      const lp = this.likedPlaylist;
      return lp ? lp.listCreateGid || lp.globalCollectionId || lp.listid || lp.id : null;
    }
  },
  actions: {
    async addToPlaylist(listId: string | number, song: Song) {
      const targetId = String(listId ?? '');
      if (!targetId) return false;

      try {
        const songData = `${song.title}|${song.hash}|${song.albumId || 0}|${song.mixSongId}`;
        const res = await addPlaylistTrack(targetId, songData);
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          logger.info('PlaylistStore', `Song ${song.title} added to playlist ${targetId}`);
          return true;
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Add to playlist error:', e);
      }
      return false;
    },
    async removeFromPlaylist(listId: string | number, song: Song) {
      const targetId = String(listId ?? '');
      if (!targetId) return false;

      try {
        const res = await deletePlaylistTrack(targetId, String(song.mixSongId));
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          logger.info('PlaylistStore', `Song ${song.title} removed from playlist ${targetId}`);
          return true;
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Remove from playlist error:', e);
      }
      return false;
    },
    /**
     * 添加到收藏 (同步远端)
     */
    async addToFavorites(song: Song) {
      // 1. 更新本地状态 (乐观更新)
      if (!this.favorites.find((s) => s.id === song.id)) {
        this.favorites.unshift(song);
      }

      // 2. 同步远端
      const listId = this.likedPlaylistId;
      if (listId) {
        try {
          const songData = `${song.title}|${song.hash}|${song.albumId || 0}|${song.mixSongId}`;
          const res = await addPlaylistTrack(listId, songData);
          if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
            logger.info('PlaylistStore', `Song ${song.title} added to favorites on cloud`);
          }
        } catch (e) {
          logger.error('PlaylistStore', 'Add to favorites sync error:', e);
        }
      }
    },

    /**
     * 从收藏移除 (同步远端)
     */
    async removeFromFavorites(id: string) {
      const song = this.favorites.find(s => s.id === id);
      if (!song) return;

      // 1. 更新本地状态
      this.favorites = this.favorites.filter((s) => s.id !== id);

      // 2. 同步远端
      const listId = this.likedPlaylistId;
      if (listId) {
        try {
          const res = await deletePlaylistTrack(listId, String(song.mixSongId));
          if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
            logger.info('PlaylistStore', `Song ${song.title} removed from favorites on cloud`);
          }
        } catch (e) {
          logger.error('PlaylistStore', 'Remove from favorites sync error:', e);
        }
      }
    },

    /**
     * 添加到历史记录 (同步远端)
     */
    async addToHistory(song: Song) {
      // 1. 更新本地状态
      this.history = [song, ...this.history.filter((s) => s.id !== song.id)].slice(0, 100);

      // 2. 同步远端
      try {
        const res = await uploadPlayHistory(song.mixSongId);
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          logger.info('PlaylistStore', `Play history uploaded: ${song.title}`);
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Upload history sync error:', e);
      }
    },

    async fetchUserPlaylists() {
      try {
        const res = await getUserPlaylists();
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          const data = 'data' in res ? (res as { data?: { info?: unknown } }).data : undefined;
          const info = 'info' in res ? (res as { info?: unknown }).info : undefined;
          const raw = data?.info ?? info ?? [];
          this.userPlaylists = Array.isArray(raw) ? raw.map((item) => mapPlaylistMeta(item)) : [];
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Fetch user playlists error:', e);
      }
    },

    async favoritePlaylist(meta: PlaylistMeta) {
      try {
        const res = await addPlaylist(meta.name, {
          type: 1,
          list_create_userid: meta.listCreateUserid,
          list_create_listid: meta.listCreateListid ?? meta.id,
          list_create_gid: meta.listCreateGid ?? meta.globalCollectionId,
          source: meta.source ?? 1,
        });
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          await this.fetchUserPlaylists();
          return true;
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Favorite playlist error:', e);
      }
      return false;
    },

    async unfavoritePlaylist(meta: PlaylistMeta) {
      try {
        const target = this.userPlaylists.find((p) => {
          const localId = String(p.listid ?? p.id);
          const originalId = String(p.listCreateGid ?? p.globalCollectionId ?? '');
          const originalListId = String(p.listCreateListid ?? '');
          const currentIds = [
            String(meta.id),
            String(meta.listid ?? ''),
            String(meta.listCreateListid ?? ''),
            String(meta.listCreateGid ?? ''),
            String(meta.globalCollectionId ?? ''),
          ];
          return (
            currentIds.includes(localId) ||
            (originalId && currentIds.includes(originalId)) ||
            (originalListId && currentIds.includes(originalListId))
          );
        });

        const listId = target?.listid ?? target?.id;
        if (!listId) return false;
        const res = await deletePlaylist(listId);
        if (res && typeof res === 'object' && 'status' in res && res.status === 1) {
          await this.fetchUserPlaylists();
          return true;
        }
      } catch (e) {
        logger.error('PlaylistStore', 'Unfavorite playlist error:', e);
      }
      return false;
    },
  },
  persist: true,
});
