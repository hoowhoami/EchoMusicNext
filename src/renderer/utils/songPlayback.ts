import type { Song, SongRelateGood } from '@/models/song';

export interface PlaybackQueueStoreLike {
  setPlaybackQueue: (songs: Song[], filteredInvalidCount?: number) => void;
  defaultList?: Song[];
  enqueuePlayNext?: (songId: string | number) => void;
  syncQueuedNextTrackIds?: () => void;
}

export interface PlaybackPlayerLike {
  playTrack: (id: string, playlist?: Song[]) => Promise<void> | void;
  currentTrackId?: string | null;
  isPlaying?: boolean;
  togglePlay?: () => void;
}

export interface ResolvedPlayableQueue {
  queue: Song[];
  firstPlayable: Song | null;
  filteredInvalidCount: number;
  sourceCount: number;
}

export interface SongPrivilegeTag {
  label: string;
  color: string;
  message: string;
}

export const isVipSong = (song: Song): boolean => song.privilege === 10 && song.payType === 3;

export const isPaidSong = (song: Song): boolean => song.privilege === 10 && song.payType === 2;

export const isNoCopyrightSong = (song: Song): boolean => song.privilege === 5;

export const isUnavailableSong = (song: Song): boolean => song.privilege === 40;

export const canPlaySong = (song: Song): boolean => {
  if (isUnavailableSong(song)) return false;
  if (isPaidSong(song)) return false;
  if (isNoCopyrightSong(song)) return song.oldCpy === 1;
  return true;
};

export const isPlayableSong = (song: Song): boolean => Boolean(song.hash?.trim()) && canPlaySong(song);

export const isSameSong = (left: Song, right: Song): boolean => {
  const leftMixSongId = String(left.mixSongId ?? '0');
  const rightMixSongId = String(right.mixSongId ?? '0');
  if (leftMixSongId !== '0' && rightMixSongId !== '0' && leftMixSongId === rightMixSongId) {
    return true;
  }

  const leftHash = String(left.hash ?? '').trim().toLowerCase();
  const rightHash = String(right.hash ?? '').trim().toLowerCase();
  if (leftHash && rightHash && leftHash === rightHash) {
    return true;
  }

  return false;
};

export const getSongPrivilegeTags = (song: Song): SongPrivilegeTag[] => {
  const tags: SongPrivilegeTag[] = [];

  if (isPaidSong(song)) {
    tags.push({ label: '付费', color: '#EF4444', message: '需要购买' });
  }
  if (isVipSong(song)) {
    tags.push({ label: 'VIP', color: '#F59E0B', message: '需要VIP' });
  }
  if (!isPlayableSong(song) && isNoCopyrightSong(song)) {
    tags.push({ label: '版权', color: '#8B5CF6', message: '无版权' });
  }
  if (isUnavailableSong(song)) {
    tags.push({ label: '音源', color: '#6B7280', message: '不可用' });
  }

  return tags;
};

export const getSongUnavailableMessage = (song: Song): string | null => {
  if (isPlayableSong(song)) return null;
  if (isPaidSong(song)) return '需要购买';
  if (isNoCopyrightSong(song)) return '无版权';
  if (isUnavailableSong(song)) return '不可用';
  return '暂无音源';
};

export const getSongQualityTag = (song: Pick<Song, 'relateGoods'>): string => {
  const goods = song.relateGoods ?? [];
  const hasQuality = (quality: string, level: number) =>
    goods.some((item: SongRelateGood) => item.quality === quality || item.level === level);

  if (hasQuality('high', 6)) return 'Hi-Res';
  if (hasQuality('flac', 5)) return 'SQ';
  if (hasQuality('320', 4)) return 'HQ';
  return '';
};

const QUALITY_LABEL_MAP: Record<string, string> = {
  high: 'Hi-Res',
  flac: 'SQ',
  '320': 'HQ',
};

const EFFECT_QUALITIES = new Set([
  'piano',
  'acappella',
  'subwoofer',
  'ancient',
  'surnay',
  'dj',
  'viper_tape',
  'viper_atmos',
  'viper_clear',
]);

export const getSongQualityTags = (relateGoods: SongRelateGood[] | undefined): string[] => {
  if (!relateGoods?.length) return [];
  const seen = new Set<string>();
  const tags: string[] = [];
  for (const item of relateGoods) {
    const quality = item.quality ?? '';
    const normalized = QUALITY_LABEL_MAP[quality] ?? '';
    if (!normalized || EFFECT_QUALITIES.has(quality) || seen.has(normalized)) continue;
    seen.add(normalized);
    tags.push(normalized);
  }
  return tags;
};

