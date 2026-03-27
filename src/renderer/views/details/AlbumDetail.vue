<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getAlbumDetail, getAlbumSongs } from '@/api/album';
import SliverHeader from '@/components/music/DetailPageSliverHeader.vue';
import ActionRow from '@/components/music/DetailPageActionRow.vue';
import SongList from '@/components/music/SongList.vue';
import SongListHeader from '@/components/music/SongListHeader.vue';
import Tabs from '@/components/ui/Tabs.vue';
import TabsList from '@/components/ui/TabsList.vue';
import TabsTrigger from '@/components/ui/TabsTrigger.vue';
import TabsContent from '@/components/ui/TabsContent.vue';
import Badge from '@/components/ui/Badge.vue';
import BatchActionDrawer from '@/components/music/BatchActionDrawer.vue';
import { Song } from '@/stores/playlist';
import { mapAlbumDetailMeta, mapAlbumSong } from '@/utils/mappers';
import type { SortField, SortOrder } from '@/components/music/SongListHeader.vue';
import { iconCurrentLocation, iconSearch, iconPlay, iconList, iconHeart } from '@/icons';

const route = useRoute();
const router = useRouter();
const albumId = route.params.id as string;

const loading = ref(true);
const album = ref<ReturnType<typeof mapAlbumDetailMeta> | null>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');
const loadedSongCount = computed(() => songs.value.length);
const showBatchDrawer = ref(false);

// 搜索和定位逻辑
const searchQuery = ref('');
const songListRef = ref<{ scrollToActive?: () => void } | null>(null);
const sliverHeaderRef = ref<{ currentHeight?: number } | null>(null);

// 计算 Tabs 的 sticky top 位置
const tabsTop = computed(() => {
  const headerHeight = sliverHeaderRef.value?.currentHeight || 52;
  return headerHeight;
});

// 排序逻辑
const sortField = ref<SortField | null>(null);
const sortOrder = ref<SortOrder>(null);

const handleSort = (field: SortField) => {
  if (sortField.value === field) {
    if (sortOrder.value === 'asc') {
      sortOrder.value = 'desc';
    } else if (sortOrder.value === 'desc') {
      sortField.value = null;
      sortOrder.value = null;
    }
  } else {
    sortField.value = field;
    sortOrder.value = 'asc';
  }
};

const sortedSongs = computed(() => {
  const base = songs.value.slice();
  if (!sortField.value || !sortOrder.value) return base;
  const compareText = (a: string, b: string) =>
    a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'base' });
  const indexMap = new Map<string, number>();
  songs.value.forEach((song, index) => {
    indexMap.set(song.id, index);
  });
  const direction = sortOrder.value === 'asc' ? 1 : -1;

  return base.sort((a, b) => {
    switch (sortField.value) {
      case 'title':
        return compareText(a.title, b.title) * direction;
      case 'album':
        return compareText(a.album ?? '', b.album ?? '') * direction;
      case 'duration':
        return (a.duration - b.duration) * direction;
      case 'index':
        return ((indexMap.get(a.id) ?? 0) - (indexMap.get(b.id) ?? 0)) * direction;
      default:
        return 0;
    }
  });
});

const fetchData = async () => {
  loading.value = true;
  try {
    const [detailRes, songsRes] = await Promise.all([
      getAlbumDetail(albumId),
      getAlbumSongs(albumId, 1, 50),
    ]);

    if (
      detailRes &&
      typeof detailRes === 'object' &&
      'status' in detailRes &&
      detailRes.status === 1
    ) {
      const data = 'data' in detailRes ? (detailRes as { data?: unknown }).data : undefined;
      const info = 'info' in detailRes ? (detailRes as { info?: unknown }).info : undefined;
      const rawList = Array.isArray(data ?? info) ? (data ?? info) : [];
      const raw = rawList[0];
      if (raw) {
        album.value = mapAlbumDetailMeta(raw);
      }
    }

    if (songsRes && typeof songsRes === 'object' && 'status' in songsRes && songsRes.status === 1) {
      const data =
        'data' in songsRes ? (songsRes as { data?: { info?: unknown } }).data : undefined;
      const info = 'info' in songsRes ? (songsRes as { info?: unknown }).info : undefined;
      const list = data?.info ?? info ?? [];
      songs.value = Array.isArray(list) ? list.map((item) => mapAlbumSong(item)) : [];
    }
  } catch (e) {
    console.error('Fetch album detail error:', e);
  } finally {
    loading.value = false;
  }
};

