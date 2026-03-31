<script setup lang="ts">
import { computed } from 'vue';
import { useRouter } from 'vue-router';
import Dialog from '@/components/ui/Dialog.vue';
import { useAuthStore } from '@/stores/auth';

const router = useRouter();
const authStore = useAuthStore();

const open = computed({
  get: () => authStore.sessionExpiredDialogOpen,
  set: (value: boolean) => {
    if (!value) authStore.hideSessionExpiredDialog();
  },
});

const handleLogin = async () => {
  authStore.hideSessionExpiredDialog();
  if (router.currentRoute.value.name !== 'login') {
    await router.push({ name: 'login' });
  }
};
</script>

<template>
  <Dialog
    v-model:open="open"
    :title="authStore.sessionExpiredPayload.title"
    :description="authStore.sessionExpiredPayload.description"
    contentClass="auth-expired-dialog"
  >
    <template #footer>
      <button class="auth-expired-btn auth-expired-btn--ghost" @click="authStore.hideSessionExpiredDialog()">
        稍后再说
      </button>
      <button class="auth-expired-btn auth-expired-btn--primary" @click="handleLogin">
        去登录
      </button>
    </template>
  </Dialog>
</template>

<style scoped>
@reference "@/style.css";

:global(.auth-expired-dialog) {
  @apply w-[420px] max-w-[92vw];
}

.auth-expired-btn {
  @apply px-5 py-2 rounded-full text-[13px] font-semibold transition-all duration-200 active:scale-95;
}

.auth-expired-btn--ghost {
  @apply border border-border-light/50 text-text-secondary hover:text-text-main hover:border-border-light/70 hover:bg-black/5 dark:hover:bg-white/5;
}

.auth-expired-btn--primary {
  @apply bg-primary text-white shadow-[0_10px_30px_rgba(0,113,227,0.22)] hover:bg-primary-hover;
}
</style>
