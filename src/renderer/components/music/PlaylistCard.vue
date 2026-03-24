<script setup lang="ts">
import { computed } from 'vue';
import { useRouter } from 'vue-router';
import Cover from '../ui/Cover.vue';

interface Props {
  id: string | number;
  name: string;
  coverUrl: string;
  creator?: string;
  songCount?: number;
  layout?: 'grid' | 'list';
}

const props = withDefaults(defineProps<Props>(), {
  layout: 'grid',
});

const router = useRouter();

const subtitle = computed(() => {
  if (props.creator && props.songCount) {
    return `${props.creator} • ${props.songCount} 首歌曲`;
  }
  return props.creator || (props.songCount ? `${props.songCount} 首歌曲` : '');
});

const handleClick = () => {
  router.push({ name: 'playlist-detail', params: { id: props.id } });
};
</script>

<template>
  <div 
    v-if="layout === 'grid'"
    class="playlist-card-grid group cursor-pointer"
    @click="handleClick"
  >
    <div class="card-container">
      <div class="cover-wrapper">
        <Cover :url="coverUrl" :size="400" class="w-full h-full" />
      </div>
      <div class="info-wrapper">
        <h3 class="title">{{ name }}</h3>
        <p v-if="subtitle" class="subtitle">{{ subtitle }}</p>
      </div>
    </div>
  </div>

  <div 
    v-else
    class="playlist-card-list group cursor-pointer"
    @click="handleClick"
  >
    <Cover :url="coverUrl" :size="200" :width="56" :height="56" :borderRadius="10" class="shrink-0" />
    <div class="info-wrapper ml-3 overflow-hidden">
      <h3 class="title">{{ name }}</h3>
      <p v-if="subtitle" class="subtitle">{{ subtitle }}</p>
    </div>
  </div>
</template>

<style scoped>
@reference "../../style.css";

/* Grid Layout */
.playlist-card-grid {
  @apply transition-all duration-300 ease-out;
}

.playlist-card-grid:hover {
  transform: scale(1.03);
}

.card-container {
  @apply p-[10px] rounded-[20px] bg-white dark:bg-white/5 transition-all duration-300;
  box-shadow: 0 8px 18px rgba(0, 0, 0, 0.08);
}

.playlist-card-grid:hover .card-container {
  box-shadow: 0 10px 24px rgba(0, 0, 0, 0.12), 0 0 24px var(--color-primary-light);
}

.cover-wrapper {
  @apply aspect-square rounded-[14px] overflow-hidden shadow-sm;
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

/* List Layout */
.playlist-card-list {
  @apply flex items-center p-3 rounded-[14px] transition-all duration-200;
}

.playlist-card-list:hover {
  background-color: rgba(0, 0, 0, 0.04);
}

.dark .playlist-card-list:hover {
  background-color: rgba(255, 255, 255, 0.04);
}

.playlist-card-list .title {
  @apply text-[14px] font-semibold text-text-main line-clamp-1;
}

.playlist-card-list .subtitle {
  @apply text-[12px] mt-1;
}
</style>
