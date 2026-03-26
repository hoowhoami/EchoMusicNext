<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import { useVModel } from '@vueuse/core';
import Drawer from '@/components/ui/Drawer.vue';
import Cover from '@/components/ui/Cover.vue';
import Tag from '@/components/ui/Tag.vue';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import { formatDuration } from '@/utils/format';

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

const listRef = ref<HTMLElement | null>(null);
const itemRefs = new Map<string, HTMLElement>();

const setItemRef = (id: string | number) => (el: Element | null) => {
  const key = String(id);
  if (el) {
    itemRefs.set(key, el as HTMLElement);
    return;
  }
  itemRefs.delete(key);
};

const scrollToTop = () => {
  listRef.value?.scrollTo({ top: 0, behavior: 'smooth' });
};

const scrollToCurrent = () => {
  const key = currentTrackId.value ? String(currentTrackId.value) : '';
  if (!key) return;
  const target = itemRefs.get(key);
  if (target) {
    target.scrollIntoView({ block: 'center', behavior: 'smooth' });
  }
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
  <Drawer v-model:open="open" side="right" overlayClass="queue-drawer-overlay" panelClass="queue-drawer">
    <div class="queue-header">
      <div>
        <div class="queue-title">播放列表</div>
        <div class="queue-subtitle">共 {{ queueTracks.length }} 首歌曲</div>
      </div>
      <div class="queue-actions">
        <button type="button" class="queue-icon-btn" title="滚动到顶部" @click="scrollToTop">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M12 19V5" />
            <path d="M5 12l7-7 7 7" />
          </svg>
        </button>
        <button type="button" class="queue-icon-btn" title="滚动到当前播放" @click="scrollToCurrent">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="3" />
            <path d="M3 12h3m12 0h3M12 3v3m0 12v3" />
          </svg>
        </button>
        <button type="button" class="queue-icon-btn" title="清空列表" @click="handleClear">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M3 6h18" />
            <path d="M8 6v14" />
            <path d="M16 6v14" />
            <path d="M9 6l1-2h4l1 2" />
          </svg>
        </button>
        <button type="button" class="queue-icon-btn" title="关闭" @click="open = false">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4">
            <path d="M18 6 6 18" />
            <path d="M6 6l12 12" />
          </svg>
        </button>
      </div>
    </div>

    <div class="queue-divider"></div>

    <div v-if="queueTracks.length === 0" class="queue-empty">
      <div class="queue-empty-icon">
        <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01" />
        </svg>
      </div>
      <div>列表为空，快去发现好音乐吧</div>
    </div>

    <div v-else ref="listRef" class="queue-list">
      <div
        v-for="(track, index) in queueTracks"
        :key="track.id"
        :ref="setItemRef(track.id)"
        class="queue-row"
        :class="{ 'is-current': String(track.id) === String(currentTrackId) }"
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

        <div class="queue-cover" :class="{ 'is-disabled': !isSongPlayable(track) }">
          <Cover :url="track.coverUrl" :size="120" :width="44" :height="44" :borderRadius="8" />
        </div>

        <div class="queue-info">
          <div class="queue-name-row">
            <span class="queue-name">{{ track.title }}</span>
            <Tag
              v-for="tag in getPrivilegeTags(track)"
              :key="tag.label"
              class="queue-tag"
              :color="tag.color"
            >
              {{ tag.label }}
            </Tag>
            <Tag v-if="getQualityTag(track)" class="queue-tag" color="#06B6D4">
              {{ getQualityTag(track) }}
            </Tag>
          </div>
          <div class="queue-artist">{{ track.artist }}</div>
        </div>

        <div class="queue-duration">{{ formatDuration(track.duration) }}</div>

        <button
          v-if="String(track.id) !== String(currentTrackId)"
          type="button"
          class="queue-remove"
          @click="handleRemove(track)"
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M18 6 6 18" />
            <path d="M6 6l12 12" />
          </svg>
        </button>
      </div>
    </div>
  </Drawer>
</template>

<style scoped>
@reference "@/style.css";

:global(.queue-drawer-overlay) {
  background: rgba(0, 0, 0, 0.16);
}

:global(.queue-drawer) {
  padding: 0;
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
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.queue-row {
  display: grid;
  grid-template-columns: 28px 44px minmax(0, 1fr) 52px 24px;
  align-items: center;
  gap: 10px;
  padding: 8px 10px;
  border-radius: 12px;
  background: rgba(0, 0, 0, 0.02);
  transition: background-color 0.2s ease, color 0.2s ease;
}

.dark .queue-row {
  background: rgba(255, 255, 255, 0.04);
}

.queue-row:hover {
  background: rgba(0, 0, 0, 0.06);
}

.dark .queue-row:hover {
  background: rgba(255, 255, 255, 0.08);
}

.queue-row.is-current {
  border: 1px solid color-mix(in srgb, var(--color-primary) 30%, transparent);
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
}

.queue-leading {
  position: relative;
  width: 28px;
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
  transition: opacity 0.2s ease, transform 0.2s ease;
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

.queue-cover {
  border-radius: 8px;
  overflow: hidden;
  position: relative;
}

.queue-cover.is-disabled::after {
  content: '';
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.35);
}

.queue-info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.queue-name-row {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.queue-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--color-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.queue-artist {
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.queue-duration {
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-secondary);
  text-align: right;
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
}

.queue-row:hover .queue-remove {
  opacity: 1;
}

.queue-remove:hover {
  color: #ef4444;
  transform: scale(1.05);
}

.queue-tag {
  flex-shrink: 0;
}

.queue-list::-webkit-scrollbar {
  width: 4px;
}

.queue-list::-webkit-scrollbar-thumb {
  background: var(--color-border-light);
  border-radius: 999px;
}

@media (max-width: 720px) {
  .queue-row {
    grid-template-columns: 24px 40px minmax(0, 1fr) 44px 22px;
  }
}
</style>