export const getSongEffectTags = (relateGoods: SongRelateGood[] | undefined): string[] => {
  if (!relateGoods?.length) return [];
  const seen = new Set<string>();
  const tags: string[] = [];
  for (const item of relateGoods) {
    const quality = item.quality ?? '';
    if (!quality || !EFFECT_QUALITIES.has(quality) || seen.has(quality)) continue;
    seen.add(quality);
    tags.push(quality);
  }
  return tags;
};

export const resolvePlayableSongForRequest = (
  requestedSong: Song,
  playlist: Song[] = [],
): Song | null => {
  if (isPlayableSong(requestedSong)) return requestedSong;
  if (playlist.length === 0) return null;

  const requestedIndex = playlist.findIndex((song) => isSameSong(song, requestedSong));
  const startIndex = requestedIndex === -1 ? 0 : requestedIndex;

  for (let step = 0; step < playlist.length; step += 1) {
    const index = requestedIndex === -1 ? step : (startIndex + step) % playlist.length;
    const song = playlist[index];
    if (isPlayableSong(song)) return song;
  }

  return null;
};

const isMeaninglessHashlessSong = (song: Song): boolean => {
  const title = String(song.title ?? '').trim();
  const artist = String(song.artist ?? '').trim();
  const album = String(song.album ?? '').trim();
  const cover = String(song.coverUrl ?? '').trim();
  const hash = String(song.hash ?? '').trim();
  const mixSongId = String(song.mixSongId ?? '0').trim();
  const artists = song.artists ?? [];

  return !hash && !title && !artist && artists.length === 0 && !album && !cover && (!mixSongId || mixSongId === '0');
};

export const resolvePlayableQueue = (
  songs: Song[],
  filteredInvalidCount = 0,
): ResolvedPlayableQueue => {
  const queue: Song[] = [];
  let hiddenCount = Math.max(0, filteredInvalidCount);

  for (const song of songs) {
    if (isMeaninglessHashlessSong(song)) {
      hiddenCount += 1;
      continue;
    }
    queue.push(song);
  }

  return {
    queue,
    firstPlayable: resolvePlayableSongForRequest(queue[0] ?? songs[0], queue),
    filteredInvalidCount: hiddenCount,
    sourceCount: queue.length + hiddenCount,
  };
};

export const replaceQueueAndPlay = async (
  playlistStore: PlaybackQueueStoreLike,
  playerStore: PlaybackPlayerLike,
  songs: Song[],
  filteredInvalidCount = 0,
): Promise<boolean> => {
  const resolved = resolvePlayableQueue(songs, filteredInvalidCount);
  if (!resolved.firstPlayable) return false;
  playlistStore.setPlaybackQueue(resolved.queue, resolved.filteredInvalidCount);
  await playerStore.playTrack(String(resolved.firstPlayable.id), resolved.queue);
  return true;
};

export const queueAndPlaySong = async (
  playlistStore: PlaybackQueueStoreLike,
  playerStore: PlaybackPlayerLike,
  song: Song,
): Promise<boolean> => {
  const activeList = playlistStore.defaultList ?? [];
  const resolvedSong = resolvePlayableSongForRequest(song, [song]);
  if (!resolvedSong) return false;

  const nextList = activeList.slice();
  const exists = nextList.some((item) => isSameSong(item, resolvedSong));
  if (!exists) {
    nextList.push(resolvedSong);
    playlistStore.setPlaybackQueue(nextList, 0);
  }

  await playerStore.playTrack(String(resolvedSong.id), playlistStore.defaultList ?? nextList);
  return true;
};

export const addSongToPlayNext = (
  playlistStore: PlaybackQueueStoreLike,
  playerStore: PlaybackPlayerLike,
  song: Song,
): boolean => {
  const resolvedSong = resolvePlayableSongForRequest(song, [song]);
  if (!resolvedSong) return false;

  const list = (playlistStore.defaultList ?? []).slice();
  const currentTrackId = String(playerStore.currentTrackId ?? '');
  const currentIndex = list.findIndex((item) => String(item.id) === currentTrackId);
  const currentSong = currentIndex >= 0 ? list[currentIndex] : null;

  if (currentSong && isSameSong(currentSong, resolvedSong)) {
    return true;
  }

  let insertIndex = currentIndex >= 0 ? currentIndex + 1 : 0;
  const existingIndex = list.findIndex((item) => isSameSong(item, resolvedSong));
  const item = existingIndex >= 0 ? list.splice(existingIndex, 1)[0] : resolvedSong;

  if (existingIndex !== -1 && currentIndex > existingIndex) {
    insertIndex -= 1;
  }

  if (insertIndex < 0) insertIndex = 0;
  if (insertIndex > list.length) insertIndex = list.length;
  list.splice(insertIndex, 0, item);
  playlistStore.setPlaybackQueue(list, 0);
  playlistStore.enqueuePlayNext?.(item.id);
  playlistStore.syncQueuedNextTrackIds?.();
  return true;
};
