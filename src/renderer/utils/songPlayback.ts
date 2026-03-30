import type { Song } from '@/models/song';

export interface PlaybackQueueStoreLike {
  setPlaybackQueue: (songs: Song[], filteredInvalidCount?: number) => void;
}

export interface PlaybackPlayerLike {
  playTrack: (id: string, playlist?: Song[]) => Promise<void> | void;
}

export interface ResolvedPlayableQueue {
  queue: Song[];
  firstPlayable: Song | null;
  filteredInvalidCount: number;
  sourceCount: number;
}

export const canPlaySong = (song: Song): boolean => {
  if (song.privilege === 40) return false;
  if (song.privilege === 10 && song.payType === 2) return false;
  if (song.privilege === 5) return song.oldCpy === 1;
  return true;
};

export const isPlayableSong = (song: Song): boolean => Boolean(song.hash?.trim()) && canPlaySong(song);

export const resolvePlayableQueue = (
  songs: Song[],
  filteredInvalidCount = 0,
): ResolvedPlayableQueue => {
  const queue: Song[] = [];
  let hiddenCount = filteredInvalidCount;

  for (const song of songs) {
    if (!isPlayableSong(song)) {
      hiddenCount += 1;
      continue;
    }
    queue.push(song);
  }

  return {
    queue,
    firstPlayable: queue[0] ?? null,
    filteredInvalidCount: hiddenCount,
    sourceCount: songs.length + filteredInvalidCount,
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
