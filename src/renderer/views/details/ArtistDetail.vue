<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getArtistDetail, getArtistSongs, getArtistAlbums } from '@/api/artist';
import SliverHeader from '@/components/music/DetailPageSliverHeader.vue';
import ActionRow from '@/components/music/DetailPageActionRow.vue';
import SongList from '@/components/music/SongList.vue';
import SongListHeader from '@/components/music/SongListHeader.vue';
import AlbumCard from '@/components/music/AlbumCard.vue';
import Tabs from '@/components/ui/Tabs.vue';
import TabsList from '@/components/ui/TabsList.vue';
import TabsTrigger from '@/components/ui/TabsTrigger.vue';
import TabsContent from '@/components/ui/TabsContent.vue';
import Badge from '@/components/ui/Badge.vue';
import BatchActionDrawer from '@/components/music/BatchActionDrawer.vue';
import { Song } from '@/stores/playlist';
import { mapAlbumMeta, mapArtistDetailMeta, mapArtistSong } from '@/utils/mappers';
import type { SortField, SortOrder } from '@/components/music/SongListHeader.vue';

const route = useRoute();
const router = useRouter();
const artistId = route.params.id as string;

const loading = ref(true);
const artist = ref<ReturnType<typeof mapArtistDetailMeta> | null>(null);
const songs = ref<Song[]>([]);
const albums = ref<ReturnType<typeof mapAlbumMeta>[]>([]);
const activeTab = ref('songs');
const loadedSongCount = computed(() => songs.value.length);
const loadedAlbumCount = computed(() => albums.value.length);
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
    const [detailRes, songsRes, albumsRes] = await Promise.all([
      getArtistDetail(artistId),
      getArtistSongs(artistId, 1, 200, 'hot'),
      getArtistAlbums(artistId, 1, 30, 'hot')
    ]);

    if (detailRes && typeof detailRes === 'object' && 'status' in detailRes && detailRes.status === 1) {
      const data = 'data' in detailRes ? (detailRes as { data?: unknown }).data : undefined;
      const info = 'info' in detailRes ? (detailRes as { info?: unknown }).info : undefined;
      const raw = data ?? info;
      if (raw) {
        artist.value = mapArtistDetailMeta(raw);
      }
    }
    
    if (songsRes && typeof songsRes === 'object' && 'status' in songsRes && songsRes.status === 1) {
      const data = 'data' in songsRes ? (songsRes as { data?: unknown }).data : undefined;
      const payload = data ?? songsRes;
      const list = (payload as { info?: unknown; list?: unknown; songs?: unknown }).info
        || (payload as { list?: unknown }).list
        || (payload as { songs?: unknown }).songs
        || [];
      songs.value = Array.isArray(list) ? list.map((item) => mapArtistSong(artistId, item)) : [];
    }

    if (albumsRes && typeof albumsRes === 'object' && 'status' in albumsRes && albumsRes.status === 1) {
      const data = 'data' in albumsRes ? (albumsRes as { data?: { info?: unknown } }).data : undefined;
      const info = 'info' in albumsRes ? (albumsRes as { info?: unknown }).info : undefined;
      const list = data?.info ?? info ?? [];
      albums.value = Array.isArray(list) ? list.map((item) => mapAlbumMeta(item)) : [];
    }
  } catch (e) {
    console.error('Fetch artist detail error:', e);
  } finally {
    loading.value = false;
  }
};

