<script setup lang="ts">
import { TabsList, type TabsListProps, useForwardProps } from 'reka-ui';

const props = defineProps<TabsListProps & { class?: string }>();
const forwardedProps = useForwardProps(props);
</script>

<template>
  <TabsList
    v-bind="forwardedProps"
    :class="['flex items-center gap-8 h-14', props.class]"
  >
    <slot />
  </TabsList>
</template>

<style scoped>
@reference "../../style.css";
</style>
