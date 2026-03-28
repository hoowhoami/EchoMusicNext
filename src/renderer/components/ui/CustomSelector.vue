<script setup lang="ts">
import { iconChevronDown } from '@/icons';

interface Props {
  label: string;
  class?: string;
}

const props = defineProps<Props>();

const emit = defineEmits<{
  (e: 'click'): void;
}>();

const handleClick = () => {
  emit('click');
};
</script>

<template>
  <button type="button" class="custom-selector" :class="props.class" @click="handleClick">
    <span class="custom-selector-label">{{ label }}</span>
    <Icon :icon="iconChevronDown" width="14" height="14" class="custom-selector-icon" />
  </button>
</template>

<style scoped>
@reference "@/style.css";

.custom-selector {
  @apply inline-flex items-center gap-2 h-9 px-3 rounded-[10px] border border-border-light bg-black/[0.06] dark:bg-white/[0.06] text-text-main text-[13px] font-semibold transition-all;
}

.custom-selector:hover {
  @apply border-primary/30 bg-black/[0.08] dark:bg-white/[0.08];
}

.custom-selector-label {
  @apply truncate;
}

.custom-selector-icon {
  @apply text-text-secondary/70;
}
</style>
