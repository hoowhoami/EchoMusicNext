<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getAlbumDetail, getAlbumSongs } from '@/api/album';
import { getAlbumComments } from '@/api/comment';
import SliverHeader from '@/components/music/DetailPageSliverHeader.vue';
import ActionRow from '@/components/music/DetailPageActionRow.vue';
import SongList from '@/components/music/SongList.vue';
import SongListHeader from '@/components/music/SongListHeader.vue';
import Tabs from '@/components/ui/Tabs.vue';
import TabsList from '@/components/ui/TabsList.vue';
import TabsTrigger from '@/components/ui/TabsTrigger.vue';
import TabsContent from '@/components/ui/TabsContent.vue';
import Badge from '@/components/ui/Badge.vue';
import Dialog from '@/components/ui/Dialog.vue';
import CommentList from '@/components/music/CommentList.vue';
import BatchActionDrawer from '@/components/music/BatchActionDrawer.vue';
import { Song } from '@/stores/playlist';
import {
  mapAlbumDetailMeta,
  mapAlbumSong,
  mapCommentItem,
  type AlbumMeta,
  type Comment,
} from '@/utils/mappers';
import type { SortField, SortOrder } from '@/components/music/SongListHeader.vue';
import { usePlayerStore } from '@/stores/player';
import { iconCurrentLocation, iconSearch, iconPlay, iconList, iconHeart } from '@/icons';

type UnknownRecord = Record<string, unknown>;

const isRecord = (value: unknown): value is UnknownRecord => {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
};

const toRecord = (value: unknown): UnknownRecord => (isRecord(value) ? value : {});

const parseIntSafe = (value: unknown): number => {
  if (value == null) return 0;
  if (typeof value === 'number') return value;
  const parsed = Number.parseInt(String(value), 10);
  return Number.isNaN(parsed) ? 0 : parsed;
};

const route = useRoute();
const router = useRouter();
const getAlbumId = () => route.params.id as string;

const loading = ref(true);
const album = ref<AlbumMeta | null>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');
const loadingComments = ref(false);
const comments = ref<Comment[]>([]);
const commentTotal = ref(0);
const commentPage = ref(1);
const hasMoreComments = ref(true);
const showBatchDrawer = ref(false);
const showIntroDialog = ref(false);

const playerStore = usePlayerStore();

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

const fetchComments = async (reset = false) => {
  if (loadingComments.value) return;
  if (reset) {
    commentPage.value = 1;
    comments.value = [];
    commentTotal.value = 0;
    hasMoreComments.value = true;
  }
  if (!hasMoreComments.value) return;

  loadingComments.value = true;
  try {
    const res = await getAlbumComments(getAlbumId(), commentPage.value);
    if (
      res &&
      typeof res === 'object' &&
      'status' in res &&
      (res as { status?: number }).status === 1
    ) {
      const record = toRecord(res);
      const data = toRecord(record.data ?? record.info ?? record);
      const listCandidate = data.list ?? data.comments ?? [];
      const list = Array.isArray(listCandidate) ? listCandidate : [];
      const mapped = list.map(mapCommentItem).filter((item) => item.content.length > 0);
      comments.value = reset ? mapped : [...comments.value, ...mapped];

      const totalRaw =
        data.total ?? data.count ?? record.total ?? record.count ?? commentTotal.value;
      const totalValue = parseIntSafe(totalRaw);
      if (totalValue > 0) {
        commentTotal.value = totalValue;
        hasMoreComments.value = comments.value.length < totalValue;
      } else {
        hasMoreComments.value = mapped.length > 0;
      }

      if (hasMoreComments.value) {
        commentPage.value += 1;
      }
    } else {
      hasMoreComments.value = false;
    }
  } catch (e) {
    console.error('Fetch album comments error:', e);
    hasMoreComments.value = false;
  } finally {
    loadingComments.value = false;
  }
};

const handleTabChange = (value: string) => {
  activeTab.value = value;
  if (value === 'comments' && comments.value.length === 0) {
    fetchComments(true);
  }
};

