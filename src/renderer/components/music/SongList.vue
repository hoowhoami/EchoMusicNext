<script setup lang="ts">
import { computed, nextTick, ref } from 'vue';
import type { Song } from '@/models/song';
import { formatDuration } from '@/utils/format';
import SongCard from './SongCard.vue';
import { RecycleScroller, RecycleScrollerInstance } from 'vue-virtual-scroller';
import 'vue-virtual-scroller/dist/vue-virtual-scroller.css';
import { iconPlay, iconPause } from '@/icons';
import { usePlayerStore } from '@/stores/player';

interface Props {
  songs: Song[];
  showIndex?: boolean;
  showCover?: boolean;
  showAlbum?: boolean;
  showDuration?: boolean;
  rowPaddingClass?: string;
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
  rowPaddingClass: '',
  searchQuery: '',
  parentPlaylistId: '',
  enableRemoveFromPlaylist: false,
});

const emit = defineEmits<{
  (e: 'more', song: Song): void;
}>();

const playerStore = usePlayerStore();

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

const itemHeight = 60;
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

const readString = (value: unknown, fallback = ''): string => {
  if (value === undefined || value === null) return fallback;
  return String(value);
};

const activeIdText = computed(() => readString(props.activeId));
const isActiveSong = (song: Song) => readString(song.id) === activeIdText.value;

const handleTogglePlay = (song: Song) => {
  if (isActiveSong(song)) {
    playerStore.togglePlay();
  } else {
    playerStore.playTrack(song.id, props.songs);
  }
};

const getScrollContainer = (): HTMLElement | null =>
  document.querySelector('.view-port') as HTMLElement | null;

const getStickyOffset = (scrollContainer: HTMLElement): number => {
  const containerTop = scrollContainer.getBoundingClientRect().top;
  const stickyNodes = Array.from(
    scrollContainer.querySelectorAll<HTMLElement>('.sliver-header-root, .song-list-sticky'),
  );
  if (stickyNodes.length === 0) return 0;
  const bottoms = stickyNodes
    .map((node) => node.getBoundingClientRect().bottom - containerTop)
    .filter((value) => Number.isFinite(value) && value > 0);
  if (bottoms.length === 0) return 0;
  return Math.max(...bottoms);
};

const adjustActiveIntoView = (smooth = false) => {
  const scrollContainer = getScrollContainer();
  if (!scrollContainer || !activeIdText.value) return;
  const row = scrollContainer.querySelector<HTMLElement>(
    `[data-song-row][data-song-id="${activeIdText.value}"]`,
  );
  if (!row) return;
  const containerRect = scrollContainer.getBoundingClientRect();
  const rowRect = row.getBoundingClientRect();
  const stickyOffset = getStickyOffset(scrollContainer);
  const topLimit = containerRect.top + stickyOffset + 8;
  const bottomLimit = containerRect.bottom - 12;
  if (rowRect.top < topLimit) {
    const target = scrollContainer.scrollTop - (topLimit - rowRect.top);
    scrollContainer.scrollTo({ top: Math.max(0, target), behavior: smooth ? 'smooth' : 'auto' });
    return;
  }
  if (rowRect.bottom > bottomLimit) {
    const target = scrollContainer.scrollTop + (rowRect.bottom - bottomLimit);
    scrollContainer.scrollTo({ top: Math.max(0, target), behavior: smooth ? 'smooth' : 'auto' });
  }
};

const scrollToActive = async () => {
  if (!activeIdText.value || !listRef.value) return;
  const index = filteredSongs.value.findIndex((s) => readString(s.id) === activeIdText.value);
  if (index === -1) return;
  listRef.value.scrollToItem(index);
  await nextTick();
  const scrollContainer = getScrollContainer();
  const scrollerEl = (listRef.value as { $el?: HTMLElement } | null)?.$el ?? null;
  if (scrollContainer && scrollerEl) {
    const containerRect = scrollContainer.getBoundingClientRect();
    const scrollerRect = scrollerEl.getBoundingClientRect();
    const scrollerOffset = scrollerRect.top - containerRect.top;
    const stickyOffset = getStickyOffset(scrollContainer);
    const targetTop =
      index * itemHeight + scrollContainer.scrollTop + scrollerOffset - stickyOffset - 8;
    scrollContainer.scrollTo({ top: Math.max(0, targetTop), behavior: 'smooth' });
    requestAnimationFrame(() => adjustActiveIntoView(true));
    return;
  }
  requestAnimationFrame(() => adjustActiveIntoView(true));
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
        class="song-list-row group rounded-lg transition-all duration-200 cursor-default"
        :style="{ height: `${itemHeight}px`, opacity: rowOpacity(song) }"
        :class="{ 'is-active': isActiveSong(song) }"
        :data-song-row="true"
        :data-song-id="readString(song.id)"
      >
        <div class="song-list-row-inner flex items-center w-full h-full" :class="props.rowPaddingClass">
          <div v-if="showIndex" class="w-10 shrink-0 flex items-center justify-start pl-2">
            <div class="relative w-4 h-4">
              <template v-if="isActiveSong(song)">
                <div
                  v-if="playerStore.isPlaying"
                  class="absolute inset-0 flex items-center justify-center text-primary cursor-pointer"
                  @click.stop="handleTogglePlay(song)"
                >
                  <Icon :icon="iconPause" width="14" height="14" />
                </div>
                <div
                  v-else
                  class="absolute inset-0 flex items-center justify-center text-primary cursor-pointer"
                  @click.stop="handleTogglePlay(song)"
                >
                  <Icon :icon="iconPlay" width="14" height="14" />
                </div>
              </template>
              <template v-else>
                <span
                  class="absolute inset-0 flex items-center justify-center text-[12px] opacity-60 transition-opacity group-hover:opacity-0"
                >
                  {{ (originalIndexMap.get(song.id) ?? 0) + 1 }}
                </span>
                <Icon
                  class="absolute inset-0 m-auto opacity-0 transition-opacity group-hover:opacity-100 text-text-main cursor-pointer"
                  :icon="iconPlay"
                  width="14"
                  height="14"
                  @click.stop="handleTogglePlay(song)"
                />
              </template>
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
              :showMore="false"
              :active="isActiveSong(song)"
              variant="list"
            />
          </div>

          <div
            v-if="showAlbum"
            class="w-[188px] min-w-0 hidden md:block pl-[18px] text-[13px] text-text-main/70 truncate"
          >
            {{ song.album || '未知专辑' }}
          </div>

          <div v-if="showDuration" class="w-16 shrink-0 pl-4 text-[12px] opacity-60 text-right whitespace-nowrap">
            {{ formatDuration(song.duration) }}
          </div>
        </div>
      </div>
    </template>

    <template #empty v-if="filteredSongs?.length === 0">
      <div class="py-20 text-center opacity-50 text-[14px] italic">
        {{ props.searchQuery ? '未找到相关歌曲' : '暂无歌曲' }}
      </div>
    </template>
  </RecycleScroller>
</template>

<style scoped>
@reference "@/style.css";

.song-list-container {
  user-select: none;
  width: 100%;
}

.song-list-row {
  width: 100%;
}

.song-list-row-inner {
  box-sizing: border-box;
}

.song-list-row:hover {
  background: var(--color-bg-card);
}

.dark .song-list-row:hover {
  background: color-mix(in srgb, #ffffff 4%, transparent);
}

.song-list-row.is-active {
  background: var(--color-bg-card);
}

.dark .song-list-row.is-active {
  background: color-mix(in srgb, #ffffff 4%, transparent);
}
</style>
