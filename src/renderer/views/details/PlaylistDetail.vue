<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getPlaylistDetail, getPlaylistTracks, getPlaylistTracksNew } from '../../api/playlist';
import { getPlaylistComments } from '../../api/comment';
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
import CommentList from '../../components/music/CommentList.vue';
import Dialog from '../../components/ui/Dialog.vue';
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
import { usePlaylistStore } from '../../stores/playlist';
import { usePlayerStore } from '../../stores/player';

interface PlaylistComment {
  id: string | number;
  userName: string;
  avatar: string;
  content: string;
  time: string;
  likeCount: number;
}

type UnknownRecord = Record<string, unknown>;

const isRecord = (value: unknown): value is UnknownRecord => {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
};

const toRecord = (value: unknown): UnknownRecord => (isRecord(value) ? value : {});

const readString = (value: unknown, fallback = ''): string => {
  if (value == null) return fallback;
  return String(value);
};

const parseIntSafe = (value: unknown): number => {
  if (value == null) return 0;
  if (typeof value === 'number') return value;
  const parsed = Number.parseInt(String(value), 10);
  return Number.isNaN(parsed) ? 0 : parsed;
};

const route = useRoute();
const router = useRouter();
const getPlaylistId = () => route.params.id as string;

const loading = ref(true);
const playlist = ref<PlaylistMeta | null>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');
const loadingComments = ref(false);
const comments = ref<PlaylistComment[]>([]);
const commentTotal = ref(0);
const commentPage = ref(1);
const hasMoreComments = ref(true);
const showIntroDialog = ref(false);

// 搜索和定位逻辑
const showSearch = ref(false);
const searchQuery = ref('');
const songListRef = ref<{ scrollToActive?: () => void } | null>(null);
const sliverHeaderRef = ref<{ currentHeight?: number } | null>(null);
const userStore = useUserStore();
const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();

const isOwnerPlaylist = computed(() => {
  const meta = playlist.value;
  const currentUserId = userStore.info?.userid;
  return !!meta && !!currentUserId && meta.listCreateUserid === currentUserId;
});

const currentPlaylistIds = computed(() => {
  const meta = playlist.value;
  if (!meta) return [] as string[];
  return [
    meta.id,
    meta.listid,
    meta.listCreateListid,
    meta.globalCollectionId,
    meta.listCreateGid,
  ]
    .filter((item): item is string | number => item !== undefined && item !== null && item !== '')
    .map((item) => String(item));
});

const isFavoritePlaylist = computed(() => {
  if (!playlist.value) return false;
  const currentIds = currentPlaylistIds.value;
  if (currentIds.length === 0) return false;
  const currentUserId = userStore.info?.userid;
  return playlistStore.userPlaylists.some((entry) => {
    if (entry.source === 2) return false;
    if (currentUserId && entry.listCreateUserid === currentUserId) return false;
    const entryIds = [
      entry.id,
      entry.listid,
      entry.listCreateListid,
      entry.globalCollectionId,
      entry.listCreateGid,
    ]
      .filter((item): item is string | number => item !== undefined && item !== null && item !== '')
      .map((item) => String(item));
    return entryIds.some((id) => currentIds.includes(id));
  });
});

const songTotalCount = computed(() => {
  const metaCount = playlist.value?.count ?? 0;
  return metaCount > 0 ? metaCount : songs.value.length;
});

const playlistTags = computed(() => {
  const raw = playlist.value?.tags ?? '';
  return raw
    .split(',')
    .map((tag) => tag.trim())
    .filter((tag) => tag.length > 0);
});

const playlistCommentId = computed(() => {
  const meta = playlist.value;
  if (!meta) return getPlaylistId();
  if (meta.globalCollectionId) return meta.globalCollectionId;
  if (meta.listCreateGid) return meta.listCreateGid;
  if (meta.listCreateUserid && meta.listCreateListid) {
    return `collection_3_${meta.listCreateUserid}_${meta.listCreateListid}_0`;
  }
  return getPlaylistId();
});

const mapCommentItem = (item: unknown): PlaylistComment => {
  const record = toRecord(item);
  return {
    id: readString(record.comment_id ?? record.id ?? ''),
    userName: readString(record.user_name ?? record.nickname ?? '匿名用户'),
    avatar: readString(record.user_img ?? record.avatar ?? ''),
    content: readString(record.content ?? ''),
    time: readString(record.add_time ?? record.time ?? ''),
    likeCount: parseIntSafe(record.like_count ?? record.count ?? 0),
  };
};

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
    const res = await getPlaylistComments(playlistCommentId.value, commentPage.value);
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const record = toRecord(res);
      const data = toRecord(record.data ?? record.info ?? record);
      const listCandidate = data.list ?? data.comments ?? [];
      const list = Array.isArray(listCandidate) ? listCandidate : [];
      const mapped = list.map(mapCommentItem).filter((item) => item.content.length > 0);
      comments.value = reset ? mapped : [...comments.value, ...mapped];

      const totalRaw = data.total ?? data.count ?? record.total ?? record.count ?? commentTotal.value;
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
    console.error('Fetch playlist comments error:', e);
    hasMoreComments.value = false;
  } finally {
    loadingComments.value = false;
  }
};

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

