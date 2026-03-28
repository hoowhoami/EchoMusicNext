<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import {
  getMusicComments,
  getPlaylistComments,
  getAlbumComments,
  getMusicClassifyComments,
  getMusicHotwordComments,
  getFloorComments,
  getFavoriteCount,
  getCommentCount,
} from '@/api/comment';
import { getSongPrivilegeLite, getSongRanking } from '@/api/music';
import { mapCommentItem, type Comment } from '@/utils/mappers';
import Drawer from '@/components/ui/Drawer.vue';
import Tabs from '@/components/ui/Tabs.vue';
import TabsList from '@/components/ui/TabsList.vue';
import TabsTrigger from '@/components/ui/TabsTrigger.vue';
import TabsContent from '@/components/ui/TabsContent.vue';
import CommentList from '@/components/music/CommentList.vue';
import Cover from '@/components/ui/Cover.vue';
import BackToTop from '@/components/ui/BackToTop.vue';
import { iconArrowLeft, iconX } from '@/icons';

interface CommentPayload {
  hot?: Comment[];
  list: Comment[];
  total: number;
  classifyList: Array<{ id: number | string; name: string; count?: number }>;
  hotwordList: Array<{ content: string; count?: number }>;
}

const route = useRoute();
const router = useRouter();
const id = route.params.id as string;
const type = route.query.type as 'music' | 'playlist' | 'album';

const songTitle = computed(() => String(route.query.title ?? ''));
const songArtist = computed(() => String(route.query.artist ?? ''));
const songAlbum = computed(() => String(route.query.album ?? ''));
const songCover = computed(() => String(route.query.cover ?? ''));
const songHash = computed(() => String(route.query.hash ?? ''));
const songAlbumId = computed(() => String(route.query.albumId ?? ''));
const songMixSongId = computed(() => String(route.query.mixSongId ?? id));

const activeMainTab = ref('comments');
const activeCommentTab = ref('hot');

const isLoadingComments = ref(false);
const isLoadingClassify = ref(false);
const isLoadingHotword = ref(false);
const total = ref(0);
const hotComments = ref<Comment[]>([]);
const comments = ref<Comment[]>([]);
const page = ref(1);
const hasMore = ref(true);

const classifyList = ref<Array<{ id: number | string; name: string; count?: number }>>([]);
const hotwordList = ref<Array<{ content: string; count?: number }>>([]);
const classifyComments = ref<Comment[]>([]);
const classifyPage = ref(1);
const hasMoreClassify = ref(true);
const selectedClassify = ref<number | string | null>(null);
const hotwordComments = ref<Comment[]>([]);
const hotwordPage = ref(1);
const hasMoreHotword = ref(true);
const selectedHotword = ref<string | null>(null);

const detailLoading = ref(false);
const privilegeData = ref<Record<string, unknown> | null>(null);
const rankingData = ref<Record<string, unknown> | null>(null);
const favoriteCount = ref(0);
const commentCount = ref(0);

const showFloor = ref(false);
const floorLoading = ref(false);
const floorReplies = ref<Comment[]>([]);
const floorTotal = ref(0);
const floorPage = ref(1);
const floorHasMore = ref(true);
const activeFloorComment = ref<Comment | null>(null);
const floorMessage = ref('');
const floorLoadMoreMessage = ref('');
const floorBodyRef = ref<HTMLElement | null>(null);

const title = computed(() => {
  switch (type) {
    case 'music':
      return '歌曲评论';
    case 'playlist':
      return '歌单评论';
    case 'album':
      return '专辑评论';
    default:
      return '评论';
  }
});

const totalLabel = computed(() => (total.value > 0 ? `(${total.value})` : ''));

const formatCount = (value: number) => {
  if (!Number.isFinite(value)) return '--';
  if (value < 10000) return value.toString();
  const fixed = (value / 10000).toFixed(value >= 100000 ? 0 : 1);
  return `${fixed.replace(/\.0$/, '')}w`;
};

