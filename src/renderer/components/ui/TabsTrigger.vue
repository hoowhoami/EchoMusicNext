<script setup lang="ts">
import { TabsTrigger, type TabsTriggerProps, useForwardProps } from 'reka-ui';
import { computed } from 'vue';

const props = defineProps<TabsTriggerProps & { class?: string }>();
const forwardedProps = useForwardProps(props);
</script>

<template>
  <TabsTrigger
    v-bind="forwardedProps"
    :class="[
      'inline-flex items-center justify-center whitespace-nowrap transition-all focus-visible:outline-none disabled:pointer-events-none disabled:opacity-50',
      props.class
    ]"
  >
    <slot />
  </TabsTrigger>
</template>
