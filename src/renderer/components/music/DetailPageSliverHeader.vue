<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, useSlots } from 'vue';
import Cover from '../ui/Cover.vue';

interface Props {
  typeLabel: string;
  title: string;
  coverUrl: string;
  hasDetails?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  hasDetails: false,
});

const slots = useSlots();

const collapsedHeight = 52;
const scrollY = ref(0);

// 根据是否有详情内容决定展开高度
const expandedHeight = computed(() => {
  const hasDetailsContent = props.hasDetails || !!slots.details;
  return hasDetailsContent ? 240 : 200;
});

// 计算当前 header 的高度（2倍速收缩）
const currentHeight = computed(() => {
  const shrinkAmount = scrollY.value * 2;
  const newHeight = expandedHeight.value - shrinkAmount;
  return Math.max(collapsedHeight, Math.min(expandedHeight.value, newHeight));
});

// 是否已折叠
const isCollapsed = computed(() => currentHeight.value <= collapsedHeight + 10);

// 暴露当前高度给父组件
defineExpose({
  currentHeight,
});

onMounted(() => {
  const scrollContainer = document.querySelector('.view-port');
  if (!scrollContainer) return;

  const handleScroll = () => {
    scrollY.value = scrollContainer.scrollTop;
  };

  scrollContainer.addEventListener('scroll', handleScroll, { passive: true });

  onUnmounted(() => {
    scrollContainer.removeEventListener('scroll', handleScroll);
  });
});
</script>

<template>
  <!-- Sticky 定位的 Header，会粘在顶部 -->
  <div
    class="sticky z-[100] bg-bg-main transition-all duration-100 overflow-hidden"
    :style="{
      height: `${currentHeight}px`,
      top: '0px'
    }"
  >
    <!-- 展开状态内容 -->
    <Transition name="header-fade">
      <div
        v-if="!isCollapsed"
        key="expanded"
        class="absolute inset-0 bg-bg-main flex flex-col justify-start pt-4 px-8"
      >
        <div class="flex items-start gap-6 flex-1 min-h-0">
          <div class="shrink-0 shadow-2xl rounded-[18px] overflow-hidden">
            <Cover :url="coverUrl" :size="400" :width="136" :height="136" />
          </div>
          <div class="flex-1 flex flex-col min-w-0 gap-2">
            <div class="flex items-start justify-between gap-3">
              <h1
                class="text-[24px] font-bold text-text-main leading-[1.08] line-clamp-2 tracking-tight"
              >
                {{ title }}
              </h1>
              <div class="type-badge shrink-0 mt-1">{{ typeLabel }}</div>
            </div>
            <div class="flex flex-col gap-2">
              <slot name="details" />
            </div>
            <div class="mt-3">
              <slot name="actions" />
            </div>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 折叠状态内容 -->
    <Transition name="header-fade">
      <div
        v-if="isCollapsed"
        key="collapsed"
        class="absolute inset-0 flex items-center px-8 gap-3 bg-bg-main/95 backdrop-blur-md border-b border-border-light/10"
      >
        <Cover
          :url="coverUrl"
          :size="100"
          :width="32"
          :height="32"
          :borderRadius="6"
          class="shrink-0 shadow-sm"
        />
        <div class="flex-1 min-w-0">
          <h2 class="text-[15px] font-bold text-text-main truncate leading-tight">
            {{ title }}
          </h2>
        </div>
        <div class="flex items-center gap-1 shrink-0">
          <slot name="collapsed-actions" />
        </div>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
@reference "../../style.css";

.type-badge {
  @apply px-2 py-1 rounded-full text-[10px] font-semibold tracking-[1.2px] uppercase;
  background-color: color-mix(in srgb, var(--color-primary) 12%, transparent);
  color: var(--color-primary);
  border: 0.5px solid color-mix(in srgb, var(--color-primary) 25%, transparent);
}

.header-fade-enter-active,
.header-fade-leave-active {
  transition: opacity 150ms ease;
}

.header-fade-enter-from,
.header-fade-leave-to {
  opacity: 0;
}
</style>
