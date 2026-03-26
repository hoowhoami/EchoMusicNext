<script setup lang="ts">
import { computed, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import Cover from '@/components/ui/Cover.vue';
import Tag from '@/components/ui/Tag.vue';
import {
  ContextMenuRoot,
  ContextMenuTrigger,
  ContextMenuPortal,
  ContextMenuContent,
  ContextMenuItem,
} from 'reka-ui';
import { formatDuration } from '@/utils/format';
import type { SongArtist, SongRelateGood } from '@/stores/playlist';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import Dialog from '@/components/ui/Dialog.vue';

interface Props {
  id: string | number;
  title: string;
  artist: string;
  hash?: string;
  coverUrl: string;
  audioUrl?: string;
  album?: string;
  duration?: number;
  class?: string;
  showAlbum?: boolean;
  showDuration?: boolean;
  showMore?: boolean;
  showCover?: boolean;
  active?: boolean;
  variant?: 'card' | 'list';
  privilege?: number;
  payType?: number;
  oldCpy?: number;
  relateGoods?: SongRelateGood[];
  artists?: SongArtist[];
  albumId?: string | number;
  mixSongId?: string | number;
  parentPlaylistId?: string | number;
  enableRemoveFromPlaylist?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  hash: '',
  audioUrl: '',
  mixSongId: '',
  parentPlaylistId: '',
  enableRemoveFromPlaylist: false,
  showAlbum: true,
  showDuration: true,
  showMore: true,
  showCover: true,
  active: false,
  variant: 'card',
});

const router = useRouter();
const route = useRoute();
const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();
const showPlaylistDialog = ref(false);
const isPlaylistLoading = ref(false);


const baseClass = computed(() =>
  props.variant === 'list'
    ? 'song-card group flex items-center gap-3 p-0 pr-8 rounded-none transition-all duration-300 bg-transparent hover:bg-transparent cursor-default'
    : 'song-card group flex items-center gap-3 p-2 rounded-xl transition-all duration-300 hover:bg-black/5 dark:hover:bg-white/5 cursor-pointer'
);

const isVip = computed(() => props.privilege === 10 && props.payType === 3);
const isPaid = computed(() => props.privilege === 10 && props.payType === 2);
const isNoCopyright = computed(() => props.privilege === 5);
const isUnavailable = computed(() => props.privilege === 40);

const canPlay = computed(() => {
  if (isUnavailable.value) return false;
  if (isPaid.value) return false;
  if (isNoCopyright.value) return props.oldCpy === 1;
  return true;
});

const isPlayable = computed(() => props.hash.trim() !== '' && canPlay.value);
const contentOpacity = computed(() => {
  if (props.variant === 'list') return 1;
  return isPlayable.value ? 1 : 0.45;
});

const qualityTag = computed(() => {
  const goods = props.relateGoods ?? [];
  const hasQuality = (quality: string, level: number) =>
    goods.some((item) => item.quality === quality || item.level === level);

  if (hasQuality('high', 6)) return 'Hi-Res';
  if (hasQuality('flac', 5)) return 'SQ';
  if (hasQuality('320', 4)) return 'HQ';
  return '';
});

const privilegeTags = computed(() => {
  const tags: { label: string; color: string }[] = [];

  if (isPaid.value) tags.push({ label: '付费', color: '#EF4444' });
  if (isVip.value) tags.push({ label: 'VIP', color: '#F59E0B' });
  if (!isPlayable.value && isNoCopyright.value) {
    tags.push({ label: '版权', color: '#8B5CF6' });
  }
  if (isUnavailable.value) tags.push({ label: '音源', color: '#6B7280' });

  return tags;
});

const isFavorite = computed(() =>
  playlistStore.favorites.some((item) => String(item.id) === String(props.id)),
);


const artistList = computed(() => {
  if (props.artists && props.artists.length > 0) return props.artists;
  if (!props.artist) return [] as SongArtist[];
  return props.artist
    .split(/[,/，]/)
    .map((name) => name.trim())
    .filter((name) => name.length > 0)
    .map((name) => ({ name }));
});