const qualityTags = computed(() => {
  const relateGoods = (privilegeData.value?.relate_goods as Record<string, unknown>[] | undefined) ?? [];
  const seen = new Set<string>();
  return relateGoods
    .map((item) => String(item.quality ?? ''))
    .filter((quality) => quality && !seen.has(quality) && seen.add(quality))
    .filter((quality) => !['piano', 'acappella', 'subwoofer', 'ancient', 'surnay', 'dj', 'viper_tape', 'viper_atmos', 'viper_clear'].includes(quality));
});

const effectTags = computed(() => {
  const relateGoods = (privilegeData.value?.relate_goods as Record<string, unknown>[] | undefined) ?? [];
  const seen = new Set<string>();
  return relateGoods
    .map((item) => String(item.quality ?? ''))
    .filter((quality) => quality && !seen.has(quality) && seen.add(quality))
    .filter((quality) => ['piano', 'acappella', 'subwoofer', 'ancient', 'surnay', 'dj', 'viper_tape', 'viper_atmos', 'viper_clear'].includes(quality));
});

const rankingInfo = computed(() => {
  const data = rankingData.value?.data as Record<string, unknown> | undefined;
  const list = (data?.info as unknown[]) ?? [];
  return Array.isArray(list) ? list : [];
});

const rankingSummary = computed(() => {
  const data = rankingData.value?.data as Record<string, unknown> | undefined;
  return String(data?.title2 ?? '');
});

const buildPayload = (data: unknown): CommentPayload => {
  const record = data && typeof data === 'object' ? (data as Record<string, unknown>) : {};
  const payload = (record.data as Record<string, unknown>) || record;
  const listCandidate = (payload.list ?? payload.comments ?? []) as unknown;
  const list = Array.isArray(listCandidate) ? listCandidate : [];
  const hotListCandidate =
    payload.weight_list ?? payload.hot_list ?? payload.star_cmts ?? payload.star_comment;
  const hotList = Array.isArray((hotListCandidate as Record<string, unknown> | undefined)?.list)
    ? (hotListCandidate as { list: unknown[] }).list
    : Array.isArray(hotListCandidate)
      ? hotListCandidate
      : [];
  const starCandidate =
    (payload.star_cmts as Record<string, unknown> | undefined)?.list ??
    (payload.star_comment as Record<string, unknown> | undefined)?.list ??
    [];
  const starList = Array.isArray(starCandidate) ? starCandidate : [];
  const classifyCandidate = payload.classify_list ?? [];
  const hotwordCandidate = payload.hot_word_list ?? [];

  const hotMapped = hotList.map(mapCommentItem).map((item) => ({ ...item, isHot: true }));
  const starMapped = starList
    .map(mapCommentItem)
    .map((item) => ({ ...item, isStar: true }));

  return {
    hot: [...starMapped, ...hotMapped],
    list: list.map(mapCommentItem),
    total: Number(payload.count ?? payload.total ?? record.count ?? record.total ?? 0) || 0,
    classifyList: Array.isArray(classifyCandidate)
      ? classifyCandidate.map((item) => ({
          id: (item as Record<string, unknown>).id as string | number,
          name: String((item as Record<string, unknown>).name ?? ''),
          count: Number((item as Record<string, unknown>).cnt ?? 0) || 0,
        }))
      : [],
    hotwordList: Array.isArray(hotwordCandidate)
      ? hotwordCandidate.map((item) => ({
          content: String((item as Record<string, unknown>).content ?? ''),
          count: Number((item as Record<string, unknown>).count ?? 0) || 0,
        }))
      : [],
  };
};

