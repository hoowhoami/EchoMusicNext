<script setup lang="ts">
import { useRouter, useRoute } from 'vue-router';
import { computed, onMounted, watch, ref } from 'vue';
import { useUserStore } from '../stores/user';
import { usePlaylistStore } from '../stores/playlist';
import Avatar from '../components/ui/Avatar.vue';
import Cover from '../components/ui/Cover.vue';
import type { PlaylistMeta } from '../utils/mappers';

const router = useRouter();
const route = useRoute();
const userStore = useUserStore();
const playlistStore = usePlaylistStore();

const isMac = computed(() => window.electron.platform === 'darwin');
const isLoggedIn = computed(() => userStore.isLoggedIn);
const userInfo = computed(() => userStore.info);

// 歌单页签状态：0 为自建，1 为收藏
const activePlaylistTab = ref(0);

// 歌单分类逻辑 (复刻 Flutter UserProvider)
const createdPlaylists = computed(() =>
  playlistStore.userPlaylists.filter((p) => p.listCreateUserid === userInfo.value?.userid),
);

const favoritedPlaylists = computed(() =>
  playlistStore.userPlaylists.filter(
    (p) => p.listCreateUserid !== userInfo.value?.userid && p.source !== 2,
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
      { name: '我最喜爱', path: '/main/collection', icon: 'heart' },
    ],
  },
];

const navigateTo = (path: string) => {
  router.push(path);
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

// 图标渲染映射 (Lucide 风格)
const getIcon = (name: string) => {
  if (name === 'sparkles')
    return '<path d="m12 3-1.912 5.813a2 2 0 0 1-1.275 1.275L3 12l5.813 1.912a2 2 0 0 1 1.275 1.275L12 21l1.912-5.813a2 2 0 0 1 1.275-1.275L21 12l-5.813-1.912a2 2 0 0 1-1.275-1.275L12 3Z"/>';
  if (name === 'compass')
    return '<circle cx="12" cy="12" r="10"/><path d="m16.24 7.76-2.12 6.36-6.36 2.12 2.12-6.36 6.36-2.12z"/>';
  if (name === 'search') return '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>';
  if (name === 'clock') return '<circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>';
  if (name === 'cloud')
    return '<path d="M17.5 19c2.5 0 4.5-2 4.5-4.5 0-2.3-1.7-4.2-3.9-4.5C17.6 6.6 14.2 4 10.2 4 6.8 4 3.9 6.2 3.1 9.4 1.3 10.2 0 11.9 0 14c0 2.8 2.2 5 5 5h12.5z"/>';
  if (name === 'heart')
    return '<path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/>';
  return '';
};

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
  { immediate: true }
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
          <svg
            width="19"
            height="19"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2.2"
            stroke-linecap="round"
            stroke-linejoin="round"
          >
            <path
              d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"
            />
            <circle cx="12" cy="12" r="3" />
          </svg>
        </button>
      </div>
    </div>

    <!-- 固定区域 3: 基础菜单 -->
    <div class="px-4 shrink-0 no-drag">
      <div v-for="group in menuGroups" :key="group.title" class="mb-4">
        <h2
          class="px-3.5 text-[11px] font-semibold text-text-main/40 uppercase tracking-[0.5px] mb-2"
        >
          {{ group.title }}
        </h2>
        <nav class="space-y-0.5">
          <button
            v-for="item in group.items"
            :key="item.path"
            @click="navigateTo(item.path)"
            :class="[
              'w-full flex items-center gap-3.5 px-3.5 py-2 rounded-[14px] transition-all duration-200 group cursor-pointer active:scale-[0.98]',
              route.path === item.path
                ? 'bg-primary/[0.12] text-primary'
                : 'text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]',
            ]"
          >
            <svg
              width="18"
              height="18"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2.2"
              stroke-linecap="round"
              stroke-linejoin="round"
              :class="[
                route.path === item.path
                  ? 'text-primary'
                  : 'text-text-main opacity-60 group-hover:opacity-100',
              ]"
              v-html="getIcon(item.icon)"
            ></svg>
            <span
              class="text-[14px]"
              :class="[route.path === item.path ? 'font-semibold' : 'font-normal']"
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
              : 'font-normal text-text-main opacity-40 hover:opacity-60',
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
              : 'font-normal text-text-main opacity-40 hover:opacity-60',
          ]"
        >
          收藏歌单/专辑
        </button>
      </div>
      <button
        :class="[
          'p-1 hover:bg-black/[0.05] dark:hover:bg-white/[0.05] rounded-md transition-all text-text-main opacity-40 shrink-0',
          activePlaylistTab === 0 ? 'visible opacity-40' : 'invisible opacity-0',
        ]"
      >
        <svg
          width="14"
          height="14"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2.5"
        >
          <path d="M12 5v14M5 12h14" />
        </svg>
      </button>
    </div>

    <!-- 滚动区域: 歌单列表 -->
    <div class="flex-1 overflow-y-auto px-4 pb-6 no-scrollbar no-drag">
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
              >{{ p.name }}</span>
            </div>
          </button>
          <div
            v-if="createdPlaylists.length === 0"
            class="py-8 text-center opacity-20 text-[12px] italic"
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
              >{{ p.name }}</span>
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
              >{{ a.name }}</span>
            </div>
          </button>
          <div
            v-if="favoritedPlaylists.length === 0 && favoritedAlbums.length === 0"
            class="py-8 text-center opacity-20 text-[12px] italic"
          >
            暂无收藏内容
          </div>
        </template>
      </nav>

      <!-- 未登录空状态 -->
      <div v-if="!isLoggedIn" class="px-3.5 py-8 text-center">
        <span class="text-[12px] font-normal text-text-main opacity-30 italic"
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