const resolveRouteId = (value: unknown) => (Array.isArray(value) ? value[0] : value);

const resolveNumericId = (value?: string | number) => {
  if (value === undefined || value === null) return null;
  const parsed = Number.parseInt(String(value), 10);
  if (Number.isNaN(parsed) || parsed <= 0) return null;
  return parsed;
};

const isSameRoute = (name: string, id: string | number) => {
  const routeId = resolveRouteId(route.params.id);
  return route.name === name && String(routeId) === String(id);
};

const albumDetailId = computed(() => resolveNumericId(props.albumId));
const hasAlbumDetail = computed(() => {
  if (!albumDetailId.value) return false;
  return Boolean((props.album ?? '').trim());
});

const isArtistClickable = (artist: SongArtist) => {
  const artistId = resolveNumericId(artist.id);
  if (!artistId) return false;
  return !isSameRoute('artist-detail', artistId);
};

const isAlbumClickable = computed(() => {
  const albumId = albumDetailId.value;
  if (!albumId || !hasAlbumDetail.value) return false;
  return !isSameRoute('album-detail', albumId);
});

const goToArtist = (artist: SongArtist) => {
  const artistId = resolveNumericId(artist.id);
  if (!artistId || isSameRoute('artist-detail', artistId)) return;
  router.push({
    name: 'artist-detail',
    params: { id: String(artistId) },
  });
};

const goToAlbum = () => {
  const albumId = albumDetailId.value;
  if (!albumId || !hasAlbumDetail.value || isSameRoute('album-detail', albumId)) return;
  router.push({
    name: 'album-detail',
    params: { id: String(albumId) },
  });
};

const goToSongDetail = () => {
  router.push({ name: 'playing' });
};

const goToComments = () => {
  const commentId = resolveNumericId(props.mixSongId) ?? resolveNumericId(props.id) ?? String(props.id);
  router.push({
    name: 'comment',
    params: { id: String(commentId) },
    query: { type: 'music' },
  });
};

const buildSongPayload = (): Song => ({
  id: String(props.id),
  title: props.title,
  artist: props.artist,
  artists: props.artists,
  album: props.album,
  albumId: props.albumId,
  duration: props.duration ?? 0,
  coverUrl: props.coverUrl,
  audioUrl: props.audioUrl,
  hash: props.hash,
  mixSongId: props.mixSongId ?? props.id,
  privilege: props.privilege,
  payType: props.payType,
  oldCpy: props.oldCpy,
  relateGoods: props.relateGoods,
});

const handlePlayNow = () => {
  if (!isPlayable.value) return;
  const payload = buildSongPayload();
  const list = playlistStore.defaultList;
  const exists = list.find((item) => String(item.id) === String(payload.id));
  if (!exists) {
    playlistStore.defaultList = [payload, ...list];
  }
  void playerStore.playTrack(String(payload.id));
  router.push({ name: 'playing' });
};

const handlePlayNext = () => {
  if (!isPlayable.value) return;
  const payload = buildSongPayload();
  const list = playlistStore.defaultList.slice();
  const existingIndex = list.findIndex((item) => String(item.id) === String(payload.id));
  const currentIndex = list.findIndex((item) => String(item.id) === String(playerStore.currentTrackId ?? ''));

  const item = existingIndex >= 0 ? list.splice(existingIndex, 1)[0] : payload;
  const insertIndex = currentIndex >= 0 ? currentIndex + 1 : list.length;
  list.splice(insertIndex, 0, item);
  playlistStore.defaultList = list;
};

const handleAddToPlaylist = async () => {
  showPlaylistDialog.value = true;
  if (playlistStore.userPlaylists.length === 0) {
    isPlaylistLoading.value = true;
    await playlistStore.fetchUserPlaylists();
    isPlaylistLoading.value = false;
  }
};

const handleSelectPlaylist = async (listId: string | number) => {
  await playlistStore.addToPlaylist(String(listId), buildSongPayload());
  showPlaylistDialog.value = false;
};

const handleRemoveFromPlaylist = () => {
  if (!props.parentPlaylistId) return;
  void playlistStore.removeFromPlaylist(String(props.parentPlaylistId), buildSongPayload());
};

