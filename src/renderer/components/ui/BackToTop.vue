<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';

const props = defineProps<{
  targetSelector?: string;
  threshold?: number;
}>();

const visible = ref(false);
let scrollTarget: HTMLElement | null = null;

const handleScroll = () => {
  if (!scrollTarget) return;
  visible.value = scrollTarget.scrollTop > (props.threshold || 300);
};

const scrollToTop = () => {
  if (!scrollTarget) return;
  scrollTarget.scrollTo({
    top: 0,
    behavior: 'smooth'
  });
};

onMounted(() => {
  scrollTarget = props.targetSelector 
    ? document.querySelector(props.targetSelector) 
    : document.querySelector('.view-port');
    
  if (scrollTarget) {
    scrollTarget.addEventListener('scroll', handleScroll);
  }
});

onUnmounted(() => {
  if (scrollTarget) {
    scrollTarget.removeEventListener('scroll', handleScroll);
  }
});
</script>

<template>
  <Transition name="fade">
    <button
      v-if="visible"
      @click="scrollToTop"
      class="fixed right-8 bottom-32 z-50 p-3 rounded-full bg-white dark:bg-bg-card shadow-lg hover:shadow-xl border border-border-light text-text-main transition-all duration-300 active:scale-95 group"
      aria-label="Back to top"
    >
      <svg 
        xmlns="http://www.w3.org/2000/svg" 
        width="20" 
        height="20" 
        viewBox="0 0 24 24" 
        fill="none" 
        stroke="currentColor" 
        stroke-width="2" 
        stroke-linecap="round" 
        stroke-linejoin="round"
        class="transition-transform group-hover:-translate-y-1"
      >
        <path d="m18 15-6-6-6 6"/>
      </svg>
    </button>
  </Transition>
</template>

<style scoped>
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease, transform 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
  transform: translateY(10px);
}
</style>
