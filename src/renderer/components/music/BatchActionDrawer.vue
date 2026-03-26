<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useVModel } from '@vueuse/core';
import Drawer from '@/components/ui/Drawer.vue';
import Dialog from '@/components/ui/Dialog.vue';
import Cover from '@/components/ui/Cover.vue';
import Tag from '@/components/ui/Tag.vue';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import { useUserStore } from '@/stores/user';
import { formatDuration } from '@/utils/format';

interface Props {
  open?: boolean;
  songs: Song[];
  sourceId?: string | number;
}

const props = withDefaults(defineProps<Props>(), {
  open: false,
  sourceId: '',
});

const emit = defineEmits<{
  (e: 'update:open', value: boolean): void;
}>();

const open = useVModel(props, 'open', emit, { defaultValue: false });
const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();
const userStore = useUserStore();

const selectedKeys = ref<Set<string>>(new Set());
const showPlaylistDialog = ref(false);
const isPlaylistLoading = ref(false);

const selectedSongs = computed(() =>
  props.songs.filter((song) => selectedKeys.value.has(String(song.id))),
);

const isAllSelected = computed(() =>
  props.songs.length > 0 && selectedKeys.value.size === props.songs.length,
);

const isIndeterminate = computed(() =>
  selectedKeys.value.size > 0 && !isAllSelected.value,
);

const toggleSelectAll = () => {
  if (isAllSelected.value) {
    selectedKeys.value = new Set();
    return;
  }
  selectedKeys.value = new Set(props.songs.map((song) => String(song.id)));
};

const toggleSong = (song: Song) => {
  const key = String(song.id);
  const next = new Set(selectedKeys.value);
  if (next.has(key)) {
    next.delete(key);
  } else {
    next.add(key);
  }
  selectedKeys.value = next;
};

const clearSelection = () => {
  selectedKeys.value = new Set();
};

watch(
  () => open.value,
  (value) => {
    if (!value) {
      clearSelection();
    }
  },
);

watch(
  () => props.songs,
  () => {
    clearSelection();
  },
);

const isSongPlayable = (song: Song) => {
  const isUnavailable = song.privilege === 40;
  const isPaid = song.privilege === 10 && song.payType === 2;
  const isNoCopyright = song.privilege === 5;

  if (isUnavailable || isPaid) return false;
  if (isNoCopyright) return song.oldCpy === 1;
  return Boolean(song.hash?.trim());
};

const getQualityTag = (song: Song) => {
  const goods = song.relateGoods ?? [];
  const hasQuality = (quality: string, level: number) =>
    goods.some((item) => item.quality === quality || item.level === level);

  if (hasQuality('high', 6)) return 'Hi-Res';
  if (hasQuality('flac', 5)) return 'SQ';
  if (hasQuality('320', 4)) return 'HQ';
  return '';
};

const getPrivilegeTags = (song: Song) => {
  const tags: { label: string; color: string }[] = [];
  const isVip = song.privilege === 10 && song.payType === 3;
  const isPaid = song.privilege === 10 && song.payType === 2;
  const isNoCopyright = song.privilege === 5;
  const isUnavailable = song.privilege === 40;

  if (isPaid) tags.push({ label: '付费', color: '#EF4444' });
  if (isVip) tags.push({ label: 'VIP', color: '#F59E0B' });
  if (!isSongPlayable(song) && isNoCopyright) {
    tags.push({ label: '版权', color: '#8B5CF6' });
  }
  if (isUnavailable) tags.push({ label: '音源', color: '#6B7280' });

  return tags;
};

const canPlaySelected = computed(() => selectedSongs.value.some((song) => isSongPlayable(song)));
const canAddSelected = computed(() => userStore.isLoggedIn && selectedSongs.value.length > 0);
const canRemoveSelected = computed(
  () => Boolean(props.sourceId) && userStore.isLoggedIn && selectedSongs.value.length > 0,
);

const handlePlaySelected = () => {
  if (!canPlaySelected.value) return;
  const playable = selectedSongs.value.find((song) => isSongPlayable(song));
  if (!playable) return;

  const nextList = selectedSongs.value.slice();
  playlistStore.defaultList = nextList;
  playerStore.playTrack(String(playable.id));
  open.value = false;
};