const fetchMusicComments = async (reset = false) => {
  if (isLoadingComments.value) return;
  if (reset) {
    page.value = 1;
    comments.value = [];
    total.value = 0;
    hasMore.value = true;
  }
  isLoadingComments.value = true;
  try {
    const res = await getMusicComments(songMixSongId.value || id, page.value, 30, {
      showClassify: reset,
      showHotwordList: reset,
    });
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const payload = buildPayload(res);
      if (reset) {
        hotComments.value = payload.hot ?? [];
        classifyList.value = payload.classifyList;
        hotwordList.value = payload.hotwordList;
        if (!selectedClassify.value && classifyList.value.length > 0) {
          selectedClassify.value = classifyList.value[0].id;
        }
        if (!selectedHotword.value && hotwordList.value.length > 0) {
          selectedHotword.value = hotwordList.value[0].content;
        }
      }
      comments.value = reset
        ? payload.list
        : [...comments.value, ...payload.list.map((item) => ({ ...item, isHot: false }))];
      total.value = payload.total;
      hasMore.value = total.value > 0 ? comments.value.length < total.value : payload.list.length >= 30;
      if (hasMore.value) page.value += 1;
    }
  } finally {
    isLoadingComments.value = false;
  }
};

const fetchPlaylistComments = async (reset = false) => {
  if (isLoadingComments.value) return;
  if (reset) {
    page.value = 1;
    comments.value = [];
    total.value = 0;
    hasMore.value = true;
  }
  isLoadingComments.value = true;
  try {
    const res = await getPlaylistComments(id, page.value, 30, {
      showClassify: reset,
      showHotwordList: reset,
    });
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const payload = buildPayload(res);
      if (reset) {
        hotComments.value = payload.hot ?? [];
      }
      comments.value = reset
        ? payload.list
        : [...comments.value, ...payload.list.map((item) => ({ ...item, isHot: false }))];
      total.value = payload.total;
      hasMore.value = total.value > 0 ? comments.value.length < total.value : payload.list.length >= 30;
      if (hasMore.value) page.value += 1;
    }
  } finally {
    isLoadingComments.value = false;
  }
};

const fetchAlbumComments = async (reset = false) => {
  if (isLoadingComments.value) return;
  if (reset) {
    page.value = 1;
    comments.value = [];
    total.value = 0;
    hasMore.value = true;
  }
  isLoadingComments.value = true;
  try {
    const res = await getAlbumComments(id, page.value, 30, {
      showClassify: reset,
      showHotwordList: reset,
    });
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const payload = buildPayload(res);
      if (reset) {
        hotComments.value = payload.hot ?? [];
      }
      comments.value = reset
        ? payload.list
        : [...comments.value, ...payload.list.map((item) => ({ ...item, isHot: false }))];
      total.value = payload.total;
      hasMore.value = total.value > 0 ? comments.value.length < total.value : payload.list.length >= 30;
      if (hasMore.value) page.value += 1;
    }
  } finally {
    isLoadingComments.value = false;
  }
};

const fetchClassifyComments = async (reset = false) => {
  if (!selectedClassify.value) return;
  if (type !== 'music') return;
  if (isLoadingClassify.value) return;
  if (reset) {
    classifyPage.value = 1;
    classifyComments.value = [];
    hasMoreClassify.value = true;
  }
  isLoadingClassify.value = true;
  try {
    const res = await getMusicClassifyComments(
      songMixSongId.value || id,
      selectedClassify.value,
      classifyPage.value,
      30,
    );
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const payload = buildPayload(res);
      classifyComments.value = reset ? payload.list : [...classifyComments.value, ...payload.list];
      const selectedItem = classifyList.value.find((item) => item.id === selectedClassify.value);
      const totalCount = payload.total || selectedItem?.count || 0;
      hasMoreClassify.value = totalCount > 0
        ? classifyComments.value.length < totalCount
        : payload.list.length >= 30;
      if (hasMoreClassify.value) classifyPage.value += 1;
    }
  } finally {
    isLoadingClassify.value = false;
  }
};

const fetchHotwordComments = async (reset = false) => {
  if (!selectedHotword.value) return;
  if (type !== 'music') return;
  if (isLoadingHotword.value) return;
  if (reset) {
    hotwordPage.value = 1;
    hotwordComments.value = [];
    hasMoreHotword.value = true;
  }
  isLoadingHotword.value = true;
  try {
    const res = await getMusicHotwordComments(
      songMixSongId.value || id,
      selectedHotword.value,
      hotwordPage.value,
      30,
    );
    if (res && typeof res === 'object' && 'status' in res && (res as { status?: number }).status === 1) {
      const payload = buildPayload(res);
      hotwordComments.value = reset ? payload.list : [...hotwordComments.value, ...payload.list];
      const selectedItem = hotwordList.value.find((item) => item.content === selectedHotword.value);
      const totalCount = payload.total || selectedItem?.count || 0;
      hasMoreHotword.value = totalCount > 0
        ? hotwordComments.value.length < totalCount
        : payload.list.length >= 30;
      if (hasMoreHotword.value) hotwordPage.value += 1;
    }
  } finally {
    isLoadingHotword.value = false;
  }
};

