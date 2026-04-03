<script setup lang="ts">
import { onMounted, watch, onUnmounted } from 'vue';
import { RouterView } from 'vue-router';
import { useRoute } from 'vue-router';
import AuthExpiredDialog from '@/components/app/AuthExpiredDialog.vue';
import { usePlayerStore } from './stores/player';
import { useSettingStore } from './stores/setting';
import { useUserStore } from './stores/user';
import { initShortcutSync, syncGlobalShortcuts } from '@/utils/shortcuts';
import { initDesktopLyricSync } from '@/utils/desktopLyric';

const player = usePlayerStore();
const settings = useSettingStore();
const user = useUserStore();
const route = useRoute();
const isDesktopLyricWindow = () => route.name === 'desktop-lyric';
let disposeShortcuts: (() => void) | null = null;
let disposeDesktopLyricSync: (() => void) | null = null;

const updateTheme = () => {
  const isDark =
    settings.theme === 'dark' ||
    (settings.theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.classList.toggle('dark', isDark);
};

onMounted(() => {
  if (!isDesktopLyricWindow()) {
    player.init();
    void settings.hydrateDesktopLyric();
    void initDesktopLyricSync().then((dispose) => {
      disposeDesktopLyricSync = dispose;
    });
  }
  updateTheme();
  settings.syncTheme();
  settings.syncCloseBehavior();
  settings.syncRememberWindowSize();
  if (!isDesktopLyricWindow()) {
    settings.syncPreventSleep(player.isPlaying);
  }
  if (settings.autoReceiveVip && user.isLoggedIn) {
    void user.autoReceiveVipIfNeeded();
  }
  if (!isDesktopLyricWindow()) {
    disposeShortcuts = initShortcutSync();
  }
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', updateTheme);
});

onUnmounted(() => {
  disposeShortcuts?.();
  disposeShortcuts = null;
  disposeDesktopLyricSync?.();
  disposeDesktopLyricSync = null;
});

watch(() => settings.theme, updateTheme);
watch(() => settings.rememberWindowSize, () => {
  if (isDesktopLyricWindow()) return;
  settings.syncRememberWindowSize();
});
watch(() => settings.preventSleep, () => {
  if (isDesktopLyricWindow()) return;
  settings.syncPreventSleep(player.isPlaying);
});
watch(() => player.isPlaying, (isPlaying) => {
  if (isDesktopLyricWindow()) return;
  settings.syncPreventSleep(isPlaying);
});
watch(
  () => [settings.autoReceiveVip, user.isLoggedIn],
  ([enabled, loggedIn]) => {
    if (enabled && loggedIn) {
      void user.autoReceiveVipIfNeeded();
    }
  },
  { immediate: true },
);
watch(
  () => [settings.globalShortcutsEnabled, settings.globalShortcutBindings],
  () => {
    if (isDesktopLyricWindow()) return;
    syncGlobalShortcuts();
  },
  { deep: true },
);
</script>

<template>
  <RouterView v-slot="{ Component }">
    <transition name="page" mode="out-in">
      <component :is="Component" />
    </transition>
  </RouterView>
  <AuthExpiredDialog />
</template>

<style>
/* 全局样式已在 style.css 中定义 */

.page-enter-active,
.page-leave-active {
  transition: all 0.3s ease-out;
}

.page-enter-from {
  opacity: 0;
  transform: translateY(8px);
}

.page-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}
</style>
