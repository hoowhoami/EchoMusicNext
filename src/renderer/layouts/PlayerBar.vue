<script setup lang="ts">
import { useRouter } from 'vue-router';
import { computed, ref } from 'vue';
import { usePlayerStore } from '../stores/player';
import { usePlaylistStore, type Song } from '../stores/playlist';
import { SliderRoot, SliderTrack, SliderRange, SliderThumb } from 'reka-ui';

const router = useRouter();
const player = usePlayerStore();
const playlist = usePlaylistStore();

const currentTrack = computed(() => {
  return playlist.defaultList.find((s: Song) => s.id === player.currentTrackId) || 
         playlist.favorites.find((s: Song) => s.id === player.currentTrackId);
});

const isFavorite = computed(() => {
  return currentTrack.value ? playlist.favorites.some(s => s.id === currentTrack.value?.id) : false;
});

const formatTime = (seconds: number) => {
  if (!seconds || isNaN(seconds)) return '00:00';
  const mins = Math.floor(seconds / 60);
  const secs = Math.floor(seconds % 60);
  return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
};

const navigateToPlaying = () => {
  router.push('/playing');
};

const handleSeek = (value: number[] | undefined) => {
  if (value && value.length > 0) {
    player.seek(value[0]);
  }
};

const handleVolumeChange = (value: number[] | undefined) => {
  if (value && value.length > 0) {
    player.setVolume(value[0] / 100); // UI is 0-100, store is 0-1
  }
};

const toggleFavorite = (e: Event) => {
  e.stopPropagation();
  if (!currentTrack.value) return;
  if (isFavorite.value) {
    playlist.removeFromFavorites(currentTrack.value.id);
  } else {
    playlist.addToFavorites(currentTrack.value);
  }
};

const isHoveringProgress = ref(false);
</script>

