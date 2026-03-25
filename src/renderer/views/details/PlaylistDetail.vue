<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getPlaylistDetail, getPlaylistTracks, getPlaylistTracksNew } from '../../api/playlist';
import SliverHeader from '../../components/music/DetailPageSliverHeader.vue';
import ActionRow from '../../components/music/DetailPageActionRow.vue';
import SongList from '../../components/music/SongList.vue';
import SongListHeader from '../../components/music/SongListHeader.vue';
import Avatar from '../../components/ui/Avatar.vue';
import Tabs from '../../components/ui/Tabs.vue';
import TabsList from '../../components/ui/TabsList.vue';
import TabsTrigger from '../../components/ui/TabsTrigger.vue';
import TabsContent from '../../components/ui/TabsContent.vue';
import Badge from '../../components/ui/Badge.vue';
import { Song } from '../../stores/playlist';
import { formatDate } from '../../utils/format';
import { useUserStore } from '../../stores/user';
import {
  mapPlaylistMeta,
  parsePlaylistTracks,
  resolvePlaylistTrackQueryId,
  type PlaylistMeta,
} from '../../utils/mappers';
import type { SortField, SortOrder } from '../../components/music/SongListHeader.vue';

const route = useRoute();
const router = useRouter();
const getPlaylistId = () => route.params.id as string;

const loading = ref(true);
const playlist = ref<PlaylistMeta | null>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');

// 搜索和定位逻辑
const showSearch = ref(false);
const searchQuery = ref('');
const songListRef = ref<{ scrollToActive?: () => void } | null>(null);
const sliverHeaderRef = ref<{ currentHeight?: number } | null>(null);
const userStore = useUserStore();

// 计算 Tabs 的 sticky top 位置
const tabsTop = computed(() => {
  const headerHeight = sliverHeaderRef.value?.currentHeight || 52;
  return headerHeight; // header 当前高度
});

// 排序逻辑
const sortField = ref<SortField | null>(null);
const sortOrder = ref<SortOrder>(null);

const handleSort = (field: SortField) => {
  if (sortField.value === field) {
    // 切换排序顺序: asc -> desc -> null
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
    const detailRes = await getPlaylistDetail(getPlaylistId());
    if (detailRes && typeof detailRes === 'object' && 'status' in detailRes && detailRes.status === 1) {
      const data = 'data' in detailRes ? (detailRes as { data?: unknown }).data : undefined;
      const info = 'info' in detailRes ? (detailRes as { info?: unknown }).info : undefined;
      const raw = Array.isArray(data ?? info) ? (data ?? info)?.[0] : data ?? info;
      if (raw) {
        playlist.value = mapPlaylistMeta(raw);
      }
    }

    const playlistMeta = playlist.value;
    const currentUserId = userStore.info?.userid;
    const queryId = resolvePlaylistTrackQueryId(getPlaylistId(), {
      listid: playlistMeta?.listid,
      listCreateGid: playlistMeta?.listCreateGid,
      listCreateUserid: playlistMeta?.listCreateUserid,
      currentUserId,
    });

    let tracksRes: unknown;
    let didFallback = false;
    try {
      tracksRes = await getPlaylistTracks(queryId, 1, 200);
    } catch (e) {
      const fallbackListId = playlistMeta?.listid ?? getPlaylistId();
      tracksRes = await getPlaylistTracksNew(fallbackListId, 1, 200);
      didFallback = true;
    }

    if (tracksRes && typeof tracksRes === 'object') {
      const hasStatus = 'status' in tracksRes;
      const statusOk = hasStatus && (tracksRes as { status?: number }).status === 1;
      const hasPayload = 'data' in tracksRes || 'info' in tracksRes;
      if (statusOk || hasPayload) {
        const payload =
          'data' in tracksRes
            ? (tracksRes as { data?: unknown }).data
            : 'info' in tracksRes
              ? (tracksRes as { info?: unknown }).info
              : tracksRes;
        const { songs: parsedSongs } = parsePlaylistTracks(payload ?? tracksRes);
        songs.value = parsedSongs;
      }
    }

    if (!didFallback && songs.value.length === 0 && playlistMeta?.listid) {
      const fallbackTracks = await getPlaylistTracksNew(playlistMeta.listid, 1, 200);
      if (fallbackTracks && typeof fallbackTracks === 'object') {
        const payload =
          'data' in fallbackTracks
            ? (fallbackTracks as { data?: unknown }).data
            : 'info' in fallbackTracks
              ? (fallbackTracks as { info?: unknown }).info
              : fallbackTracks;
        const { songs: parsedSongs } = parsePlaylistTracks(payload ?? fallbackTracks);
        if (parsedSongs.length > 0) {
          songs.value = parsedSongs;
        }
      }
    }
  } catch (e) {
    console.error('Fetch playlist error:', e);
  } finally {
    loading.value = false;
  }
};

onMounted(fetchData);
watch(
  () => route.params.id,
  () => {
    playlist.value = null;
    songs.value = [];
    fetchData();
  }
);

