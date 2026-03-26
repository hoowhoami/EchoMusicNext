<script setup lang="ts">
import Sidebar from './Sidebar.vue';
import TitleBar from './TitleBar.vue';
import PlayerBar from './PlayerBar.vue';
import BackToTop from '@/components/ui/BackToTop.vue';
</script>

<template>
  <div class="main-layout h-screen w-screen flex overflow-hidden bg-bg-main text-text-main transition-colors duration-300">
    <!-- 1. 左侧侧边栏 (固定宽度) -->
    <Sidebar class="w-[220px] shrink-0" />

    <!-- 2. 右侧容器 (内容 + 播放器) -->
    <div class="flex-1 flex flex-col min-w-0 relative">
      <!-- 2.1 主体内容区域 -->
      <main class="main-content flex-1 flex flex-col min-h-0">
        <TitleBar />
        <!-- 滚动容器 -->
        <div class="view-port flex-1 overflow-y-auto overflow-x-hidden">
          <router-view />
        </div>
      </main>

      <!-- 2.2 底部悬浮播放控制栏 (相对于右侧容器居中) -->
      <PlayerBar />
      
      <!-- 2.3 返回顶部 -->
      <BackToTop target-selector=".view-port" />
    </div>
  </div>
</template>

<style scoped>
.main-layout {
  /* 确保整体容器占满屏幕且不溢出 */
  user-select: none;
}
</style>