const fetchComments = async (reset = false) => {
  if (type === 'music') return fetchMusicComments(reset);
  if (type === 'playlist') return fetchPlaylistComments(reset);
  return fetchAlbumComments(reset);
};

const resetFloor = () => {
  floorReplies.value = [];
  floorTotal.value = 0;
  floorPage.value = 1;
  floorHasMore.value = true;
  floorMessage.value = '';
  floorLoadMoreMessage.value = '';
};

const openFloor = (comment: Comment) => {
  activeFloorComment.value = comment;
  resetFloor();
  showFloor.value = true;
  void fetchFloorReplies(true);
};

const handleFloorScroll = () => {
  if (!floorBodyRef.value) return;
  if (floorLoading.value || !floorHasMore.value) return;
  const { scrollTop, scrollHeight, clientHeight } = floorBodyRef.value;
  if (scrollHeight - scrollTop - clientHeight < 240) {
    void fetchFloorReplies();
  }
};

const fetchFloorReplies = async (reset = false) => {
  if (!activeFloorComment.value) return;
  if (floorLoading.value) return;
  if (!floorHasMore.value && !reset) return;
  if (reset) {
    floorPage.value = 1;
    floorReplies.value = [];
    floorHasMore.value = true;
  }
  floorLoading.value = true;
  try {
    const comment = activeFloorComment.value;
    const specialId = comment.specialId ?? '';
    const tid = comment.tid ?? String(comment.id);
    const mixSongId = comment.mixSongId ?? (type === 'music' ? songMixSongId.value || id : undefined);
    if (!specialId || !tid) {
      floorMessage.value = '楼层评论暂不可用';
      floorHasMore.value = false;
      return;
    }
    const res = await getFloorComments({
      specialId,
      tid,
      mixSongId,
      code: comment.code,
      resourceType: type,
      page: floorPage.value,
      pagesize: 30,
    });
    if (res && typeof res === 'object') {
      const payload = (res as { data?: unknown }).data ?? res;
      const listCandidate = (payload as Record<string, unknown>).list ?? [];
      const errCode = Number((payload as Record<string, unknown>).err_code ?? 0) || 0;
      const message = String((payload as Record<string, unknown>).message ?? '');
      const list = Array.isArray(listCandidate) ? listCandidate : [];
      const mapped = list.map(mapCommentItem);
      floorReplies.value = reset ? mapped : [...floorReplies.value, ...mapped];
      const totalCount = Number((payload as Record<string, unknown>).comments_num ?? 0) || 0;
      floorTotal.value = totalCount;
      floorHasMore.value = totalCount > 0
        ? floorReplies.value.length < totalCount
        : mapped.length >= 30;
      if (floorHasMore.value) floorPage.value += 1;
      if (floorReplies.value.length === 0) {
        floorMessage.value = errCode !== 0
          ? '楼层评论暂不可用'
          : message || '暂无回复';
      }
    }
  } catch {
    floorLoadMoreMessage.value = '加载更多失败，点击重试';
  } finally {
    floorLoading.value = false;
  }
};

const handleMainTabChange = (value: string) => {
  activeMainTab.value = value;
};

const handleCommentTabChange = (value: string) => {
  activeCommentTab.value = value;
  if (value === 'classify' && classifyComments.value.length === 0) {
    void fetchClassifyComments(true);
  }
  if (value === 'hotword' && hotwordComments.value.length === 0) {
    void fetchHotwordComments(true);
  }
};

