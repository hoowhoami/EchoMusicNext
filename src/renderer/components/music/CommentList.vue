<script setup lang="ts">
interface Comment {
  id: string | number;
  userName: string;
  avatar: string;
  content: string;
  time: string;
  likeCount: number;
  replyCount?: number;
}

interface Props {
  comments: Comment[];
  total?: number;
  loading?: boolean;
}

const props = defineProps<Props>();
</script>

<template>
  <div class="comment-list flex flex-col gap-6">
    <div v-if="loading && comments.length === 0" class="flex justify-center py-10">
      <div class="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <div v-else-if="comments.length === 0" class="text-center py-10 text-text-secondary opacity-50">
      暂无评论
    </div>

    <div v-for="comment in comments" :key="comment.id" class="flex gap-4">
      <!-- 头像 -->
      <img :src="comment.avatar" class="w-10 h-10 rounded-full object-cover shrink-0 bg-black/5" />

      <!-- 内容 -->
      <div class="flex-1 flex flex-col gap-1.5 min-w-0">
        <div class="flex items-center justify-between">
          <span class="text-[13px] font-bold text-text-main opacity-80">{{ comment.userName }}</span>
          <span class="text-[11px] text-text-secondary opacity-40">{{ comment.time }}</span>
        </div>
        
        <p class="text-[14px] leading-relaxed text-text-main whitespace-pre-wrap break-words">
          {{ comment.content }}
        </p>

        <div class="flex items-center gap-4 mt-1">
          <button class="flex items-center gap-1 text-[11px] text-text-secondary hover:text-primary transition-colors">
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M7 10v12"/><path d="M15 5.88 14 10h5.83a2 2 0 0 1 1.92 2.56l-2.33 8A2 2 0 0 1 17.5 22H4a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2h2.76a2 2 0 0 0 1.79-1.11L12 2h0a3.13 3.13 0 0 1 3 3.88Z"/>
            </svg>
            {{ comment.likeCount }}
          </button>
          
          <button class="text-[11px] text-text-secondary hover:text-primary transition-colors">
            回复
          </button>
        </div>
        
        <!-- 分割线 -->
        <div class="w-full h-px bg-border-light/30 mt-4"></div>
      </div>
    </div>
  </div>
</template>
