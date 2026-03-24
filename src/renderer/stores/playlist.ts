import { defineStore } from 'pinia';
import { getUserPlaylists, addPlaylistTrack, deletePlaylistTrack } from '../api/playlist';
import { uploadPlayHistory } from '../api/user';
import logger from '../utils/logger';

export interface Song {
  id: string;
  title: string;
  artist: string;
  album?: string;
  albumId?: string | number;
  duration: number;
  coverUrl: string;
  audioUrl: string;
  hash: string;
  mixSongId: string | number;
  lyric?: string;
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
        duration: 167,
        coverUrl: 'https://p2.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=431795921.mp3',
        hash: 'A1B2C3D4E5F6G7H8I9J0',
        mixSongId: '123456',
        lyric: sampleLrc,
      },
    ] as Song[],
    favorites: [] as Song[],
    history: [] as Song[],
    userPlaylists: [] as any[],
  }),
  getters: {
    /**
     * 获取“我喜欢”歌单
     */
    likedPlaylist(state) {
      return state.userPlaylists.find(p => 
        p.name === '我喜欢' || 
        p.name === '我喜欢的音乐' || 
        p.is_def === 1 || 
        p.type === 1
      );
    },
    likedPlaylistId(): string | number | null {
      const lp = this.likedPlaylist;
      return lp ? (lp.listid || lp.specialid || lp.gid) : null;
    }
  },
  actions: {
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
          const res: any = await addPlaylistTrack(listId, songData);
          if (res.status === 1) {
            logger.info(`[PlaylistStore] Song ${song.title} added to favorites on cloud`);
          }
        } catch (e) {
          logger.error('[PlaylistStore] Add to favorites sync error:', e);
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
          const res: any = await deletePlaylistTrack(listId, String(song.mixSongId));
          if (res.status === 1) {
            logger.info(`[PlaylistStore] Song ${song.title} removed from favorites on cloud`);
          }
        } catch (e) {
          logger.error('[PlaylistStore] Remove from favorites sync error:', e);
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
        const res: any = await uploadPlayHistory(song.mixSongId);
        if (res.status === 1) {
          logger.info(`[PlaylistStore] Play history uploaded: ${song.title}`);
        }
      } catch (e) {
        logger.error('[PlaylistStore] Upload history sync error:', e);
      }
    },

    async fetchUserPlaylists() {
      try {
        const res: any = await getUserPlaylists();
        if (res.status === 1) {
          this.userPlaylists = res.info || res.data?.info || [];
        }
      } catch (e) {
        logger.error('[PlaylistStore] Fetch user playlists error:', e);
      }
    }
  },
  persist: true,
});
