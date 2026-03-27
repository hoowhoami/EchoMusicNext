<script setup lang="ts">
import { usePlaylistStore } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import Cover from '@/components/ui/Cover.vue';
import { iconPause, iconPlay } from '@/icons';

const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();

const playSong = (id: string) => {
  playerStore.playTrack(id);
};
</script>

<template>
  <div class="home-view p-8 space-y-10">
    <!-- 1. 顶部横幅 (模拟推荐) -->
    <section class="hero-banner relative h-60 rounded-2xl overflow-hidden shadow-xl shadow-primary/10 group">
      <div class="absolute inset-0 bg-gradient-to-r from-primary/90 to-secondary/80"></div>
      <div class="absolute inset-0 bg-[url('https://images.unsplash.com/photo-1514525253361-bee8718a340b?q=80&w=1000&auto=format&fit=crop')] bg-cover bg-center mix-blend-overlay opacity-50"></div>
      <div class="relative h-full flex flex-col justify-center px-10 space-y-3">
        <span class="text-white/80 text-xs font-bold uppercase tracking-[2px]">今日推荐</span>
        <h2 class="text-4xl font-black text-white leading-tight">致敬经典：<br/>钢琴与浪漫的邂逅</h2>
        <button class="w-fit px-6 py-2.5 bg-white text-primary rounded-full text-sm font-bold shadow-lg hover:scale-105 active:scale-95 transition-all">
          立即播放
        </button>
      </div>
    </section>

    <!-- 2. 推荐列表 -->
    <section class="space-y-6">
      <div class="flex items-center justify-between">
        <h3 class="text-2xl font-black text-text-main tracking-tight">为你推荐</h3>
        <button class="text-sm font-bold text-primary hover:opacity-70 transition-opacity">查看全部</button>
      </div>

      <div class="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-6">
        <div 
          v-for="song in playlistStore.defaultList" 
          :key="song.id"
          class="song-card group cursor-pointer"
          @click="playSong(song.id)"
        >
          <!-- 封面容器 -->
          <div class="relative aspect-square rounded-xl overflow-hidden shadow-md bg-bg-card transition-all duration-300 group-hover:shadow-xl group-hover:-translate-y-1">
            <Cover :url="song.coverUrl" :size="400" class="transition-transform duration-500 group-hover:scale-110" />
            
            <!-- 播放按钮遮罩 -->
            <div class="absolute inset-0 bg-black/10 opacity-0 group-hover:opacity-100 transition-opacity flex items-center justify-center">
              <div class="w-12 h-12 rounded-full bg-primary text-white flex items-center justify-center shadow-lg transform translate-y-4 group-hover:translate-y-0 transition-all duration-300">
                 <Icon
                   v-if="playerStore.currentTrackId === song.id && playerStore.isPlaying"
                   :icon="iconPause"
                   width="24"
                   height="24"
                 />
                 <Icon v-else :icon="iconPlay" width="24" height="24" />
              </div>
            </div>
            
            <!-- 正在播放动画 -->
            <div v-if="playerStore.currentTrackId === song.id && playerStore.isPlaying" class="absolute bottom-3 right-3 flex items-end gap-0.5 h-4">
              <div class="w-1 bg-white rounded-full animate-music-bar-1"></div>
              <div class="w-1 bg-white rounded-full animate-music-bar-2"></div>
              <div class="w-1 bg-white rounded-full animate-music-bar-3"></div>
            </div>
          </div>

          <div class="mt-3 space-y-1">
            <h4 class="font-bold text-[14px] text-text-main truncate group-hover:text-primary transition-colors">{{ song.title }}</h4>
            <p class="text-[12px] text-text-main/50 font-medium truncate">{{ song.artist }}</p>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
@keyframes music-bar {
  0%, 100% { height: 4px; }
  50% { height: 16px; }
}
.animate-music-bar-1 { animation: music-bar 0.8s ease-in-out infinite; }
.animate-music-bar-2 { animation: music-bar 0.8s ease-in-out infinite 0.2s; }
.animate-music-bar-3 { animation: music-bar 0.8s ease-in-out infinite 0.4s; }
</style>