const fetchHeaderStats = async () => {
  if (type !== 'music') return;
  try {
    const [favoriteRes, commentRes] = await Promise.all([
      getFavoriteCount(songMixSongId.value || id),
      getCommentCount(songHash.value || ''),
    ]);
    if (favoriteRes && typeof favoriteRes === 'object') {
      const record = favoriteRes as Record<string, unknown>;
      const data = (record.data as Record<string, unknown>) || record;
      const list = (data.list as Record<string, unknown>[]) || [];
      const first = Array.isArray(list) ? list[0] : undefined;
      const count = Number(first?.collect_count ?? data.collect_count ?? 0) || 0;
      favoriteCount.value = count;
    }
    if (commentRes && typeof commentRes === 'object') {
      const record = commentRes as Record<string, unknown>;
      const data = (record.data as Record<string, unknown>) || record;
      const count = Number(data.count ?? data.total ?? 0) || 0;
      commentCount.value = count;
    }
  } catch {
    // ignore
  }
};

const fetchDetailData = async () => {
  if (type !== 'music') return;
  if (!songHash.value) return;
  detailLoading.value = true;
  try {
    const [privilegeRes, rankingRes] = await Promise.all([
      getSongPrivilegeLite(songHash.value, songAlbumId.value || undefined),
      getSongRanking(songMixSongId.value || id),
    ]);
    if (privilegeRes && typeof privilegeRes === 'object') {
      const record = privilegeRes as Record<string, unknown>;
      const data = (record.data as unknown[]) || [];
      const list = Array.isArray(data) ? data : [];
      privilegeData.value = list.length > 0 && typeof list[0] === 'object'
        ? (list[0] as Record<string, unknown>)
        : null;
    }
    if (rankingRes && typeof rankingRes === 'object') {
      rankingData.value = rankingRes as Record<string, unknown>;
    }
  } finally {
    detailLoading.value = false;
  }
};

onMounted(() => {
  void fetchComments(true);
  void fetchHeaderStats();
  void fetchDetailData();
});
</script>

