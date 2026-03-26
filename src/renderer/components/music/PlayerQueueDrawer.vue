<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import { useVModel } from '@vueuse/core';
import Drawer from '@/components/ui/Drawer.vue';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import { formatDuration } from '@/utils/format';
import SongCard from '@/components/music/SongCard.vue';
import { RecycleScroller, RecycleScrollerInstance } from 'vue-virtual-scroller';
import 'vue-virtual-scroller/dist/vue-virtual-scroller.css';

interface Props {
  open?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  open: false,
});

const emit = defineEmits<{
  (e: 'update:open', value: boolean): void;
}>();

const open = useVModel(props, 'open', emit, { defaultValue: false });
const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();

const queueTracks = computed(() => playlistStore.defaultList);
const currentTrackId = computed(() => playerStore.currentTrackId);

const itemHeight = 56;
const scrollerRef = ref<RecycleScrollerInstance | null>(null);

const scrollToTop = () => {
  scrollerRef.value?.scrollToPosition(0);
};

const scrollToCurrent = () => {
  const targetId = currentTrackId.value ? String(currentTrackId.value) : '';
  if (!targetId || !scrollerRef.value) return;
  const index = queueTracks.value.findIndex((song) => String(song.id) === targetId);
  if (index < 0) return;
  scrollerRef.value.scrollToItem(index);
};

watch(
  () => [open.value, currentTrackId.value, queueTracks.value.length],
  async ([isOpen]) => {
    if (!isOpen) return;
    await nextTick();
    scrollToCurrent();
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

const handlePlay = (song: Song) => {
  if (String(song.id) === String(playerStore.currentTrackId)) {
    playerStore.togglePlay();
    return;
  }
  playerStore.playTrack(String(song.id));
};

const handleRemove = (song: Song) => {
  const targetId = String(song.id);
  const list = playlistStore.defaultList;
  const index = list.findIndex((item) => String(item.id) === targetId);
  if (index === -1) return;
  const nextList = list.filter((item) => String(item.id) !== targetId);
  playlistStore.defaultList = nextList;

  if (String(playerStore.currentTrackId) !== targetId) return;

  if (nextList.length === 0) {
    playerStore.stop();
    return;
  }

  const nextIndex = Math.min(index, nextList.length - 1);
  playerStore.playTrack(String(nextList[nextIndex].id));
};

const handleClear = () => {
  if (playlistStore.defaultList.length === 0) return;
  playlistStore.defaultList = [];
  playerStore.stop();
};
</script>

<template>
  <Drawer
    v-model:open="open"
    side="right"
    overlayClass="queue-drawer-overlay"
    panelClass="queue-drawer"
  >
    <div class="queue-header">
      <div>
        <div class="queue-title">播放列表</div>
        <div class="queue-subtitle">共 {{ queueTracks.length }} 首歌曲</div>
      </div>
      <div class="queue-actions">
        <button type="button" class="queue-icon-btn" title="滚动到顶部" @click="scrollToTop">
          <svg
            width="16"
            height="16"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
          >
            <path d="M12 19V5" />
            <path d="M5 12l7-7 7 7" />
          </svg>
        </button>
        <button
          type="button"
          class="queue-icon-btn"
          title="滚动到当前播放"
          @click="scrollToCurrent"
        >
          <svg
            width="16"
            height="16"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
          >
            <circle cx="12" cy="12" r="3" />
            <path d="M3 12h3m12 0h3M12 3v3m0 12v3" />
          </svg>
        </button>
        <button type="button" class="queue-icon-btn" title="清空列表" @click="handleClear">
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
        </button>
        <button type="button" class="queue-icon-btn" title="关闭" @click="open = false">
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
    </div>

    <div class="queue-divider"></div>

    <RecycleScroller
      ref="scrollerRef"
      class="queue-list"
      :items="queueTracks"
      :item-size="itemHeight"
      key-field="id"
    >
      <template #default="{ item: track, index }">
        <div
          class="queue-row"
          :class="{
            'is-current': String(track.id) === String(currentTrackId),
            'bg-primary/5 dark:bg-primary/10 text-primary':
              String(track.id) === String(currentTrackId),
          }"
          :style="{ height: `${itemHeight}px` }"
        >
          <div class="queue-leading">
            <span class="queue-index">{{ index + 1 }}</span>
            <button type="button" class="queue-play" @click="handlePlay(track)">
              <svg
                v-if="String(track.id) !== String(currentTrackId) || !playerStore.isPlaying"
                width="14"
                height="14"
                viewBox="0 0 24 24"
                fill="currentColor"
              >
                <path d="M8 5v14l11-7z" />
              </svg>
              <svg v-else width="14" height="14" viewBox="0 0 24 24" fill="currentColor">
                <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z" />
              </svg>
            </button>
          </div>

          <div class="queue-card" :style="{ opacity: isSongPlayable(track) ? 1 : 0.45 }">
            <SongCard
              :id="track.id"
              :hash="track.hash"
              :title="track.title"
              :artist="track.artist"
              :artists="track.artists"
              :album="track.album"
              :albumId="track.albumId"
              :coverUrl="track.coverUrl"
              :duration="track.duration"
              :audioUrl="track.audioUrl"
              :mixSongId="track.mixSongId"
              :privilege="track.privilege"
              :payType="track.payType"
              :oldCpy="track.oldCpy"
              :relateGoods="track.relateGoods"
              :showCover="true"
              :showAlbum="false"
              :showDuration="false"
              :active="String(track.id) === String(currentTrackId)"
              :showMore="false"
              variant="list"
            />
          </div>

          <div
            class="queue-duration"
            :style="{ width: '64px', marginLeft: '6px', textAlign: 'right' }"
          >
            {{ formatDuration(track.duration) }}
          </div>

          <button
            v-if="String(track.id) !== String(currentTrackId)"
            type="button"
            class="queue-remove"
            @click="handleRemove(track)"
          >
            <svg
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2"
            >
              <path d="M18 6 6 18" />
              <path d="M6 6l12 12" />
            </svg>
          </button>
        </div>
      </template>

      <template #empty v-if="queueTracks?.length === 0">
        <div class="queue-empty">
          <div class="queue-empty-icon">
            <svg
              width="40"
              height="40"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="1.6"
            >
              <path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01" />
            </svg>
          </div>
          <div>列表为空，快去发现好音乐吧</div>
        </div>
      </template>
    </RecycleScroller>
  </Drawer>
</template>

<style scoped>
@reference "@/style.css";

:global(.queue-drawer-overlay) {
  background: rgba(0, 0, 0, 0.16);
}

:global(.queue-drawer) {
  padding: 0;
  box-shadow: none;
}

.queue-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 18px 16px 12px 20px;
}

