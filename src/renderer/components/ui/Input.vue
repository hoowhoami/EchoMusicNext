<script setup lang="ts">
import { useVModel } from '@vueuse/core';
import { Primitive, type PrimitiveProps } from 'reka-ui';

interface Props extends PrimitiveProps {
  modelValue?: string | number;
  type?: string;
  placeholder?: string;
  class?: string;
  inputClass?: string;
  showClear?: boolean;
}

const props = withDefaults(defineProps<Props>(), {
  as: 'input',
  type: 'text',
  showClear: true
});

const emits = defineEmits(['update:modelValue', 'clear']);

const value = useVModel(props, 'modelValue', emits);

const handleClear = () => {
  value.value = '';
  emits('clear');
};
</script>

<template>
  <div :class="['relative group w-full', props.class]">
    <input
      v-model="value"
      :type="type"
      :placeholder="placeholder"
      :class="[
        'w-full h-14 pl-6 pr-12 bg-black/[0.03] dark:bg-white/[0.03] border border-transparent focus:border-primary/30 focus:bg-transparent rounded-2xl outline-none transition-all font-medium text-[15px] placeholder:opacity-30',
        props.inputClass
      ]"
    />
    
    <!-- 清除图标按钮 -->
    <button 
      v-if="showClear && value" 
      @click="handleClear"
      type="button"
      class="absolute right-4 top-1/2 -translate-y-1/2 w-6 h-6 flex items-center justify-center text-text-main opacity-20 hover:opacity-50 transition-all active:scale-90"
    >
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M18 6 6 18M6 6l12 12"/></svg>
    </button>
  </div>
</template>