<template>
  <div class="comment-page bg-bg-main min-h-full">
    <div class="comment-header">
      <button class="comment-back" @click="router.back()">
        <Icon :icon="iconArrowLeft" width="20" height="20" />
      </button>
      <div class="comment-title">
        {{ title }}
        <span class="comment-title-count" v-if="totalLabel">{{ totalLabel }}</span>
      </div>
      <div class="comment-spacer"></div>
    </div>

    <div class="comment-content-wrap">
      <Tabs :model-value="activeMainTab" @update:model-value="handleMainTabChange">
        <div class="comment-main-tabs">
          <TabsList class="comment-main-list">
            <TabsTrigger value="detail">详情</TabsTrigger>
            <TabsTrigger value="comments">评论</TabsTrigger>
          </TabsList>
        </div>

        <TabsContent value="detail">
          <div class="song-detail-panel">
            <div class="song-detail-cover">
              <Cover :url="songCover" :size="480" :width="140" :height="140" :borderRadius="20" />
            </div>
            <div class="song-detail-info">
              <div class="song-detail-title">{{ songTitle || '未知歌曲' }}</div>
              <div class="song-detail-sub">
                <span>{{ songArtist || '未知歌手' }}</span>
                <span v-if="songAlbum">• {{ songAlbum }}</span>
              </div>
              <div class="song-detail-meta">
                <span>收藏：{{ formatCount(favoriteCount) }}</span>
                <span>评论：{{ formatCount(commentCount) }}</span>
              </div>
            </div>
          </div>

          <div v-if="detailLoading" class="comment-detail-empty">加载中...</div>

          <div v-if="!detailLoading && (qualityTags.length || effectTags.length)" class="detail-section">
            <div v-if="qualityTags.length" class="detail-block">
              <div class="detail-title">可选音质</div>
              <div class="detail-tags">
                <span v-for="tag in qualityTags" :key="tag" class="detail-tag">{{ tag }}</span>
              </div>
            </div>
            <div v-if="effectTags.length" class="detail-block">
              <div class="detail-title">可用音效</div>
              <div class="detail-tags">
                <span v-for="tag in effectTags" :key="tag" class="detail-tag">{{ tag }}</span>
              </div>
            </div>
          </div>

          <div v-if="!detailLoading" class="detail-section">
            <div class="detail-title">榜单成就</div>
            <div v-if="rankingSummary" class="detail-summary">• {{ rankingSummary }}</div>
            <div v-if="rankingInfo.length === 0" class="comment-detail-empty">暂无榜单数据</div>
            <div v-else class="ranking-list">
              <div v-for="(rank, index) in rankingInfo" :key="index" class="ranking-card">
                <div class="ranking-title">{{ rank.platform_name || '未知平台' }}</div>
                <div class="ranking-meta">
                  <span>累计上榜：{{ rank.ranking_times || 0 }}次</span>
                  <span>最近上榜：{{ rank.last_time || '未知' }}</span>
                </div>
                <div class="ranking-rank">第 {{ rank.ranking_num || 0 }} 名</div>
              </div>
            </div>
          </div>
        </TabsContent>

        <TabsContent value="comments">
          <div class="comment-input">
            <textarea placeholder="发表评论..." disabled />
            <button disabled>发布评论</button>
            <div class="comment-input-hint">暂未开放评论发布</div>
          </div>

          <Tabs
            v-if="type === 'music'"
            :model-value="activeCommentTab"
            @update:model-value="handleCommentTabChange"
          >
            <div class="comment-sub-tabs">
              <TabsList class="comment-sub-list">
                <TabsTrigger value="hot">精彩评论</TabsTrigger>
                <TabsTrigger value="all">全部评论</TabsTrigger>
                <TabsTrigger value="classify">分类评论</TabsTrigger>
                <TabsTrigger value="hotword">热词评论</TabsTrigger>
              </TabsList>
            </div>

            <TabsContent value="hot">
              <div v-if="hotComments.some((item) => item.isStar)" class="comment-section-title">歌手说</div>
              <CommentList
                :comments="hotComments.filter((item) => item.isStar)"
                :loading="isLoadingComments"
                :onTapReplies="openFloor"
              />
              <div v-if="hotComments.some((item) => item.isHot)" class="comment-section-title">热门评论</div>
              <CommentList
                :comments="hotComments.filter((item) => item.isHot)"
                :loading="isLoadingComments"
                :onTapReplies="openFloor"
              />
              <div v-if="comments.length" class="comment-section-title">最新评论</div>
              <CommentList :comments="comments" :loading="isLoadingComments" :onTapReplies="openFloor" />
            </TabsContent>
            <TabsContent value="all">
              <CommentList :comments="comments" :loading="isLoadingComments" :onTapReplies="openFloor" />
              <div v-if="hasMore" class="comment-load-more">
                <button @click="fetchComments()" :disabled="isLoadingComments">{{ isLoadingComments ? '加载中...' : '加载更多' }}</button>
              </div>
            </TabsContent>
            <TabsContent value="classify">
              <div class="comment-chip-row">
                <button
                  v-for="item in classifyList"
                  :key="item.id"
                  :class="['comment-chip', selectedClassify === item.id && 'is-active']"
                  @click="selectedClassify = item.id; fetchClassifyComments(true)"
                >
                  {{ item.name }}
                </button>
              </div>
              <CommentList :comments="classifyComments" :loading="isLoadingClassify" :onTapReplies="openFloor" />
              <div v-if="hasMoreClassify" class="comment-load-more">
                <button @click="fetchClassifyComments()" :disabled="isLoadingClassify">{{ isLoadingClassify ? '加载中...' : '加载更多' }}</button>
              </div>
            </TabsContent>
            <TabsContent value="hotword">
              <div class="comment-chip-row">
                <button
                  v-for="item in hotwordList"
                  :key="item.content"
                  :class="['comment-chip', selectedHotword === item.content && 'is-active']"
                  @click="selectedHotword = item.content; fetchHotwordComments(true)"
                >
                  {{ item.content }}
                </button>
              </div>
              <CommentList :comments="hotwordComments" :loading="isLoadingHotword" :onTapReplies="openFloor" />
              <div v-if="hasMoreHotword" class="comment-load-more">
                <button @click="fetchHotwordComments()" :disabled="isLoadingHotword">{{ isLoadingHotword ? '加载中...' : '加载更多' }}</button>
              </div>
            </TabsContent>
          </Tabs>

          <template v-else>
            <CommentList :comments="hotComments" :loading="isLoadingComments" :onTapReplies="openFloor" />
            <div class="comment-section-title">最新评论</div>
            <CommentList :comments="comments" :loading="isLoadingComments" :onTapReplies="openFloor" />
            <div v-if="hasMore" class="comment-load-more">
              <button @click="fetchComments()" :disabled="isLoadingComments">{{ isLoadingComments ? '加载中...' : '加载更多' }}</button>
            </div>
          </template>
        </TabsContent>
      </Tabs>
    </div>

    <Drawer v-model:open="showFloor" side="bottom" panelClass="comment-floor">
      <div class="comment-floor-header">
        <div class="comment-floor-title">楼层评论</div>
        <button class="comment-floor-close" @click="showFloor = false">
          <Icon :icon="iconX" width="16" height="16" />
        </button>
      </div>
      <div class="comment-floor-body" ref="floorBodyRef" @scroll="handleFloorScroll">
        <div class="comment-floor-section">原评论</div>
        <CommentList
          v-if="activeFloorComment"
          :comments="[activeFloorComment]"
          :showDivider="false"
          :loading="false"
        />
        <div class="comment-floor-section">
          回复{{ floorTotal > 0 ? ` (${floorTotal})` : '' }}
        </div>
        <CommentList :comments="floorReplies" :loading="floorLoading" :showDivider="true" />
        <div v-if="!floorLoading && floorReplies.length === 0" class="comment-floor-empty">
          {{ floorMessage || '暂无回复' }}
        </div>
        <div v-if="floorHasMore" class="comment-load-more">
          <button @click="fetchFloorReplies()" :disabled="floorLoading">
            {{ floorLoading ? '加载中...' : floorLoadMoreMessage || '加载更多' }}
          </button>
        </div>
      </div>
    </Drawer>

    <BackToTop target-selector=".comment-content-wrap" :threshold="360" />
  </div>
