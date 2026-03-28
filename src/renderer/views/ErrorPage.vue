<script setup lang="ts">
import { computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { iconTriangleAlert, iconRefreshCw, iconChevronLeft } from '@/icons';

const route = useRoute();
const router = useRouter();

const errorMessage = computed(() => (route.query.message as string) || '发生了一些未知的错误');
const errorStatus = computed(() => (route.query.status as string) || 'Error');

const handleRetry = () => {
  const retryPath = route.query.from as string;
  if (retryPath) {
    router.replace(retryPath);
  } else {
    router.back();
  }
};

const handleGoBack = () => {
  router.back();
};
</script>

<template>
  <div class="error-page-container flex flex-col items-center justify-center min-h-[60vh] px-6 text-center">
    <div class="w-20 h-20 rounded-3xl bg-red-500/10 flex items-center justify-center mb-8">
      <Icon :icon="iconTriangleAlert" width="40" height="40" class="text-red-500" />
    </div>
    
    <h1 class="text-2xl font-bold text-text-main mb-2">出错了</h1>
    <div class="text-[14px] text-text-secondary opacity-60 mb-8 max-w-md mx-auto leading-relaxed">
      {{ errorMessage }}
      <div v-if="errorStatus" class="mt-1 text-[12px] font-mono opacity-40">
        Status: {{ errorStatus }}
      </div>
    </div>

    <div class="flex items-center gap-4">
      <button 
        @click="handleGoBack"
        class="flex items-center gap-2 px-6 py-2.5 rounded-full border border-border-light/40 text-[14px] font-semibold text-text-main hover:bg-black/5 dark:hover:bg-white/5 transition-colors"
      >
        <Icon :icon="iconChevronLeft" width="18" height="18" />
        返回上一页
      </button>
      
      <button 
        @click="handleRetry"
        class="flex items-center gap-2 px-6 py-2.5 rounded-full bg-primary text-[14px] font-semibold text-white hover:opacity-90 transition-opacity shadow-lg shadow-primary/20"
      >
        <Icon :icon="iconRefreshCw" width="16" height="16" />
        重试一次
      </button>
    </div>
  </div>
</template>

<style scoped>
.error-page-container {
  animation: fade-in 0.4s ease-out;
}

@keyframes fade-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
