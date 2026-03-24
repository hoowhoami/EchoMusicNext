<script setup lang="ts">
import { AvatarRoot, AvatarImage, AvatarFallback } from 'reka-ui';

interface Props {
  src?: string;
  alt?: string;
  class?: string;
  skeletonClass?: string;
  errorClass?: string;
  showSkeleton?: boolean;
  delayMs?: number;
}

const props = withDefaults(defineProps<Props>(), {
  src: '',
  alt: '',
  class: 'w-full h-full object-cover',
  skeletonClass: '',
  errorClass: '',
  showSkeleton: true,
  delayMs: 0,
});
</script>

<template>
  <AvatarRoot :class="['relative flex overflow-hidden shrink-0', props.class]">
    <!-- 1. 图片主体 -->
    <AvatarImage
      v-if="src"
      :src="src"
      :alt="alt"
      class="h-full w-full object-cover transition-opacity duration-500"
    />

    <!-- 2. 加载中 & 失败占位 -->
    <AvatarFallback
      :delay-ms="delayMs"
      class="flex h-full w-full items-center justify-center bg-black/[0.03] dark:bg-white/[0.03]"
    >
      <!-- 加载中骨架屏 -->
      <div 
        v-if="showSkeleton"
        class="absolute inset-0 bg-black/[0.05] dark:bg-white/[0.05] animate-pulse z-10"
        :class="skeletonClass"
      ></div>
      
      <!-- 失败图标 (用户头像占位) -->
      <svg width="60%" height="60%" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" class="opacity-10 text-text-main z-20" :class="errorClass">
        <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>
      </svg>
    </AvatarFallback>
  </AvatarRoot>
</template>