</template>

<style scoped>
@reference "@/style.css";

.comment-page {
  padding: 0 24px 40px;
}

.comment-header {
  position: sticky;
  top: 0;
  z-index: 120;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 20px 0 10px;
  background: var(--color-bg-main);
}

.comment-back {
  width: 36px;
  height: 36px;
  border-radius: 12px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: var(--color-bg-card);
  border: 1px solid var(--color-border-light);
  color: var(--color-text-main);
}

.comment-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--color-text-main);
  display: flex;
  align-items: center;
  gap: 6px;
}

.comment-title-count {
  font-size: 12px;
  color: var(--color-text-secondary);
}

.comment-spacer {
  flex: 1;
}

.comment-content-wrap {
  max-width: 860px;
  margin: 0 auto;
  max-height: calc(100vh - 120px);
  overflow-y: auto;
  padding-bottom: 40px;
}

.comment-main-tabs,
.comment-sub-tabs {
  margin-top: 16px;
  margin-bottom: 16px;
}

.comment-main-list,
.comment-sub-list {
  background: color-mix(in srgb, var(--color-text-main) 8%, transparent);
  padding: 4px;
  border-radius: 12px;
  border: 1px solid color-mix(in srgb, var(--color-text-main) 12%, transparent);
}

.comment-input {
  position: relative;
  margin: 12px 0 24px;
}

.comment-input textarea {
  width: 100%;
  min-height: 96px;
  padding: 16px 18px;
  border-radius: 18px;
  border: 1px solid transparent;
  background: color-mix(in srgb, var(--color-text-main) 4%, transparent);
  color: var(--color-text-main);
  font-size: 14px;
  outline: none;
  resize: none;
}

.comment-input textarea:disabled {
  opacity: 0.6;
}

