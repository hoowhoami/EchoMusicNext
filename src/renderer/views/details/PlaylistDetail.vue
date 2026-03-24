<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { getPlaylistDetail, getPlaylistTracks, getPlaylistTracksNew } from '../../api/playlist';
import SliverHeader from '../../components/music/DetailPageSliverHeader.vue';
import ActionRow from '../../components/music/DetailPageActionRow.vue';
import SongList from '../../components/music/SongList.vue';
import Avatar from '../../components/ui/Avatar.vue';
import Tabs from '../../components/ui/Tabs.vue';
import TabsList from '../../components/ui/TabsList.vue';
import TabsTrigger from '../../components/ui/TabsTrigger.vue';
import TabsContent from '../../components/ui/TabsContent.vue';
import Badge from '../../components/ui/Badge.vue';
import { Song } from '../../stores/playlist';
import { formatDate } from '../../utils/format';

const route = useRoute();
const router = useRouter();
const playlistId = route.params.id as string;
const listType = route.query.type as string;

const loading = ref(true);
const playlist = ref<any>(null);
const songs = ref<Song[]>([]);
const activeTab = ref('songs');

// 搜索和定位逻辑
const showSearch = ref(false);
const searchQuery = ref('');
const songListRef = ref<any>(null);

const fetchData = async () => {
  loading.value = true;
  try {
    const detailRes: any = await getPlaylistDetail(playlistId);
    if (detailRes.status === 1) {
      playlist.value = detailRes.data || detailRes.info;
    }

    let tracksRes: any;
    try {
      if (listType === 'user') {
        tracksRes = await getPlaylistTracksNew(playlistId);
      } else {
        tracksRes = await getPlaylistTracks(playlistId);
      }
    } catch (e) {
      tracksRes = await (listType === 'user' ? getPlaylistTracks(playlistId) : getPlaylistTracksNew(playlistId));
    }
    
    if (tracksRes?.status === 1) {
      const list = tracksRes.data?.info || tracksRes.info || [];
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
    console.error('Fetch playlist error:', e);
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
  <div class="playlist-detail-container bg-bg-main min-h-full">
    <div v-if="loading && !playlist" class="flex items-center justify-center py-40">
      <div class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <template v-else-if="playlist">
      <!-- 1. Sliver Header -->
      <SliverHeader 
        typeLabel="PLAYLIST"
        :title="playlist.specialname || playlist.name"
        :coverUrl="playlist.imgurl || playlist.cover"
        :expandedHeight="260"
      >
        <template #details>
          <div class="flex items-center gap-3 text-text-main/70">
            <div class="flex items-center gap-2">
              <Avatar :src="playlist.user_pic || playlist.avatar" :size="20" class="rounded-full overflow-hidden" />
              <span class="text-[13px] font-semibold text-primary">{{ playlist.nickname || 'Unknown' }}</span>
            </div>
            <span class="text-[11px] font-semibold opacity-40">{{ formatDate(playlist.publishtime || playlist.create_time, 'YYYY-MM-DD') }} 创建</span>
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
           <button @click="router.push({ name: 'comment', params: { id: playlistId }, query: { type: 'playlist' } })" class="p-2 rounded-lg hover:bg-black/5 dark:hover:bg-white/5 text-text-main opacity-60">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>
           </button>
        </template>
      </SliverHeader>

      <!-- 2. 内容主体 -->
      <div class="relative z-10">
        <!-- 2.1 歌单介绍 -->
        <div v-if="playlist.intro" class="px-8 py-4">
          <h3 class="text-[15px] font-bold text-text-main mb-1.5">歌单介绍</h3>
          <p class="text-[12px] text-text-secondary line-clamp-1 leading-relaxed opacity-80">{{ playlist.intro }}</p>
          <button class="text-[11px] font-bold text-primary mt-1 hover:underline">查看详情</button>
        </div>

        <!-- 2.2 底部 Tabs -->
        <div class="px-8 pb-12">
          <Tabs v-model="activeTab" class="w-full">
            <div class="sticky top-[52px] z-[90] bg-bg-main -mx-8 px-8 mb-4">
              <div class="flex items-center justify-between h-14">
                <TabsList class="bg-transparent border-none">
                  <TabsTrigger value="songs">
                    <span class="relative">
                      歌曲
                      <Badge :count="songs.length" />
                    </span>
                  </TabsTrigger>
                  <TabsTrigger value="comments">
                    <span class="relative">
                      评论
                      <Badge :count="playlist.commentcount || 0" />
                    </span>
                  </TabsTrigger>
                </TabsList>

                <!-- 右侧搜索与定位 -->
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
              <!-- 表头 (不透明) -->
              <div class="sticky top-[106px] z-[80] bg-bg-main flex items-center px-4 py-2 text-[12px] text-text-secondary opacity-40 font-bold border-b border-border-light/30 -mx-4 mb-2 shadow-[0_2px_10px_-5px_rgba(0,0,0,0.05)]">
                <div class="w-10 shrink-0 text-center">#</div>
                <div class="flex-1 min-w-0 ml-4">标题</div>
                <div class="w-1/4 min-w-0 ml-4 hidden md:block">专辑</div>
                <div class="w-20 shrink-0 text-right">时长</div>
                <div class="w-10 shrink-0"></div>
              </div>
              <SongList ref="songListRef" :songs="songs" :searchQuery="searchQuery" />
            </TabsContent>
            
            <TabsContent value="comments" class="py-20 text-center">
              <button 
                @click="router.push({ name: 'comment', params: { id: playlistId }, query: { type: 'playlist' } })"
                class="px-8 py-2 rounded-full bg-primary/10 text-primary font-bold text-[14px]"
              >
                查看全部 {{ playlist.commentcount || 0 }} 条评论
              </button>
            </TabsContent>
          </Tabs>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
@reference "../../style.css";

.playlist-detail-container {
  margin-top: -52px;
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