const fetchAllArtistSongs = async (totalCount: number) => {
  if (songs.value.length >= totalCount) return;
  const pageSize = 200;
  const seenIds = new Set(songs.value.map((song) => song.id));
  let page = 2;
  while (songs.value.length < totalCount) {
    const res = await getArtistSongs(artistId, page, pageSize, 'hot');
    if (!res || typeof res !== 'object' || !('status' in res) || res.status !== 1) break;
    const data = 'data' in res ? (res as { data?: unknown }).data : undefined;
    const payload = data ?? res;
    const list = (payload as { info?: unknown; list?: unknown; songs?: unknown }).info
      || (payload as { list?: unknown }).list
      || (payload as { songs?: unknown }).songs
      || [];
    const nextSongs = Array.isArray(list) ? list.map((item) => mapArtistSong(artistId, item)) : [];
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

const fetchAllArtistAlbums = async (totalCount: number) => {
  if (albums.value.length >= totalCount) return;
  const pageSize = 30;
  const seenIds = new Set(albums.value.map((album) => String(album.id)));
  let page = 2;
  while (albums.value.length < totalCount) {
    const res = await getArtistAlbums(artistId, page, pageSize, 'hot');
    if (!res || typeof res !== 'object' || !('status' in res) || res.status !== 1) break;
    const data = 'data' in res ? (res as { data?: { info?: unknown } }).data : undefined;
    const info = 'info' in res ? (res as { info?: unknown }).info : undefined;
    const list = data?.info ?? info ?? [];
    const nextAlbums = Array.isArray(list) ? list.map((item) => mapAlbumMeta(item)) : [];
    const filtered = nextAlbums.filter((album) => {
      const id = String(album.id);
      if (seenIds.has(id)) return false;
      seenIds.add(id);
      return true;
    });
    if (filtered.length === 0) break;
    albums.value = [...albums.value, ...filtered];
    page += 1;
  }
};

onMounted(async () => {
  await fetchData();
  const totalSongs = artist.value?.songCount ?? 0;
  const totalAlbums = artist.value?.albumCount ?? 0;
  if (totalSongs > songs.value.length) {
    void fetchAllArtistSongs(totalSongs);
  }
  if (totalAlbums > albums.value.length) {
    void fetchAllArtistAlbums(totalAlbums);
  }
});

const secondaryActions = computed(() => [
  {
    icon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>',
    label: '关注',
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
  <div class="artist-detail-container bg-bg-main min-h-full">
    <div v-if="loading && !artist" class="flex items-center justify-center py-40">
      <div class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <template v-else-if="artist">
      <!-- 1. Sliver Header -->
      <SliverHeader
        ref="sliverHeaderRef"
        typeLabel="ARTIST"
        :title="artist.name"
        :coverUrl="artist.pic"
        :hasDetails="!!artist.intro"
        :expandedHeight="176"
      >
        <template #details>
          <div class="flex flex-col gap-1 text-text-main/60">
            <div class="text-[13px] font-semibold text-primary">
              {{ artist.songCount || songs.length }} 歌曲 • {{ artist.albumCount || albums.length }} 专辑
            </div>
            <div v-if="artist.intro" class="text-[11px] line-clamp-1 opacity-70">
              {{ artist.intro }}
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
                <TabsTrigger value="albums">
                  <span class="relative">专辑 <Badge :count="loadedAlbumCount" /></span>
                </TabsTrigger>
                <TabsTrigger value="intro">
                  <span>详情</span>
                </TabsTrigger>
              </TabsList>

              <!-- 右侧操作 -->
              <div v-if="activeTab === 'songs'" class="flex items-center gap-2">
                <div class="relative">
                  <input
                    v-model="searchQuery"
                    type="text"
                    placeholder="搜索歌曲..."
                    class="w-52 h-9 pl-8 pr-3 rounded-lg bg-black/[0.05] dark:bg-white/[0.08] outline-none text-[12px] focus:ring-1 focus:ring-primary/30 transition-all"
                  />
                  <svg class="absolute left-2.5 top-1/2 -translate-y-1/2 opacity-30" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
                </div>
                <button @click="handleLocate" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60" title="定位当前播放">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 3v4"/><path d="M12 17v4"/><path d="M3 12h4"/><path d="M17 12h4"/><circle cx="12" cy="12" r="3"/></svg>
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
          <SongList ref="songListRef" :songs="sortedSongs" :searchQuery="searchQuery" :showCover="true" />
        </TabsContent>

          <TabsContent value="albums" class="mt-4 px-6">
            <div class="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-6 px-2">
              <AlbumCard
                v-for="album in albums"
                :key="album.id"
                :id="album.id"
                :name="album.name"
                :coverUrl="album.pic"
                :artist="album.singerName"
                :publishTime="album.publishTime"
              />
            </div>
          </TabsContent>

          <TabsContent value="intro" class="mt-3 px-6">
            <div class="max-w-3xl text-[14px] leading-relaxed text-text-secondary whitespace-pre-wrap py-4 px-2 opacity-80">
              {{ artist.intro || '暂无简介' }}
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
