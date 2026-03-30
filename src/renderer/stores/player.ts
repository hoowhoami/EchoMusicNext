import { defineStore } from 'pinia';
import { usePlaylistStore } from './playlist';
import { useLyricStore } from './lyric';
import { useSettingStore } from './setting';
import logger from '@/utils/logger';
import { getCloudSongUrl, getSongClimax, getSongPrivilegeLite, getSongUrl } from '@/api/music';
import {
  PlayerEngine,
  type MediaSessionMeta,
  type MediaSessionState,
  type PlayerEngineEvents,
} from '@/utils/player';
import { getCoverUrl } from '@/utils/music';
import type { Song } from '@/models/song';
import { isPlayableSong } from '@/utils/songPlayback';

type AudioQualityValue = '128' | '320' | 'flac' | 'high';
type AudioEffectValue =
  | 'none'
  | 'piano'
  | 'acappella'
  | 'subwoofer'
  | 'ancient'
  | 'surnay'
  | 'dj'
  | 'viper_tape'
  | 'viper_atmos'
  | 'viper_clear';

const QUALITY_PRIORITY: AudioQualityValue[] = ['high', 'flac', '320', '128'];

const normalizeQuality = (value: string | undefined): AudioQualityValue => {
  if (value === '128' || value === '320' || value === 'flac' || value === 'high') return value;
  return 'high';
};

const normalizeEffect = (value: string | undefined): AudioEffectValue => {
  const options: AudioEffectValue[] = [
    'none',
    'piano',
    'acappella',
    'subwoofer',
    'ancient',
    'surnay',
    'dj',
    'viper_tape',
    'viper_atmos',
    'viper_clear',
  ];
  return options.includes(value as AudioEffectValue) ? (value as AudioEffectValue) : 'none';
};

const clampNumber = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

const findPlayableIndex = (
  songs: Song[],
  startIndex: number,
  forward: boolean,
  inclusive = true,
): number => {
  if (songs.length === 0) return -1;
  const normalizedStart = startIndex >= 0 ? startIndex % songs.length : 0;
  for (let step = 0; step < songs.length; step += 1) {
    const offset = inclusive ? step : step + 1;
    const index = forward
      ? (normalizedStart + offset) % songs.length
      : (normalizedStart - offset + songs.length) % songs.length;
    if (isPlayableSong(songs[index])) return index;
  }
  return -1;
};

const resolveUrlFromResponse = (payload: unknown): string => {
  if (!payload) return '';
  if (typeof payload === 'string') return payload.trim();
  if (Array.isArray(payload)) {
    const first = payload.find((item) => typeof item === 'string' && item.trim());
    return typeof first === 'string' ? first : '';
  }
  if (typeof payload === 'object') {
    const record = payload as Record<string, unknown>;
    const urlField = record.url ?? record.play_url ?? record.playUrl;
    if (typeof urlField === 'string' && urlField.trim()) return urlField;
    if (Array.isArray(urlField)) {
      const candidate = urlField.find((item) => typeof item === 'string' && item.trim());
      return typeof candidate === 'string' ? candidate : '';
    }
    if ('data' in record) return resolveUrlFromResponse(record.data);
    if ('info' in record) return resolveUrlFromResponse(record.info);
  }
  return '';
};

const findTrackById = (
  id: string | null,
  list: Song[] | null | undefined,
  playlistStore: ReturnType<typeof usePlaylistStore>,
): Song | undefined => {
  if (!id) return undefined;
  const targetId = String(id);
  const pool = [list ?? [], playlistStore.defaultList, playlistStore.favorites, playlistStore.history];
  for (const group of pool) {
    const found = group.find((song) => String(song.id) === targetId);
    if (found) return found;
  }
  return undefined;
};

type PendingSwitch = {
  previousTrackId: string | null;
  previousPlaylist: Song[] | null;
  previousAudioUrl: string;
  previousTime: number;
  wasPlaying: boolean;
  targetTrackId: string;
};

