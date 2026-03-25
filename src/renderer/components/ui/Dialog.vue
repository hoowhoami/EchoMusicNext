<script setup lang="ts">
import { computed, useSlots } from 'vue';
import { useVModel } from '@vueuse/core';
import {
  DialogContent,
  DialogDescription,
  DialogOverlay,
  DialogPortal,
  DialogRoot,
  DialogTitle,
  DialogClose,
} from 'reka-ui';

interface Props {
  open?: boolean;
  title?: string;
  description?: string;
  showClose?: boolean;
  overlayClass?: string;
  contentClass?: string;
}

const props = withDefaults(defineProps<Props>(), {
  open: false,
  showClose: false,
});

const emit = defineEmits<{
  (e: 'update:open', value: boolean): void;
}>();

const open = useVModel(props, 'open', emit, { defaultValue: false });
const slots = useSlots();

const hasTitle = computed(() => Boolean(props.title) || Boolean(slots.title));
const hasDescription = computed(() => Boolean(props.description) || Boolean(slots.description));
const hasHeader = computed(() => hasTitle.value || hasDescription.value);
const hasFooter = computed(() => Boolean(slots.footer));
const hasBody = computed(() => Boolean(slots.default));

const overlayClass = computed(() => ['dialog-overlay', props.overlayClass]);
const contentClass = computed(() => ['dialog-content', props.contentClass]);
const overlayStyles = computed(() => ({
  position: 'fixed',
  inset: '0',
  zIndex: '200',
}));
const contentStyles = computed(() => ({
  position: 'fixed',
  left: '50%',
  top: '50%',
  zIndex: '210',
}));
const bodyClass = computed(() => [
  'dialog-body',
  hasHeader.value ? 'mt-4' : null,
  hasFooter.value ? 'mb-2' : null,
]);
</script>

<template>
  <DialogRoot v-model:open="open">
    <DialogPortal>
      <DialogOverlay as-child>
        <div :class="overlayClass" :style="overlayStyles" />
      </DialogOverlay>

      <DialogContent as-child>
        <div :class="contentClass" :style="contentStyles">
          <DialogClose v-if="props.showClose" as-child>
            <button class="dialog-close" type="button" aria-label="关闭">
              <svg
                width="14"
                height="14"
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                stroke-width="2.5"
                stroke-linecap="round"
                stroke-linejoin="round"
              >
                <path d="M18 6 6 18" />
                <path d="M6 6l12 12" />
              </svg>
            </button>
          </DialogClose>

          <div v-if="hasHeader" class="dialog-header">
            <DialogTitle v-if="hasTitle" as-child>
              <h3 class="dialog-title">
                <slot name="title">{{ props.title }}</slot>
              </h3>
            </DialogTitle>
            <DialogDescription v-if="hasDescription" as-child>
              <p class="dialog-description">
                <slot name="description">{{ props.description }}</slot>
              </p>
            </DialogDescription>
          </div>

          <div v-if="hasBody" :class="bodyClass">
            <slot />
          </div>

          <div v-if="hasFooter" class="dialog-footer">
            <slot name="footer" />
          </div>
        </div>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>

<style scoped>
@reference "../../style.css";

:global(.dialog-overlay) {
  @apply fixed inset-0 bg-black/30 backdrop-blur-[1px] z-[200];
  opacity: 0;
  transition: opacity 0.16s ease-out;
}

:global(.dialog-overlay[data-state='open']) {
  opacity: 1;
}

:global(.dialog-overlay[data-state='closed']) {
  opacity: 0;
}

:global(.dialog-content) {
  @apply fixed left-1/2 top-1/2 z-[210] w-[420px] max-w-[92vw] -translate-x-1/2 -translate-y-1/2 rounded-2xl bg-bg-main border border-border-light/40 p-6 shadow-2xl;
  opacity: 0;
  transform: translate(-50%, -50%) scale(0.98);
  transition:
    opacity 0.18s ease-out,
    transform 0.18s ease-out;
  will-change: transform, opacity;
}

:global(.dialog-content[data-state='open']) {
  opacity: 1;
  transform: translate(-50%, -50%) scale(1);
}

:global(.dialog-content[data-state='closed']) {
  opacity: 0;
  transform: translate(-50%, -50%) scale(0.98);
}

.dialog-header {
  @apply space-y-2;
}

.dialog-title {
  @apply text-lg font-bold text-text-main;
}

.dialog-description {
  @apply text-sm text-text-secondary;
}

.dialog-body {
  @apply text-sm text-text-main;
}

.dialog-footer {
  @apply flex justify-end gap-3 mt-6;
}

.dialog-close {
  @apply absolute top-4 right-4 w-8 h-8 rounded-full flex items-center justify-center text-text-main/50 hover:text-text-main transition-colors;
}

.dialog-close:focus-visible {
  @apply outline-none ring-2 ring-primary/30;
}
</style>
