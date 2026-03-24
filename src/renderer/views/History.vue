<script setup lang="ts">
import { usePlaylistStore } from '../stores/playlist';
import { usePlayerStore } from '../stores/player';
import Cover from '../components/ui/Cover.vue';

const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();

const playSong = (id: string) => {
  playerStore.playTrack(id);
};
</script>

<template>
  <div class="history-view p-8 space-y-8">
    <header>
      <h1 class="text-4xl font-black tracking-tight text-text-main">最近播放</h1>
      <p class="text-text-secondary font-medium mt-2">最近播放的 100 首歌曲</p>
    </header>

    <div class="space-y-1">
      <div v-if="playlistStore.history.length > 0">
        <div 
          v-for="(song, index) in playlistStore.history" 
          :key="song.id + index"
          class="flex items-center px-4 py-3 rounded-xl hover:bg-bg-light group cursor-pointer transition-colors"
          @click="playSong(song.id)"
        >
          <div class="w-12 text-sm font-bold opacity-40">{{ index + 1 }}</div>
          <div class="flex-1 flex items-center gap-4">
            <Cover :url="song.coverUrl" :size="100" :width="40" :height="40" :borderRadius="8" />
            <span :class="['text-sm font-bold', playerStore.currentTrackId === song.id ? 'text-primary' : 'text-text-main']">
              {{ song.title }}
            </span>
          </div>
          <div class="text-sm font-medium text-text-secondary opacity-60">{{ song.artist }}</div>
        </div>
      </div>
      <div v-else class="py-20 flex flex-col items-center justify-center opacity-20">
        <p class="text-xl font-black">暂无播放历史</p>
      </div>
    </div>
  </div>
</template>