type ClimaxMark = { start: number; end: number };

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
    isLoading: false,
    lastError: '' as string | null,
    currentPlaylist: null as Song[] | null,
    currentAudioUrl: '' as string,
    recentSeekIgnoreEnd: false,
    settingsWatcherRegistered: false,
    pendingSwitch: null as PendingSwitch | null,
    isDraggingProgress: false,
    pendingSettingRefresh: false,
    climaxMarks: [] as ClimaxMark[],
  }),
  actions: {
    init() {
      const lyricStore = useLyricStore();
      const settingStore = useSettingStore();
      // 恢复持久化的音量与倍速
      engine.setVolume(this.volume);
      engine.setPlaybackRate(this.playbackRate);

      this.registerSettingWatchers(settingStore);

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
          if (this.recentSeekIgnoreEnd) {
            this.recentSeekIgnoreEnd = false;
            return;
          }
          this.handlePlaybackEnded();
        },
        play: () => {
          this.isPlaying = true;
          this.isLoading = false;
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
          this.lastError = event?.type ?? 'playback-error';
          this.isLoading = false;
          this.isPlaying = false;

          if (this.pendingSwitch?.targetTrackId === this.currentTrackId) {
            void this.recoverFromPendingSwitch('engine-error');
          }
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

    registerSettingWatchers(settingStore: ReturnType<typeof useSettingStore>) {
      if (this.settingsWatcherRegistered) return;
      this.settingsWatcherRegistered = true;

      let snapshot = {
        audioQuality: settingStore.audioQuality,
        audioEffect: settingStore.audioEffect,
        compatibilityMode: settingStore.compatibilityMode,
        volumeFade: settingStore.volumeFade,
        volumeFadeTime: settingStore.volumeFadeTime,
      };

      settingStore.$subscribe((_mutation, state) => {
        const shouldRefresh =
          state.audioQuality !== snapshot.audioQuality ||
          state.audioEffect !== snapshot.audioEffect ||
          state.compatibilityMode !== snapshot.compatibilityMode;
        const shouldUpdateFade =
          state.volumeFade !== snapshot.volumeFade ||
          state.volumeFadeTime !== snapshot.volumeFadeTime;

        snapshot = {
          audioQuality: state.audioQuality,
          audioEffect: state.audioEffect,
          compatibilityMode: state.compatibilityMode,
          volumeFade: state.volumeFade,
          volumeFadeTime: state.volumeFadeTime,
        };

        if (shouldRefresh) {
          if (this.isLoading || this.pendingSettingRefresh) {
            this.pendingSettingRefresh = true;
          } else {
            void this.refreshCurrentTrack();
          }
        }

        if (shouldUpdateFade && this.isPlaying) {
          void this.fadeVolume(this.volume, { durationMs: 120, respectUserVolume: false });
        }
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

    notifySeekStart() {
      this.isDraggingProgress = true;
    },

    notifySeekEnd() {
      this.isDraggingProgress = false;
    },

    setVolume(value: number) {
      this.volume = engine.setVolume(value);
    },

    async setVolumeSmooth(value: number, durationMs?: number) {
      await this.fadeVolume(value, { durationMs, respectUserVolume: false });
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
      if (this.isDraggingProgress) {
        this.isDraggingProgress = false;
      }
      engine.seek(targetTime);
      this.currentTime = targetTime;
      this.recentSeekIgnoreEnd = true;
      window.setTimeout(() => {
        this.recentSeekIgnoreEnd = false;
      }, 800);
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

    async playTrack(id: string, playlist?: Song[]) {
      const playlistStore = usePlaylistStore();
      const lyricStore = useLyricStore();
      const settingStore = useSettingStore();
      const sourceList = playlist ?? playlistStore.defaultList;
      const resolvedId = String(id);
      const track =
        sourceList.find((s) => String(s.id) === resolvedId) ||
        playlistStore.favorites.find((s) => String(s.id) === resolvedId) ||
        playlistStore.history.find((s) => String(s.id) === resolvedId);

      if (!track) return;

      if (!isPlayableSong(track)) {
        logger.warn('PlayerStore', 'Track not playable:', track);
        this.lastError = 'track-not-playable';
        return;
      }

      const previousTrackId = this.currentTrackId;
      const previousPlaylist = this.currentPlaylist;
      const previousAudioUrl = this.currentAudioUrl;
      const previousTime = this.currentTime;
      const wasPlaying = this.isPlaying;

      this.isLoading = true;
      this.lastError = null;

      const resolved = await this.resolveAudioUrl(track);
      if (!resolved) {
        this.isLoading = false;
        this.lastError = 'audio-url-unavailable';
        return;
      }

      this.pendingSwitch = {
        previousTrackId,
        previousPlaylist,
        previousAudioUrl,
        previousTime,
        wasPlaying,
        targetTrackId: String(track.id),
      };

      if (wasPlaying && settingStore.volumeFade) {
        const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1000);
        await this.fadeVolume(0, { durationMs: fadeMs, respectUserVolume: true });
      } else {
        engine.setVolume(this.volume);
      }

      this.currentTrackId = id;
      this.currentPlaylist = sourceList;
      playlistStore.consumeQueuedNextTrackId(id);
      playlistStore.syncQueuedNextTrackIds();
      this.currentAudioUrl = resolved;
      track.audioUrl = resolved;
      this.climaxMarks = [];
      this.currentTime = 0;
      this.duration = 0;

      engine.setSource(resolved);

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
        this.isLoading = false;
        if (settingStore.volumeFade) {
          this.fadeIn();
        } else {
          engine.setVolume(this.volume);
        }
        playlistStore.addToHistory(track);
        this.pendingSwitch = null;
        void this.fetchClimaxMarks(track);
        if (this.pendingSettingRefresh) {
          this.pendingSettingRefresh = false;
          void this.refreshCurrentTrack();
        }
      } catch (error) {
        logger.error('PlayerStore', 'Play track failed:', error);
        this.isLoading = false;
        this.lastError = 'playback-failed';

        if (previousTrackId && previousAudioUrl) {
          this.currentTrackId = previousTrackId;
          this.currentPlaylist = previousPlaylist;
          this.currentAudioUrl = previousAudioUrl;
          engine.setSource(previousAudioUrl);
          if (previousTime > 0) {
            engine.seek(previousTime);
          }
          if (wasPlaying) {
            try {
              await engine.play();
            } catch (resumeError) {
              logger.error('PlayerStore', 'Resume previous track failed:', resumeError);
            }
          }
        }

        this.pendingSwitch = null;

        if (settingStore.volumeFade) {
          this.fadeIn();
        } else {
          engine.setVolume(this.volume);
        }

        if (this.pendingSettingRefresh) {
          this.pendingSettingRefresh = false;
          void this.refreshCurrentTrack();
        }
      }
    },

    stop() {
      const playlistStore = usePlaylistStore();
      engine.reset();
      this.currentTime = 0;
      this.duration = 0;
      this.isPlaying = false;
      this.currentTrackId = null;
      this.currentAudioUrl = '';
      this.isLoading = false;
      this.pendingSwitch = null;
      playlistStore.queuedNextTrackIds = [];
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
      playlistStore.syncQueuedNextTrackIds();
      const list = playlistStore.defaultList.length > 0 ? playlistStore.defaultList : (this.currentPlaylist ?? []);
      if (list.length === 0) return;

      const queuedNextId = playlistStore.peekQueuedNextTrackId();
      if (queuedNextId) {
        const queuedSong = list.find((song) => String(song.id) === queuedNextId);
        if (queuedSong && isPlayableSong(queuedSong)) {
          playlistStore.consumeQueuedNextTrackId(queuedNextId);
          void this.playTrack(String(queuedSong.id), list);
          return;
        }
        playlistStore.consumeQueuedNextTrackId(queuedNextId);
      }

      let nextIndex = 0;
      const currentIndex = list.findIndex((s) => String(s.id) === String(this.currentTrackId));

      if (this.playMode === 'random') {
        nextIndex = this.pickRandomIndex(list.length, currentIndex);
      } else if (this.playMode === 'single') {
        nextIndex = currentIndex;
      } else {
        nextIndex = (currentIndex + 1) % list.length;
      }

      if (this.playMode !== 'random') {
        nextIndex = findPlayableIndex(list, nextIndex, true, true);
      } else if (!isPlayableSong(list[nextIndex])) {
        nextIndex = findPlayableIndex(list, nextIndex, true, false);
      }

      const nextSong = list[nextIndex];
      if (!nextSong) return;
      void this.playTrack(String(nextSong.id), list);
    },

    prev() {
      const playlistStore = usePlaylistStore();
      const list = playlistStore.defaultList.length > 0 ? playlistStore.defaultList : (this.currentPlaylist ?? []);
      if (list.length === 0) return;

      const currentIndex = list.findIndex((s) => String(s.id) === String(this.currentTrackId));
      let prevIndex = (currentIndex - 1 + list.length) % list.length;

      prevIndex = findPlayableIndex(list, prevIndex, false, true);
      const prevSong = list[prevIndex];
      if (!prevSong) return;
      this.playTrack(prevSong.id, list);
    },

    async resolveAudioUrl(track: Song, options?: { forceReload?: boolean }): Promise<string> {
      if (!track.hash) return '';
      if (track.audioUrl && !options?.forceReload) return track.audioUrl;

      const settingStore = useSettingStore();
      const audioQuality = normalizeQuality(settingStore.audioQuality);
      const audioEffect = normalizeEffect(settingStore.audioEffect);
      const compatibilityMode = settingStore.compatibilityMode ?? true;

      if (track.source === 'cloud') {
        const cloudUrl = await getCloudSongUrl(track.hash, track.mixSongId, track.albumId);
        return cloudUrl ?? '';
      }

      let relateGoods = track.relateGoods ?? [];
      if (relateGoods.length === 0) {
        try {
          const privilegeRes = await getSongPrivilegeLite(track.hash, track.albumId);
          if (privilegeRes && typeof privilegeRes === 'object') {
            const payload = 'data' in privilegeRes ? (privilegeRes as { data?: unknown }).data : privilegeRes;
            const list = Array.isArray(payload) ? payload : [];
            const first = list[0] as Record<string, unknown> | undefined;
            const goods = (first?.relate_goods ?? first?.relateGoods ?? []) as unknown;
            if (Array.isArray(goods)) {
              relateGoods = goods
                .filter((item) => typeof item === 'object' && item !== null)
                .map((item) => item as Record<string, unknown>)
                .map((item) => ({
                  hash: typeof item.hash === 'string' ? item.hash : undefined,
                  quality: typeof item.quality === 'string' ? item.quality : undefined,
                  level: typeof item.level === 'number' ? item.level : undefined,
                }));
              track.relateGoods = relateGoods;
            }
          }
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch privilege lite failed:', error);
        }
      }

      if (audioEffect !== 'none') {
        try {
          const effectRes = await getSongUrl(track.hash, audioEffect);
          const effectUrl = resolveUrlFromResponse(effectRes);
          if (effectUrl) return effectUrl;
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch effect url failed:', error);
        }
      }

      const qualityPriority = QUALITY_PRIORITY;
      const userIndex = Math.max(0, qualityPriority.indexOf(audioQuality));
      const candidates = [audioQuality];
      if (compatibilityMode) {
        for (let i = userIndex + 1; i < qualityPriority.length; i += 1) {
          const value = qualityPriority[i];
          if (!candidates.includes(value)) candidates.push(value);
        }
      }

      for (const quality of candidates) {
        const matched = relateGoods.find((item) => item.quality === quality && item.hash);
        if (!matched?.hash) continue;
        try {
          const res = await getSongUrl(matched.hash, quality);
          const url = resolveUrlFromResponse(res);
          if (url) return url;
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch quality url failed:', error);
        }
      }

      if (compatibilityMode) {
        try {
          const res = await getSongUrl(track.hash);
          const url = resolveUrlFromResponse(res);
          if (url) return url;
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch fallback url failed:', error);
        }
      }

      return '';
    },

    async refreshCurrentTrack() {
      if (!this.currentTrackId) return;
      if (this.isLoading) {
        this.pendingSettingRefresh = true;
        return;
      }
      const playlistStore = usePlaylistStore();
      const settingStore = useSettingStore();
      const track = findTrackById(this.currentTrackId, this.currentPlaylist, playlistStore);
      if (!track) return;

      this.pendingSettingRefresh = false;

      const wasPlaying = this.isPlaying;
      const previousTime = this.currentTime;
      this.isLoading = true;

      const resolved = await this.resolveAudioUrl(track, { forceReload: true });
      if (!resolved) {
        this.isLoading = false;
        this.lastError = 'audio-url-unavailable';
        return;
      }

      if (wasPlaying && settingStore.volumeFade) {
        const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1000);
        await this.fadeVolume(0, { durationMs: fadeMs, respectUserVolume: true });
      } else {
        engine.setVolume(this.volume);
      }

      this.currentAudioUrl = resolved;
      track.audioUrl = resolved;
      engine.setSource(resolved);
      engine.setPlaybackRate(this.playbackRate);
      void this.fetchClimaxMarks(track);

      if (previousTime > 0) {
        engine.seek(previousTime);
        this.currentTime = previousTime;
        useLyricStore().updateCurrentIndex(previousTime);
      }

      if (wasPlaying) {
        try {
          await engine.play();
        } catch (error) {
          logger.error('PlayerStore', 'Reload track failed:', error);
        }
      }

      if (wasPlaying && settingStore.volumeFade) {
        this.fadeIn();
      } else if (wasPlaying) {
        engine.setVolume(this.volume);
      }

      this.isLoading = false;
    },

    async recoverFromPendingSwitch(reason: string) {
      if (!this.pendingSwitch) return;
      const fallback = this.pendingSwitch;
      this.pendingSwitch = null;
      if (!fallback.previousTrackId || !fallback.previousAudioUrl) return;

      this.currentTrackId = fallback.previousTrackId;
      this.currentPlaylist = fallback.previousPlaylist;
      this.currentAudioUrl = fallback.previousAudioUrl;
      engine.setSource(fallback.previousAudioUrl);
      if (fallback.previousTime > 0) {
        engine.seek(fallback.previousTime);
      }
      if (fallback.wasPlaying) {
        try {
          await engine.play();
        } catch (resumeError) {
          logger.error('PlayerStore', 'Recover previous track failed:', resumeError, reason);
        }
      }
    },

    handlePlaybackEnded() {
      if (this.playMode === 'single') {
        this.seek(0);
        void engine.play();
        return;
      }
      this.next();
    },

    async fetchClimaxMarks(track: Song) {
      if (!track.hash) return;
      try {
        const res = await getSongClimax(track.hash);
        const data = res && typeof res === 'object' ? (res as { data?: unknown }).data : undefined;
        const list = Array.isArray(data) ? data : [];
        const marks: ClimaxMark[] = [];
        const duration = track.duration || this.duration || 0;
        const total = duration > 0 ? duration : 1;

        list.forEach((item) => {
          if (!item || typeof item !== 'object') return;
          const record = item as Record<string, unknown>;
          const startRaw = record.start_time ?? record.starttime ?? record.start ?? 0;
          const endRaw = record.end_time ?? record.endtime ?? record.end ?? 0;
          const startMs = Number(startRaw);
          const endMs = Number(endRaw || startMs + 15000);
          if (!Number.isFinite(startMs)) return;
          const start = Math.max(0, startMs / 1000);
          const end = Math.max(start, endMs / 1000);
          const normalizedStart = start / total;
          const normalizedEnd = end / total;
          if (normalizedStart <= 1) {
            marks.push({
              start: normalizedStart,
              end: Math.min(1, normalizedEnd),
            });
          }
        });

        this.climaxMarks = marks;
      } catch (error) {
        logger.warn('PlayerStore', 'Fetch climax marks failed:', error);
      }
    },

    prepareFadeOut() {
      if (!this.isPlaying) return;
      const settingStore = useSettingStore();
      if (!settingStore.volumeFade) return;
      const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1000);
      void this.fadeVolume(0, { durationMs: fadeMs, respectUserVolume: true });
    },

    fadeIn() {
      const settingStore = useSettingStore();
      if (!settingStore.volumeFade) return;
      const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1200);
      void this.fadeVolume(this.volume, { durationMs: fadeMs, respectUserVolume: false });
    },

    fadeVolume(
      target: number,
      options?: { durationMs?: number; respectUserVolume?: boolean }
    ): Promise<void> {
      const durationMs = Math.max(0, options?.durationMs ?? 1000);
      const respectUserVolume = options?.respectUserVolume ?? false;
      const targetValue = respectUserVolume ? Math.min(target, this.volume) : target;
      return engine.fadeTo(targetValue, durationMs).then(() => {
        if (!respectUserVolume) {
          this.volume = engine.volume;
        }
      });
    },

    pickRandomIndex(length: number, currentIndex: number) {
      if (length <= 1) return currentIndex;
      let nextIndex = Math.floor(Math.random() * length);
      if (nextIndex === currentIndex) {
        nextIndex = (nextIndex + 1) % length;
      }
      return nextIndex;
    },
  },
  persist: {
    pick: ['volume', 'playMode', 'currentTrackId', 'playbackRate'],
  },
});
