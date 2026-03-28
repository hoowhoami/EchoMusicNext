<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import Dialog from '@/components/ui/Dialog.vue';
import CustomTabBar from '@/components/ui/CustomTabBar.vue';

export interface PickerOption {
  id: string;
  name: string;
  group?: string;
}

interface Props {
  open: boolean;
  title: string;
  options: PickerOption[];
  selectedId?: string;
  maxWidth?: number;
}

const props = withDefaults(defineProps<Props>(), {
  open: false,
  options: () => [],
  selectedId: '',
  maxWidth: 500,
});

const emit = defineEmits<{
  (e: 'update:open', value: boolean): void;
  (e: 'select', option: PickerOption): void;
}>();

const groupedOptions = computed(() => {
  const groups = new Map<string, PickerOption[]>();
  props.options.forEach((opt) => {
    const key = opt.group ?? '';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key)!.push(opt);
  });
  return Array.from(groups.entries());
});

const tabs = computed(() => groupedOptions.value.map(([key]) => key).filter((key) => key !== ''));
const hasTabs = computed(() => tabs.value.length > 1);
const activeTabIndex = ref(0);

const activeOptions = computed(() => {
  if (!hasTabs.value) return props.options;
  const key = tabs.value[activeTabIndex.value];
  const group = groupedOptions.value.find(([groupKey]) => groupKey === key);
  return group ? group[1] : props.options;
});

const handleSelect = (option: PickerOption) => {
  emit('select', option);
  emit('update:open', false);
};

watch(
  () => props.open,
  (open) => {
    if (!open) return;
    if (!hasTabs.value) return;
    const selectedOption = props.options.find((opt) => opt.id === props.selectedId);
    if (!selectedOption?.group) return;
    const index = tabs.value.findIndex((tab) => tab === selectedOption.group);
    activeTabIndex.value = index >= 0 ? index : 0;
  },
);
</script>

<template>
  <Dialog
    v-model:open="props.open"
    :title="props.title"
    contentClass="custom-picker-dialog"
    showClose
  >
    <div v-if="hasTabs" class="custom-picker-tabs">
      <CustomTabBar v-model="activeTabIndex" :tabs="tabs" />
    </div>
    <div class="custom-picker-options">
      <button
        v-for="opt in activeOptions"
        :key="opt.id"
        type="button"
        class="custom-picker-option"
        :class="{ active: opt.id === props.selectedId }"
        @click="handleSelect(opt)"
      >
        {{ opt.name }}
      </button>
    </div>
  </Dialog>
</template>

<style scoped>
@reference "@/style.css";

.custom-picker-dialog {
  width: min(520px, 92vw);
}

.custom-picker-tabs {
  margin-bottom: 12px;
}

.custom-picker-options {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.custom-picker-option {
  @apply px-4 py-2 rounded-[10px] border border-border-light text-[12px] font-semibold text-text-main transition-all;
  background: color-mix(in srgb, var(--color-text-main) 6%, transparent);
}

.custom-picker-option.active {
  @apply border-primary text-primary;
  background: color-mix(in srgb, var(--color-primary) 15%, transparent);
}

.custom-picker-option:hover {
  @apply border-primary/40;
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
}
</style>
