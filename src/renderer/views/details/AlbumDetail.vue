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
const showSearch = ref(false);
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

const fetchData = async () => {
  loading.value = true;
  try {
    const [detailRes, songsRes] = await Promise.all([
      getAlbumDetail(albumId),
      getAlbumSongs(albumId, 1, 50)
    ]);

    if (detailRes && typeof detailRes === 'object' && 'status' in detailRes && detailRes.status === 1) {
      const data = 'data' in detailRes ? (detailRes as { data?: unknown }).data : undefined;
      const info = 'info' in detailRes ? (detailRes as { info?: unknown }).info : undefined;
      const rawList = Array.isArray(data ?? info) ? (data ?? info) : [];
      const raw = rawList[0];
      if (raw) {
        album.value = mapAlbumDetailMeta(raw);
      }
    }
    
    if (songsRes && typeof songsRes === 'object' && 'status' in songsRes && songsRes.status === 1) {
      const data = 'data' in songsRes ? (songsRes as { data?: { info?: unknown } }).data : undefined;
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
    icon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>',
    label: '收藏',
    onTap: () => {}
  }
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
      <div class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
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
            <div class="text-[11px] font-semibold opacity-40">
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
           <button @click="handlePlayAll" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-primary">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
           </button>
           <button @click="openBatchDrawer" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M3 6h18"/><path d="M3 12h18"/><path d="M3 18h18"/></svg>
           </button>
           <button @click="router.push({ name: 'comment', params: { id: albumId }, query: { type: 'album' } })" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>
           </button>
        </template>
      </SliverHeader>

      <BatchActionDrawer v-model:open="showBatchDrawer" :songs="songs" />

      <!-- 2. Sticky Tabs + 表头 -->
      <div class="sticky z-[90] bg-bg-main" :style="{ top: `${tabsTop}px` }">
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
                <Transition name="search-expand">
                  <div v-if="showSearch" class="relative">
                    <input
                      v-model="searchQuery"
                      type="text"
                      placeholder="搜索歌曲..."
                      class="w-48 h-9 pl-8 pr-3 rounded-lg bg-black/[0.05] dark:bg-white/[0.08] outline-none text-[12px] focus:ring-1 focus:ring-primary/30 transition-all"
                      @blur="!searchQuery && (showSearch = false)"
                      autofocus
                    />
                    <svg class="absolute left-2.5 top-1/2 -translate-y-1/2 opacity-30" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
                  </div>
                </Transition>
                <button @click="showSearch = !showSearch" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60" :class="{ 'text-primary opacity-100': showSearch }">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
                </button>
                <button @click="handleLocate" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="12" cy="12" r="3"/><path d="M3 12h3m12 0h3M12 3v3m0 12v3"/></svg>
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
          <TabsContent value="songs" class="px-6">
            <SongList ref="songListRef" :songs="songs" :searchQuery="searchQuery" :showCover="true" />
          </TabsContent>

          <TabsContent value="intro" class="mt-3 px-6">
            <div class="max-w-3xl text-[14px] leading-relaxed text-text-secondary whitespace-pre-wrap py-4 px-2 opacity-80">
              {{ album.intro || '暂无专辑介绍' }}
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </template>
  </div>
</template>


<style scoped>
@reference "@/style.css";

.search-expand-enter-active, .search-expand-leave-active {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.search-expand-enter-from, .search-expand-leave-to {
  opacity: 0;
  width: 0;
  transform: translateX(10px);
}

:deep(.song-list) {
  @apply px-0;
}
</style>
