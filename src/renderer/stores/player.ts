import { defineStore } from 'pinia';
import { usePlaylistStore } from './playlist';
import { useLyricStore } from './lyric';
import { useSettingStore } from './setting';
import logger from '@/utils/logger';
import { getSongUrl } from '@/api/music';
import {
  PlayerEngine,
  type MediaSessionMeta,
  type MediaSessionState,
  type PlayerEngineEvents,
} from '@/utils/player';
import { getCoverUrl } from '@/utils/music';

const AUDIO_URL_FIELDS = [
  'url',
  'play_url',
  'playUrl',
  'url_128',
  'url_320',
  'url_flac',
  'audio_url',
  'audioUrl',
] as const;

const resolveAudioUrl = (payload: unknown): string => {
  if (!payload) return '';
  if (typeof payload === 'string') return payload;

  if (Array.isArray(payload)) {
    for (const item of payload) {
      const url = resolveAudioUrl(item);
      if (url) return url;
    }
    return '';
  }

  if (typeof payload === 'object') {
    const record = payload as Record<string, unknown>;
    for (const key of AUDIO_URL_FIELDS) {
      const value = record[key];
      if (typeof value === 'string' && value.trim()) {
        return value;
      }
    }
    if ('data' in record) {
      return resolveAudioUrl(record.data);
    }
  }

  return '';
};

export type PlayMode = 'list' | 'single' | 'random';

// 保持一个全局 PlayerEngine 实例
const engine = new PlayerEngine();

const buildMediaMeta = (track: Song | undefined): MediaSessionMeta | null => {
  if (!track) return null;
  return {
    title: track.title,
    artist: track.artist || '未知歌手',
    album: track.album ?? '',
    artwork: [96, 128, 192, 256, 384, 512].map((size) => ({
      src: getCoverUrl(track.coverUrl, size),
      sizes: `${size}x${size}`,
      type: 'image/jpeg',
    })),
  };
};

const buildMediaState = (state: {
  isPlaying: boolean;
  duration: number;
  currentTime: number;
  playbackRate: number;
}): MediaSessionState => ({
  isPlaying: state.isPlaying,
  duration: state.duration,
  currentTime: state.currentTime,
  playbackRate: state.playbackRate,
});

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
      // 恢复持久化的音量与倍速
      engine.setVolume(this.volume);
      engine.setPlaybackRate(this.playbackRate);

      const events: PlayerEngineEvents = {
        timeUpdate: (currentTime) => {
          this.currentTime = currentTime;
          lyricStore.updateCurrentIndex(currentTime);
          engine.updateMediaPlaybackState(
            buildMediaState({
              isPlaying: this.isPlaying,
              duration: this.duration,
              currentTime,
              playbackRate: this.playbackRate,
            })
          );
        },
        durationChange: (duration) => {
          this.duration = duration;
          engine.updateMediaPlaybackState(
            buildMediaState({
              isPlaying: this.isPlaying,
              duration,
              currentTime: this.currentTime,
              playbackRate: this.playbackRate,
            })
          );
        },
        ended: () => {
          this.next();
        },
        play: () => {
          this.isPlaying = true;
          engine.updateMediaPlaybackState(
            buildMediaState({
              isPlaying: true,
              duration: this.duration,
              currentTime: this.currentTime,
              playbackRate: this.playbackRate,
            })
          );
        },
        pause: () => {
          this.isPlaying = false;
          engine.updateMediaPlaybackState(
            buildMediaState({
              isPlaying: false,
              duration: this.duration,
              currentTime: this.currentTime,
              playbackRate: this.playbackRate,
            })
          );
        },
        error: (event) => {
          logger.error('PlayerStore', 'Audio playback error:', event);
          this.isPlaying = false;
        },
      };

      engine.setEvents(events);
      engine.setMediaSessionHandlers({
        play: () => this.togglePlay(),
        pause: () => this.togglePlay(),
        previoustrack: () => this.prev(),
        nexttrack: () => this.next(),
        seekto: (time) => this.seek(time),
        seekbackward: (offset) => this.seek(Math.max(0, this.currentTime - offset)),
        seekforward: (offset) => this.seek(Math.min(this.duration, this.currentTime + offset)),
      });
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
        engine.pause();
        return;
      }

      if (!engine.source) {
        await this.playTrack(this.currentTrackId);
        return;
      }

      try {
        await engine.play();
      } catch (error) {
        logger.error('PlayerStore', 'Playback failed:', error);
      }
    },

    setVolume(value: number) {
      this.volume = engine.setVolume(value);
    },

    setPlaybackRate(rate: number) {
      this.playbackRate = engine.setPlaybackRate(rate);
      engine.updateMediaPlaybackState(
        buildMediaState({
          isPlaying: this.isPlaying,
          duration: this.duration,
          currentTime: this.currentTime,
          playbackRate: this.playbackRate,
        })
      );
    },

    seek(time: number) {
      const targetTime = Math.max(0, Math.min(this.duration, time));
      engine.seek(targetTime);
      this.currentTime = targetTime;
      useLyricStore().updateCurrentIndex(targetTime);
      engine.updateMediaPlaybackState(
        buildMediaState({
          isPlaying: this.isPlaying,
          duration: this.duration,
          currentTime: targetTime,
          playbackRate: this.playbackRate,
        })
      );
    },

    setPlayMode(mode: PlayMode) {
      this.playMode = mode;
      engine.updateMediaPlaybackState(
        buildMediaState({
          isPlaying: this.isPlaying,
          duration: this.duration,
          currentTime: this.currentTime,
          playbackRate: this.playbackRate,
        })
      );
    },

    async playTrack(id: string) {
      const playlistStore = usePlaylistStore();
      const lyricStore = useLyricStore();
      const track =
        playlistStore.defaultList.find((s) => s.id === id) ||
        playlistStore.favorites.find((s) => s.id === id) ||
        playlistStore.history.find((s) => s.id === id);

      if (!track) return;

      this.currentTrackId = id;

      if (!track.audioUrl && track.hash) {
        try {
          const settingStore = useSettingStore();
          const quality = settingStore.audioQuality ?? '';
          const res = await getSongUrl(track.hash, quality);
          const url = resolveAudioUrl(res);
          if (url) {
            track.audioUrl = url;
          }
        } catch (error) {
          logger.error('PlayerStore', 'Fetch play url failed:', error);
        }
      }

      if (!track.audioUrl) {
        logger.warn('PlayerStore', 'Track has no audioUrl:', track);
        engine.pause();
        this.isPlaying = false;
        return;
      }

      engine.setSource(track.audioUrl);

      // 设置歌词
      if (track.lyric) {
        lyricStore.setLyric(track.lyric);
      } else {
        lyricStore.setLyric('');
      }

      const mediaMeta = buildMediaMeta(track);
      if (mediaMeta) engine.updateMediaMetadata(mediaMeta);
      engine.updateMediaPlaybackState(
        buildMediaState({
          isPlaying: this.isPlaying,
          duration: this.duration,
          currentTime: this.currentTime,
          playbackRate: this.playbackRate,
        })
      );

      try {
        await engine.play();
        playlistStore.addToHistory(track);
      } catch (error) {
        logger.error('PlayerStore', 'Play track failed:', error);
      }
    },

    stop() {
      engine.reset();
      this.currentTime = 0;
      this.duration = 0;
      this.isPlaying = false;
      this.currentTrackId = null;
      useLyricStore().setLyric('');
      engine.updateMediaPlaybackState(
        buildMediaState({
          isPlaying: false,
          duration: 0,
          currentTime: 0,
          playbackRate: this.playbackRate,
        })
      );
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
