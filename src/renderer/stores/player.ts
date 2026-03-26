import { defineStore } from 'pinia';
import { usePlaylistStore } from './playlist';
import { useLyricStore } from './lyric';
import logger from '@/utils/logger';

export type PlayMode = 'list' | 'single' | 'random';

// 保持一个全局 Audio 实例
const audio = new Audio();

export const usePlayerStore = defineStore('player', {
  state: () => ({
    isPlaying: false,
    volume: 0.8,
    currentTime: 0,
    duration: 0,
    playbackRate: 1,
    playMode: 'list' as PlayMode,
    currentTrackId: null as string | null,
  }),
  actions: {
    init() {
      const lyricStore = useLyricStore();
      // 恢复持久化的音量
      audio.volume = this.volume;
      audio.playbackRate = this.playbackRate;

      // 监听音频事件
      audio.ontimeupdate = () => {
        this.currentTime = audio.currentTime;
        lyricStore.updateCurrentIndex(this.currentTime);
      };

      audio.ondurationchange = () => {
        this.duration = audio.duration;
      };

      audio.onended = () => {
        this.next();
      };

      audio.onplay = () => {
        this.isPlaying = true;
      };

      audio.onpause = () => {
        this.isPlaying = false;
      };

      // 错误处理
      audio.onerror = (e) => {
        logger.error('PlayerStore', 'Audio playback error:', e);
        this.isPlaying = false;
      };
    },

    async togglePlay() {
      if (!this.currentTrackId) {
        const playlist = usePlaylistStore();
        if (playlist.defaultList.length > 0) {
          this.playTrack(playlist.defaultList[0].id);
        }
        return;
      }

      if (this.isPlaying) {
        audio.pause();
      } else {
        try {
          await audio.play();
        } catch (error) {
          logger.error('PlayerStore', 'Playback failed:', error);
        }
      }
    },

    setVolume(value: number) {
      this.volume = Math.max(0, Math.min(1, value));
      audio.volume = this.volume;
    },

    setPlaybackRate(rate: number) {
      const next = Math.max(0.5, Math.min(2, rate));
      this.playbackRate = next;
      audio.playbackRate = next;
    },

    seek(time: number) {
      const targetTime = Math.max(0, Math.min(this.duration, time));
      audio.currentTime = targetTime;
      this.currentTime = targetTime;
      useLyricStore().updateCurrentIndex(targetTime);
    },

    setPlayMode(mode: PlayMode) {
      this.playMode = mode;
    },

    async playTrack(id: string) {
      const playlistStore = usePlaylistStore();
      const lyricStore = useLyricStore();
      const track = playlistStore.defaultList.find((s) => s.id === id) || 
                    playlistStore.favorites.find((s) => s.id === id);
      
      if (track) {
        this.currentTrackId = id;
        audio.src = track.audioUrl;
        audio.load();

        // 设置歌词
        if (track.lyric) {
          lyricStore.setLyric(track.lyric);
        } else {
          lyricStore.setLyric('');
        }

        try {
          await audio.play();
          playlistStore.addToHistory(track);
        } catch (error) {
          logger.error('PlayerStore', 'Play track failed:', error);
        }
      }
    },

    stop() {
      audio.pause();
      audio.currentTime = 0;
      this.currentTime = 0;
      this.duration = 0;
      this.isPlaying = false;
      this.currentTrackId = null;
      useLyricStore().setLyric('');
    },

    next() {
      const playlistStore = usePlaylistStore();
      const list = playlistStore.defaultList;
      if (list.length === 0) return;

      let nextIndex = 0;
      const currentIndex = list.findIndex((s) => s.id === this.currentTrackId);

      if (this.playMode === 'random') {
        nextIndex = Math.floor(Math.random() * list.length);
      } else if (this.playMode === 'single') {
        nextIndex = currentIndex;
      } else {
        nextIndex = (currentIndex + 1) % list.length;
      }

      this.playTrack(list[nextIndex].id);
    },

    prev() {
      const playlistStore = usePlaylistStore();
      const list = playlistStore.defaultList;
      if (list.length === 0) return;

      const currentIndex = list.findIndex((s) => s.id === this.currentTrackId);
      let prevIndex = (currentIndex - 1 + list.length) % list.length;

      this.playTrack(list[prevIndex].id);
    },
  },
  persist: {
    pick: ['volume', 'playMode', 'currentTrackId', 'playbackRate'],
  },
});
