<script setup lang="ts">
import { onMounted, watch } from 'vue';
import { RouterView } from 'vue-router';
import MainLayout from '@/layouts/MainLayout.vue';
import { usePlayerStore } from './stores/player';
import { useSettingStore } from './stores/setting';

const player = usePlayerStore();
const settings = useSettingStore();

const updateTheme = () => {
  const isDark =
    settings.theme === 'dark' ||
    (settings.theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.classList.toggle('dark', isDark);
};

onMounted(() => {
  player.init();
  updateTheme();
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', updateTheme);
});

watch(() => settings.theme, updateTheme);
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
