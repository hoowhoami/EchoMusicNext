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
import { getCoverUrl } from '@/utils/cover';
import type { Song, SongRelateGood } from '@/models/song';
import { doesRelateGoodMatchQuality, getSongQualityCandidates, isPlayableSong, resolveEffectiveSongQuality } from '@/utils/song';

export type AudioQualityValue = '128' | '320' | 'flac' | 'high';
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

const privilegeLiteRequests = new Map<string, Promise<SongRelateGood[]>>();

const parseRelateGoodsFromPrivilege = (payload: unknown): SongRelateGood[] => {
  if (!payload || typeof payload !== 'object') return [];
  const source = 'data' in (payload as Record<string, unknown>)
    ? (payload as { data?: unknown }).data
    : payload;
  const list = Array.isArray(source) ? source : [];
  const first = list[0] as Record<string, unknown> | undefined;
  const goods = (first?.relate_goods ?? first?.relateGoods ?? []) as unknown;
  if (!Array.isArray(goods)) return [];
  return goods
    .filter((item) => typeof item === 'object' && item !== null)
    .map((item) => item as Record<string, unknown>)
    .map((item) => ({
      hash: typeof item.hash === 'string' ? item.hash : undefined,
      quality: typeof item.quality === 'string' ? item.quality : undefined,
      level: typeof item.level === 'number' ? item.level : undefined,
    }));
};