const fetchAllAlbumSongs = async (totalCount: number) => {
  if (songs.value.length >= totalCount) return;
  const pageSize = 50;
  const seenIds = new Set(songs.value.map((song) => song.id));
  let page = 2;
  while (songs.value.length < totalCount) {
    const res = await getAlbumSongs(albumId, page, pageSize);
    if (!res || typeof res !== 'object' || !('status' in res) || res.status !== 1) break;
    const data = 'data' in res ? (res as { data?: { info?: unknown } }).data : undefined;
    const info = 'info' in res ? (res as { info?: unknown }).info : undefined;
    const list = data?.info ?? info ?? [];
    const nextSongs = Array.isArray(list) ? list.map((item) => mapAlbumSong(item)) : [];
    const filtered = nextSongs.filter((song) => {
      if (seenIds.has(song.id)) return false;
      seenIds.add(song.id);
      return true;
    });
    if (filtered.length === 0) break;
    songs.value = [...songs.value, ...filtered];
    page += 1;
  }
};

onMounted(async () => {
  await fetchData();
  const totalSongs = album.value?.songCount ?? 0;
  if (totalSongs > songs.value.length) {
    void fetchAllAlbumSongs(totalSongs);
  }
});

const secondaryActions = computed(() => [
  {
    icon: iconHeart,
    label: '收藏',
    onTap: () => {},
  },
]);

const handlePlayAll = () => {};
const openBatchDrawer = () => {
  if (songs.value.length === 0) return;
  showBatchDrawer.value = true;
};
const handleLocate = () => songListRef.value?.scrollToActive();
</script>

<template>
  <div class="album-detail-container bg-bg-main min-h-full">
    <div v-if="loading && !album" class="flex items-center justify-center py-40">
      <div
        class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"
      ></div>
    </div>

    <template v-else-if="album">
      <!-- 1. Sliver Header -->
      <SliverHeader
        ref="sliverHeaderRef"
        typeLabel="ALBUM"
        :title="album.name"
        :coverUrl="album.pic"
        :hasDetails="!!album.intro"
        :expandedHeight="176"
      >
        <template #details>
          <div class="flex flex-col gap-1 text-text-main/60">
            <div class="text-[13px] font-semibold text-primary">
              {{ album.singerName }}
            </div>
            <div class="text-[11px] font-semibold opacity-60">
              {{ album.publishTime }} • {{ songs.length }} 首歌曲
            </div>
          </div>
        </template>

        <template #actions>
          <ActionRow
            :secondaryActions="secondaryActions"
            @play="handlePlayAll"
            @batch="openBatchDrawer"
          />
        </template>

        <template #collapsed-actions>
          <button
            @click="handlePlayAll"
            class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-primary"
          >
            <Icon :icon="iconPlay" width="20" height="20" />
          </button>
          <button
            @click="openBatchDrawer"
            class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60"
          >
            <Icon :icon="iconList" width="18" height="18" />
          </button>
        </template>
      </SliverHeader>

      <BatchActionDrawer v-model:open="showBatchDrawer" :songs="songs" />

      <!-- 2. Sticky Tabs + 表头 -->
      <div class="song-list-sticky sticky z-[90] bg-bg-main" :style="{ top: `${tabsTop}px` }">
        <Tabs v-model="activeTab" class="w-full">
          <!-- Tab 切换栏 -->
          <div class="px-6 border-b border-border-light/10">
            <div class="flex items-center justify-between h-14">
              <TabsList class="bg-transparent border-none">
                <TabsTrigger value="songs">
                  <span class="relative">歌曲 <Badge :count="loadedSongCount" /></span>
                </TabsTrigger>
                <TabsTrigger value="intro">
                  <span>介绍</span>
                </TabsTrigger>
              </TabsList>

              <!-- 右侧操作 -->
                <div v-if="activeTab === 'songs'" class="flex items-center gap-2">
                  <div class="relative">
                    <input
                      v-model="searchQuery"
                      type="text"
                      placeholder="搜索歌曲..."
                      class="song-search-input w-52 h-9 pl-8 pr-3 rounded-lg bg-white border border-black/30 shadow-sm text-text-main placeholder:text-text-main/50 dark:bg-white/[0.08] dark:border-white/10 dark:shadow-none outline-none text-[12px] focus:ring-1 focus:ring-primary/40 transition-all"
                    />
                  <Icon
                    class="absolute left-2.5 top-1/2 -translate-y-1/2 text-text-main/60 dark:text-text-main/60"
                    :icon="iconSearch"
                    width="14"
                    height="14"
                  />
                </div>
                <button
                  @click="handleLocate"
                  class="song-locate-btn p-2 rounded-lg"
                  title="定位当前播放"
                >
                  <Icon :icon="iconCurrentLocation" width="18" height="18" />
                </button>
              </div>
            </div>
          </div>

          <!-- 表头 (仅在歌曲 tab 显示) -->
          <SongListHeader
            v-if="activeTab === 'songs'"
            :sortField="sortField"
            :sortOrder="sortOrder"
            :showCover="true"
            @sort="handleSort"
          />
        </Tabs>
      </div>

      <!-- 3. 内容区域 -->
      <div class="pb-12">
        <Tabs v-model="activeTab" class="w-full">
          <TabsContent value="songs" class="px-6 flex flex-col flex-1 min-h-0">
            <SongList
              ref="songListRef"
              :songs="sortedSongs"
              :searchQuery="searchQuery"
              :showCover="true"
            />
          </TabsContent>

          <TabsContent value="intro" class="mt-3 px-6">
            <div
              class="max-w-3xl text-[14px] leading-relaxed text-text-secondary whitespace-pre-wrap py-4 px-2 opacity-80"
            >
              {{ album.intro || '暂无专辑介绍' }}
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </template>
  </div>