const handleAddToPlaylist = async () => {
  if (!canAddSelected.value) return;
  showPlaylistDialog.value = true;
  if (playlistStore.userPlaylists.length === 0) {
    isPlaylistLoading.value = true;
    await playlistStore.fetchUserPlaylists();
    isPlaylistLoading.value = false;
  }
};

const handleSelectPlaylist = async (listId: string | number) => {
  for (const song of selectedSongs.value) {
    await playlistStore.addToPlaylist(String(listId), song);
  }
  showPlaylistDialog.value = false;
  open.value = false;
};

const handleRemoveFromPlaylist = async () => {
  if (!canRemoveSelected.value) return;
  for (const song of selectedSongs.value) {
    await playlistStore.removeFromPlaylist(String(props.sourceId), song);
  }
  open.value = false;
};
</script>

<template>
  <Drawer
    v-model:open="open"
    side="bottom"
    overlayClass="batch-drawer-overlay"
    panelClass="batch-drawer"
  >
    <div class="batch-header">
      <div class="batch-title">批量操作</div>
      <div class="batch-actions">
        <button type="button" class="batch-action" :disabled="!canPlaySelected" @click="handlePlaySelected">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5v14l11-7z" />
          </svg>
          播放
        </button>
        <button type="button" class="batch-action" :disabled="!canAddSelected" @click="handleAddToPlaylist">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M12 5v14" />
            <path d="M5 12h14" />
          </svg>
          添加到
        </button>
        <button
          type="button"
          class="batch-action danger"
          :disabled="!canRemoveSelected"
          @click="handleRemoveFromPlaylist"
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M3 6h18" />
            <path d="M8 6v14" />
            <path d="M16 6v14" />
            <path d="M9 6l1-2h4l1 2" />
          </svg>
          删除
        </button>
      </div>
      <button type="button" class="batch-close" @click="open = false">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4">
          <path d="M18 6 6 18" />
          <path d="M6 6l12 12" />
        </svg>
      </button>
    </div>

    <div class="batch-selection">
      <button type="button" class="batch-select" @click="toggleSelectAll">
        <span class="batch-checkbox" :class="{ checked: isAllSelected, indeterminate: isIndeterminate }"></span>
        全选
      </button>
      <div class="batch-count">已选 {{ selectedKeys.size }} / {{ songs.length }}</div>
    </div>

    <div class="batch-list">
      <div
        v-for="song in songs"
        :key="song.id"
        class="batch-row"
        :class="{ selected: selectedKeys.has(String(song.id)) }"
        @click="toggleSong(song)"
      >
        <span class="batch-checkbox" :class="{ checked: selectedKeys.has(String(song.id)) }"></span>
        <div class="batch-cover" :class="{ disabled: !isSongPlayable(song) }">
          <Cover :url="song.coverUrl" :size="120" :width="40" :height="40" :borderRadius="8" />
        </div>
        <div class="batch-info">
          <div class="batch-name-row">
            <span class="batch-name">{{ song.title }}</span>
            <Tag
              v-for="tag in getPrivilegeTags(song)"
              :key="tag.label"
              class="batch-tag"
              :color="tag.color"
            >
              {{ tag.label }}
            </Tag>
            <Tag v-if="getQualityTag(song)" class="batch-tag" color="#06B6D4">
              {{ getQualityTag(song) }}
            </Tag>
          </div>
          <div class="batch-artist">{{ song.artist }}</div>
        </div>
        <div class="batch-duration">{{ formatDuration(song.duration) }}</div>
      </div>
    </div>
  </Drawer>

  <Dialog
    v-model:open="showPlaylistDialog"
    title="添加到歌单"
    contentClass="batch-playlist-dialog"
    showClose
  >
    <div class="batch-playlist-body">
      <div v-if="isPlaylistLoading" class="batch-playlist-status">加载歌单中...</div>
      <div v-else-if="playlistStore.userPlaylists.length === 0" class="batch-playlist-status">暂无可用歌单</div>
      <button
        v-for="entry in playlistStore.userPlaylists"
        :key="entry.listid ?? entry.id"
        type="button"
        class="playlist-picker-item"
        @click="handleSelectPlaylist(entry.listid ?? entry.id)"
      >
        <span class="batch-playlist-name">{{ entry.name }}</span>
        <span class="batch-playlist-count">{{ entry.count ?? 0 }} 首</span>
      </button>
    </div>
  </Dialog>
</template>

<style scoped>
@reference "@/style.css";

