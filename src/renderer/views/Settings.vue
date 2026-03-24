<script setup lang="ts">
import { useSettingStore } from '../stores/setting';

const settingStore = useSettingStore();
</script>

<template>
  <div class="settings-view p-8 space-y-12 max-w-3xl transition-colors duration-300">
    <header>
      <h1 class="text-4xl font-black tracking-tight text-text-main">设置</h1>
    </header>

    <section class="space-y-6">
      <h2 class="text-lg font-bold text-text-main/60 uppercase tracking-wider">外观</h2>
      <div class="bg-bg-sidebar rounded-2xl p-6 space-y-6 transition-colors duration-300">
        <div class="flex items-center justify-between">
          <div class="space-y-1">
            <h3 class="font-bold">外观模式</h3>
            <p class="text-sm text-text-secondary">选择你喜欢的主题风格</p>
          </div>
          <div class="flex p-1 bg-bg-main rounded-xl shadow-sm border border-border-light transition-colors duration-300">
            <button 
              v-for="mode in ['light', 'dark', 'system']" 
              :key="mode"
              @click="settingStore.setTheme(mode as any)"
              :class="[
                'px-4 py-1.5 text-xs font-bold rounded-lg transition-all',
                settingStore.theme === mode ? 'bg-primary text-white shadow-md' : 'text-text-secondary hover:bg-black/5 dark:hover:bg-white/5'
              ]"
            >
              {{ mode === 'light' ? '浅色' : mode === 'dark' ? '深色' : '跟随系统' }}
            </button>
          </div>
        </div>

        <div class="h-px bg-border-light/50"></div>

        <div class="flex items-center justify-between">
          <div class="space-y-1">
            <h3 class="font-bold">语言</h3>
            <p class="text-sm text-text-secondary">选择界面显示语言</p>
          </div>
          <select class="bg-bg-main text-text-main border border-border-light rounded-lg px-3 py-1.5 text-sm font-bold focus:outline-none transition-colors duration-300">
            <option value="zh-CN">简体中文</option>
            <option value="en-US">English</option>
          </select>
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <h2 class="text-lg font-bold text-text-main/60 uppercase tracking-wider">播放</h2>
      <div class="bg-bg-sidebar rounded-2xl p-6 space-y-6 transition-colors duration-300">
        <div class="flex items-center justify-between">
          <div class="space-y-1">
            <h3 class="font-bold">自动播放</h3>
            <p class="text-sm text-text-secondary">启动应用时自动继续播放</p>
          </div>
          <div 
            @click="settingStore.autoPlay = !settingStore.autoPlay"
            :class="['w-12 h-6 rounded-full relative transition-colors cursor-pointer', settingStore.autoPlay ? 'bg-primary' : 'bg-text-secondary/30']"
          >
            <div :class="['absolute top-1 w-4 h-4 bg-white rounded-full transition-all', settingStore.autoPlay ? 'left-7' : 'left-1']"></div>
          </div>
        </div>

        <div class="h-px bg-border-light/50"></div>

        <div class="flex items-center justify-between">
          <div class="space-y-1">
            <h3 class="font-bold">全局快捷键</h3>
            <p class="text-sm text-text-secondary">允许在后台使用媒体按键控制</p>
          </div>
           <div 
            @click="settingStore.shortcutEnabled = !settingStore.shortcutEnabled"
            :class="['w-12 h-6 rounded-full relative transition-colors cursor-pointer', settingStore.shortcutEnabled ? 'bg-primary' : 'bg-text-secondary/30']"
          >
            <div :class="['absolute top-1 w-4 h-4 bg-white rounded-full transition-all', settingStore.shortcutEnabled ? 'left-7' : 'left-1']"></div>
          </div>
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <h2 class="text-lg font-bold text-text-main/60 uppercase tracking-wider">关于</h2>
      <div class="bg-bg-sidebar rounded-2xl p-6 flex flex-col items-center space-y-4 transition-colors duration-300">
        <div class="w-20 h-20 bg-primary rounded-3xl shadow-xl flex items-center justify-center text-white">
           <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>
        </div>
        <div class="text-center">
          <h3 class="text-xl font-black">EchoMusicNext</h3>
          <p class="text-sm text-text-secondary font-medium">Version 1.0.0 (Alpha)</p>
        </div>
        <p class="text-xs text-text-secondary text-center max-w-xs leading-relaxed opacity-60">
          基于 Electron + Vue3 + TypeScript 开发的跨平台音乐播放器。致敬原版 EchoMusic。
        </p>
      </div>
    </section>
  </div>
</template>

<style scoped>
.settings-view {
  animation: fade-in 0.6s cubic-bezier(0.4, 0, 0.2, 1);
}

@keyframes fade-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
