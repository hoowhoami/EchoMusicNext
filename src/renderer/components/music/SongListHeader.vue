<script setup lang="ts">
import { iconChevronDown, iconChevronUp } from '@/icons';

export type SortField = 'index' | 'title' | 'album' | 'duration';
export type SortOrder = 'asc' | 'desc' | null;

interface Props {
  showIndex?: boolean;
  showAlbum?: boolean;
  showCover?: boolean;
  sortField?: SortField | null;
  sortOrder?: SortOrder;
}

const props = withDefaults(defineProps<Props>(), {
  showIndex: true,
  showAlbum: true,
  showCover: true,
  sortField: null,
  sortOrder: null,
});

const emit = defineEmits<{
  (e: 'sort', field: SortField): void;
}>();

const handleSort = (field: SortField) => {
  emit('sort', field);
};
</script>

<template>
  <div
    class="flex items-center px-6 py-2 text-[12px] text-text-main/80 font-bold border-b border-border-light/30"
  >
    <div
      v-if="showIndex"
      class="shrink-0 text-left pl-4 cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      @click="handleSort('index')"
    >
      <span>#</span>
      <Icon
        v-if="sortField === 'index'"
        class="sort-icon"
        :class="{ 'rotate-180': sortOrder === 'desc' }"
        :icon="iconChevronDown"
      />
      <span v-else class="sort-icon-stack">
        <Icon class="sort-icon" :icon="iconChevronUp" />
        <Icon class="sort-icon sort-icon-down" :icon="iconChevronDown" />
      </span>
    </div>

    <div
      class="flex-1 min-w-0 cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      :class="props.showCover ? 'ml-4' : ''"
      @click="handleSort('title')"
    >
      <span>标题</span>
      <Icon
        v-if="sortField === 'title'"
        class="sort-icon"
        :class="{ 'rotate-180': sortOrder === 'desc' }"
        :icon="iconChevronDown"
      />
      <span v-else class="sort-icon-stack">
        <Icon class="sort-icon" :icon="iconChevronUp" />
        <Icon class="sort-icon sort-icon-down" :icon="iconChevronDown" />
      </span>
    </div>

    <div
      v-if="showAlbum"
      class="w-48 min-w-0 hidden md:flex cursor-pointer hover:opacity-100 transition-opacity items-center gap-1 whitespace-nowrap"
      @click="handleSort('album')"
    >
      <span>专辑</span>
      <Icon
        v-if="sortField === 'album'"
        class="sort-icon"
        :class="{ 'rotate-180': sortOrder === 'desc' }"
        :icon="iconChevronDown"
      />
      <span v-else class="sort-icon-stack">
        <Icon class="sort-icon" :icon="iconChevronUp" />
        <Icon class="sort-icon sort-icon-down" :icon="iconChevronDown" />
      </span>
    </div>

    <div
      class="w-16 shrink-0 cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      @click="handleSort('duration')"
    >
      <span>时长</span>
      <Icon
        v-if="sortField === 'duration'"
        class="sort-icon"
        :class="{ 'rotate-180': sortOrder === 'desc' }"
        :icon="iconChevronDown"
      />
      <span v-else class="sort-icon-stack">
        <Icon class="sort-icon" :icon="iconChevronUp" />
        <Icon class="sort-icon sort-icon-down" :icon="iconChevronDown" />
      </span>
    </div>
  </div>
</template>

<style scoped>
@reference "@/style.css";

.sort-icon {
  width: 16px;
  height: 16px;
}

.sort-icon-stack {
  display: flex;
  flex-direction: column;
  line-height: 1;
  opacity: 0.4;
}

.sort-icon-down {
  margin-top: -10px;
}
</style>
