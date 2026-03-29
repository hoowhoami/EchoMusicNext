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
    <div class="card-container flex flex-col">
      <div class="cover-shell">
        <div class="cover-wrapper">
          <Cover :url="coverUrl" :size="400" :borderRadius="'50%'" class="w-full h-full" />
        </div>
      </div>
      <div class="info-wrapper w-full">
        <h3 class="title">{{ name }}</h3>
        <p class="subtitle">
          {{ songCount || 0 }} 歌曲 <span class="mx-0.5 opacity-60">•</span> {{ albumCount || 0 }} 专辑
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
  @apply p-3 rounded-[20px] bg-white dark:bg-white/5 transition-all duration-300;
  box-shadow: 0 8px 18px rgba(0, 0, 0, 0.08);
}

.artist-card:hover .card-container {
  box-shadow: 0 10px 24px rgba(0, 0, 0, 0.12), 0 0 24px var(--color-primary-light);
}

.cover-shell {
  height: 150px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.cover-wrapper {
  width: 126px;
  height: 126px;
  border-radius: 999px;
  overflow: hidden;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
}

.info-wrapper {
  margin-top: 6px;
  min-height: 38px;
  text-align: left;
}

.title {
  @apply text-[13px] font-semibold text-text-main line-clamp-1;
  line-height: 1.15;
}

.subtitle {
  @apply text-[11px] font-semibold text-text-secondary line-clamp-1 opacity-80;
  margin-top: 3px;
}
</style>
