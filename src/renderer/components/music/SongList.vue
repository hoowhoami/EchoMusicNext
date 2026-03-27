<script setup lang="ts">
import { computed, ref } from 'vue';
import { Song } from '@/stores/playlist';
import { formatDuration } from '@/utils/format';
import SongCard from './SongCard.vue';
import { RecycleScroller, RecycleScrollerInstance } from 'vue-virtual-scroller';
import 'vue-virtual-scroller/dist/vue-virtual-scroller.css';

interface Props {
  songs: Song[];
  showIndex?: boolean;
  showCover?: boolean;
  showAlbum?: boolean;
  showDuration?: boolean;
  activeId?: string | number;
  searchQuery?: string;
  parentPlaylistId?: string | number;
  enableRemoveFromPlaylist?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  showIndex: true,
  showCover: true,
  showAlbum: true,
  showDuration: true,
  searchQuery: '',
  parentPlaylistId: '',
  enableRemoveFromPlaylist: false,
});

const emit = defineEmits<{
  (e: 'play', song: Song): void;
  (e: 'more', song: Song): void;
}>();

// 搜索过滤
const filteredSongs = computed(() => {
  if (!props.searchQuery.trim()) return props.songs;
  const q = props.searchQuery.toLowerCase();
  return props.songs.filter(
    (s) =>
      s.title.toLowerCase().includes(q) ||
      s.artist.toLowerCase().includes(q) ||
      s.album?.toLowerCase().includes(q),
  );
});

const itemHeight = 56;
const originalIndexMap = computed(() => {
  const map = new Map<string | number, number>();
  props.songs.forEach((song, index) => {
    map.set(song.id, index);
  });
  return map;
});

const isSongPlayable = (song: Song) => {
  const isUnavailable = song.privilege === 40;
  const isPaid = song.privilege === 10 && song.payType === 2;
  const isNoCopyright = song.privilege === 5;

  if (isUnavailable || isPaid) return false;
  if (isNoCopyright) return song.oldCpy === 1;
  return Boolean(song.hash?.trim());
};

const rowOpacity = (song: Song) => (isSongPlayable(song) ? 1 : 0.45);

const listRef = ref<RecycleScrollerInstance | null>(null);

const scrollToActive = () => {
  if (!props.activeId || !listRef.value) return;
  const index = filteredSongs.value.findIndex((s) => s.id === props.activeId);
  if (index === -1) return;
  listRef.value.scrollToItem(index);
};

defineExpose({ scrollToActive, filteredCount: computed(() => filteredSongs.value.length) });
</script>

<template>
  <RecycleScroller
    ref="listRef"
    class="song-list-container"
    :items="filteredSongs"
    :item-size="itemHeight"
    key-field="id"
    page-mode
  >
    <template #default="{ item: song }">
      <div
        class="song-list-row group flex items-center py-0 rounded-lg transition-all duration-200 cursor-default"
        :style="{ height: `${itemHeight}px`, opacity: rowOpacity(song) }"
        :class="{ 'bg-primary/5 dark:bg-primary/10 text-primary': activeId === song.id }"
        @dblclick="emit('play', song)"
      >
        <div v-if="showIndex" class="w-10 shrink-0 flex items-center justify-start pl-2">
          <div class="relative w-4 h-4">
            <div
              v-if="activeId === song.id"
              class="absolute inset-0 flex items-center justify-center gap-0.5"
            >
              <div class="w-0.5 h-full bg-primary animate-bounce-slow"></div>
              <div class="w-0.5 h-2/3 bg-primary animate-bounce-fast"></div>
              <div class="w-0.5 h-full bg-primary animate-bounce-medium"></div>
            </div>
            <span
              v-else
              class="absolute inset-0 flex items-center justify-center text-[12px] opacity-40 transition-opacity group-hover:opacity-0"
            >
              {{ (originalIndexMap.get(song.id) ?? 0) + 1 }}
            </span>
            <svg
              v-if="activeId !== song.id"
              xmlns="http://www.w3.org/2000/svg"
              width="20"
              height="20"
              viewBox="0 0 24 24"
              fill="currentColor"
              class="absolute inset-0 m-auto opacity-0 transition-opacity group-hover:opacity-100 text-text-main cursor-pointer"
            >
              <path d="M8 5v14l11-7z" />
            </svg>
          </div>
        </div>

        <div class="flex-1 min-w-0 ml-4">
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
            :parentPlaylistId="props.parentPlaylistId"
            :enableRemoveFromPlaylist="props.enableRemoveFromPlaylist"
            :showCover="showCover"
            :showAlbum="false"
            :showDuration="false"
            :active="activeId === song.id"
            variant="list"
          />
        </div>

        <div
          v-if="showAlbum"
          class="w-48 min-w-0 hidden md:block text-[13px] opacity-60 truncate pr-4"
        >
          {{ song.album || '未知专辑' }}
        </div>

        <div v-if="showDuration" class="w-16 shrink-0 text-[12px] opacity-40">
          {{ formatDuration(song.duration) }}
        </div>
      </div>
    </template>

    <template #empty v-if="filteredSongs?.length === 0">
      <div class="py-20 text-center opacity-30 text-[14px] italic">
        {{ props.searchQuery ? '未找到相关歌曲' : '暂无歌曲' }}
      </div>
    </template>
  </RecycleScroller>
</template>

<style scoped>
@reference "@/style.css";

.song-list-container {
  user-select: none;
}

:global(.view-port::-webkit-scrollbar) {
  width: 10px;
}

:global(.view-port::-webkit-scrollbar-track) {
  background: transparent;
}

:global(.view-port::-webkit-scrollbar-thumb) {
  background: rgba(0, 0, 0, 0.18);
  border-radius: 10px;
}

:global(.dark .view-port::-webkit-scrollbar-thumb) {
  background: rgba(255, 255, 255, 0.2);
}

:global(.view-port) {
  scrollbar-width: thin;
  scrollbar-color: rgba(0, 0, 0, 0.18) transparent;
}

:global(.dark .view-port) {
  scrollbar-color: rgba(255, 255, 255, 0.2) transparent;
}

.song-list-row:hover {
  background: rgba(0, 0, 0, 0.08);
}

.dark .song-list-row:hover {
  background: rgba(255, 255, 255, 0.32);
}

.animate-bounce-slow {
  animation: bounce 1s infinite;
}
.animate-bounce-medium {
  animation: bounce 0.8s infinite;
}
.animate-bounce-fast {
  animation: bounce 1.2s infinite;
}

@keyframes bounce {
  0%,
  100% {
    height: 100%;
  }
  50% {
    height: 30%;
  }
}
</style>