.comment-input button {
  position: absolute;
  right: 14px;
  bottom: 12px;
  padding: 6px 16px;
  border-radius: 999px;
  background: var(--color-primary);
  color: #fff;
  font-size: 12px;
  font-weight: 600;
}

.comment-input button:disabled {
  opacity: 0.6;
}

.comment-input-hint {
  position: absolute;
  right: 16px;
  top: 10px;
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.comment-chip-row {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
  margin-bottom: 16px;
}

.comment-chip {
  padding: 6px 12px;
  border-radius: 999px;
  border: 1px solid color-mix(in srgb, var(--color-text-main) 12%, transparent);
  background: var(--color-bg-card);
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.comment-chip.is-active {
  color: var(--color-primary);
  border-color: color-mix(in srgb, var(--color-primary) 40%, transparent);
  background: color-mix(in srgb, var(--color-primary) 12%, transparent);
}

.comment-section-title {
  margin: 20px 0 10px;
  font-size: 13px;
  font-weight: 700;
  color: var(--color-text-secondary);
}

.comment-load-more {
  display: flex;
  justify-content: center;
  margin: 20px 0 32px;
}

.comment-load-more button {
  padding: 8px 24px;
  border-radius: 999px;
  border: 1px solid var(--color-border-light);
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-main);
  background: var(--color-bg-card);
}

.comment-detail-empty {
  padding: 40px 0;
  text-align: center;
  color: var(--color-text-secondary);
}

.song-detail-panel {
  display: flex;
  gap: 20px;
  padding: 20px;
  border-radius: 20px;
  background: color-mix(in srgb, var(--color-text-main) 4%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-text-main) 10%, transparent);
  align-items: center;
}

.song-detail-cover {
  flex-shrink: 0;
}

.song-detail-info {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.song-detail-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--color-text-main);
}

.song-detail-sub {
  font-size: 14px;
  font-weight: 600;
  color: var(--color-text-secondary);
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.song-detail-meta {
  font-size: 12px;
  color: color-mix(in srgb, var(--color-text-main) 55%, transparent);
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
}

.detail-section {
  margin-top: 24px;
  padding: 20px;
  border-radius: 20px;
  background: color-mix(in srgb, var(--color-text-main) 4%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-text-main) 8%, transparent);
}

.detail-block + .detail-block {
  margin-top: 16px;
}

.detail-title {
  font-size: 14px;
  font-weight: 700;
  color: var(--color-text-main);
  margin-bottom: 12px;
}

.detail-tags {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.detail-tag {
  padding: 6px 12px;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-primary);
  background: color-mix(in srgb, var(--color-primary) 12%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-primary) 24%, transparent);
}

.detail-summary {
  font-size: 12px;
  color: var(--color-text-secondary);
  margin-bottom: 12px;
}

.ranking-list {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.ranking-card {
  padding: 18px;
  border-radius: 18px;
  background: color-mix(in srgb, var(--color-text-main) 6%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-text-main) 10%, transparent);
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.ranking-title {
  font-size: 16px;
  font-weight: 700;
  color: var(--color-text-main);
}

.ranking-meta {
  display: flex;
  gap: 20px;
  font-size: 12px;
  color: var(--color-text-secondary);
  flex-wrap: wrap;
}

.ranking-rank {
  font-size: 18px;
  font-weight: 700;
  color: var(--color-text-main);
}

:global(.comment-floor) {
  padding: 0;
  border-radius: 24px;
  overflow: hidden;
}

.comment-floor-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px 8px;
}

.comment-floor-title {
  font-size: 16px;
  font-weight: 700;
  color: var(--color-text-main);
}

.comment-floor-close {
  width: 32px;
  height: 32px;
  border-radius: 999px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: var(--color-bg-card);
  border: 1px solid var(--color-border-light);
}

.comment-floor-body {
  padding: 0 16px 16px;
  max-height: 72vh;
  overflow-y: auto;
}

.comment-floor-empty {
  padding: 16px 0 24px;
  text-align: center;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-secondary);
}

.comment-floor-section {
  margin: 12px 0 8px;
  font-size: 12px;
  font-weight: 700;
  color: var(--color-text-secondary);
}
</style>
