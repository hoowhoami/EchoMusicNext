<script setup lang="ts">
import type { IconifyIcon } from '@iconify/types';
import { iconPlay, iconList } from '@/icons';
interface Action {
  icon: IconifyIcon;
  label: string;
  onTap: () => void | Promise<void>;
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
      <div class="icon-wrap">
        <Icon :icon="action.icon" width="16" height="16" />
      </div>
      <span>{{ action.label }}</span>
    </button>

    <!-- 主要操作 (播放) -->
    <button @click="emit('play')" class="action-btn primary">
      <Icon :icon="iconPlay" width="16" height="16" />
      <span>{{ playLabel }}</span>
    </button>

    <!-- 批量 (抽屉) -->
    <button @click="emit('batch')" class="action-btn secondary">
      <Icon :icon="iconList" width="16" height="16" />
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
</style>