const handleTabChange = (value: string) => {
  activeTab.value = value;
  if (value === 'comments' && comments.value.length === 0) {
    fetchComments(true);
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
    comments.value = [];
    commentPage.value = 1;
    commentTotal.value = 0;
    hasMoreComments.value = true;
    fetchData();
    if (activeTab.value === 'comments') {
      fetchComments(true);
    }
  }
);

watch(
  () => playlistCommentId.value,
  (nextId, prevId) => {
    if (nextId !== prevId && activeTab.value === 'comments') {
      fetchComments(true);
    }
  }
);

const secondaryActions = computed(() => [
  {
    icon: isFavoritePlaylist.value
      ? '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>'
      : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>',
    label: isFavoritePlaylist.value ? '已收藏' : '收藏',
    emphasized: isFavoritePlaylist.value,
    onTap: async () => {
      if (!playlist.value) return;
      if (!userStore.isLoggedIn) return;
      if (isOwnerPlaylist.value) return;
      if (isFavoritePlaylist.value) {
        await playlistStore.unfavoritePlaylist(playlist.value);
      } else {
        await playlistStore.favoritePlaylist(playlist.value);
      }
    },
  },
  ...(isOwnerPlaylist.value
    ? []
    : [
        {
          icon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 6h18"/><path d="M3 12h18"/><path d="M3 18h18"/></svg>',
          label: '管理',
          onTap: () => {},
        },
      ]),
]);

const handlePlayAll = () => {
  if (songs.value.length === 0) return;
  playerStore.playTrack(songs.value[0].id);
};
const handleLocate = () => songListRef.value?.scrollToActive();

const activeSongId = computed(() => playerStore.currentTrackId ?? undefined);

const sortedSongs = computed(() => {
  const base = songs.value.slice();
  if (!sortField.value || !sortOrder.value) return base;
  const compareText = (a: string, b: string) => a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'base' });
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
      default:
        return 0;
    }
  });
});

const handlePlaySong = (song: Song) => {
  playerStore.playTrack(song.id);
};
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
        :expandedHeight="200"
        :collapsedHeight="56"
      >
        <template #details>
          <div class="flex flex-col gap-2">
            <div class="flex items-center gap-3">
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
              <span class="text-[11px] font-semibold text-text-main/40"
                >{{
                  formatDate(playlist.publishDate || playlist.createTime, 'YYYY-MM-DD')
                }}
                创建</span
              >
            </div>
            <div class="flex items-center flex-wrap gap-2 text-[11px] font-semibold">
              <span class="inline-flex items-center gap-1 text-text-main/50">
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M9 18V5l12-2v13" />
                  <circle cx="6" cy="18" r="3" />
                  <circle cx="18" cy="16" r="3" />
                </svg>
                {{ songTotalCount }}
              </span>
              <span
                v-for="tag in playlistTags"
                :key="tag"
                class="px-2 py-0.5 rounded-md text-[10px] font-semibold text-primary bg-primary/10 border border-primary/20"
              >
                {{ tag }}
              </span>
            </div>
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

      <div v-if="playlist.intro" class="px-6 pt-[10px] pb-[6px]">
        <div class="text-[15px] font-semibold text-text-main">歌单介绍</div>
        <div class="mt-[6px] text-[12px] leading-relaxed text-text-secondary line-clamp-1">
          {{ playlist.intro }}
        </div>
        <button
          type="button"
          class="mt-[2px] text-[11px] font-semibold text-primary"
          @click="showIntroDialog = true"
        >
          查看详情
        </button>
      </div>

      <!-- 2. Sticky Tabs + 表头 -->
      <div class="sticky z-[90] bg-bg-main" :style="{ top: `${tabsTop}px` }">
        <Tabs :model-value="activeTab" class="w-full" @update:model-value="handleTabChange">
          <!-- Tab 切换栏 -->
          <div class="px-6 border-b border-border-light/10">
            <div class="flex items-center justify-between h-14">
              <TabsList class="bg-transparent border-none">
                <TabsTrigger value="songs">
                  <span class="relative">歌曲 <Badge :count="songTotalCount" /></span>
                </TabsTrigger>
                <TabsTrigger value="comments">
                  <span class="relative">
                    评论
                    <Badge v-if="commentTotal > 0" :count="commentTotal" class="-right-6" />
                  </span>
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
        <Tabs :model-value="activeTab" class="w-full" @update:model-value="handleTabChange">
          <TabsContent value="songs" class="px-6">
            <SongList
              ref="songListRef"
              :songs="sortedSongs"
              :searchQuery="searchQuery"
              :activeId="activeSongId"
              @play="handlePlaySong"
            />
          </TabsContent>

          <TabsContent value="comments" class="px-6 py-10">
            <div class="max-w-4xl mx-auto">
              <div class="flex items-center justify-between mb-8">
                <div class="text-[16px] font-semibold text-text-main">
                  评论
                  <span v-if="commentTotal > 0" class="text-[12px] font-normal opacity-40 ml-2">
                    {{ commentTotal }}
                  </span>
                </div>
                <button
                  @click="
                    router.push({
                      name: 'comment',
                      params: { id: getPlaylistId() },
                      query: { type: 'playlist' },
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
                  class="px-6 py-2 rounded-full border border-border-light/40 text-[12px] font-semibold text-text-secondary hover:text-primary hover:border-primary/40 transition-colors disabled:opacity-50"
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
        :title="'歌单介绍'"
        :description="playlist.intro"
        contentClass="max-w-[720px]"
        showClose
      />
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
