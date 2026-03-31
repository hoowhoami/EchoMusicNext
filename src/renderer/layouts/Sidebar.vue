<script setup lang="ts">
import { useRouter, useRoute } from 'vue-router';
import { computed, onMounted, watch, ref } from 'vue';
import { useUserStore } from '@/stores/user';
import { usePlaylistStore } from '@/stores/playlist';
import Avatar from '@/components/ui/Avatar.vue';
import Cover from '@/components/ui/Cover.vue';
import type { PlaylistMeta } from '@/utils/mappers';
import {
  iconSparkles,
  iconCompass,
  iconSearch,
  iconClock,
  iconCloud,
  iconHeart,
  iconSettings,
  iconPlus,
} from '@/icons';

const router = useRouter();
const route = useRoute();
const userStore = useUserStore();
const playlistStore = usePlaylistStore();

const isMac = computed(() => window.electron.platform === 'darwin');
const isLoggedIn = computed(() => userStore.isLoggedIn);
const userInfo = computed(() => userStore.info);

// 歌单页签状态：0 为自建，1 为收藏
const activePlaylistTab = ref(0);

const likedPlaylistId = computed(() => String(playlistStore.likedPlaylistId ?? ''));

const isLikedPlaylist = (playlist: PlaylistMeta) => {
  const likedId = likedPlaylistId.value;
  if (!likedId) return false;

  const candidateIds = [
    playlist.id,
    playlist.listid,
    playlist.listCreateGid,
    playlist.globalCollectionId,
    playlist.listCreateListid,
  ]
    .filter((value) => value !== undefined && value !== null && String(value) !== '')
    .map((value) => String(value));

  return candidateIds.includes(likedId);
};

const createdPlaylists = computed(() =>
  playlistStore.userPlaylists.filter(
    (p) => p.listCreateUserid === userInfo.value?.userid && !isLikedPlaylist(p),
  ),
);

const favoritedPlaylists = computed(() =>
  playlistStore.userPlaylists.filter(
    (p) => p.listCreateUserid !== userInfo.value?.userid && p.source !== 2 && !isLikedPlaylist(p),
  ),
);

const favoritedAlbums = computed(() => playlistStore.userPlaylists.filter((p) => p.source === 2));

const menuGroups = [
  {
    title: '发现音乐',
    items: [
      { name: '为您推荐', path: '/main/home', icon: 'sparkles' },
      { name: '探索发现', path: '/main/explore', icon: 'compass' },
      { name: '全网搜索', path: '/main/search', icon: 'search' },
    ],
  },
  {
    title: '我的乐库',
    items: [
      { name: '播放历史', path: '/main/history', icon: 'clock' },
      { name: '我的云盘', path: '/main/cloud', icon: 'cloud' },
      { name: '我最喜爱', path: '/main/home', icon: 'heart', action: 'liked-playlist' },
    ],
  },
];

const navigateTo = (path: string) => {
  router.push(path);
};

const likedPlaylistRouteId = computed(() => {
  const likedPlaylist = playlistStore.likedPlaylist;
  if (!likedPlaylist) return '';
  return getPlaylistRouteId(likedPlaylist);
});

const navigateToLikedPlaylist = async () => {
  if (!isLoggedIn.value) {
    await router.push('/login');
    return;
  }

  let likedPlaylist = playlistStore.likedPlaylist;
  if (!likedPlaylist) {
    await playlistStore.fetchUserPlaylists();
    likedPlaylist = playlistStore.likedPlaylist;
  }

  if (!likedPlaylist) return;
  navigateToPlaylist(likedPlaylist);
};

const isMenuItemDisabled = (item: { path: string; action?: string }) => {
  return item.action === 'liked-playlist' && !isLoggedIn.value;
};

const handleMenuClick = (item: { path: string; action?: string }) => {
  if (isMenuItemDisabled(item)) return;
  if (item.action === 'liked-playlist') {
    void navigateToLikedPlaylist();
    return;
  }
  navigateTo(item.path);
};

const isMenuItemActive = (item: { path: string; action?: string }) => {
  if (item.action === 'liked-playlist') {
    return (
      route.name === 'playlist-detail' && activePlaylistRouteId.value === likedPlaylistRouteId.value
    );
  }
  return route.path === item.path;
};

const isOwnerPlaylist = (playlist: PlaylistMeta) => {
  const currentUserId = userStore.info?.userid;
  return !!currentUserId && playlist.listCreateUserid === currentUserId;
};