:global(.batch-drawer-overlay) {
  background: rgba(0, 0, 0, 0.22);
}

:global(.batch-drawer) {
  padding: 0;
  max-height: calc(100vh - 120px);
}

.batch-header {
  display: grid;
  grid-template-columns: 1fr auto auto;
  align-items: center;
  gap: 12px;
  padding: 16px 18px 12px 20px;
}

.batch-title {
  font-size: 16px;
  font-weight: 700;
  color: var(--color-text-main);
}

.batch-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.batch-action {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 7px 12px;
  border-radius: 10px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-main);
  background: rgba(0, 0, 0, 0.04);
  transition: transform 0.2s ease, background-color 0.2s ease, color 0.2s ease;
}

.batch-action:hover {
  transform: scale(1.02);
  color: var(--color-primary);
  background: rgba(0, 0, 0, 0.08);
}

.dark .batch-action {
  background: rgba(255, 255, 255, 0.08);
}

.dark .batch-action:hover {
  background: rgba(255, 255, 255, 0.12);
}

.batch-action.danger {
  color: #ef4444;
}

.batch-action:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  transform: none;
}

.batch-close {
  width: 30px;
  height: 30px;
  border-radius: 10px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--color-text-secondary);
  transition: all 0.2s ease;
}

.batch-close:hover {
  color: var(--color-text-main);
  transform: scale(1.06);
}

.batch-selection {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0 20px 12px;
  font-size: 12px;
  color: var(--color-text-secondary);
}

.batch-select {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
  color: var(--color-text-main);
}

.batch-count {
  font-weight: 600;
}

.batch-list {
  padding: 0 14px 16px 18px;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.batch-row {
  display: grid;
  grid-template-columns: 20px 44px minmax(0, 1fr) 60px;
  align-items: center;
  gap: 12px;
  padding: 8px 10px;
  border-radius: 12px;
  background: rgba(0, 0, 0, 0.02);
  transition: background-color 0.2s ease, color 0.2s ease;
}

.dark .batch-row {
  background: rgba(255, 255, 255, 0.04);
}

.batch-row:hover {
  background: rgba(0, 0, 0, 0.06);
}

.dark .batch-row:hover {
  background: rgba(255, 255, 255, 0.08);
}

.batch-row.selected {
  border: 1px solid color-mix(in srgb, var(--color-primary) 30%, transparent);
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
}

.batch-checkbox {
  width: 14px;
  height: 14px;
  border-radius: 4px;
  border: 1.5px solid var(--color-border-light);
  position: relative;
  display: inline-block;
}

.batch-checkbox.checked {
  border-color: var(--color-primary);
  background: var(--color-primary);
}

.batch-checkbox.checked::after {
  content: '';
  position: absolute;
  left: 3px;
  top: 1px;
  width: 5px;
  height: 8px;
  border: 2px solid #fff;
  border-top: none;
  border-left: none;
  transform: rotate(45deg);
}

.batch-checkbox.indeterminate {
  border-color: var(--color-primary);
  background: var(--color-primary);
}

.batch-checkbox.indeterminate::after {
  content: '';
  position: absolute;
  left: 3px;
  top: 6px;
  width: 7px;
  height: 2px;
  background: #fff;
  border-radius: 999px;
}

.batch-cover {
  border-radius: 8px;
  overflow: hidden;
  position: relative;
}

.batch-cover.disabled::after {
  content: '';
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.35);
}

.batch-info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.batch-name-row {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.batch-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--color-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.batch-artist {
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.batch-duration {
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-secondary);
  text-align: right;
}

.batch-tag {
  flex-shrink: 0;
}

.batch-list::-webkit-scrollbar {
  width: 4px;
}

.batch-list::-webkit-scrollbar-thumb {
  background: var(--color-border-light);
  border-radius: 999px;
}

@media (max-width: 720px) {
  :global(.batch-drawer) {
    bottom: 10px;
    width: 94vw;
  }

  .batch-row {
    grid-template-columns: 20px 40px minmax(0, 1fr) 50px;
  }
}

.batch-playlist-body {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.batch-playlist-status {
  padding: 18px 0;
  text-align: center;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.batch-playlist-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--color-text-main);
}

.batch-playlist-count {
  font-size: 11px;
  color: var(--color-text-secondary);
}

:deep(.batch-playlist-dialog) {
  max-width: 420px;
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
