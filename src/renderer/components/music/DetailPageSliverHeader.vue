<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import Cover from '../ui/Cover.vue';

interface Props {
  typeLabel: string;
  title: string;
  coverUrl: string;
  hasDetails?: boolean;
  expandedHeight?: number;
  collapsedHeight?: number;
}

const props = withDefaults(defineProps<Props>(), {
  hasDetails: false,
  expandedHeight: 200,
  collapsedHeight: 56,
});

const collapsedHeight = computed(() => props.collapsedHeight);
const scrollY = ref(0);

const expandedHeight = computed(() => props.expandedHeight);

// 计算当前 header 的高度（2倍速收缩）
const currentHeight = computed(() => {
  const shrinkAmount = scrollY.value * 2;
  const newHeight = expandedHeight.value - shrinkAmount;
  return Math.max(collapsedHeight.value, Math.min(expandedHeight.value, newHeight));
});

// 是否已折叠
const isCollapsed = computed(() => currentHeight.value <= collapsedHeight.value + 10);

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
        class="absolute inset-0 bg-bg-main flex flex-col justify-start px-6 pt-[20px] pb-[10px]"
      >
        <div class="flex items-center gap-5 flex-1 min-h-0">
          <div class="shrink-0 rounded-[18px] overflow-hidden">
            <Cover :url="coverUrl" :size="400" :width="136" :height="136" :borderRadius="18" />
          </div>
          <div class="flex-1 flex flex-col min-w-0 gap-2">
            <div class="flex items-start justify-between gap-3">
              <h1 class="flex-1 min-w-0 text-[24px] font-semibold text-text-main leading-[1.08] line-clamp-2">
                {{ title }}
              </h1>
              <div class="type-badge shrink-0 mt-1">{{ typeLabel }}</div>
            </div>
            <div class="flex flex-col gap-2">
              <slot name="details" />
            </div>
            <div class="mt-2">
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
        class="absolute inset-0 flex items-center px-5 gap-3 bg-bg-main/95 backdrop-blur-md border-b border-border-light/10"
      >
        <Cover
          :url="coverUrl"
          :size="100"
          :width="32"
          :height="32"
          :borderRadius="6"
          class="shrink-0"
        />
        <div class="flex-1 min-w-0">
          <h2 class="text-[16px] font-semibold text-text-main truncate leading-tight">
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
  background-color: color-mix(in srgb, var(--color-primary) 7%, transparent);
  color: var(--color-primary);
  border: 0.5px solid color-mix(in srgb, var(--color-primary) 16%, transparent);
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