const getPlaylistRouteId = (playlist: PlaylistMeta) => {
  if (playlist.source === 2) {
    return String(playlist.listCreateListid ?? playlist.id);
  }
  if (isOwnerPlaylist(playlist)) {
    return String(playlist.globalCollectionId ?? playlist.id);
  }
  return String(playlist.listCreateGid ?? playlist.globalCollectionId ?? playlist.id);
};

const activePlaylistRouteId = computed(() => {
  return route.name === 'playlist-detail' ? String(route.params.id ?? '') : '';
});

const activeAlbumRouteId = computed(() => {
  return route.name === 'album-detail' ? String(route.params.id ?? '') : '';
});

const findPlaylistByRouteId = (routeId: string, source?: number) => {
  if (!routeId) return undefined;
  return playlistStore.userPlaylists.find((playlist) => {
    if (source !== undefined && playlist.source !== source) return false;
    return getPlaylistRouteId(playlist) === routeId;
  });
};

const isActivePlaylist = (playlist: PlaylistMeta) => {
  if (playlist.source === 2) return false;
  return getPlaylistRouteId(playlist) === activePlaylistRouteId.value;
};

const isActiveAlbum = (playlist: PlaylistMeta) => {
  if (playlist.source !== 2) return false;
  return getPlaylistRouteId(playlist) === activeAlbumRouteId.value;
};

const navigateToPlaylist = (playlist: PlaylistMeta) => {
  if (playlist.source === 2) {
    const id = getPlaylistRouteId(playlist);
    if (route.name === 'album-detail' && activeAlbumRouteId.value === id) return;
    router.push({ name: 'album-detail', params: { id } });
  } else {
    const id = getPlaylistRouteId(playlist);
    if (route.name === 'playlist-detail' && activePlaylistRouteId.value === id) return;
    router.push({
      name: 'playlist-detail',
      params: { id },
      query: { type: isOwnerPlaylist(playlist) ? 'user' : 'special' },
    });
  }
};

const iconMap = {
  sparkles: iconSparkles,
  compass: iconCompass,
  search: iconSearch,
  clock: iconClock,
  cloud: iconCloud,
  heart: iconHeart,
} as const;

// 初始加载和登录状态监听
const syncCloudData = () => {
  if (isLoggedIn.value) {
    userStore.fetchUserInfo();
    playlistStore.fetchUserPlaylists();
  }
};

onMounted(() => {
  syncCloudData();
});

watch(isLoggedIn, (val) => {
  if (val) {
    syncCloudData();
  } else {
    playlistStore.userPlaylists = [];
  }
});

watch(
  () => [route.name, route.params.id, playlistStore.userPlaylists.length, userStore.info?.userid],
  () => {
    if (route.name === 'playlist-detail') {
      const currentId = activePlaylistRouteId.value;
      const matched = findPlaylistByRouteId(currentId) || findPlaylistByRouteId(currentId, 1);
      if (matched) {
        activePlaylistTab.value = isOwnerPlaylist(matched) ? 0 : 1;
      }
      return;
    }

    if (route.name === 'album-detail') {
      const currentId = activeAlbumRouteId.value;
      const matched = findPlaylistByRouteId(currentId, 2);
      if (matched) {
        activePlaylistTab.value = 1;
      }
    }
  },
  { immediate: true },
);
</script>