const summarizeSong = (track: Song | undefined) => {
  if (!track) return null;
  return {
    id: String(track.id),
    title: track.title,
    artist: track.artist || '未知歌手',
    album: track.album || '',
    duration: track.duration || 0,
    hash: track.hash || '',
    privilege: track.privilege ?? null,
    payType: track.payType ?? null,
    source: track.source || '',
  };
};

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
    outputDeviceWatcherRegistered: false,
    currentAudioQualityOverride: null as AudioQualityValue | null,
  }),
  actions: {
    init() {
      const lyricStore = useLyricStore();
      const settingStore = useSettingStore();
      logger.info('PlayerStore', 'Initializing player store', {
        volume: this.volume,
        playbackRate: this.playbackRate,
        playMode: this.playMode,
      });
      // 恢复持久化的音量与倍速
      engine.setVolume(this.volume);
      engine.setPlaybackRate(this.playbackRate);

      this.registerSettingWatchers(settingStore);
      this.registerOutputDeviceWatcher(settingStore);
      void this.refreshOutputDevices(settingStore);

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
            logger.debug('PlayerStore', 'Ignore ended event after recent seek');
            this.recentSeekIgnoreEnd = false;
            return;
          }
          logger.info('PlayerStore', 'Received playback ended event', {
            currentTrackId: this.currentTrackId,
            currentTime: this.currentTime,
            duration: this.duration,
            playMode: this.playMode,
          });
          this.handlePlaybackEnded();
        },
        play: () => {
          logger.info('PlayerStore', 'Playback started', {
            currentTrackId: this.currentTrackId,
            currentTime: this.currentTime,
            duration: this.duration,
          });
          this.isPlaying = true;
          this.isLoading = false;
          settingStore.syncPreventSleep(true);
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
          logger.info('PlayerStore', 'Playback paused', {
            currentTrackId: this.currentTrackId,
            currentTime: this.currentTime,
            duration: this.duration,
          });
          this.isPlaying = false;
          settingStore.syncPreventSleep(false);
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
          settingStore.syncPreventSleep(false);

          if (settingStore.autoNext && this.currentPlaylist?.length) {
            this.pendingSwitch = null;
            void this.next();
            return;
          }

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
        defaultAudioQuality: settingStore.defaultAudioQuality,
        audioEffect: settingStore.audioEffect,
        compatibilityMode: settingStore.compatibilityMode,
        volumeFade: settingStore.volumeFade,
        volumeFadeTime: settingStore.volumeFadeTime,
        outputDevice: settingStore.outputDevice,
      };

      settingStore.$subscribe((_mutation, state) => {
        const shouldRefreshDefaultQuality =
          this.currentAudioQualityOverride === null
          && state.defaultAudioQuality !== snapshot.defaultAudioQuality;
        const shouldRefresh =
          shouldRefreshDefaultQuality ||
          state.audioEffect !== snapshot.audioEffect ||
          state.compatibilityMode !== snapshot.compatibilityMode;
        const shouldUpdateFade =
          state.volumeFade !== snapshot.volumeFade ||
          state.volumeFadeTime !== snapshot.volumeFadeTime;
        const shouldUpdateOutputDevice = state.outputDevice !== snapshot.outputDevice;

        snapshot = {
          defaultAudioQuality: state.defaultAudioQuality,
          audioEffect: state.audioEffect,
          compatibilityMode: state.compatibilityMode,
          volumeFade: state.volumeFade,
          volumeFadeTime: state.volumeFadeTime,
          outputDevice: state.outputDevice,
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

        if (shouldUpdateOutputDevice) {
          void this.applyOutputDevice(state.outputDevice, settingStore);
        }
      });
    },

    async togglePlay() {
      logger.info('PlayerStore', 'Toggle play requested', {
        currentTrackId: this.currentTrackId,
        isPlaying: this.isPlaying,
        hasSource: !!engine.source,
      });
      if (!this.currentTrackId) {
        const playlist = usePlaylistStore();
        if (playlist.defaultList.length > 0) {
          logger.info('PlayerStore', 'No active track, playing first item from default list', {
            firstTrackId: playlist.defaultList[0]?.id,
            listLength: playlist.defaultList.length,
          });
          this.playTrack(playlist.defaultList[0].id);
        }
        return;
      }

      if (this.isPlaying) {
        engine.pause();
        return;
      }

      if (!engine.source) {
        logger.info('PlayerStore', 'No active audio source, replaying current track', {
          currentTrackId: this.currentTrackId,
        });
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
      logger.debug('PlayerStore', 'Volume updated', {
        requested: value,
        applied: this.volume,
      });
    },

    async setVolumeSmooth(value: number, durationMs?: number) {
      await this.fadeVolume(value, { durationMs, respectUserVolume: false });
    },

    setPlaybackRate(rate: number) {
      this.playbackRate = engine.setPlaybackRate(rate);
      logger.info('PlayerStore', 'Playback rate updated', {
        rate: this.playbackRate,
      });
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
      logger.info('PlayerStore', 'Seek requested', {
        currentTrackId: this.currentTrackId,
        from: this.currentTime,
        to: targetTime,
        duration: this.duration,
      });
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
      logger.info('PlayerStore', 'Play mode updated', {
        mode,
      });
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
      logger.info('PlayerStore', 'Play track requested', {
        requestedTrackId: String(id),
        sourceListLength: sourceList.length,
        currentTrackId: this.currentTrackId,
        isPlaying: this.isPlaying,
      });
      const resolvedId = String(id);
      const track =
        sourceList.find((s) => String(s.id) === resolvedId) ||
        playlistStore.favorites.find((s) => String(s.id) === resolvedId) ||
        playlistStore.history.find((s) => String(s.id) === resolvedId);

      if (!track) {
        logger.warn('PlayerStore', 'Requested track not found in available lists', {
          requestedTrackId: resolvedId,
          sourceListLength: sourceList.length,
        });
        return;
      }

      logger.info('PlayerStore', 'Resolved track for playback', summarizeSong(track));

      if (!isPlayableSong(track)) {
        logger.warn('PlayerStore', 'Track not playable:', track);
        this.lastError = 'track-not-playable';
        return;
      }

      const previousTrackId = this.currentTrackId;
      const isSameTrackReplay = previousTrackId === resolvedId;
      const previousPlaylist = this.currentPlaylist;
      const previousAudioUrl = this.currentAudioUrl;
      const previousTime = this.currentTime;
      const wasPlaying = this.isPlaying;

      this.isLoading = true;
      this.lastError = null;

      logger.info('PlayerStore', 'Resolving audio url for track', {
        track: summarizeSong(track),
        defaultAudioQuality: settingStore.defaultAudioQuality,
        audioEffect: settingStore.audioEffect,
        compatibilityMode: settingStore.compatibilityMode,
      });
      const resolved = await this.resolveAudioUrl(track);
      if (!resolved) {
        logger.error('PlayerStore', 'Resolve audio url failed', {
          track: summarizeSong(track),
          autoNext: settingStore.autoNext,
        });
        this.isLoading = false;
        this.lastError = 'audio-url-unavailable';
        if (settingStore.autoNext && sourceList.length > 0) {
          this.currentPlaylist = sourceList;
          void this.next();
        }
        return;
      }

      if (!isSameTrackReplay) {
        this.currentAudioQualityOverride = null;
      }

      this.pendingSwitch = {
        previousTrackId,
        previousPlaylist,
        previousAudioUrl,
        previousTime,
        wasPlaying,
        targetTrackId: String(track.id),
      };
      logger.info('PlayerStore', 'Prepared pending switch', {
        previousTrackId,
        previousTime,
        wasPlaying,
        targetTrackId: String(track.id),
      });

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

      logger.info('PlayerStore', 'Binding resolved source to engine', {
        track: summarizeSong(track),
        audioUrlLength: resolved.length,
      });
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
        logger.info('PlayerStore', 'Play track succeeded', {
          track: summarizeSong(track),
          wasPlaying,
          playlistLength: sourceList.length,
        });
        this.isLoading = false;
        if (settingStore.volumeFade) {
          this.fadeIn();
        } else {
          engine.setVolume(this.volume);
        }
        logger.info('PlayerStore', 'Adding track to play history', summarizeSong(track));
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
        settingStore.syncPreventSleep(false);

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

    getEffectiveAudioQuality(settingStore = useSettingStore()): AudioQualityValue {
      return normalizeQuality(this.currentAudioQualityOverride ?? settingStore.defaultAudioQuality);
    },

    getResolvedAudioQuality(track: Pick<Song, 'relateGoods'>, settingStore = useSettingStore()): AudioQualityValue {
      return resolveEffectiveSongQuality(
        track,
        this.getEffectiveAudioQuality(settingStore),
        settingStore.compatibilityMode ?? true,
      );
    },

    async ensureTrackRelateGoods(track: Song): Promise<SongRelateGood[]> {
      const existing = track.relateGoods ?? [];
      if (existing.length > 0) return existing;
      if (!track.hash || track.source === 'cloud') return existing;

      const requestKey = `${track.hash}:${track.albumId ?? ''}`;
      const pending = privilegeLiteRequests.get(requestKey);
      if (pending) return pending;

      logger.info('PlayerStore', 'Preloading privilege lite for track', summarizeSong(track));
      const request = (async () => {
        try {
          const privilegeRes = await getSongPrivilegeLite(track.hash, track.albumId);
          const relateGoods = parseRelateGoodsFromPrivilege(privilegeRes);
          track.relateGoods = relateGoods;
          logger.info('PlayerStore', 'Preloaded privilege lite relateGoods', {
            track: summarizeSong(track),
            count: relateGoods.length,
            qualities: relateGoods.map((item) => item.quality ?? item.level ?? 'unknown'),
          });
          return relateGoods;
        } catch (error) {
          logger.warn('PlayerStore', 'Preload privilege lite failed:', error, summarizeSong(track));
          return existing;
        } finally {
          privilegeLiteRequests.delete(requestKey);
        }
      })();

      privilegeLiteRequests.set(requestKey, request);
      return request;
    },

    setCurrentAudioQualityOverride(quality: AudioQualityValue | null, options?: { refresh?: boolean }) {
      const nextQuality = quality ? normalizeQuality(quality) : null;
      if (this.currentAudioQualityOverride === nextQuality) return;
      this.currentAudioQualityOverride = nextQuality;
      logger.info('PlayerStore', 'Current track audio quality override updated', {
        override: this.currentAudioQualityOverride,
        effectiveAudioQuality: this.getEffectiveAudioQuality(),
        refresh: options?.refresh ?? true,
      });
      if (options?.refresh === false) return;
      if (!this.currentTrackId) return;
      if (this.isLoading || this.pendingSettingRefresh) {
        this.pendingSettingRefresh = true;
        return;
      }
      void this.refreshCurrentTrack();
    },

    resetCurrentAudioQualityOverride() {
      this.setCurrentAudioQualityOverride(null, { refresh: false });
    },

    stop() {
      const playlistStore = usePlaylistStore();
      logger.info('PlayerStore', 'Stopping playback and resetting player state', {
        currentTrackId: this.currentTrackId,
        currentTime: this.currentTime,
      });
      engine.reset();
      this.currentTime = 0;
      this.duration = 0;
      this.isPlaying = false;
      this.currentTrackId = null;
      this.currentAudioUrl = '';
      this.currentAudioQualityOverride = null;
      this.isLoading = false;
      this.pendingSwitch = null;
      this.outputDeviceWatcherRegistered = false;
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
      logger.info('PlayerStore', 'Skip to next requested', {
        currentTrackId: this.currentTrackId,
        playMode: this.playMode,
      });
      const list = playlistStore.defaultList.length > 0 ? playlistStore.defaultList : (this.currentPlaylist ?? []);
      if (list.length === 0) return;

      const queuedNextId = playlistStore.peekQueuedNextTrackId();
      if (queuedNextId) {
        logger.info('PlayerStore', 'Found queued next track', {
          queuedNextId,
        });
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
      if (!nextSong) {
        logger.warn('PlayerStore', 'No playable next track found', {
          listLength: list.length,
          currentTrackId: this.currentTrackId,
        });
        return;
      }
      logger.info('PlayerStore', 'Next track resolved', {
        nextIndex,
        track: summarizeSong(nextSong),
      });
      void this.playTrack(String(nextSong.id), list);
    },

    prev() {
      const playlistStore = usePlaylistStore();
      const list = playlistStore.defaultList.length > 0 ? playlistStore.defaultList : (this.currentPlaylist ?? []);
      logger.info('PlayerStore', 'Skip to previous requested', {
        currentTrackId: this.currentTrackId,
        playMode: this.playMode,
      });
      if (list.length === 0) return;

      const currentIndex = list.findIndex((s) => String(s.id) === String(this.currentTrackId));
      let prevIndex = (currentIndex - 1 + list.length) % list.length;

      prevIndex = findPlayableIndex(list, prevIndex, false, true);
      const prevSong = list[prevIndex];
      if (!prevSong) {
        logger.warn('PlayerStore', 'No playable previous track found', {
          listLength: list.length,
          currentTrackId: this.currentTrackId,
        });
        return;
      }
      logger.info('PlayerStore', 'Previous track resolved', {
        prevIndex,
        track: summarizeSong(prevSong),
      });
      this.playTrack(prevSong.id, list);
    },


    registerOutputDeviceWatcher(settingStore: ReturnType<typeof useSettingStore>) {
      if (this.outputDeviceWatcherRegistered) return;
      this.outputDeviceWatcherRegistered = true;
      if (!navigator.mediaDevices?.addEventListener) {
        logger.info('PlayerStore', 'Output device watcher unavailable: mediaDevices.addEventListener not supported');
        return;
      }

      logger.info('PlayerStore', 'Output device watcher registered');
      navigator.mediaDevices.addEventListener('devicechange', () => {
        logger.info('PlayerStore', 'Detected media device change, refreshing output devices');
        void this.refreshOutputDevices(settingStore);
      });
    },

    async refreshOutputDevices(settingStore: ReturnType<typeof useSettingStore>) {
      const fallbackOptions = [
        { label: '系统默认', value: 'default' },
      ];

      logger.info('PlayerStore', 'Refreshing output devices', {
        currentOutputDevice: settingStore.outputDevice,
        hasEnumerateDevices: typeof navigator.mediaDevices?.enumerateDevices === 'function',
        hasGetUserMedia: typeof navigator.mediaDevices?.getUserMedia === 'function',
      });

      if (!navigator.mediaDevices?.enumerateDevices) {
        logger.warn('PlayerStore', 'Output device enumeration unsupported in current environment');
        settingStore.outputDevices = fallbackOptions;
        settingStore.outputDeviceType = 'default';
        settingStore.setOutputDeviceStatus('unsupported', '当前系统暂不支持在应用内切换输出设备，请使用系统声音设置切换。');
        return;
      }

      try {
        const devices = await navigator.mediaDevices.enumerateDevices();
        const outputs = devices.filter((device) => device.kind === 'audiooutput');
        const outputOptions = outputs
          .filter((device) => device.deviceId && device.deviceId !== 'default')
          .map((device, index) => ({
            label: device.label || `输出设备 ${index + 1}`,
            value: device.deviceId,
          }));

        settingStore.outputDevices = [...fallbackOptions, ...outputOptions];

        logger.info('PlayerStore', 'Output devices enumerated', {
          totalDevices: devices.length,
          outputCount: outputs.length,
          options: outputOptions.map((device) => ({
            label: device.label,
            value: device.value,
          })),
        });

        const labelsMissing = outputs.length > 0 && outputs.every((device) => !device.label);
        if (labelsMissing) {
          settingStore.setOutputDeviceStatus('permission', '系统未返回设备名称，可能需要授予媒体设备权限后才能显示完整列表。');
        } else if (outputs.length <= 1) {
          settingStore.setOutputDeviceStatus('ready', '当前仅检测到系统默认输出设备。');
        } else {
          settingStore.setOutputDeviceStatus('ready', '已检测到可用输出设备。');
        }

        const currentOutput = settingStore.outputDevice;
        const hasCurrentDevice =
          currentOutput === 'default'
          || outputOptions.some((item) => item.value === currentOutput);

        if (!hasCurrentDevice) {
          const shouldPause = settingStore.pauseOnDeviceChange && this.isPlaying;
          logger.warn('PlayerStore', 'Current output device missing, fallback to default', {
            currentOutput,
            shouldPause,
          });
          settingStore.outputDevice = 'default';
          settingStore.outputDeviceType = 'default';
          await this.applyOutputDevice('default', settingStore);
          if (shouldPause) {
            engine.pause();
          }
          settingStore.setOutputDeviceStatus('fallback', '所选输出设备已不可用，已自动切回系统默认输出。');
          return;
        }

        await this.applyOutputDevice(currentOutput, settingStore);
      } catch (error) {
        logger.warn('PlayerStore', 'Refresh output devices failed:', error);
        settingStore.outputDevices = fallbackOptions;
        settingStore.outputDeviceType = 'default';
        const errorName = error instanceof DOMException ? error.name : '';
        if (errorName === 'NotAllowedError' || errorName === 'PermissionDeniedError') {
          settingStore.setOutputDeviceStatus('permission', '尚未获得音频设备权限，请先授权后再获取完整设备列表。');
        } else {
          settingStore.setOutputDeviceStatus('error', '获取输出设备失败，请检查系统音频权限或稍后重试。');
        }
      }
    },

    async requestOutputDevicePermission(settingStore = useSettingStore()) {
      logger.info('PlayerStore', 'Requesting output device permission', {
        currentOutputDevice: settingStore.outputDevice,
        hasGetUserMedia: typeof navigator.mediaDevices?.getUserMedia === 'function',
      });

      if (!navigator.mediaDevices?.getUserMedia) {
        settingStore.setOutputDeviceStatus('unsupported', '当前系统暂不支持在应用内请求音频设备权限，请使用系统声音设置切换输出设备。');
        return false;
      }

      try {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
        logger.info('PlayerStore', 'Output device permission granted', {
          trackCount: stream.getTracks().length,
        });
        stream.getTracks().forEach((track) => track.stop());
        await this.refreshOutputDevices(settingStore);

        if (settingStore.outputDeviceStatus === 'permission') {
          settingStore.setOutputDeviceStatus('permission', '已发起权限请求，但系统仍未返回完整设备信息，请确认系统已允许麦克风或媒体设备访问。');
          return false;
        }

        return true;
      } catch (error) {
        logger.warn('PlayerStore', 'Request output device permission failed:', error);
        const errorName = error instanceof DOMException ? error.name : '';
        if (errorName === 'NotAllowedError' || errorName === 'PermissionDeniedError') {
          settingStore.setOutputDeviceStatus('permission', '未获得音频设备权限，请在系统设置中允许麦克风或媒体设备访问后重试。');
        } else if (errorName === 'NotFoundError') {
          settingStore.setOutputDeviceStatus('error', '未检测到可用音频设备，请连接设备后重试。');
        } else {
          settingStore.setOutputDeviceStatus('error', '请求音频设备权限失败，请稍后重试。');
        }
        return false;
      }
    },

    async applyOutputDevice(deviceId: string, settingStore = useSettingStore()) {
      const targetDeviceId = deviceId;
      logger.info('PlayerStore', 'Applying output device', {
        requestedDeviceId: targetDeviceId,
        currentOutputDevice: settingStore.outputDevice,
      });
      const applied = await engine.setOutputDevice(targetDeviceId);
      if (!applied && targetDeviceId !== 'default') {
        logger.warn('PlayerStore', 'Apply output device failed, falling back to default', {
          requestedDeviceId: targetDeviceId,
        });
        await engine.setOutputDevice('default');
        settingStore.outputDevice = 'default';
        settingStore.setOutputDeviceStatus('fallback', '当前设备不支持切换到所选输出，已回退到系统默认输出。');
      } else if (!applied) {
        logger.warn('PlayerStore', 'Apply output device unsupported in current environment', {
          requestedDeviceId: targetDeviceId,
        });
        settingStore.setOutputDeviceStatus('unsupported', '当前系统暂不支持在应用内切换输出设备，请使用系统声音设置切换。');
      } else if (deviceId === 'default') {
        logger.info('PlayerStore', 'Output device switched to system default');
        settingStore.setOutputDeviceStatus('ready', '当前使用系统默认输出设备。');
      } else {
        const matched = settingStore.outputDevices.find((item) => item.value === deviceId);
        logger.info('PlayerStore', 'Output device switched successfully', {
          requestedDeviceId: targetDeviceId,
          label: matched?.label || '所选输出设备',
        });
        settingStore.setOutputDeviceStatus('ready', `已切换到 ${matched?.label || '所选输出设备'}。`);
      }
      settingStore.outputDeviceType = 'default';
    },

    async resolveAudioUrl(track: Song, options?: { forceReload?: boolean }): Promise<string> {
      if (!track.hash) {
        logger.warn('PlayerStore', 'Resolve audio url skipped because track hash is missing', summarizeSong(track));
        return '';
      }
      if (track.audioUrl && !options?.forceReload) {
        logger.debug('PlayerStore', 'Reuse cached audio url', {
          track: summarizeSong(track),
          forceReload: !!options?.forceReload,
        });
        return track.audioUrl;
      }

      const settingStore = useSettingStore();
      const audioQuality = this.getEffectiveAudioQuality(settingStore);
      const audioEffect = normalizeEffect(settingStore.audioEffect);
      const compatibilityMode = settingStore.compatibilityMode ?? true;

      if (track.source === 'cloud') {
        logger.info('PlayerStore', 'Resolving cloud track audio url', summarizeSong(track));
        const cloudUrl = await getCloudSongUrl(track.hash, track.mixSongId, track.albumId);
        if (cloudUrl) {
          logger.info('PlayerStore', 'Resolved cloud track audio url successfully', summarizeSong(track));
        } else {
          logger.warn('PlayerStore', 'Resolved cloud track audio url is empty', summarizeSong(track));
        }
        return cloudUrl ?? '';
      }

      let relateGoods = track.relateGoods ?? [];
      if (relateGoods.length === 0) {
        relateGoods = await this.ensureTrackRelateGoods(track);
      }

      if (audioEffect !== 'none') {
        try {
          logger.debug('PlayerStore', 'Trying effect audio url', {
            track: summarizeSong(track),
            audioEffect,
          });
          const effectRes = await getSongUrl(track.hash, audioEffect);
          const effectUrl = resolveUrlFromResponse(effectRes);
          if (effectUrl) {
            logger.info('PlayerStore', 'Resolved effect audio url successfully', {
              track: summarizeSong(track),
              audioEffect,
            });
            return effectUrl;
          }
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch effect url failed:', error);
        }
      }

      const candidates = getSongQualityCandidates(audioQuality, compatibilityMode);

      for (const quality of candidates) {
        const matched = relateGoods.find((item) => doesRelateGoodMatchQuality(item, quality) && item.hash);
        if (!matched?.hash) {
          logger.debug('PlayerStore', 'Skip quality candidate because relateGoods hash is missing', {
            track: summarizeSong(track),
            quality,
          });
          continue;
        }
        try {
          logger.debug('PlayerStore', 'Trying quality audio url', {
            track: summarizeSong(track),
            quality,
            hash: matched.hash,
          });
          const res = await getSongUrl(matched.hash, quality);
          const url = resolveUrlFromResponse(res);
          if (url) {
            logger.info('PlayerStore', 'Resolved quality audio url successfully', {
              track: summarizeSong(track),
              quality,
            });
            return url;
          }
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch quality url failed:', error);
        }
      }

      if (compatibilityMode) {
        try {
          logger.debug('PlayerStore', 'Trying fallback audio url with original hash', summarizeSong(track));
          const res = await getSongUrl(track.hash);
          const url = resolveUrlFromResponse(res);
          if (url) {
            logger.info('PlayerStore', 'Resolved fallback audio url successfully', summarizeSong(track));
            return url;
          }
        } catch (error) {
          logger.warn('PlayerStore', 'Fetch fallback url failed:', error);
        }
      }

      logger.error('PlayerStore', 'All audio url attempts failed', {
        track: summarizeSong(track),
        effectiveAudioQuality: audioQuality,
        audioEffect,
        compatibilityMode,
      });
      return '';
    },

    async refreshCurrentTrack() {
      if (!this.currentTrackId) return;
      if (this.isLoading) {
        logger.info('PlayerStore', 'Refresh current track deferred because player is loading', {
          currentTrackId: this.currentTrackId,
        });
        this.pendingSettingRefresh = true;
        return;
      }
      const playlistStore = usePlaylistStore();
      const settingStore = useSettingStore();
      const track = findTrackById(this.currentTrackId, this.currentPlaylist, playlistStore);
      if (!track) {
        logger.warn('PlayerStore', 'Refresh current track failed because active track is missing', {
          currentTrackId: this.currentTrackId,
        });
        return;
      }

      logger.info('PlayerStore', 'Refreshing current track source', {
        track: summarizeSong(track),
        wasPlaying: this.isPlaying,
        currentTime: this.currentTime,
      });
      this.pendingSettingRefresh = false;

      const wasPlaying = this.isPlaying;
      const previousTime = this.currentTime;
      this.isLoading = true;

      const resolved = await this.resolveAudioUrl(track, { forceReload: true });
      if (!resolved) {
        logger.error('PlayerStore', 'Refresh current track failed because resolved url is empty', summarizeSong(track));
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
          logger.info('PlayerStore', 'Refresh current track replay succeeded', {
            track: summarizeSong(track),
            restoredTime: previousTime,
          });
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
      logger.warn('PlayerStore', 'Recovering from pending switch', {
        reason,
        previousTrackId: fallback.previousTrackId,
        targetTrackId: fallback.targetTrackId,
        previousTime: fallback.previousTime,
        wasPlaying: fallback.wasPlaying,
      });
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
      logger.info('PlayerStore', 'Handling playback ended', {
        currentTrackId: this.currentTrackId,
        playMode: this.playMode,
      });
      if (this.playMode === 'single') {
        logger.info('PlayerStore', 'Single repeat mode active, replay current track');
        this.seek(0);
        void engine.play();
        return;
      }
      this.next();
    },

    async fetchClimaxMarks(track: Song) {
      if (!track.hash) return;
      logger.debug('PlayerStore', 'Fetching climax marks', summarizeSong(track));
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
        logger.debug('PlayerStore', 'Fetched climax marks', {
          track: summarizeSong(track),
          count: marks.length,
        });
      } catch (error) {
        logger.warn('PlayerStore', 'Fetch climax marks failed:', error);
      }
    },

    prepareFadeOut() {
      if (!this.isPlaying) return;
      const settingStore = useSettingStore();
      if (!settingStore.volumeFade) return;
      const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1000);
      logger.debug('PlayerStore', 'Prepare fade out before stop or switch', {
        currentTrackId: this.currentTrackId,
        durationMs: fadeMs,
      });
      void this.fadeVolume(0, { durationMs: fadeMs, respectUserVolume: true });
    },

    fadeIn() {
      const settingStore = useSettingStore();
      if (!settingStore.volumeFade) return;
      const fadeMs = clampNumber(settingStore.volumeFadeTime ?? 1000, 120, 1200);
      logger.debug('PlayerStore', 'Fade in playback volume', {
        targetVolume: this.volume,
        durationMs: fadeMs,
      });
      void this.fadeVolume(this.volume, { durationMs: fadeMs, respectUserVolume: false });
    },

    fadeVolume(
      target: number,
      options?: { durationMs?: number; respectUserVolume?: boolean }
    ): Promise<void> {
      const durationMs = Math.max(0, options?.durationMs ?? 1000);
      logger.debug('PlayerStore', 'Fade volume requested', {
        targetVolume: target,
        durationMs,
        respectUserVolume: options?.respectUserVolume ?? false,
      });
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
      logger.debug('PlayerStore', 'Picking random next index', {
        length,
        currentIndex,
      });
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
