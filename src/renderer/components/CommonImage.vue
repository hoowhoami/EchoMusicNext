<script setup lang="ts">
import { ref, watch } from 'vue';

interface Props {
  src?: string;
  alt?: string;
  className?: string;
  loadingClassName?: string;
  errorClassName?: string;
  showSkeleton?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  src: '',
  alt: '',
  className: 'w-full h-full object-cover',
  loadingClassName: '',
  errorClassName: '',
  showSkeleton: true,
});

const status = ref<'loading' | 'success' | 'error'>('loading');

watch(() => props.src, (newSrc) => {
  if (newSrc) {
    status.value = 'loading';
  } else {
    status.value = 'error';
  }
}, { immediate: true });

const handleLoad = () => {
  status.value = 'success';
};

const handleError = () => {
  status.value = 'error';
};
</script>

<template>
  <div class="relative overflow-hidden group" :class="className">
    <!-- 1. 加载中骨架屏 (闪烁效果) -->
    <div 
      v-if="status === 'loading' && showSkeleton"
      class="absolute inset-0 bg-black/[0.05] dark:bg-white/[0.05] animate-pulse-slow z-10"
      :class="loadingClassName"
    ></div>

    <!-- 2. 图片主体 -->
    <img 
      v-if="src"
      :src="src" 
      :alt="alt"
      @load="handleLoad" 
      @error="handleError"
      class="transition-opacity duration-500"
      :class="[
        className,
        status === 'success' ? 'opacity-100' : 'opacity-0'
      ]"
    />

    <!-- 3. 加载失败占位 (SVG) -->
    <div 
      v-if="status === 'error' || (!src && status !== 'loading')" 
      class="absolute inset-0 flex items-center justify-center bg-black/[0.03] dark:bg-white/[0.03] z-20"
      :class="errorClassName"
    >
      <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="opacity-10 text-text-main"><rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/></svg>
    </div>
  </div>
</template>

<style scoped>
.animate-pulse-slow {
  animation: pulse 2.5s cubic-bezier(0.4, 0, 0.6, 1) infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}
</style>
