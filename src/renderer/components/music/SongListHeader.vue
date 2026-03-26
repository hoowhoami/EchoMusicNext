<script setup lang="ts">
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
  <div class="flex items-center px-6 py-2 text-[12px] text-text-secondary opacity-40 font-bold border-b border-border-light/30">
    <div v-if="showIndex" class="w-10 shrink-0 text-left pl-2">#</div>

    <div
      class="flex-1 min-w-0 cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      :class="props.showCover ? 'ml-4' : ''"
      @click="handleSort('title')"
    >
      <span>标题</span>
      <svg v-if="sortField === 'title'" class="w-3 h-3" :class="{ 'rotate-180': sortOrder === 'desc' }" viewBox="0 0 24 24" fill="currentColor">
        <path d="M7 10l5 5 5-5z"/>
      </svg>
    </div>

    <div
      v-if="showAlbum"
      class="w-48 min-w-0 hidden md:block cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      @click="handleSort('album')"
    >
      <span>专辑</span>
      <svg v-if="sortField === 'album'" class="w-3 h-3" :class="{ 'rotate-180': sortOrder === 'desc' }" viewBox="0 0 24 24" fill="currentColor">
        <path d="M7 10l5 5 5-5z"/>
      </svg>
    </div>

    <div
      class="w-16 shrink-0 cursor-pointer hover:opacity-100 transition-opacity flex items-center gap-1"
      @click="handleSort('duration')"
    >
      <span>时长</span>
      <svg v-if="sortField === 'duration'" class="w-3 h-3" :class="{ 'rotate-180': sortOrder === 'desc' }" viewBox="0 0 24 24" fill="currentColor">
        <path d="M7 10l5 5 5-5z"/>
      </svg>
    </div>
  </div>
</template>

<style scoped>
@reference "@/style.css";
</style>
