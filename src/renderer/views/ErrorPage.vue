<script setup lang="ts">
import { computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { iconTriangleAlert, iconRefreshCw, iconChevronLeft } from '@/icons';
import Button from '@/components/ui/Button.vue';

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
      <Button
        variant="outline"
        size="sm"
        @click="handleGoBack"
        class="flex items-center gap-2 rounded-full"
      >
        <Icon :icon="iconChevronLeft" width="18" height="18" />
        返回上一页
      </Button>
      
      <Button
        variant="primary"
        size="sm"
        @click="handleRetry"
        class="flex items-center gap-2 rounded-full"
      >
        <Icon :icon="iconRefreshCw" width="16" height="16" />
        重试一次
      </Button>
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
