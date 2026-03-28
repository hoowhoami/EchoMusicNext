<script setup lang="ts">
import { onMounted, watch, onUnmounted } from 'vue';
import { RouterView } from 'vue-router';
import { usePlayerStore } from './stores/player';
import { useSettingStore } from './stores/setting';
import { initShortcutSync, syncGlobalShortcuts } from '@/utils/shortcuts';

const player = usePlayerStore();
const settings = useSettingStore();
let disposeShortcuts: (() => void) | null = null;

const updateTheme = () => {
  const isDark =
    settings.theme === 'dark' ||
    (settings.theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.classList.toggle('dark', isDark);
};

onMounted(() => {
  player.init();
  updateTheme();
  disposeShortcuts = initShortcutSync();
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', updateTheme);
});

onUnmounted(() => {
  disposeShortcuts?.();
  disposeShortcuts = null;
});

watch(() => settings.theme, updateTheme);
watch(
  () => [settings.globalShortcutsEnabled, settings.globalShortcutBindings],
  () => syncGlobalShortcuts(),
  { deep: true },
);
</script>

<template>
  <RouterView v-slot="{ Component }">
    <transition name="page" mode="out-in">
      <component :is="Component" />
    </transition>
  </RouterView>
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
