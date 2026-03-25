<script setup lang="ts">
import { computed, watch, ref, onMounted, onUnmounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';

const route = useRoute();
const router = useRouter();

const isMac = computed(() => window.electron.platform === 'darwin');

const canGoBack = ref(false);
const canGoForward = ref(false);

const updateNavState = () => {
  if (typeof window === 'undefined') return;
  const historyState = window.history.state as { back?: string | null; forward?: string | null } | null;
  const skipCurrent = route.meta?.skipHistory === true;
  canGoBack.value = !skipCurrent && !!historyState?.back;
  canGoForward.value = !skipCurrent && !!historyState?.forward;
};

const handleControl = (action: 'minimize' | 'maximize' | 'close') => {
  window.electron.windowControl(action);
};

const goBack = () => {
  if (canGoBack.value) router.back();
};
const goForward = () => {
  if (canGoForward.value) router.forward();
};
const refresh = () => window.location.reload();

watch(
  () => route.fullPath,
  () => {
    updateNavState();
  },
  { immediate: true }
);

onMounted(() => {
  window.addEventListener('popstate', updateNavState);
});

onUnmounted(() => {
  window.removeEventListener('popstate', updateNavState);
});
</script>

<template>
  <header class="title-bar drag flex items-center px-6 shrink-0 select-none transition-colors duration-300 z-[100] bg-transparent">
    <!-- 1. 左侧：导航按钮 (no-drag) -->
    <div class="flex items-center gap-1 no-drag">
      <button @click="goBack" class="nav-btn group" :disabled="!canGoBack" title="后退">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" :class="['text-text-main transition-opacity', canGoBack ? 'opacity-40 group-hover:opacity-100' : 'opacity-20']"><path d="m15 18-6-6 6-6"/></svg>
      </button>
      <button @click="goForward" class="nav-btn group" :disabled="!canGoForward" title="前进">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" :class="['text-text-main transition-opacity', canGoForward ? 'opacity-40 group-hover:opacity-100' : 'opacity-20']"><path d="m9 18 6-6-6-6"/></svg>
      </button>
      <button @click="refresh" class="nav-btn group" title="刷新">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" class="text-text-main opacity-40 group-hover:opacity-100 transition-opacity">
          <path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M3 21v-5h5"/>
        </svg>
      </button>
    </div>

    <!-- 2. 中间：空白区域 -->
    <div class="flex-1 h-full"></div>

    <!-- 3. 右侧：窗口控制 (no-drag) -->
    <div v-if="!isMac" class="window-controls flex items-center no-drag h-full ml-4">
      <button @click="handleControl('minimize')" class="control-btn hover:bg-text-main/5">
        <svg width="10" height="1" viewBox="0 0 10 1"><rect width="10" height="1" fill="currentColor"/></svg>
      </button>
      <button @click="handleControl('maximize')" class="control-btn hover:bg-text-main/5">
        <svg width="9" height="9" viewBox="0 0 10 10"><path d="M1,1 L9,1 L9,9 L1,9 Z" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>
      </button>
      <button @click="handleControl('close')" class="control-btn hover:bg-red-500 hover:text-white">
        <svg width="10" height="10" viewBox="0 0 10 10"><path d="M1,1 L9,9 M9,1 L1,9" stroke="currentColor" stroke-width="1.2"/></svg>
      </button>
    </div>
  </header>
</template>

<style scoped>
.title-bar {
  height: 52px;
}

.nav-btn {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 8px;
  transition: all 0.2s;
  background: transparent;
  border: none;
}

.nav-btn:hover {
  background-color: rgba(0, 0, 0, 0.04);
}

.dark .nav-btn:hover {
  background-color: rgba(255, 255, 255, 0.04);
}

.control-btn {
  width: 46px;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--color-text-main);
  background: transparent;
  border: none;
}
</style>
.nav-btn:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.nav-btn:disabled:hover {
  background-color: transparent;
}