const fetchData = async () => {
  loading.value = true;
  try {
    const albumId = getAlbumId();
    const [detailRes, songsRes] = await Promise.all([
      getAlbumDetail(albumId),
      getAlbumSongs(albumId, 1, 30),
    ]);

    if (
      detailRes &&
      typeof detailRes === 'object' &&
      'status' in detailRes &&
      detailRes.status === 1
    ) {
      const data = 'data' in detailRes ? (detailRes as { data?: unknown }).data : undefined;
      const info = 'info' in detailRes ? (detailRes as { info?: unknown }).info : undefined;
      const rawList = Array.isArray(data ?? info)
        ? (data ?? info)
        : (data ?? info)
          ? [data ?? info]
          : [];
      const raw = rawList[0];
      if (raw) {
        album.value = mapAlbumDetailMeta(raw);
      }
    }

    if (songsRes && typeof songsRes === 'object' && 'status' in songsRes && songsRes.status === 1) {
      const data =
        'data' in songsRes ? (songsRes as { data?: { info?: unknown } }).data : undefined;
      const info = 'info' in songsRes ? (songsRes as { info?: unknown }).info : undefined;
      const list = data?.songs ?? info ?? [];
      songs.value = Array.isArray(list) ? list.map((item) => mapAlbumSong(item)) : [];
    }

    const totalSongs = album.value?.songCount ?? 0;
    if (totalSongs > songs.value.length) {
      void fetchAllAlbumSongs(totalSongs);
    }
  } catch (e) {
    console.error('Fetch album detail error:', e);
  } finally {
    loading.value = false;
  }
};

const fetchAllAlbumSongs = async (totalCount: number) => {
  if (songs.value.length >= totalCount) return;
  const pageSize = 30;
  const albumId = getAlbumId();
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

onMounted(fetchData);

watch(
  () => route.params.id,
  () => {
    album.value = null;
    songs.value = [];
    comments.value = [];
    commentPage.value = 1;
    commentTotal.value = 0;
    hasMoreComments.value = true;
    fetchData();
    if (activeTab.value === 'comments') {
      fetchComments(true);
    }
  },
);

const secondaryActions = computed(() => [
  {
    icon: iconHeart,
    label: '收藏',
    onTap: () => {},
  },
]);

const handlePlayAll = () => {
  if (songs.value.length === 0) return;
  playerStore.playTrack(songs.value[0].id);
};
const openBatchDrawer = () => {
  if (songs.value.length === 0) return;
  showBatchDrawer.value = true;
};
const handleLocate = () => songListRef.value?.scrollToActive();

const activeSongId = computed(() => playerStore.currentTrackId ?? undefined);
const loadedSongCount = computed(() => songs.value.length);
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
              {{ album.publishTime }} • {{ album.songCount }} 首歌曲
            </div>
            <button
              v-if="album.intro"
              type="button"
              class="mt-1 text-left text-[11px] font-semibold text-primary"
              @click="showIntroDialog = true"
            >
              查看专辑详情
            </button>
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
        <Tabs :model-value="activeTab" class="w-full" @update:model-value="handleTabChange">
          <!-- Tab 切换栏 -->
          <div class="px-6 border-b border-border-light/10">
            <div class="flex items-center justify-between h-14">
              <TabsList class="bg-transparent border-none gap-8">
                <TabsTrigger value="songs">
                  <span class="relative">歌曲 <Badge :count="loadedSongCount" /></span>
                </TabsTrigger>
                <TabsTrigger value="comments">
                  <span class="relative">
                    评论
                    <Badge v-if="commentTotal > 0" :count="commentTotal" class="-right-6" />
                  </span>
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
        <Tabs :model-value="activeTab" class="w-full" @update:model-value="handleTabChange">
          <TabsContent value="songs" class="px-6 flex flex-col flex-1 min-h-0">
            <SongList
              ref="songListRef"
              :songs="sortedSongs"
              :searchQuery="searchQuery"
              :activeId="activeSongId"
              :showCover="true"
            />
          </TabsContent>

          <TabsContent value="comments" class="px-6 py-10">
            <div class="max-w-4xl mx-auto">
              <div class="flex items-center justify-between mb-8">
                <div class="text-[16px] font-semibold text-text-main">
                  评论
                  <span v-if="commentTotal > 0" class="text-[12px] font-normal opacity-60 ml-2">
                    {{ commentTotal }}
                  </span>
                </div>
                <button
                  @click="
                    router.push({
                      name: 'comment',
                      params: { id: getAlbumId() },
                      query: { type: 'album' },
                    })
                  "
                  class="px-4 py-1.5 rounded-full border border-border-light/40 text-[12px] font-semibold text-text-secondary hover:text-primary hover:border-primary/40 transition-colors"
                >
                  查看全部
                </button>
              </div>

              <CommentList :comments="comments" :loading="loadingComments" :total="commentTotal" />

              <div v-if="hasMoreComments" class="flex justify-center mt-8">
                <button
                  @click="fetchComments()"
                  :disabled="loadingComments"
                  class="px-6 py-2 rounded-full border border-border-light/40 text-[12px] font-semibold text-text-secondary hover:text-primary hover:border-primary/40 transition-colors disabled:opacity-60"
                >
                  {{ loadingComments ? '加载中...' : '加载更多' }}
                </button>
              </div>
            </div>
          </TabsContent>
        </Tabs>
      </div>

      <Dialog
        v-model:open="showIntroDialog"
        title="专辑介绍"
        :description="album.intro"
        contentClass="max-w-[720px]"
        showClose
      />
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
