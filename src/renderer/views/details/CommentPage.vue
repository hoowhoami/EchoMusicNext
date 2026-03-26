<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useRoute } from 'vue-router';
import { getMusicComments, getPlaylistComments, getAlbumComments } from '@/api/comment';
import CommentList from '@/components/music/CommentList.vue';

const route = useRoute();
const id = route.params.id as string;
const type = route.query.type as 'music' | 'playlist' | 'album';

const loading = ref(true);
const comments = ref<any[]>([]);
const total = ref(0);
const page = ref(1);

const title = computed(() => {
  switch (type) {
    case 'music': return '歌曲评论';
    case 'playlist': return '歌单评论';
    case 'album': return '专辑评论';
    default: return '评论';
  }
});

const fetchData = async () => {
  loading.value = true;
  try {
    let res: any;
    if (type === 'music') res = await getMusicComments(id, page.value);
    else if (type === 'playlist') res = await getPlaylistComments(id, page.value);
    else if (type === 'album') res = await getAlbumComments(id, page.value);

    if (res?.status === 1) {
      const data = res.data || res.info;
      const list = data.list || data.comments || [];
      comments.value = list.map((item: any) => ({
        id: item.comment_id || item.id,
        userName: item.user_name || item.nickname,
        avatar: item.user_img || item.avatar,
        content: item.content,
        time: item.add_time || item.time,
        likeCount: item.like_count || item.count || 0
      }));
      total.value = data.total || data.count || 0;
    }
  } catch (e) {
    console.error('Fetch comments error:', e);
  } finally {
    loading.value = false;
  }
};

onMounted(fetchData);
</script>

<template>
  <div class="comment-page px-8 py-12">
    <div class="max-w-4xl mx-auto">
      <div class="flex items-center justify-between mb-10">
        <h1 class="text-2xl font-bold text-text-main flex items-center gap-3">
          {{ title }}
          <span v-if="total > 0" class="text-[14px] font-normal opacity-40">({{ total }})</span>
        </h1>
        
        <!-- 返回按钮 -->
        <button @click="$router.back()" class="p-2 rounded-full hover:bg-black/5 dark:hover:bg-white/10 transition-colors">
          <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M18 6 6 18"/><path d="m6 6 12 12"/>
          </svg>
        </button>
      </div>

      <!-- 评论输入框 (模拟) -->
      <div class="mb-12">
        <div class="relative">
          <textarea 
            placeholder="发表评论..." 
            class="w-full h-24 p-4 rounded-2xl bg-black/[0.03] dark:bg-white/[0.03] border border-transparent focus:border-primary/30 focus:bg-white dark:focus:bg-black/20 outline-none transition-all resize-none text-[14px]"
          ></textarea>
          <button class="absolute bottom-3 right-3 px-4 py-1.5 rounded-full bg-primary text-white text-[12px] font-medium shadow-lg shadow-primary/20 hover:bg-primary-hover transition-all">
            发布评论
          </button>
        </div>
      </div>

      <CommentList :comments="comments" :loading="loading" :total="total" />
      
      <!-- 加载更多 -->
      <div v-if="comments.length < total" class="flex justify-center mt-10">
        <button 
          @click="page++; fetchData()" 
          :disabled="loading"
          class="px-8 py-2 rounded-full border border-border-light hover:bg-black/5 dark:hover:bg-white/10 text-[13px] transition-all disabled:opacity-50"
        >
          {{ loading ? '加载中...' : '加载更多' }}
        </button>
      </div>
    </div>
  </div>
</template>