const handleFavorite = () => {
  if (playlistStore.favorites.find((item) => item.id === props.id)) {
    playlistStore.removeFromFavorites(String(props.id));
    return;
  }

  playlistStore.addToFavorites(buildSongPayload());
};


</script>

<template>
  <ContextMenuRoot>
    <ContextMenuTrigger as-child>
      <div
        :class="[baseClass, props.class]"
      >
    <!-- 封面 -->
    <div
      v-if="showCover"
      class="relative w-10 h-10 shrink-0 rounded-md shadow-sm"
      :style="{ opacity: contentOpacity }"
    >
      <Cover :url="coverUrl" :size="160" :borderRadius="4" class="w-full h-full" />
      
    </div>
    
    <!-- 信息 -->
    <div class="flex-1 min-w-0 flex flex-col gap-0.5" :style="{ opacity: contentOpacity }">
      <div class="flex items-center min-w-0">
        <h3
          class="text-[13px] font-medium line-clamp-1"
          :class="props.active ? 'text-primary' : 'text-text-main'"
        >
          {{ title }}
        </h3>
        <Tag
          v-for="tag in privilegeTags"
          :key="tag.label"
          class="song-tag"
          :color="tag.color"
        >
          {{ tag.label }}
        </Tag>
      </div>
      <div
        class="text-[11px] line-clamp-1 opacity-80 flex items-center gap-1 min-w-0"
        :class="props.active ? 'text-primary/70' : 'text-text-secondary'"
      >
        <span
          v-for="(artistItem, index) in artistList"
          :key="`${artistItem.name}-${index}`"
          :class="isArtistClickable(artistItem) ? 'song-artist song-link' : 'song-artist'"
          @click.stop="isArtistClickable(artistItem) && goToArtist(artistItem)"
        >
          {{ artistItem.name }}
          <span v-if="index < artistList.length - 1" class="mx-1 opacity-50">/</span>
        </span>
        <Tag
          v-if="qualityTag"
          class="song-tag-inline"
          color="#06B6D4"
        >
          {{ qualityTag }}
        </Tag>
        <button
          v-if="showAlbum && album"
          type="button"
          :class="isAlbumClickable ? 'song-album song-link opacity-60' : 'song-album opacity-60'"
          @click.stop="isAlbumClickable && goToAlbum()"
        >
          • {{ album }}
        </button>
      </div>
    </div>
    
    <!-- 详情及评论 / 收藏 -->
    <div class="song-actions ml-1" :class="showMore ? '' : 'song-actions-static'" @click.stop>
      <button
        type="button"
        class="song-action"
        title="详情及评论"
        @click.stop="goToSongDetail"
      >
        <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 15a4 4 0 0 1-4 4H7l-4 4V7a4 4 0 0 1 4-4h10a4 4 0 0 1 4 4z" />
        </svg>
      </button>
      <button
        type="button"
        class="song-action"
        title="收藏"
        @click.stop="handleFavorite"
      >
        <svg v-if="isFavorite" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="currentColor" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.6l-1-1a5.5 5.5 0 0 0-7.8 7.8l1 1L12 21l7.8-7.6 1-1a5.5 5.5 0 0 0 0-7.8z" />
        </svg>
        <svg v-else xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.6l-1-1a5.5 5.5 0 0 0-7.8 7.8l1 1L12 21l7.8-7.6 1-1a5.5 5.5 0 0 0 0-7.8z" />
        </svg>
      </button>
    </div>

    <!-- 时长 -->
    <div
      v-if="showDuration && duration"
      class="text-[11px] text-text-secondary opacity-40 px-2 group-hover:opacity-80 transition-opacity"
    >
      {{ formatDuration(duration) }}
    </div>
    
      </div>
    </ContextMenuTrigger>
    <ContextMenuPortal>
      <ContextMenuContent
        class="song-context-menu"
        side="bottom"
        :side-offset="4"
        :side-flip="true"
        :align-flip="true"
        :collision-padding="{ top: 8, right: 8, bottom: 96, left: 8 }"
        align="start"
      >
        <ContextMenuItem class="song-context-item" @select="handlePlayNow">
          立即播放
        </ContextMenuItem>
        <ContextMenuItem class="song-context-item" @select="handlePlayNext">
          添加下一首播放
        </ContextMenuItem>
        <ContextMenuItem class="song-context-item" @select="handleAddToPlaylist">
          添加到歌单
        </ContextMenuItem>
        <div v-if="props.enableRemoveFromPlaylist" class="song-context-separator"></div>
        <ContextMenuItem
          v-if="props.enableRemoveFromPlaylist"
          class="song-context-item text-red-500"
          @select="handleRemoveFromPlaylist"
        >
          从歌单删除
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenuPortal>
  </ContextMenuRoot>

  <Dialog
    v-model:open="showPlaylistDialog"
    title="添加到歌单"
    contentClass="max-w-[420px]"
    showClose
  >
    <div class="flex flex-col gap-3">
      <div v-if="isPlaylistLoading" class="py-6 text-center text-text-secondary text-[12px]">
        加载歌单中...
      </div>
      <div v-else-if="playlistStore.userPlaylists.length === 0" class="py-6 text-center text-text-secondary text-[12px]">
        暂无可用歌单
      </div>
      <button
        v-for="entry in playlistStore.userPlaylists"
        :key="entry.listid ?? entry.id"
        type="button"
        class="playlist-picker-item"
        @click="handleSelectPlaylist(entry.listid ?? entry.id)"
      >
        <span class="text-[13px] font-semibold text-text-main truncate">{{ entry.name }}</span>
        <span class="text-[11px] text-text-secondary/60">{{ entry.count ?? 0 }} 首</span>
      </button>
    </div>
  </Dialog>
