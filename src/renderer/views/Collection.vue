<script setup lang="ts">
import { usePlaylistStore } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import Cover from '@/components/ui/Cover.vue';
import { iconHeart, iconPlay } from '@/icons';

const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();

const playSong = (id: string) => {
  playerStore.playTrack(id);
};

const removeFromFavorites = (id: string, event: Event) => {
  event.stopPropagation();
  playlistStore.removeFromFavorites(id);
};
</script>

<template>
  <div class="collection-view p-8 space-y-8">
    <header class="flex items-end gap-6">
      <div class="w-48 h-48 rounded-2xl bg-gradient-to-br from-primary to-secondary shadow-2xl flex items-center justify-center text-white">
        <Icon :icon="iconHeart" width="80" height="80" />
      </div>
      <div class="pb-2 space-y-2">
        <span class="text-xs font-bold uppercase tracking-[2px] opacity-60">歌单</span>
        <h1 class="text-5xl font-black tracking-tight">我最喜爱的音乐</h1>
        <p class="text-text-secondary font-medium">{{ playlistStore.favorites.length }} 首歌曲</p>
      </div>
    </header>

    <div class="space-y-1">
      <!-- 表头 -->
      <div class="flex items-center px-4 py-2 text-[12px] font-bold text-text-secondary uppercase tracking-wider opacity-60 border-b border-border-light/50">
        <div class="w-12">#</div>
        <div class="flex-1">标题</div>
        <div class="w-48">艺人</div>
        <div class="w-24 text-right">时长</div>
        <div class="w-12"></div>
      </div>

      <!-- 歌曲列表 -->
      <div v-if="playlistStore.favorites.length > 0">
        <div 
          v-for="(song, index) in playlistStore.favorites" 
          :key="song.id"
          class="flex items-center px-4 py-3 rounded-xl hover:bg-bg-sidebar dark:hover:bg-white/5 group cursor-pointer transition-colors"
          @click="playSong(song.id)"
        >
          <div class="w-12 text-sm font-bold opacity-60 group-hover:opacity-0 transition-opacity">
            {{ index + 1 }}
          </div>
          <!-- 正在播放小图标 -->
          <div v-if="playerStore.currentTrackId === song.id" class="absolute left-4 w-12 flex justify-start">
             <Icon class="text-primary" :icon="iconPlay" width="16" height="16" />
          </div>

          <div class="flex-1 flex items-center gap-4">
            <Cover :url="song.coverUrl" :size="100" :width="40" :height="40" :borderRadius="8" class="shadow-sm" />
            <span :class="['text-sm font-bold', playerStore.currentTrackId === song.id ? 'text-primary' : 'text-text-main']">
              {{ song.title }}
            </span>
          </div>

          <div class="w-48 text-sm font-medium text-text-secondary">{{ song.artist }}</div>
          
          <div class="w-24 text-right text-sm font-medium text-text-secondary opacity-60 tabular-nums">
             {{ Math.floor(song.duration / 60) }}:{{ (song.duration % 60).toString().padStart(2, '0') }}
          </div>

          <div class="w-12 flex justify-end">
            <button @click="removeFromFavorites(song.id, $event)" class="text-red-500 opacity-0 group-hover:opacity-100 transition-opacity">
               <Icon :icon="iconHeart" width="18" height="18" />
            </button>
          </div>
        </div>
      </div>

      <div v-else class="py-20 flex flex-col items-center justify-center space-y-4 opacity-40">
        <Icon :icon="iconHeart" width="64" height="64" />
        <p class="text-xl font-black">暂无收藏歌曲</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.collection-view {
  animation: fade-in 0.6s cubic-bezier(0.4, 0, 0.2, 1);
}

@keyframes fade-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
