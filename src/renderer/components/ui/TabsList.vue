<script setup lang="ts">
import { TabsList, type TabsListProps, useForwardProps } from 'radix-vue';

const props = defineProps<TabsListProps & { class?: string }>();
const forwardedProps = useForwardProps(props);
</script>

<template>
  <TabsList
    v-bind="forwardedProps"
    :class="['flex items-center justify-center', props.class]"
  >
    <slot />
  </TabsList>
</template>