const secondaryActions = computed(() => [
  {
    icon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>',
    label: '收藏',
    onTap: () => {},
  },
]);

const handlePlayAll = () => {};
const handleLocate = () => songListRef.value?.scrollToActive();
</script>

<template>
  <div class="playlist-detail-container bg-bg-main min-h-full">
    <div v-if="loading && !playlist" class="flex items-center justify-center py-40">
      <div
        class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"
      ></div>
    </div>

    <template v-else-if="playlist">
      <!-- 1. Sliver Header -->
      <SliverHeader
        ref="sliverHeaderRef"
        typeLabel="PLAYLIST"
        :title="playlist.name"
        :coverUrl="playlist.pic"
        :hasDetails="true"
      >
        <template #details>
          <div class="flex items-center gap-3 text-text-main/70">
            <div class="flex items-center gap-2">
              <Avatar
                :src="playlist.userPic"
                :size="20"
                class="rounded-full overflow-hidden"
              />
              <span class="text-[13px] font-semibold text-primary">{{
                playlist.nickname || 'Unknown'
              }}</span>
            </div>
            <span class="text-[11px] font-semibold opacity-40"
              >{{
                formatDate(playlist.publishDate || playlist.createTime, 'YYYY-MM-DD')
              }}
              创建</span
            >
          </div>
        </template>

        <template #actions>
          <ActionRow :secondaryActions="secondaryActions" @play="handlePlayAll" />
        </template>

        <template #collapsed-actions>
          <button
            @click="handlePlayAll"
            class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-primary"
          >
            <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor">
              <path d="M8 5v14l11-7z" />
            </svg>
          </button>
          <button
            @click="
              router.push({
                name: 'comment',
                params: { id: getPlaylistId() },
                query: { type: 'playlist' },
              })
            "
            class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60"
          >
            <svg
              width="18"
              height="18"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2.2"
            >
              <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z" />
            </svg>
          </button>
        </template>
      </SliverHeader>

      <!-- 2. Sticky Tabs + 表头 -->
      <div class="sticky z-[90] bg-bg-main" :style="{ top: `${tabsTop}px` }">
        <Tabs v-model="activeTab" class="w-full">
          <!-- Tab 切换栏 -->
          <div class="px-8 border-b border-border-light/10">
            <div class="flex items-center justify-between h-14">
              <TabsList class="bg-transparent border-none">
                <TabsTrigger value="songs">
                  <span class="relative">歌曲 <Badge :count="songs.length" /></span>
                </TabsTrigger>
                <TabsTrigger value="comments">
                  <span class="relative">评论</span>
                </TabsTrigger>
              </TabsList>

              <!-- 右侧搜索与定位 -->
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
                    <svg
                      class="absolute left-2.5 top-1/2 -translate-y-1/2 opacity-30"
                      width="14"
                      height="14"
                      viewBox="0 0 24 24"
                      fill="none"
                      stroke="currentColor"
                      stroke-width="2.5"
                    >
                      <circle cx="11" cy="11" r="8" />
                      <path d="m21 21-4.3-4.3" />
                    </svg>
                  </div>
                </Transition>
                <button
                  @click="showSearch = !showSearch"
                  class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60"
                  :class="{ 'text-primary opacity-100': showSearch }"
                >
                  <svg
                    width="18"
                    height="18"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    stroke-width="2.2"
                  >
                    <circle cx="11" cy="11" r="8" />
                    <path d="m21 21-4.3-4.3" />
                  </svg>
                </button>
                <button
                  @click="handleLocate"
                  class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60"
                >
                  <svg
                    width="18"
                    height="18"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    stroke-width="2.2"
                  >
                    <circle cx="12" cy="12" r="3" />
                    <path d="M3 12h3m12 0h3M12 3v3m0 12v3" />
                  </svg>
                </button>
              </div>
            </div>
          </div>

          <!-- 表头 (仅在歌曲 tab 显示) -->
          <SongListHeader
            v-if="activeTab === 'songs'"
            :sortField="sortField"
            :sortOrder="sortOrder"
            @sort="handleSort"
          />
        </Tabs>
      </div>

      <!-- 3. 内容区域 -->
      <div class="pb-12">
        <Tabs v-model="activeTab" class="w-full">
          <TabsContent value="songs" class="px-8">
            <SongList ref="songListRef" :songs="songs" :searchQuery="searchQuery" />
          </TabsContent>

          <TabsContent value="comments" class="py-20 text-center">
            <button
              @click="
                router.push({
                  name: 'comment',
                params: { id: getPlaylistId() },
                  query: { type: 'playlist' },
                })
              "
              class="px-8 py-2 rounded-full bg-primary/10 text-primary font-bold text-[14px]"
            >
              查看全部评论
            </button>
          </TabsContent>
        </Tabs>
      </div>
    </template>
  </div>
</template>

<style scoped>
@reference "../../style.css";

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
