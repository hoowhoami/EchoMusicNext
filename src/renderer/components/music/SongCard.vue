<script setup lang="ts">
import Cover from '../ui/Cover.vue';
import { formatDuration } from '../../utils/format';

interface Props {
  id: string | number;
  title: string;
  artist: string;
  coverUrl: string;
  album?: string;
  duration?: number;
  class?: string;
}

const props = defineProps<Props>();
</script>

<template>
  <div 
    class="song-card group flex items-center gap-3 p-2 rounded-xl transition-all duration-300 hover:bg-black/5 dark:hover:bg-white/5 cursor-pointer"
    :class="props.class"
  >
    <!-- 封面 -->
    <div class="relative w-12 h-12 shrink-0 rounded-lg overflow-hidden shadow-sm">
      <Cover :url="coverUrl" :size="200" class="w-full h-full" />
      
      <!-- 播放图标 (Hover) -->
      <div class="absolute inset-0 bg-black/20 opacity-0 group-hover:opacity-100 transition-opacity duration-300 flex items-center justify-center">
        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="currentColor" class="text-white">
          <path d="M8 5v14l11-7z"/>
        </svg>
      </div>
    </div>
    
    <!-- 信息 -->
    <div class="flex-1 min-w-0 flex flex-col gap-0.5">
      <h3 class="text-[13px] font-medium text-text-main line-clamp-1 group-hover:text-primary transition-colors">
        {{ title }}
      </h3>
      <p class="text-[11px] text-text-secondary line-clamp-1 opacity-80">
        {{ artist }} <span v-if="album" class="opacity-40 ml-1">• {{ album }}</span>
      </p>
    </div>
    
    <!-- 时长 -->
    <div v-if="duration" class="text-[11px] text-text-secondary opacity-40 px-2 group-hover:opacity-80 transition-opacity">
      {{ formatDuration(duration) }}
    </div>
    
    <!-- 更多操作 -->
    <button class="p-1.5 rounded-full hover:bg-black/5 dark:hover:bg-white/10 opacity-0 group-hover:opacity-100 transition-all text-text-secondary">
      <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="12" cy="12" r="1"/><circle cx="12" cy="5" r="1"/><circle cx="12" cy="19" r="1"/>
      </svg>
    </button>
  </div>
</template>

<style scoped>
.song-card {
  user-select: none;
}
</style>
