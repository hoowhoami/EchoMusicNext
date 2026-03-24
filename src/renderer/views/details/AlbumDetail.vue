<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getAlbumDetail, getAlbumSongs } from '../../api/album';
import SliverHeader from '../../components/music/DetailPageSliverHeader.vue';
import ActionRow from '../../components/music/DetailPageActionRow.vue';
import SongList from '../../components/music/SongList.vue';
import Tabs from '../../components/ui/Tabs.vue';
import TabsList from '../../components/ui/TabsList.vue';
import TabsTrigger from '../../components/ui/TabsTrigger.vue';
import TabsContent from '../../components/ui/TabsContent.vue';
import Badge from '../../components/ui/Badge.vue';
import { Song } from '../../stores/playlist';

const route = useRoute();
const router = useRouter();
const albumId = route.params.id as string;

const loading = ref(true);
const album = ref<any>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');

// 搜索和定位逻辑
const showSearch = ref(false);
const searchQuery = ref('');
const songListRef = ref<any>(null);

const fetchData = async () => {
  loading.value = true;
  try {
    const [detailRes, songsRes]: any = await Promise.all([
      getAlbumDetail(albumId),
      getAlbumSongs(albumId)
    ]);

    if (detailRes.status === 1) {
      album.value = detailRes.data?.[0] || detailRes.info?.[0];
    }
    
    if (songsRes.status === 1) {
      const list = songsRes.data?.info || songsRes.info || [];
      songs.value = list.map((item: any) => ({
        id: item.hash || item.mixsongid,
        title: item.songname || item.filename,
        artist: item.singername,
        album: item.album_name,
        duration: item.duration,
        coverUrl: item.img || item.cover,
        hash: item.hash,
        mixSongId: item.mixsongid
      }));
    }
  } catch (e) {
    console.error('Fetch album detail error:', e);
  } finally {
    loading.value = false;
  }
};

onMounted(fetchData);

const secondaryActions = computed(() => [
  {
    icon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>',
    label: '收藏',
    onTap: () => {}
  }
]);

const handlePlayAll = () => {};
const handleLocate = () => songListRef.value?.scrollToActive();
</script>

<template>
  <div class="album-detail-container bg-bg-main min-h-full">
    <div v-if="loading && !album" class="flex items-center justify-center py-40">
      <div class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <template v-else-if="album">
      <!-- 1. Sliver Header -->
      <SliverHeader 
        typeLabel="ALBUM"
        :title="album.album_name"
        :coverUrl="album.sizable_cover?.replace('{size}', '400') || album.imgurl"
        :expandedHeight="260"
      >
        <template #details>
          <div class="flex flex-col gap-1 text-text-main/60">
            <div class="text-[13px] font-semibold text-primary">
              {{ album.author_name }}
            </div>
            <div class="text-[11px] font-semibold opacity-40">
              {{ album.publish_date }} • {{ songs.length }} 首歌曲
            </div>
          </div>
        </template>

        <template #actions>
          <ActionRow 
            :secondaryActions="secondaryActions"
            @play="handlePlayAll"
          />
        </template>

        <template #collapsed-actions>
           <button @click="handlePlayAll" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-primary">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
           </button>
           <button @click="router.push({ name: 'comment', params: { id: albumId }, query: { type: 'album' } })" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>
           </button>
        </template>
      </SliverHeader>

      <!-- 2. 内容主体 -->
      <div class="relative z-10 px-8 pb-12">
        <Tabs v-model="activeTab" class="w-full">
          <!-- Sticky Tabs (Removed border-b) -->
          <div class="sticky top-[52px] z-50 bg-bg-main/95 backdrop-blur-md -mx-8 px-8 mb-4">
            <div class="flex items-center justify-between h-14">
              <TabsList class="bg-transparent border-none">
                <TabsTrigger value="songs">
                  <span class="relative">歌曲 <Badge :count="songs.length" /></span>
                </TabsTrigger>
                <TabsTrigger value="intro">
                  <span>介绍</span>
                </TabsTrigger>
              </TabsList>

              <!-- 右侧操作 -->
              <div v-if="activeTab === 'songs'" class="flex items-center gap-2">
                <Transition name="search-expand">
                  <div v-if="showSearch" class="relative">
                    <input 
                      v-model="searchQuery"
                      type="text" 
                      placeholder="搜索歌曲..." 
                      class="w-48 h-9 pl-8 pr-3 rounded-lg bg-black/[0.05] dark:bg-white/[0.08] outline-none text-[12px] focus:ring-1 focus:ring-primary/30 transition-all"
                      @blur="!searchQuery && (showSearch = false)"
                      autofocus
                    />
                    <svg class="absolute left-2.5 top-1/2 -translate-y-1/2 opacity-30" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
                  </div>
                </Transition>
                <button @click="showSearch = !showSearch" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60" :class="{ 'text-primary opacity-100': showSearch }">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
                </button>
                <button @click="handleLocate" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 opacity-60">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="12" cy="12" r="3"/><path d="M3 12h3m12 0h3M12 3v3m0 12v3"/></svg>
                </button>
              </div>
            </div>
          </div>

          <TabsContent value="songs">
            <div class="sticky top-[108px] z-40 bg-bg-main/95 backdrop-blur-md flex items-center px-4 py-2 text-[12px] text-text-secondary opacity-40 font-bold border-b border-border-light/30 -mx-4 mb-2">
              <div class="w-10 shrink-0 text-center">#</div>
              <div class="flex-1 min-w-0 ml-4">标题</div>
              <div class="w-1/4 min-w-0 ml-4 hidden md:block">专辑</div>
              <div class="w-20 shrink-0 text-right">时长</div>
              <div class="w-10 shrink-0"></div>
            </div>
            <SongList ref="songListRef" :songs="songs" :searchQuery="searchQuery" />
          </TabsContent>

          <TabsContent value="intro" class="mt-4">
            <div class="max-w-3xl text-[14px] leading-relaxed text-text-secondary whitespace-pre-wrap py-4 px-2 opacity-80">
              {{ album.intro || '暂无专辑介绍' }}
            </div>
          </TabsContent>
        </Tabs>
      </div>
    </template>
  </div>
</template>

<style scoped>
@reference "../../style.css";

.album-detail-container {
  margin-top: -52px;
}

.tab-item {
  @apply relative h-full flex items-center text-[15px] font-bold text-text-main opacity-40 transition-all cursor-pointer;
}

.tab-item.active {
  @apply opacity-100;
}

.tab-badge {
  @apply absolute -top-1.5 -right-5 text-[9px] font-black opacity-40 scale-90;
}

.active-line {
  @apply absolute bottom-0 left-1/2 -translate-x-1/2 w-0 h-0.5 bg-primary rounded-full transition-all duration-300;
}

.tab-item.active .active-line {
  @apply w-6;
}

.search-expand-enter-active, .search-expand-leave-active {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.search-expand-enter-from, .search-expand-leave-to {
  opacity: 0;
  width: 0;
  transform: translateX(10px);
}

:deep(.song-list) {
  @apply px-0;
}
</style>
