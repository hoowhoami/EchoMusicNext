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
  isHot?: boolean;
  replyActionLabel?: string;
  specialId?: string;
  tid?: string;
  code?: string;
  mixSongId?: string;
  isStar?: boolean;
}

interface Props {
  comments: Comment[];
  total?: number;
  loading?: boolean;
  showDivider?: boolean;
  onTapReplies?: (comment: Comment) => void;
}

const props = withDefaults(defineProps<Props>(), {
  showDivider: true,
});

const formatLike = (value: number) => {
  if (value < 10000) return value.toString();
  const fixed = (value / 10000).toFixed(value >= 100000 ? 0 : 1);
  return `${fixed.replace(/\.0$/, '')}w`;
};
</script>

<template>
  <div class="comment-list flex flex-col gap-6">
    <div v-if="loading && comments.length === 0" class="flex justify-center py-10">
      <div class="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <div v-else-if="comments.length === 0" class="text-center py-10 text-text-secondary opacity-50">
      暂无评论
    </div>

    <div v-for="comment in comments" :key="comment.id" class="comment-card">
      <div class="comment-card-inner" :class="{ 'is-star': comment.isStar }">
        <div class="comment-header">
          <div class="comment-avatar">
            <img v-if="comment.avatar" :src="comment.avatar" alt="avatar" />
            <div v-else class="comment-avatar-fallback">?</div>
          </div>
          <div class="comment-meta">
            <div class="comment-name">
              {{ comment.userName }}
              <span v-if="comment.isHot" class="comment-badge">热门</span>
              <span v-if="comment.isStar" class="comment-badge comment-badge-star">歌手说</span>
            </div>
            <div class="comment-time">{{ comment.time }}</div>
          </div>
          <div class="comment-like">
            <Icon :icon="iconThumbsUp" width="12" height="12" />
            {{ formatLike(comment.likeCount) }}
          </div>
        </div>

        <p class="comment-content">
          {{ comment.content }}
        </p>

        <button
          v-if="comment.replyCount && comment.replyCount > 0"
          class="comment-reply"
          @click="props.onTapReplies?.(comment)"
        >
          {{ comment.replyActionLabel || `查看${comment.replyCount}条回复` }}
        </button>
      </div>
      <div v-if="showDivider" class="comment-divider"></div>
    </div>
  </div>
</template>

<style scoped>
@reference "@/style.css";

.comment-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.comment-card-inner {
  padding: 18px;
  border-radius: 18px;
  background: color-mix(in srgb, var(--color-text-main) 3%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-text-main) 6%, transparent);
}

.comment-card-inner.is-star {
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
  border-color: color-mix(in srgb, var(--color-primary) 28%, transparent);
}

.comment-header {
  display: flex;
  align-items: flex-start;
  gap: 12px;
}

.comment-avatar {
  width: 36px;
  height: 36px;
  border-radius: 14px;
  overflow: hidden;
  flex-shrink: 0;
  background: color-mix(in srgb, var(--color-primary) 12%, transparent);
  display: flex;
  align-items: center;
  justify-content: center;
}

.comment-avatar img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.comment-avatar-fallback {
  font-size: 14px;
  font-weight: 700;
  color: var(--color-primary);
}

.comment-meta {
  flex: 1;
  min-width: 0;
}

.comment-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--color-text-main);
  display: flex;
  align-items: center;
  gap: 8px;
}

.comment-time {
  margin-top: 2px;
  font-size: 10px;
  color: color-mix(in srgb, var(--color-text-main) 55%, transparent);
}

.comment-like {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border-radius: 999px;
  background: var(--color-bg-card);
  border: 1px solid color-mix(in srgb, var(--color-text-main) 6%, transparent);
  color: var(--color-primary);
  font-size: 11px;
  font-weight: 600;
}

.comment-content {
  margin-top: 12px;
  font-size: 14px;
  line-height: 1.5;
  color: var(--color-text-main);
  white-space: pre-wrap;
  word-break: break-word;
}

.comment-reply {
  margin-top: 12px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-primary);
}

.comment-badge {
  padding: 2px 6px;
  border-radius: 999px;
  font-size: 10px;
  font-weight: 700;
  color: var(--color-primary);
  background: color-mix(in srgb, var(--color-primary) 12%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-primary) 20%, transparent);
}

.comment-badge-star {
  color: #f59e0b;
  border-color: rgba(245, 158, 11, 0.35);
  background: rgba(245, 158, 11, 0.15);
}

.comment-divider {
  height: 1px;
  background: color-mix(in srgb, var(--color-border-light) 60%, transparent);
}
</style>
