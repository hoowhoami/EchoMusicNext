<script setup lang="ts">
import { computed, ref, onMounted, watch } from 'vue';
import { useRouter } from 'vue-router';
import { usePlayerStore } from '@/stores/player';
import { usePlaylistStore, type Song } from '@/stores/playlist';
import { useLyricStore } from '@/stores/lyric';
import OverlayHeader from '@/layouts/OverlayHeader.vue';
import Cover from '@/components/ui/Cover.vue';
import { getCoverUrl } from '@/utils/music';

const router = useRouter();
const player = usePlayerStore();
const playlist = usePlaylistStore();
const lyricStore = useLyricStore();

const currentTrack = computed(() => {
  return playlist.defaultList.find((s: Song) => s.id === player.currentTrackId) || 
         playlist.favorites.find((s: Song) => s.id === player.currentTrackId);
});

const backgroundUrl = computed(() => getCoverUrl(currentTrack.value?.coverUrl, 400));

// 歌词滚动逻辑
const lyricListRef = ref<HTMLElement | null>(null);

watch(() => lyricStore.currentIndex, (newIndex) => {
  if (lyricListRef.value && newIndex !== -1) {
    const activeLine = lyricListRef.value.children[newIndex] as HTMLElement;
    if (activeLine) {
      const containerHeight = lyricListRef.value.offsetHeight;
      const offset = activeLine.offsetTop - containerHeight / 2 + activeLine.offsetHeight / 2;
      lyricListRef.value.scrollTo({
        top: offset,
        behavior: 'smooth'
      });
    }
  }
});

onMounted(() => {
  // 初始同步一次歌词滚动
  if (lyricStore.currentIndex !== -1) {
     setTimeout(() => {
        const activeLine = lyricListRef.value?.children[lyricStore.currentIndex] as HTMLElement;
        if (activeLine) {
          const containerHeight = lyricListRef.value?.offsetHeight || 0;
          const offset = activeLine.offsetTop - containerHeight / 2 + activeLine.offsetHeight / 2;
          lyricListRef.value?.scrollTo({ top: offset });
        }
     }, 100);
  }
});
</script>

<template>
  <div class="playing-view relative h-full w-full overflow-hidden bg-bg-main text-text-main transition-colors duration-500 select-none flex flex-col">
    <!-- 1. 背景高斯模糊 -->
    <div class="absolute inset-0 z-0 transition-all duration-1000 overflow-hidden">
       <div 
        class="absolute inset-0 bg-cover bg-center scale-110 blur-[80px] opacity-25 dark:opacity-45 saturate-150 transition-all duration-1000"
        :style="{ backgroundImage: `url(${backgroundUrl})` }"
       ></div>
       <div class="absolute inset-0 bg-bg-main/40 dark:bg-black/40"></div>
    </div>

    <!-- 2. 顶部工具栏 -->
    <OverlayHeader 
      class="relative z-10" 
      :title="currentTrack?.album || 'EchoMusic'" 
      :show-back="true"
      :transparent="true" 
    />

    <!-- 3. 主体内容 -->
    <main class="relative z-10 flex-1 flex flex-col lg:flex-row items-center justify-center gap-12 lg:gap-24 px-12 lg:px-24 mb-16 overflow-hidden">
      
      <!-- 3.1 左侧：封面 -->
      <div class="flex-1 flex flex-col items-center justify-center space-y-8 animate-slide-right">
        <div class="relative group">
          <div class="w-64 h-64 lg:w-96 lg:h-96 rounded-2xl overflow-hidden shadow-[0_20px_50px_rgba(0,0,0,0.2)] dark:shadow-[0_20px_50px_rgba(0,0,0,0.5)] transition-all duration-500 group-hover:scale-[1.02]">
            <Cover :url="currentTrack?.coverUrl" :size="800" class="w-full h-full" :borderRadius="24" />
          </div>
          <!-- 封面发光装饰 -->
          <div 
            class="absolute -inset-4 blur-3xl opacity-25 -z-10 bg-primary/40 rounded-full transition-all duration-1000"
            :style="{ background: `radial-gradient(circle, ${'#0071E3'} 0%, transparent 70%)` }"
          ></div>
        </div>

        <div class="text-center space-y-2 max-w-md">
          <h2 class="text-3xl lg:text-4xl font-black tracking-tight truncate">{{ currentTrack?.title || '未在播放' }}</h2>
          <p class="text-lg lg:text-xl font-medium opacity-70 truncate">{{ currentTrack?.artist || 'Unknown Artist' }}</p>
        </div>
      </div>

      <!-- 3.2 右侧：歌词 -->
      <div class="flex-1 h-full w-full max-w-xl flex flex-col items-center justify-center animate-slide-left overflow-hidden">
        <div 
          ref="lyricListRef"
          class="lyric-container w-full h-full overflow-y-auto no-scrollbar mask-gradient py-[40vh]"
        >
          <div v-if="lyricStore.lines.length > 0" class="space-y-8">
            <div 
              v-for="(line, index) in lyricStore.lines" 
              :key="index"
              :class="[
                'lyric-line text-2xl lg:text-3xl font-bold transition-all duration-500 cursor-pointer hover:opacity-100',
                lyricStore.currentIndex === index ? 'text-primary scale-105 opacity-100' : 'text-text-main/40 dark:text-white/40 opacity-60'
              ]"
              @click="player.seek(line.time)"
            >
              {{ line.text }}
            </div>
          </div>
          <div v-else class="h-full flex items-center justify-center">
            <p class="text-2xl font-bold opacity-40">纯音乐，请欣赏</p>
          </div>
        </div>
      </div>

    </main>

    <!-- 底部播放器控制由全局 PlayerBar 处理 -->
  </div>
</template>

<style scoped>
.playing-view {
  animation: fade-in 0.8s cubic-bezier(0.4, 0, 0.2, 1);
}

.mask-gradient {
  mask-image: linear-gradient(
    to bottom,
    transparent 0%,
    black 20%,
    black 80%,
    transparent 100%
  );
}

.lyric-container {
  scroll-behavior: smooth;
}

.lyric-line {
  line-height: 1.4;
  text-shadow: 0 2px 10px rgba(0,0,0,0.05);
}

.dark .lyric-line {
  text-shadow: 0 2px 10px rgba(0,0,0,0.2);
}

@keyframes fade-in {
  from { opacity: 0; }
  to { opacity: 1; }
}

.animate-slide-right {
  animation: slide-right 1s cubic-bezier(0.2, 0.8, 0.2, 1);
}

.animate-slide-left {
  animation: slide-left 1s cubic-bezier(0.2, 0.8, 0.2, 1);
}

@keyframes slide-right {
  from { opacity: 0; transform: translateX(-40px); }
  to { opacity: 1; transform: translateX(0); }
}

@keyframes slide-left {
  from { opacity: 0; transform: translateX(40px); }
  to { opacity: 1; transform: translateX(0); }
}
</style>