<template>
  <aside
    class="sidebar h-full flex flex-col bg-bg-sidebar border-r border-border-light select-none transition-all duration-300 relative"
  >
    <!-- 固定区域 1: 顶部拖拽区 -->
    <div :class="['drag w-full shrink-0', isMac ? 'h-14' : 'h-10']"></div>

    <!-- 固定区域 2: 用户卡片 -->
    <div :class="['px-4 pb-3 shrink-0 no-drag', isMac ? 'mt-2' : 'mt-0']">
      <div
        class="user-info-card flex items-center bg-bg-info-card border border-black/[0.08] dark:border-white/10 rounded-[20px] p-1 transition-all duration-200"
      >
        <div
          @click="navigateTo(isLoggedIn ? '/main/profile' : '/login')"
          class="flex-1 flex items-center gap-3 p-1.5 rounded-[14px] hover:bg-black/[0.04] dark:hover:bg-white/[0.04] cursor-pointer transition-all active:scale-[0.98]"
        >
          <div
            class="w-[34px] h-[34px] shrink-0 rounded-full bg-primary/10 flex items-center justify-center overflow-hidden"
          >
            <Avatar :src="isLoggedIn ? userInfo?.pic : ''" class="w-full h-full" />
          </div>
          <div class="flex flex-col min-w-0">
            <span
              class="text-[13px] font-semibold text-text-main truncate leading-tight tracking-tight"
            >
              {{ isLoggedIn ? userInfo?.nickname : '未登录' }}
            </span>
            <span class="text-[9px] text-text-secondary font-medium opacity-60 tracking-wider">
              {{ isLoggedIn ? `Lv.${userInfo?.p_grade || 0}` : '点击登录账号' }}
            </span>
          </div>
        </div>
        <div class="w-[1px] h-[22px] bg-black/[0.1] dark:bg-white/[0.1] mx-1.5"></div>
        <!-- 2.3 设置按钮 -->
        <button
          @click="navigateTo('/main/settings')"
          class="p-2 mr-1 rounded-[14px] hover:bg-black/[0.04] dark:hover:bg-white/[0.04] text-text-secondary transition-all active:scale-90"
        >
          <Icon :icon="iconSettings" width="19" height="19" />
        </button>
      </div>
    </div>

    <!-- 固定区域 3: 基础菜单 -->
    <div class="px-4 shrink-0 no-drag">
      <div v-for="group in menuGroups" :key="group.title" class="mb-4">
        <h2
          class="px-3.5 text-[11px] font-semibold text-text-main/60 uppercase tracking-[0.5px] mb-2"
        >
          {{ group.title }}
        </h2>
        <nav class="space-y-0.5">
          <button
            v-for="item in group.items"
            :key="item.path"
            @click="handleMenuClick(item)"
            :disabled="isMenuItemDisabled(item)"
            :title="isMenuItemDisabled(item) ? '登录后可用' : undefined"
            :class="[
              'w-full flex items-center gap-3.5 px-3.5 py-2 rounded-[14px] transition-all duration-200 group active:scale-[0.98]',
              isMenuItemDisabled(item)
                ? 'cursor-not-allowed opacity-35 text-text-main/55'
                : isMenuItemActive(item)
                  ? 'cursor-pointer bg-primary/[0.12] text-primary'
                  : 'cursor-pointer text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]',
            ]"
          >
            <Icon
              width="18"
              height="18"
              :icon="iconMap[item.icon as keyof typeof iconMap]"
              :class="[
                isMenuItemDisabled(item)
                  ? 'text-text-main opacity-40'
                  : isMenuItemActive(item)
                    ? 'text-primary'
                    : 'text-text-main opacity-60 group-hover:opacity-100',
              ]"
            />
            <span
              class="text-[14px]"
              :class="[isMenuItemActive(item) ? 'font-semibold' : 'font-normal']"
              >{{ item.name }}</span
            >
          </button>
        </nav>
      </div>
    </div>

    <!-- 滚动区域标题: 歌单分类页签 (固定在滚动区上方) -->
    <div class="pl-7.5 pr-4 h-7 flex items-center justify-between mb-2 shrink-0 no-drag mt-2">
      <div class="flex items-center gap-2">
        <button
          @click="activePlaylistTab = 0"
          :class="[
            'text-[11px] uppercase tracking-[0.3px] transition-all duration-200 whitespace-nowrap',
            activePlaylistTab === 0
              ? 'font-bold text-primary'
              : 'font-normal text-text-main opacity-60 hover:opacity-80',
          ]"
        >
          自建歌单
        </button>
        <div class="w-[1px] h-2.5 bg-black/[0.15] dark:bg-white/[0.15] mx-0.5"></div>
        <button
          @click="activePlaylistTab = 1"
          :class="[
            'text-[11px] uppercase tracking-[0.3px] transition-all duration-200 whitespace-nowrap',
            activePlaylistTab === 1
              ? 'font-bold text-primary'
              : 'font-normal text-text-main opacity-60 hover:opacity-80',
          ]"
        >
          收藏歌单/专辑
        </button>
      </div>
      <button
        :class="[
          'p-1 hover:bg-black/[0.05] dark:hover:bg-white/[0.05] rounded-md transition-all text-text-main opacity-60 shrink-0',
          activePlaylistTab === 0 ? 'visible opacity-60' : 'invisible opacity-0',
        ]"
      >
        <Icon :icon="iconPlus" width="14" height="14" />
      </button>
    </div>

    <!-- 滚动区域: 歌单列表 -->
    <div class="flex-1 overflow-y-auto px-4 pb-6 no-drag sidebar-scroll">
      <nav v-if="isLoggedIn" class="space-y-0.5">
        <!-- 渲染自建歌单 -->
        <template v-if="activePlaylistTab === 0">
          <button
            v-for="p in createdPlaylists"
            :key="p.listid || p.id"
            @click="navigateToPlaylist(p)"
            :class="[
              'w-full flex items-center gap-3 px-3.5 py-1.5 rounded-[12px] group cursor-pointer active:scale-[0.98] transition-all',
              isActivePlaylist(p)
                ? 'bg-primary/[0.12] text-primary'
                : 'text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]',
            ]"
          >
            <Cover
              :url="p.pic"
              :size="100"
              :width="28"
              :height="28"
              :borderRadius="6"
              class="shrink-0"
            />
            <div class="flex flex-col items-start min-w-0">
              <span
                :class="[
                  'text-[13px] truncate w-full font-medium tracking-tight',
                  isActivePlaylist(p) ? 'text-primary' : 'text-text-main/90',
                ]"
                >{{ p.name }}</span
              >
            </div>
          </button>
          <div
            v-if="createdPlaylists.length === 0"
            class="py-8 text-center opacity-40 text-[12px] italic"
          >
            暂无自建歌单
          </div>
        </template>

        <!-- 渲染收藏歌单 + 收藏专辑 -->
        <template v-else>
          <button
            v-for="p in favoritedPlaylists"
            :key="p.listid || p.id"
            @click="navigateToPlaylist(p)"
            :class="[
              'w-full flex items-center gap-3 px-3.5 py-1.5 rounded-[12px] group cursor-pointer active:scale-[0.98] transition-all',
              isActivePlaylist(p)
                ? 'bg-primary/[0.12] text-primary'
                : 'text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]',
            ]"
          >
            <Cover
              :url="p.pic"
              :size="100"
              :width="28"
              :height="28"
              :borderRadius="6"
              class="shrink-0"
            />
            <div class="flex flex-col items-start min-w-0">
              <span
                :class="[
                  'text-[13px] truncate w-full font-medium tracking-tight',
                  isActivePlaylist(p) ? 'text-primary' : 'text-text-main/90',
                ]"
                >{{ p.name }}</span
              >
            </div>
          </button>

          <div
            v-if="favoritedAlbums.length > 0"
            class="my-2.5 mx-3.5 h-[1px] bg-black/[0.05] dark:bg-white/[0.05]"
          ></div>

          <button
            v-for="a in favoritedAlbums"
            :key="a.listid || a.id"
            @click="navigateToPlaylist(a)"
            :class="[
              'w-full flex items-center gap-3 px-3.5 py-1.5 rounded-[12px] group cursor-pointer active:scale-[0.98] transition-all',
              isActiveAlbum(a)
                ? 'bg-primary/[0.12] text-primary'
                : 'text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]',
            ]"
          >
            <Cover
              :url="a.pic"
              :size="100"
              :width="28"
              :height="28"
              :borderRadius="6"
              class="shrink-0"
            />
            <div class="flex flex-col items-start min-w-0">
              <span
                :class="[
                  'text-[13px] truncate w-full font-medium tracking-tight',
                  isActiveAlbum(a) ? 'text-primary' : 'text-text-main/90',
                ]"
                >{{ a.name }}</span
              >
            </div>
          </button>
          <div
            v-if="favoritedPlaylists.length === 0 && favoritedAlbums.length === 0"
            class="py-8 text-center opacity-40 text-[12px] italic"
          >
            暂无收藏内容
          </div>
        </template>
      </nav>

      <!-- 未登录空状态 -->
      <div v-if="!isLoggedIn" class="px-3.5 py-8 text-center">
        <span class="text-[12px] font-normal text-text-main opacity-50 italic"
          >登录同步云端歌单</span
        >
      </div>
    </div>
  </aside>
</template>

<style scoped>
.sidebar {
  width: 230px;
}

/* 隐藏滚动条但保留功能 */
.no-scrollbar::-webkit-scrollbar {
  display: none;
}
.no-scrollbar {
  -ms-overflow-style: none;
  scrollbar-width: none;
}
</style>
