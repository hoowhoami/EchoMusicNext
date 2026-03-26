<script setup lang="ts">
import { useRouter } from 'vue-router';
import Cover from '@/components/ui/Cover.vue';

interface Props {
  id: string | number;
  name: string;
  coverUrl: string;
  songCount?: number;
  albumCount?: number;
}

const props = defineProps<Props>();
const router = useRouter();

const handleClick = () => {
  router.push({ name: 'artist-detail', params: { id: props.id } });
};
</script>

<template>
  <div class="artist-card group cursor-pointer" @click="handleClick">
    <div class="card-container flex flex-col items-center">
      <div class="cover-wrapper">
        <Cover :url="coverUrl" :size="400" :borderRadius="'50%'" class="w-full h-full" />
      </div>
      <div class="info-wrapper text-center w-full">
        <h3 class="title">{{ name }}</h3>
        <p class="subtitle">
          {{ songCount || 0 }} 歌曲 <span class="mx-0.5 opacity-40">•</span> {{ albumCount || 0 }} 专辑
        </p>
      </div>
    </div>
  </div>
</template>

<style scoped>
@reference "@/style.css";

.artist-card {
  @apply transition-all duration-300 ease-out;
}

.artist-card:hover {
  transform: scale(1.03);
}

.card-container {
  @apply p-[10px] rounded-[20px] bg-white dark:bg-white/5 transition-all duration-300;
  box-shadow: 0 8px 18px rgba(0, 0, 0, 0.08);
}

.artist-card:hover .card-container {
  box-shadow: 0 10px 24px rgba(0, 0, 0, 0.12), 0 0 24px var(--color-primary-light);
}

.cover-wrapper {
  @apply aspect-square rounded-full overflow-hidden shadow-sm;
}

.info-wrapper {
  @apply mt-2 px-0.5;
}

.title {
  @apply text-[13px] font-semibold text-text-main line-clamp-1 leading-[1.2];
}

.subtitle {
  @apply text-[11px] font-semibold text-text-secondary line-clamp-1 mt-0.5 opacity-80;
}
</style>
