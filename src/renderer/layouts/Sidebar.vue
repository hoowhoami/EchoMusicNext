<script setup lang="ts">
import { useRouter, useRoute } from 'vue-router';
import { computed } from 'vue';
import { useUserStore } from '../stores/user';
import Avatar from '../components/ui/Avatar.vue';

const router = useRouter();
const route = useRoute();
const userStore = useUserStore();

const isMac = computed(() => window.electron.platform === 'darwin');
const isLoggedIn = computed(() => userStore.isLoggedIn);
const userInfo = computed(() => userStore.info);

const menuGroups = [
  {
    title: '发现音乐',
    items: [
      { name: '为您推荐', path: '/main/home', icon: 'rocket' },
      { name: '探索发现', path: '/main/explore', icon: 'compass' },
      { name: '全网搜索', path: '/main/search', icon: 'search' },
    ]
  },
  {
    title: '我的乐库',
    items: [
      { name: '播放历史', path: '/main/history', icon: 'clock' },
      { name: '我的云盘', path: '/main/cloud', icon: 'cloud' },
      { name: '我最喜爱', path: '/main/collection', icon: 'heart' },
    ]
  }
];

const navigateTo = (path: string) => {
  console.log('[Sidebar] Navigating to:', path);
  router.push(path);
};

// 图标渲染映射
const getIcon = (name: string) => {
  if (name === 'rocket') return '<path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.71-2.13.71-2.13l-4.42-.87zM21 3s-9 0-12 9c-1.2 3.6 0 7 0 7l3-3c0 0-1-1 0-3s3-3 3-3l3-3s1 1 3 0l-3-3z"/>';
  if (name === 'compass') return '<circle cx="12" cy="12" r="10"/><path d="m16.24 7.76-2.12 6.36-6.36 2.12 2.12-6.36 6.36-2.12z"/>';
  if (name === 'search') return '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>';
  if (name === 'clock') return '<circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>';
  if (name === 'cloud') return '<path d="M17.5 19c2.5 0 4.5-2 4.5-4.5 0-2.3-1.7-4.2-3.9-4.5C17.6 6.6 14.2 4 10.2 4 6.8 4 3.9 6.2 3.1 9.4 1.3 10.2 0 11.9 0 14c0 2.8 2.2 5 5 5h12.5z"/>';
  if (name === 'heart') return '<path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/>';
  return '';
};
</script>

