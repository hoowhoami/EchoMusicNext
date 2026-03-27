<script setup lang="ts">
interface Action {
  icon: string;
  label: string;
  onTap: () => void;
  emphasized?: boolean;
}

interface Props {
  playLabel?: string;
  onPlay?: () => void;
  secondaryActions?: Action[];
}

const props = withDefaults(defineProps<Props>(), {
  playLabel: '播放',
  secondaryActions: () => [],
});

const emit = defineEmits<{
  (e: 'play'): void;
  (e: 'batch'): void;
}>();
</script>

<template>
  <div class="action-row-wrap flex flex-wrap gap-2">
    <!-- 次要操作 (收藏、批量等) -->
    <button
      v-for="action in secondaryActions"
      :key="action.label"
      @click="action.onTap"
      class="action-btn secondary"
      :class="{ 'emphasized': action.emphasized }"
    >
      <div class="icon-wrap" v-html="action.icon"></div>
      <span>{{ action.label }}</span>
    </button>

    <!-- 主要操作 (播放) -->
    <button @click="emit('play')" class="action-btn primary">
      <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
        <path d="M8 5v14l11-7z"/>
      </svg>
      <span>{{ playLabel }}</span>
    </button>

    <!-- 批量 (抽屉) -->
    <button @click="emit('batch')" class="action-btn secondary">
      <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M3 6h18"/>
        <path d="M3 12h18"/>
        <path d="M3 18h18"/>
      </svg>
      <span>批量</span>
    </button>
  </div>
</template>

<style scoped>
@reference "@/style.css";

.action-btn {
  @apply flex items-center gap-2 px-3 h-9 rounded-lg text-[12px] font-semibold transition-all active:scale-95 select-none;
  background-color: var(--bg-info-card);
  color: var(--color-text-main);
}

.action-btn.primary {
  @apply bg-primary text-white hover:bg-primary-hover;
}

.action-btn.secondary.emphasized {
  @apply text-primary bg-primary/10;
}

.action-btn:hover {
  @apply brightness-95;
}

.dark .action-btn {
  background-color: rgba(255, 255, 255, 0.08);
}

.icon-wrap {
  @apply flex items-center justify-center w-4 h-4;
}

:deep(svg) {
  width: 16px;
  height: 16px;
}
</style>