</template>

<style scoped>
@reference "@/style.css";

.song-card {
  user-select: none;
}

.song-tag {
  margin-left: 8px;
  flex-shrink: 0;
}

.song-tag-inline {
  margin-left: 6px;
  vertical-align: middle;
  flex-shrink: 0;
}


.song-artist {
  white-space: nowrap;
  display: inline-flex;
  align-items: center;
  min-width: 0;
}

.song-album {
  white-space: nowrap;
}

.song-link {
  cursor: pointer;
}

.song-link:hover {
  color: var(--primary);
}

.song-actions {
  display: flex;
  align-items: center;
  gap: 6px;
  opacity: 0;
  transition: opacity 0.2s ease;
}

.song-card:hover .song-actions,
.song-actions-static {
  opacity: 1;
}

.song-action {
  width: 28px;
  height: 28px;
  border-radius: 999px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-secondary);
  transition: all 0.2s ease;
}

.song-action:hover {
  color: var(--primary);
  transform: scale(1.12);
}

:deep(.song-context-menu) {
  min-width: 172px;
  padding: 6px;
  border-radius: 12px;
  background: var(--color-bg-card);
  border: 1px solid var(--color-border-light);
  box-shadow: 0 12px 28px rgba(0, 0, 0, 0.12);
  display: flex;
  flex-direction: column;
  gap: 4px;
  z-index: 1200;
}

:deep(.song-context-item) {
  width: 100%;
  text-align: left;
  padding: 6px 10px;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  user-select: none;
  color: var(--color-text-main);
  transition: all 0.2s ease;
}

:deep(.song-context-item:hover) {
  background-color: rgba(0, 0, 0, 0.05);
  color: var(--color-primary);
}

:deep(.dark .song-context-item:hover) {
  background-color: rgba(255, 255, 255, 0.08);
}

:deep(.song-context-separator) {
  height: 1px;
  margin: 4px 6px;
  background-color: var(--color-border-light);
}

.playlist-picker-item {
  width: 100%;
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid var(--color-border-light);
  background: var(--color-bg-card);
  text-align: left;
  display: flex;
  align-items: center;
  justify-content: space-between;
  color: var(--color-text-main);
  transition: color 0.2s ease, border-color 0.2s ease;
}

.playlist-picker-item:hover {
  border-color: var(--color-primary);
  color: var(--color-primary);
}
</style>