</template>

<style scoped>
.song-search-input {
  background-color: #ffffff !important;
  border-color: rgba(0, 0, 0, 0.3) !important;
  color: #1d1d1f !important;
}

.song-search-input::placeholder {
  color: rgba(29, 29, 31, 0.5) !important;
}

.song-search-input:focus {
  border-color: rgba(0, 113, 227, 0.8) !important;
  box-shadow: 0 0 0 1px rgba(0, 113, 227, 0.2) !important;
}

.dark .song-search-input {
  background-color: rgba(255, 255, 255, 0.08) !important;
  border-color: rgba(255, 255, 255, 0.1) !important;
  color: #f5f5f7 !important;
}

.dark .song-search-input::placeholder {
  color: rgba(245, 245, 247, 0.5) !important;
}

.dark .song-search-input:focus {
  border-color: rgba(0, 113, 227, 0.7) !important;
  box-shadow: 0 0 0 1px rgba(0, 113, 227, 0.3) !important;
}

.song-locate-btn {
  background-color: #ffffff;
  border: 1px solid rgba(0, 0, 0, 0.18);
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.06);
  color: rgba(29, 29, 31, 0.7);
  transition: all 0.2s ease;
}

.song-locate-btn:hover {
  border-color: rgba(0, 0, 0, 0.28);
  color: rgba(29, 29, 31, 0.9);
}

.dark .song-locate-btn {
  background-color: rgba(255, 255, 255, 0.08);
  border-color: rgba(255, 255, 255, 0.12);
  box-shadow: none;
  color: rgba(245, 245, 247, 0.7);
}

.dark .song-locate-btn:hover {
  border-color: rgba(255, 255, 255, 0.22);
  color: rgba(245, 245, 247, 0.9);
}
</style>

<style scoped>
@reference "@/style.css";

.search-expand-enter-active,
.search-expand-leave-active {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.search-expand-enter-from,
.search-expand-leave-to {
  opacity: 0;
  width: 0;
  transform: translateX(10px);
}

:deep(.song-list) {
  @apply px-0;
}
</style>
