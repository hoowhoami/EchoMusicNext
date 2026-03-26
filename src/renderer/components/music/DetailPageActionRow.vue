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
    <button @click="onPlay" class="action-btn primary">
      <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
        <path d="M8 5v14l11-7z"/>
      </svg>
      <span>{{ playLabel }}</span>
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

.action-btn.secondary.emphasized {
  @apply text-primary bg-primary/10;
}

.action-btn.primary {
  @apply bg-primary text-white hover:bg-primary-hover;
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
