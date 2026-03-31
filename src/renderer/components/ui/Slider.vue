<script setup lang="ts">
import { computed } from 'vue';
import { SliderRoot, SliderTrack, SliderRange, SliderThumb } from 'reka-ui';

type SliderOrientation = 'horizontal' | 'vertical';

interface Props {
  modelValue?: number;
  min?: number;
  max?: number;
  step?: number;
  disabled?: boolean;
  orientation?: SliderOrientation;
  class?: string;
  trackClass?: string;
  rangeClass?: string;
  thumbClass?: string;
}

const props = withDefaults(defineProps<Props>(), {
  min: 0,
  max: 100,
  step: 1,
  disabled: false,
  orientation: 'horizontal',
});

const emit = defineEmits<{
  (e: 'update:modelValue', value: number): void;
  (e: 'valueCommit', value: number): void;
}>();

const normalizedValue = computed(() => props.modelValue ?? props.min);

const handleUpdate = (value?: number[]) => {
  if (!value?.length) return;
  emit('update:modelValue', value[0]);
};

const handleCommit = (value?: number[]) => {
  if (!value?.length) return;
  emit('valueCommit', value[0]);
};

const rootClass = computed(() => [
  'slider-root',
  props.orientation === 'vertical' ? 'slider-root-vertical' : 'slider-root-horizontal',
  props.class,
]);

const trackClass = computed(() => ['slider-track', props.trackClass]);
const rangeClass = computed(() => ['slider-range', props.rangeClass]);
const thumbClass = computed(() => ['slider-thumb', props.thumbClass]);
</script>

<template>
  <SliderRoot
    :model-value="[normalizedValue]"
    :min="props.min"
    :max="props.max"
    :step="props.step"
    :disabled="props.disabled"
    :orientation="props.orientation"
    :class="rootClass"
    @update:model-value="handleUpdate"
    @value-commit="handleCommit"
  >
    <SliderTrack :class="trackClass">
      <SliderRange :class="rangeClass" />
    </SliderTrack>
    <SliderThumb :class="thumbClass" />
  </SliderRoot>
</template>

<style scoped>
@reference "@/style.css";

.slider-root {
  @apply relative select-none touch-none cursor-pointer;
}

.slider-root-horizontal {
  @apply flex items-center h-6;
}

.slider-root-vertical {
  @apply flex flex-col items-center w-6;
}

.slider-track {
  @apply relative grow rounded-full bg-black/[0.06] dark:bg-white/[0.08];
}

.slider-root-horizontal .slider-track {
  @apply h-[3px];
}

.slider-root-vertical .slider-track {
  @apply w-[3px];
}

.slider-range {
  @apply absolute rounded-full bg-primary;
}

.slider-root-horizontal .slider-range {
  @apply h-full;
}

.slider-root-vertical .slider-range {
  @apply w-full;
}

.slider-thumb {
  @apply block w-3 h-3 rounded-full bg-white border border-black/10 shadow-sm transition-shadow focus-visible:outline-none;
}

.slider-thumb:focus-visible {
  box-shadow: 0 0 0 2px color-mix(in srgb, var(--color-primary) 28%, transparent);
}

.slider-root[data-disabled] {
  @apply opacity-60 cursor-not-allowed;
}

.slider-root[data-disabled] .slider-thumb {
  @apply cursor-not-allowed;
}
</style>
