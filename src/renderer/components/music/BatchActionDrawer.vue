<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useVModel } from '@vueuse/core';
import Drawer from '@/components/ui/Drawer.vue';
import Dialog from '@/components/ui/Dialog.vue';
import { CheckboxIndicator, CheckboxRoot } from 'reka-ui';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import { useUserStore } from '@/stores/user';
import { formatDuration } from '@/utils/format';
import SongCard from '@/components/music/SongCard.vue';
import { RecycleScroller } from 'vue-virtual-scroller';
import 'vue-virtual-scroller/dist/vue-virtual-scroller.css';

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

const isAllSelected = computed(
  () => props.songs.length > 0 && selectedKeys.value.size === props.songs.length,
);

const isIndeterminate = computed(() => selectedKeys.value.size > 0 && !isAllSelected.value);

type CheckboxState = boolean | 'indeterminate';

const selectAllState = computed<CheckboxState>(() => {
  if (isAllSelected.value) return true;
  if (isIndeterminate.value) return 'indeterminate';
  return false;
});

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

const setSongChecked = (song: Song, value: CheckboxState) => {
  const key = String(song.id);
  const next = new Set(selectedKeys.value);
  if (value === true) {
    next.add(key);
  } else {
    next.delete(key);
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

const itemHeight = 56;

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
    side="right"
    overlayClass="batch-drawer-overlay"
    panelClass="batch-drawer"
  >
    <div class="batch-header">
      <div class="batch-title">批量操作</div>
      <div class="batch-actions">
        <button
          type="button"
          class="batch-action"
          :disabled="!canPlaySelected"
          @click="handlePlaySelected"
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5v14l11-7z" />
          </svg>
          播放
        </button>
        <button
          type="button"
          class="batch-action"
          :disabled="!canAddSelected"
          @click="handleAddToPlaylist"
        >
          <svg
            width="16"
            height="16"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
          >
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
          <svg
            width="16"
            height="16"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
          >
            <path d="M3 6h18" />
            <path d="M8 6v14" />
            <path d="M16 6v14" />
            <path d="M9 6l1-2h4l1 2" />
          </svg>
          删除
        </button>
      </div>
      <button type="button" class="batch-close" @click="open = false">
        <svg
          width="16"
          height="16"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2.4"
        >
          <path d="M18 6 6 18" />
          <path d="M6 6l12 12" />
        </svg>
      </button>
    </div>

    <div class="batch-selection">
      <button type="button" class="batch-select" @click="toggleSelectAll">
        <CheckboxRoot
          class="batch-checkbox"
          :model-value="selectAllState"
          @update:model-value="toggleSelectAll"
          @click.stop
        >
          <CheckboxIndicator as-child>
            <span class="batch-checkbox-indicator"></span>
          </CheckboxIndicator>
        </CheckboxRoot>
        全选
      </button>
      <div class="batch-count">已选 {{ selectedKeys.size }} / {{ songs.length }}</div>
    </div>

    <RecycleScroller class="batch-list" :items="props.songs" :item-size="itemHeight" key-field="id">
      <template #default="{ item: song }">
        <div
          class="batch-row"
          :class="{ 'text-primary': selectedKeys.has(String(song.id)) }"
          :style="{ height: `${itemHeight}px` }"
          @click="toggleSong(song)"
        >
          <div class="batch-leading" @click.stop>
            <CheckboxRoot
              class="batch-checkbox"
              :model-value="selectedKeys.has(String(song.id))"
              @update:model-value="setSongChecked(song, $event)"
            >
              <CheckboxIndicator as-child>
                <span class="batch-checkbox-indicator"></span>
              </CheckboxIndicator>
            </CheckboxRoot>
          </div>
          <div class="batch-card" :style="{ opacity: isSongPlayable(song) ? 1 : 0.45 }">
            <SongCard
              :id="song.id"
              :hash="song.hash"
              :title="song.title"
              :artist="song.artist"
              :artists="song.artists"
              :album="song.album"
              :albumId="song.albumId"
              :coverUrl="song.coverUrl"
              :duration="song.duration"
              :audioUrl="song.audioUrl"
              :mixSongId="song.mixSongId"
              :privilege="song.privilege"
              :payType="song.payType"
              :oldCpy="song.oldCpy"
              :relateGoods="song.relateGoods"
              :queueContext="props.songs"
              :showCover="true"
              :showAlbum="false"
              :showDuration="false"
              :active="false"
              :showMore="false"
              :disableLinks="true"
              variant="list"
            />
          </div>
          <div class="batch-album">{{ song.album || '未知专辑' }}</div>
          <div class="batch-duration">{{ formatDuration(song.duration) }}</div>
        </div>
      </template>

      <template #empty v-if="props.songs?.length === 0">
        <div class="batch-empty">暂无歌曲</div>
      </template>
    </RecycleScroller>
  </Drawer>

  <Dialog
    v-model:open="showPlaylistDialog"
    title="添加到歌单"
    overlayClass="batch-playlist-overlay"
    contentClass="batch-playlist-dialog max-w-[420px]"
    showClose
  >
    <div class="batch-playlist-body">
      <div v-if="isPlaylistLoading" class="batch-playlist-status">加载歌单中...</div>
      <div v-else-if="playlistStore.userPlaylists.length === 0" class="batch-playlist-status">
        暂无可用歌单
      </div>
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
  box-shadow: none;
  width: min(600px, 96vw);
  top: 0;
  bottom: var(--drawer-bottom-offset, 96px);
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
  transition:
    transform 0.2s ease,
    background-color 0.2s ease,
    color 0.2s ease;
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
  padding: 0 20px 12px 30px;
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
  flex: 1;
  padding: 0 14px 16px 18px;
  overflow: auto;
}

:global(.batch-list::-webkit-scrollbar) {
  width: 10px;
}

:global(.batch-list::-webkit-scrollbar-track) {
  background: transparent;
}

:global(.batch-list::-webkit-scrollbar-thumb) {
  background: rgba(0, 0, 0, 0.18);
  border-radius: 10px;
}

:global(.dark .batch-list::-webkit-scrollbar-thumb) {
  background: rgba(255, 255, 255, 0.2);
}

:global(.batch-list) {
  scrollbar-width: thin;
  scrollbar-color: rgba(0, 0, 0, 0.18) transparent;
}

:global(.dark .batch-list) {
  scrollbar-color: rgba(255, 255, 255, 0.2) transparent;
}

.batch-empty {
  padding: 20px 0 28px;
  text-align: center;
  font-size: 13px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.batch-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0;
  border-radius: 8px;
  transition:
    background-color 0.2s ease,
    color 0.2s ease;
  cursor: default;
}

.batch-row:hover {
  background: rgba(0, 0, 0, 0.05);
}

.dark .batch-row:hover {
  background: rgba(255, 255, 255, 0.2);
}

.batch-leading {
  width: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.batch-checkbox {
  width: 14px;
  height: 14px;
  border-radius: 3px;
  border: 1px solid var(--color-border-light);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: transparent;
}

.batch-checkbox[data-state='checked'],
.batch-checkbox[data-state='indeterminate'] {
  border-color: var(--color-primary);
  background: var(--color-primary);
}

.batch-checkbox-indicator {
  width: 8px;
  height: 8px;
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
}

.batch-checkbox[data-state='checked'] .batch-checkbox-indicator::after {
  content: '';
  position: absolute;
  left: 50%;
  top: 50%;
  width: 4px;
  height: 7px;
  border: 2px solid #fff;
  border-top: none;
  border-left: none;
  transform: translate(-50%, -55%) rotate(45deg);
}

.batch-checkbox[data-state='indeterminate'] .batch-checkbox-indicator::after {
  content: '';
  width: 8px;
  height: 2px;
  border: none;
  background: #fff;
  border-radius: 999px;
  position: absolute;
  left: 50%;
  top: 50%;
  transform: translate(-50%, -50%);
}

.batch-card {
  min-width: 0;
  flex: 1;
}

.batch-card :deep(.song-actions) {
  display: none;
}

.batch-album {
  width: 180px;
  flex: 0 1 180px;
  min-width: 0;
  display: block;
  font-size: 13px;
  opacity: 0.6;
  color: var(--color-text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.batch-duration {
  width: 64px;
  flex-shrink: 0;
  font-size: 12px;
  opacity: 0.4;
  color: var(--color-text-secondary);
}

@media (max-width: 720px) {
  :global(.batch-drawer) {
    bottom: 10px;
    width: 94vw;
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

:global(.batch-playlist-overlay) {
  z-index: 1600 !important;
}

:global(.batch-playlist-dialog) {
  z-index: 1610 !important;
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
  transition:
    color 0.2s ease,
    border-color 0.2s ease;
}

.playlist-picker-item:hover {
  border-color: var(--color-primary);
  color: var(--color-primary);
}
</style>