.queue-title {
  font-size: 16px;
  font-weight: 700;
  color: var(--color-text-main);
}

.queue-subtitle {
  margin-top: 4px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.queue-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.queue-icon-btn {
  width: 28px;
  height: 28px;
  border-radius: 9px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--color-text-secondary);
  transition: all 0.2s ease;
}

.queue-icon-btn:hover {
  color: var(--color-primary);
  transform: scale(1.06);
}

.queue-divider {
  height: 1px;
  margin: 0 18px;
  background: var(--color-border-light);
}

.queue-empty {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  color: var(--color-text-secondary);
  font-size: 13px;
  font-weight: 600;
}

.queue-empty-icon {
  opacity: 0.35;
}

.queue-list {
  flex: 1;
  padding: 10px 12px 14px 14px;
  overflow: auto;
  position: relative;
}

.queue-row {
  display: flex;
  align-items: center;
  gap: 10px;
  align-items: center;
  padding: 0;
  border-radius: 8px;
  transition:
    background-color 0.2s ease,
    color 0.2s ease;
  cursor: default;
}

.queue-row:hover {
  background: rgba(0, 0, 0, 0.05);
}

.dark .queue-row:hover {
  background: rgba(255, 255, 255, 0.2);
}

.queue-leading {
  position: relative;
  width: 40px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.queue-index {
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-secondary);
  transition: opacity 0.2s ease;
}

.queue-play {
  position: absolute;
  inset: 0;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  color: var(--color-text-main);
  opacity: 0;
  transition:
    opacity 0.2s ease,
    transform 0.2s ease;
}

.queue-row:hover .queue-play,
.queue-row.is-current .queue-play {
  opacity: 1;
}

.queue-row:hover .queue-index,
.queue-row.is-current .queue-index {
  opacity: 0;
}

.queue-play:hover {
  transform: scale(1.1);
  color: var(--color-primary);
}

.queue-card {
  flex: 1;
  min-width: 0;
}

.queue-card :deep(.song-actions) {
  display: none;
}

.queue-duration {
  width: 64px;
  flex-shrink: 0;
  font-size: 12px;
  opacity: 0.4;
  margin-left: 8px;
  text-align: right;
  color: var(--color-text-secondary);
}

.queue-remove {
  width: 24px;
  height: 24px;
  border-radius: 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--color-text-secondary);
  opacity: 0;
  transition: all 0.2s ease;
  position: relative;
  left: -8px;
}

.queue-row:hover .queue-remove {
  opacity: 1;
}

.queue-remove:hover {
  color: #ef4444;
  transform: scale(1.05);
}

@media (max-width: 720px) {
  .queue-row {
    gap: 10px;
  }
}
</style>