<template>
  <aside class="sidebar h-full flex flex-col bg-bg-sidebar border-r border-border-light select-none transition-all duration-300 relative">
    
    <!-- 1. 顶部拖拽区 (增加高度以适配 macOS 红绿灯) -->
    <div :class="['drag w-full shrink-0', isMac ? 'h-14' : 'h-10']"></div>
    
    <!-- 2. 用户信息 card (1:1 复刻 EchoMusic style) -->
    <div :class="['px-4 pb-3 shrink-0 no-drag', isMac ? 'mt-2' : 'mt-0']">
      <div class="user-info-card flex items-center bg-bg-info-card border border-black/[0.08] dark:border-white/10 rounded-[20px] p-1 transition-all duration-200">
        <!-- 2.1 头像 & 昵称 -->
        <div 
          @click="navigateTo(isLoggedIn ? '/main/profile' : '/login')"
          class="flex-1 flex items-center gap-3 p-1.5 rounded-[14px] hover:bg-black/[0.04] dark:hover:bg-white/[0.04] cursor-pointer transition-all active:scale-[0.98]"
        >
          <div class="w-[34px] h-[34px] shrink-0 rounded-full bg-primary/10 flex items-center justify-center overflow-hidden">
             <Avatar :src="isLoggedIn ? userInfo?.pic : ''" class="w-full h-full" />
          </div>
          <div class="flex flex-col min-w-0">
            <span class="text-[13px] font-semibold text-text-main truncate leading-tight tracking-tight">
              {{ isLoggedIn ? userInfo?.nickname : '未登录' }}
            </span>
            <span class="text-[9px] text-text-secondary font-medium opacity-60 tracking-wider">
              {{ isLoggedIn ? `Lv.${userInfo?.p_grade || 0}` : '点击登录账号' }}
            </span>
          </div>
        </div>

        <!-- 2.2 垂直分割线 (Flutter withAlpha(40)) -->
        <div class="w-[1px] h-[22px] bg-black/[0.1] dark:bg-white/[0.1] mx-1.5"></div>

        <!-- 2.3 设置按钮 -->
        <button 
          @click="navigateTo('/main/settings')"
          class="p-2 mr-1 rounded-[14px] hover:bg-black/[0.04] dark:hover:bg-white/[0.04] text-text-secondary transition-all active:scale-90"
        >
          <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M12 8c1.1 0 2 .9 2 2s-.9 2-2 2-2-.9-2-2 .9-2 2-2zm0-6c-1.1 0-2 .9-2 2v.1c-1 .3-1.9.8-2.6 1.4l-.1-.1c-.8-.8-2.1-.8-2.9 0-.8.8-.8 2.1 0 2.9l.1.1c-.6.7-1.1 1.6-1.4 2.6H3c-1.1 0-2 .9-2 2s.9 2 2 2h.1c.3 1 .8 1.9 1.4 2.6l-.1.1c-.8.8-.8 2.1 0 2.9.8.8 2.1.8 2.9 0l.1-.1c.7.6 1.6 1.1 2.6 1.4v.1c0 1.1.9 2 2 2s2-.9 2-2v-.1c1-.3 1.9-.8 2.6-1.4l.1.1c.8.8 2.1.8 2.9 0 .8-.8.8-2.1 0-2.9l-.1-.1c.6-.7 1.1-1.6 1.4-2.6h.1c1.1 0 2-.9 2-2s-.9-2-2-2h-.1c-.3-1-.8-1.9-1.4-2.6l.1-.1c.8-.8.8-2.1 0-2.9-.8-.8-2.1-.8-2.9 0l-.1.1c-.7-.6-1.6-1.1-2.6-1.4V4c0-1.1-.9-2-2-2z"/></svg>
        </button>
      </div>
    </div>

    <!-- 3. 菜单列表 (严格复刻 Padding and Spacing) -->
    <div class="flex-1 overflow-y-auto px-4 space-y-5.5 no-scrollbar pt-3 no-drag">
      <div v-for="group in menuGroups" :key="group.title">
        <h2 class="px-3.5 text-[11px] font-semibold text-text-main/40 uppercase tracking-[0.5px] mb-2.5">
          {{ group.title }}
        </h2>
        <nav class="space-y-0.5">
          <button
            v-for="item in group.items"
            :key="item.path"
            @click="navigateTo(item.path)"
            :class="[
              'w-full flex items-center gap-3.5 px-3.5 py-3 rounded-[14px] transition-all duration-200 group cursor-pointer active:scale-[0.98]',
              route.path === item.path
                ? 'bg-primary/[0.12] text-primary shadow-none'
                : 'text-text-main/90 hover:bg-black/[0.04] dark:hover:bg-white/[0.04]'
            ]"
          >
            <svg 
              width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"
              :class="[route.path === item.path ? 'text-primary' : 'text-text-main opacity-60 group-hover:opacity-100']"
              v-html="getIcon(item.icon)"
            ></svg>
            <span class="text-[14px]" :class="[route.path === item.path ? 'font-semibold' : 'font-normal']">{{ item.name }}</span>
          </button>
        </nav>
      </div>
      
      <!-- 歌单标题页签 (完全复刻 buildTabGroupTitle) -->
      <div>
        <div class="px-3.5 flex items-center justify-between mb-2.5">
          <div class="flex items-center gap-2">
            <span class="text-[11px] font-semibold text-primary uppercase tracking-[0.5px]">自建歌单</span>
            <span class="text-[11px] text-text-main opacity-10">|</span>
            <span class="text-[11px] font-normal text-text-main opacity-40">收藏歌单</span>
          </div>
          <button class="p-1 hover:bg-black/[0.05] dark:hover:bg-white/[0.05] rounded-md transition-colors text-text-main opacity-40">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 5v14M5 12h14"/></svg>
          </button>
        </div>
        <!-- 空状态 -->
        <div class="px-3.5 py-8 text-center">
           <span class="text-[12px] font-normal text-text-main opacity-30 italic">登录同步云端歌单</span>
        </div>
      </div>
    </div>
    
  </aside>
</template>

<style scoped>
.sidebar {
  width: 220px;
}
</style>
