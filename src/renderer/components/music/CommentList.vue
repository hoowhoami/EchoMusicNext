<script setup lang="ts">
import { iconThumbsUp } from '@/icons';
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
          <span class="text-[11px] text-text-secondary opacity-60">{{ comment.time }}</span>
        </div>
        
        <p class="text-[14px] leading-relaxed text-text-main whitespace-pre-wrap break-words">
          {{ comment.content }}
        </p>

        <div class="flex items-center gap-4 mt-1">
          <button class="flex items-center gap-1 text-[11px] text-text-secondary hover:text-primary transition-colors">
            <Icon :icon="iconThumbsUp" width="14" height="14" />
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
