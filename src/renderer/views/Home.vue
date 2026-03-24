<script setup lang="ts">
import { usePlaylistStore } from '../stores/playlist';
import { usePlayerStore } from '../stores/player';

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
      <div class="absolute inset-0 bg-[url('https://images.unsplash.com/photo-1514525253361-bee8718a340b?q=80&w=1000&auto=format&fit=crop')] bg-cover bg-center mix-blend-overlay opacity-40"></div>
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
            <img :src="song.coverUrl" class="w-full h-full object-cover transition-transform duration-500 group-hover:scale-110" />
            
            <!-- 播放按钮遮罩 -->
            <div class="absolute inset-0 bg-black/10 opacity-0 group-hover:opacity-100 transition-opacity flex items-center justify-center">
              <div class="w-12 h-12 rounded-full bg-primary text-white flex items-center justify-center shadow-lg transform translate-y-4 group-hover:translate-y-0 transition-all duration-300">
                 <svg v-if="playerStore.currentTrackId === song.id && playerStore.isPlaying" width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/></svg>
                 <svg v-else width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
              </div>
            </div>
            
            <!-- 正在播放动画 -->
            <div v-if="playerStore.currentTrackId === song.id && playerStore.isPlaying" class="absolute bottom-3 right-3 flex items-end gap-0.5 h-4">
              <div class="w-1 bg-white rounded-full animate-music-bar-1"></div>
              <div class="w-1 bg-white rounded-full animate-music-bar-2"></div>
              <div class="w-1 bg-white rounded-full animate-music-bar-3"></div>
            </div>
          </div>

          <!-- 文本信息 -->
          <div class="mt-3.5 space-y-0.5">
            <div class="text-[14.5px] font-bold text-text-main truncate group-hover:text-primary transition-colors">{{ song.title }}</div>
            <div class="text-[12.5px] font-medium text-text-secondary truncate opacity-80">{{ song.artist }}</div>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.home-view {
  animation: fade-in 0.6s cubic-bezier(0.4, 0, 0.2, 1);
}

@keyframes fade-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}

.animate-music-bar-1 { height: 60%; animation: music-bar 0.8s ease-in-out infinite; }
.animate-music-bar-2 { height: 100%; animation: music-bar 1.2s ease-in-out infinite; }
.animate-music-bar-3 { height: 80%; animation: music-bar 1.0s ease-in-out infinite; }

@keyframes music-bar {
  0%, 100% { height: 40%; }
  50% { height: 100%; }
}
</style>
