<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue';
import Cover from '../ui/Cover.vue';

interface Props {
  typeLabel: string;
  title: string;
  coverUrl: string;
  expandedHeight?: number;
}

const props = withDefaults(defineProps<Props>(), {
  expandedHeight: 260,
});

const scrollY = ref(0);
const collapsedHeight = 52;
// 缩短折叠阈值，让交互更灵敏
const collapseThreshold = computed(() => props.expandedHeight - collapsedHeight - 60);

let scrollTarget: HTMLElement | null = null;

const handleScroll = (e: Event) => {
  const target = e.target as HTMLElement;
  scrollY.value = target.scrollTop;
};

onMounted(() => {
  scrollTarget = document.querySelector('.view-port');
  if (scrollTarget) {
    scrollTarget.addEventListener('scroll', handleScroll);
    scrollY.value = scrollTarget.scrollTop;
  }
});

onUnmounted(() => {
  if (scrollTarget) {
    scrollTarget.removeEventListener('scroll', handleScroll);
  }
});

const isCollapsed = computed(() => scrollY.value > collapseThreshold.value);

// 动画进度 (0 -> 1)
const progress = computed(() => {
  return Math.min(Math.max(scrollY.value / collapseThreshold.value, 0), 1);
});

// 样式计算
const expandedOpacity = computed(() => 1 - Math.min(progress.value * 2, 1));
const expandedScale = computed(() => 1 - progress.value * 0.1);
const expandedTranslateY = computed(() => progress.value * 30);
</script>

<template>
  <!-- Root 容器不再限制高度，仅作为占位 -->
  <div class="sliver-header-root w-full" :style="{ height: `${expandedHeight}px` }">
    
    <!-- 真正的吸顶容器：使用 fixed 或在父级 scroll 容器下的 sticky -->
    <!-- 我们保持 sticky top-0，但必须确保它不在一个随之滚动的父容器里 -->
    <div 
      class="fixed-top-container sticky top-0 left-0 right-0 z-[100] transition-colors duration-300"
      :style="{ 
        height: isCollapsed ? `${collapsedHeight}px` : `${expandedHeight}px`,
        backgroundColor: isCollapsed ? 'var(--bg-main)' : 'transparent'
      }"
    >
      <!-- 背景模糊 & 底边线 (仅折叠时显示) -->
      <div 
        v-if="isCollapsed"
        class="absolute inset-0 z-0 bg-bg-main/80 backdrop-blur-xl border-b border-border-light shadow-sm"
      ></div>

      <div class="relative w-full h-full pt-[52px] overflow-hidden">
        <!-- 1. 折叠后的精简状态 (吸顶后的 Cover 和 标题) -->
        <Transition name="slide-up">
          <div 
            v-if="isCollapsed" 
            key="collapsed"
            class="absolute top-0 left-0 right-0 h-[52px] flex items-center px-5 gap-3"
          >
            <Cover :url="coverUrl" :size="100" :width="32" :height="32" :borderRadius="6" class="shrink-0 shadow-sm" />
            <div class="flex-1 min-w-0">
              <h2 class="text-[16px] font-bold text-text-main truncate leading-tight">
                {{ title }}
              </h2>
            </div>
            <div class="flex items-center gap-1">
              <slot name="collapsed-actions"></slot>
            </div>
          </div>
        </Transition>

        <!-- 2. 展开状态 (原始大封面和按钮) -->
        <div 
          v-if="!isCollapsed"
          key="expanded"
          class="absolute inset-0 top-[52px] flex flex-col px-8"
          :style="{ 
            opacity: expandedOpacity,
            transform: `scale(${expandedScale}) translateY(${expandedTranslateY}px)`
          }"
        >
          <div class="flex items-center gap-6">
            <!-- 大封面 -->
            <div class="shrink-0 shadow-2xl rounded-[20px] overflow-hidden origin-bottom-left">
              <Cover :url="coverUrl" :size="400" :width="140" :height="140" />
            </div>

            <!-- 右侧文本 -->
            <div class="flex-1 flex flex-col min-w-0 gap-2.5">
              <div class="flex items-start justify-between gap-3">
                <h1 class="text-[26px] font-bold text-text-main leading-[1.15] line-clamp-2 tracking-tight">
                  {{ title }}
                </h1>
                <div class="type-badge shrink-0 mt-1.5">
                  {{ typeLabel }}
                </div>
              </div>
              
              <div class="flex flex-col gap-2">
                <slot name="details"></slot>
              </div>

              <div class="mt-2.5">
                <slot name="actions"></slot>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
@reference "../../style.css";

.sliver-header-root {
  /* 确保占位正确 */
  position: relative;
  z-index: 100;
}

.fixed-top-container {
  /* 关键：sticky 定位 */
  pointer-events: none;
}

.fixed-top-container > * {
  pointer-events: auto;
}

.type-badge {
  @apply px-2 py-0.5 rounded-md text-[10px] font-extrabold tracking-widest uppercase;
  background-color: var(--color-primary);
  color: white;
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.25s cubic-bezier(0.2, 0.8, 0.2, 1);
}

.slide-up-enter-from {
  opacity: 0;
  transform: translateY(15px);
}

.slide-up-leave-to {
  opacity: 0;
  transform: translateY(-15px);
}
</style>