<template>
  <div class="player-bar-container absolute bottom-[5px] left-2 right-2 z-[1000]">
    <footer class="player-bar w-full h-[76px] bg-bg-main dark:bg-[#1C1C1E] border border-black/[0.05] dark:border-white/[0.05] rounded-[12px] shadow-[0_10px_24px_rgba(0,0,0,0.12),0_2px_6px_rgba(0,0,0,0.05)] flex items-center px-4 select-none no-drag transition-all duration-300">
      
      <!-- 1. 左侧：歌曲信息 (完全复刻 _PlayerSongInfo) -->
      <div class="flex items-center gap-3 w-[260px] shrink-0">
        <div class="relative w-[50px] h-[50px] shrink-0 cursor-pointer group rounded-lg overflow-hidden bg-black/[0.04] dark:bg-white/[0.04]" @click="navigateToPlaying">
          <img v-if="currentTrack" :src="currentTrack.coverUrl" class="w-full h-full object-cover transition-transform duration-500 group-hover:scale-110" />
          <div v-else class="w-full h-full flex items-center justify-center text-text-main/20">
             <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>
          </div>
        </div>
        
        <div class="flex flex-col min-w-0 flex-1">
          <div class="text-[13.5px] font-semibold text-text-main truncate leading-tight hover:text-primary cursor-pointer transition-colors" @click="navigateToPlaying">
            {{ currentTrack ? currentTrack.title : '未在播放' }}
          </div>
          <div class="text-[11px] text-text-main/50 font-medium truncate mt-1">
            {{ currentTrack ? currentTrack.artist : 'EchoMusic' }}
          </div>
        </div>

        <button @click="toggleFavorite" class="p-2 transition-all hover:scale-110 active:scale-90" :class="isFavorite ? 'text-red-500' : 'text-text-main/20 hover:text-primary'">
           <svg width="20" height="20" viewBox="0 0 24 24" :fill="isFavorite ? 'currentColor' : 'none'" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l8.78-8.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>
        </button>
      </div>
      
      <!-- 2. 中间：播放控制 & 进度条 (完全复刻 _PlayerCenterControls) -->
      <div class="flex-1 flex flex-col items-center justify-center gap-1">
        <div class="flex items-center gap-6">
          <!-- 播放模式 -->
          <button @click="player.setPlayMode(player.playMode === 'list' ? 'random' : player.playMode === 'random' ? 'single' : 'list')" class="p-2 text-text-main/40 hover:text-primary transition-all">
            <svg v-if="player.playMode === 'list'" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M17 2l4 4-4 4M3 11v-1a4 4 0 014-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 01-4 4H3"/></svg>
            <svg v-else-if="player.playMode === 'random'" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M16 3h5v5M4 20L21 3M21 16v5h-5M15 15l6 6M4 4l5 5"/></svg>
            <svg v-else width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M17 2l4 4-4 4M3 11v-1a4 4 0 014-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 01-4 4H3"/><path d="M11 15h2v-4h-2"/></svg>
          </button>

          <button @click="player.prev" class="p-2 text-text-main/70 hover:text-primary transition-all active:scale-90">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="currentColor"><path d="M6 6h2v12H6zm3.5 6l8.5 6V6z"/></svg>
          </button>
          
          <button @click="player.togglePlay" class="w-[38px] h-[38px] rounded-full bg-text-main/5 dark:bg-white/10 flex items-center justify-center hover:scale-110 active:scale-95 transition-all">
            <svg v-if="!player.isPlaying" width="20" height="20" viewBox="0 0 24 24" fill="currentColor" class="ml-1"><path d="M8 5v14l11-7z"/></svg>
            <svg v-else width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"/></svg>
          </button>
          
          <button @click="player.next" class="p-2 text-text-main/70 hover:text-primary transition-all active:scale-90">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="currentColor"><path d="M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"/></svg>
          </button>

          <!-- 音量控制 (简化版悬浮触发) -->
          <div class="relative group/vol">
            <button class="p-2 text-text-main/40 hover:text-primary transition-all">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M11 5L6 9H2v6h4l5 4V5zM15.54 8.46a5 5 0 0 1 0 7.07"/></svg>
            </button>
            <!-- 简易音量调节 -->
            <div class="absolute bottom-full left-1/2 -translate-x-1/2 pb-4 opacity-0 group-hover/vol:opacity-100 pointer-events-none group-hover/vol:pointer-events-auto transition-opacity">
               <div class="bg-bg-main dark:bg-[#2C2C2E] border border-black/5 p-3 rounded-xl shadow-xl h-32 flex flex-col items-center">
                  <SliderRoot
                    :model-value="[player.volume * 100]"
                    :max="100"
                    orientation="vertical"
                    class="relative flex flex-col items-center select-none touch-none w-5 h-full"
                    @update:model-value="handleVolumeChange"
                  >
                    <SliderTrack class="bg-black/5 dark:bg-white/10 relative grow rounded-full w-1">
                      <SliderRange class="absolute bg-primary rounded-full w-full" />
                    </SliderTrack>
                    <SliderThumb class="block w-3 h-3 bg-white border border-black/5 rounded-full shadow-md focus:outline-none" />
                  </SliderRoot>
               </div>
            </div>
          </div>
        </div>
        
        <!-- 进度条系统 (1:1 复刻 _InlineProgressRow) -->
        <div class="w-full max-w-[420px] flex items-center gap-2 px-2 h-[14px]">
          <span class="text-[11px] font-medium text-text-main/40 w-9 text-left tabular-nums">{{ formatTime(player.currentTime) }}</span>
          <SliderRoot
            :model-value="[player.currentTime]"
            :max="player.duration || 100"
            :step="0.1"
            class="relative flex items-center select-none touch-none flex-1 h-4 group/progress"
            @update:model-value="handleSeek"
            @mouseenter="isHoveringProgress = true"
            @mouseleave="isHoveringProgress = false"
          >
            <SliderTrack class="bg-black/[0.06] dark:bg-white/[0.06] relative grow rounded-full h-[3.4px]">
              <SliderRange class="absolute bg-primary rounded-full h-full" />
            </SliderTrack>
            <SliderThumb 
              class="block w-2.5 h-2.5 bg-white border border-black/10 rounded-full shadow-sm focus:outline-none transition-opacity duration-200" 
              :class="[isHoveringProgress ? 'opacity-100' : 'opacity-0']"
            />
          </SliderRoot>
          <span class="text-[11px] font-medium text-text-main/40 w-9 text-right tabular-nums">{{ formatTime(player.duration) }}</span>
        </div>
      </div>

      <!-- 3. 右侧：功能选项 (完全复刻 _PlayerRightActions) -->
      <div class="w-[260px] shrink-0 flex justify-end items-center gap-1.5">
         <button class="p-2 text-text-main/40 hover:text-primary transition-all" title="倍速">
           <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 2v4m0 12v4M4.93 4.93l2.83 2.83m8.48 8.48l2.83 2.83M2 12h4m12 0h4M4.93 19.07l2.83-2.83m8.48-8.48l2.83-2.83"/></svg>
         </button>
         <button class="p-2 text-text-main/40 hover:text-primary transition-all" title="音质">
           <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 20v-8m0-4V4m-5 12v-2m0-4v-2m10 10v-6m0-4V8"/></svg>
         </button>
         <button class="p-2 text-text-main/40 hover:text-primary transition-all" title="播放队列">
           <svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/></svg>
         </button>
      </div>
      
    </footer>
  </div>
</template>

<style scoped>
.player-bar-wrapper {
  /* 容器固定高度 */
}

.player-bar {
  /* 严格遵循 Flutter 阴影参数 */
}
</style>
