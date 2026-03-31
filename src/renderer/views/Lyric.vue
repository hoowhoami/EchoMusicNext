<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { usePlayerStore, type PlayMode } from '@/stores/player';
import { usePlaylistStore } from '@/stores/playlist';
import { useLyricStore } from '@/stores/lyric';
import type { Song } from '@/models/song';
import OverlayHeader from '@/layouts/OverlayHeader.vue';
import Cover from '@/components/ui/Cover.vue';
import Slider from '@/components/ui/Slider.vue';
import { formatDuration } from '@/utils/format';
import { closeTransientView } from '@/utils/navigation';
import { getCoverUrl } from '@/utils/music';
import Button from '@/components/ui/Button.vue';
import {
  iconChevronDown,
  iconHeart,
  iconPause,
  iconPlay,
  iconRepeat,
  iconListRestart,
  iconShuffle,
  iconSkipBack,
  iconSkipForward,
  iconVolume2,
  iconVolume1,
  iconVolumeX,
} from '@/icons';

const router = useRouter();
const route = useRoute();
const playerStore = usePlayerStore();
const playlistStore = usePlaylistStore();
const lyricStore = useLyricStore();

const lyricListRef = ref<HTMLElement | null>(null);
const progressValue = ref(0);
const isProgressDragging = ref(false);
const volumeValue = ref(Math.round(playerStore.volume * 100));

const currentTrack = computed<Song | undefined>(() => {
  const currentId = String(playerStore.currentTrackId ?? '');
  if (!currentId) return undefined;
  return (
    playlistStore.defaultList.find((song) => String(song.id) === currentId) ||
    playlistStore.favorites.find((song) => String(song.id) === currentId) ||
    playlistStore.history.find((song) => String(song.id) === currentId)
  );
});

const backgroundUrl = computed(() => getCoverUrl(currentTrack.value?.coverUrl, 900));

const currentIndex = computed(() => lyricStore.currentIndex);
const hasLyrics = computed(() => lyricStore.lines.length > 0);
const isFavorite = computed(() => {
  if (!currentTrack.value) return false;
  return playlistStore.favorites.some((song) => String(song.id) === String(currentTrack.value?.id));
});

const playModeMeta = computed(() => {
  const map: Record<PlayMode, { icon: typeof iconRepeat; label: string }> = {
    list: { icon: iconRepeat, label: '列表循环' },
    single: { icon: iconListRestart, label: '单曲循环' },
    random: { icon: iconShuffle, label: '随机播放' },
  };
  return map[playerStore.playMode];
});

const volumeIcon = computed(() => {
  if (playerStore.volume <= 0) return iconVolumeX;
  if (playerStore.volume < 0.45) return iconVolume1;
  return iconVolume2;
});

const scrollToCurrentLine = (smooth: boolean) => {
  const container = lyricListRef.value;
  const index = lyricStore.currentIndex;
  if (!container || index < 0) return;

  const target = container.querySelector<HTMLElement>(`[data-lyric-index="${index}"]`);
  if (!target) return;

  const offset = target.offsetTop - container.clientHeight / 2 + target.offsetHeight / 2;
  container.scrollTo({ top: Math.max(0, offset), behavior: smooth ? 'smooth' : 'auto' });
};

const cyclePlayMode = () => {
  const nextMode: Record<PlayMode, PlayMode> = {
    list: 'random',
    random: 'single',
    single: 'list',
  };
  playerStore.setPlayMode(nextMode[playerStore.playMode]);
};

const toggleFavorite = () => {
  if (!currentTrack.value) return;
  if (isFavorite.value) {
    void playlistStore.removeFromFavorites(String(currentTrack.value.id));
    return;
  }
  void playlistStore.addToFavorites(currentTrack.value);
};

const handleProgressInput = (value: number) => {
  progressValue.value = value;
};

const handleProgressCommit = (value: number) => {
  playerStore.notifySeekEnd();
  isProgressDragging.value = false;
  playerStore.seek(value);
};

const handleProgressPointerDown = () => {
  isProgressDragging.value = true;
  playerStore.notifySeekStart();
};

const handleVolumeInput = (value: number) => {
  volumeValue.value = value;
  playerStore.setVolume(value / 100);
};

watch(
  () => lyricStore.currentIndex,
  async (index, previous) => {
    if (index === previous) return;
    await nextTick();
    scrollToCurrentLine(previous !== -1);
  },
);

watch(
  () => playerStore.currentTime,
  (value) => {
    if (isProgressDragging.value) return;
    progressValue.value = value;
  },
  { immediate: true },
);

watch(
  () => playerStore.volume,
  (value) => {
    volumeValue.value = Math.round(value * 100);
  },
  { immediate: true },
);

watch(
  () => currentTrack.value?.id,
  async () => {
    await nextTick();
    scrollToCurrentLine(false);
  },
);

const closeLyricPage = async () => {
  await closeTransientView(router, route);
};

const handleKeydown = (event: KeyboardEvent) => {
  if (event.key !== 'Escape') return;
  event.preventDefault();
  void closeLyricPage();
};

onMounted(() => {
  void nextTick(() => scrollToCurrentLine(false));
  window.addEventListener('keydown', handleKeydown);
});

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeydown);
});
</script>

<template>
  <div class="lyric-view relative h-screen w-screen overflow-hidden text-white select-none">
    <div class="absolute inset-0 overflow-hidden">
      <div
        class="absolute inset-[-6%] scale-110 bg-cover bg-center blur-[90px] saturate-[1.35]"
        :style="{ backgroundImage: backgroundUrl ? `url(${backgroundUrl})` : undefined }"
      ></div>
      <div class="absolute inset-0 lyric-view__backdrop"></div>
      <div class="absolute inset-0 lyric-view__noise opacity-[0.16]"></div>
      <div
        class="absolute left-[8%] top-[16%] h-56 w-56 rounded-full bg-primary/20 blur-[120px]"
      ></div>
      <div
        class="absolute bottom-[12%] right-[10%] h-72 w-72 rounded-full bg-primary/18 blur-[140px]"
      ></div>
    </div>

    <OverlayHeader />

    <div class="relative z-10 flex h-full flex-col">
      <div class="flex items-center justify-between px-6 pt-11 pb-3 no-drag">
        <Button variant="unstyled" size="none" type="button" class="lyric-top-btn" title="返回" @click="closeLyricPage">
          <Icon :icon="iconChevronDown" width="22" height="22" />
        </Button>
        <div class="text-center max-w-[48vw]">
          <p class="text-[11px] uppercase tracking-[0.45em] text-white/45">Lyrics</p>
          <h1 class="mt-1 truncate text-[14px] font-semibold text-white/85">
            {{ currentTrack?.album || 'EchoMusic' }}
          </h1>
        </div>
        <div class="w-11"></div>
      </div>

      <main class="flex-1 min-h-0 px-8 pb-8 pt-2">
        <div class="mx-auto flex h-full max-w-[1520px] items-center gap-10 xl:gap-14">
          <section class="flex min-w-0 flex-[0.95] items-center justify-center">
            <div class="w-full max-w-[460px] text-center">
              <div class="lyric-cover-shell mx-auto">
                <div class="lyric-cover-glow"></div>
                <div class="lyric-cover-frame">
                  <Cover
                    :url="currentTrack?.coverUrl"
                    :size="800"
                    class="h-full w-full"
                    :borderRadius="30"
                  />
                </div>
              </div>

              <div class="mt-10 space-y-3 px-4">
                <h2
                  class="truncate text-[clamp(30px,4vw,48px)] font-black tracking-[-0.04em] text-white"
                >
                  {{ currentTrack?.title || '未在播放' }}
                </h2>
                <p class="truncate text-[clamp(16px,2vw,22px)] font-semibold text-white/62">
                  {{ currentTrack?.artist || '等待播放中' }}
                </p>
              </div>
            </div>
          </section>

          <section class="flex min-w-0 flex-[1.25] flex-col justify-center self-stretch pb-5 pt-8">
            <div
              class="flex-1 min-h-0 overflow-hidden rounded-[36px] border border-white/8 bg-white/[0.045] px-6 py-6 shadow-[0_24px_90px_rgba(0,0,0,0.26)] backdrop-blur-2xl"
            >
              <div ref="lyricListRef" class="lyric-scroll h-full overflow-y-auto px-3">
                <div class="min-h-full py-[32vh]">
                  <template v-if="hasLyrics">
                    <Button variant="unstyled" size="none"
                      v-for="(line, index) in lyricStore.lines"
                      :key="`${line.time}-${index}`"
                      type="button"
                      :data-lyric-index="index"
                      :class="[
                        'lyric-line group w-full border-none bg-transparent px-4 py-4 text-left transition-all duration-300',
                        currentIndex === index
                          ? 'is-current'
                          : currentIndex > index
                            ? 'is-past'
                            : 'is-future',
                      ]"
                      @click="playerStore.seek(line.time)"
                    >
                      <span
                        class="block text-[clamp(22px,2.8vw,38px)] font-black tracking-[-0.03em] leading-[1.22]"
                      >
                        {{ line.text }}
                      </span>
                    </Button>
                  </template>
                  <div
                    v-else
                    class="flex min-h-[40vh] items-center justify-center px-8 text-center"
                  >
                    <div class="space-y-3">
                      <p class="text-[30px] font-black tracking-[-0.04em] text-white/88">
                        纯音乐，请欣赏
                      </p>
                      <p class="text-sm font-medium text-white/45">当前歌曲暂无可滚动歌词</p>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <div
              class="mt-7 rounded-[30px] border border-white/8 bg-black/20 px-6 py-5 shadow-[0_18px_56px_rgba(0,0,0,0.24)] backdrop-blur-2xl no-drag"
            >
              <div
                class="mb-3 flex items-center justify-between text-[12px] font-semibold text-white/42"
              >
                <span>{{
                  formatDuration(isProgressDragging ? progressValue : playerStore.currentTime)
                }}</span>
                <span>{{ formatDuration(playerStore.duration) }}</span>
              </div>

              <div @pointerdown="handleProgressPointerDown">
                <Slider
                  :model-value="progressValue"
                  :min="0"
                  :max="Math.max(playerStore.duration, 1)"
                  :step="1"
                  class="lyric-progress-slider"
                  track-class="lyric-progress-track"
                  range-class="lyric-progress-range"
                  thumb-class="lyric-progress-thumb"
                  @update:model-value="handleProgressInput"
                  @value-commit="handleProgressCommit"
                />
              </div>

              <div class="mt-6 flex items-center justify-between gap-4">
                <div class="flex items-center gap-2.5">
                  <Button variant="unstyled" size="none" type="button" class="lyric-icon-btn" title="收藏" @click="toggleFavorite">
                    <Icon
                      :icon="iconHeart"
                      width="20"
                      height="20"
                      :class="isFavorite ? 'text-[#ff6b81]' : 'text-white/72'"
                    />
                  </Button>
                  <Button variant="unstyled" size="none"
                    type="button"
                    class="lyric-icon-btn"
                    :title="playModeMeta.label"
                    @click="cyclePlayMode"
                  >
                    <Icon :icon="playModeMeta.icon" width="20" height="20" class="text-white/78" />
                  </Button>
                </div>

                <div class="flex items-center gap-3">
                  <Button variant="unstyled" size="none"
                    type="button"
                    class="lyric-transport-btn"
                    title="上一曲"
                    @click="playerStore.prev()"
                  >
                    <Icon :icon="iconSkipBack" width="22" height="22" />
                  </Button>
                  <Button variant="unstyled" size="none"
                    type="button"
                    class="lyric-play-btn"
                    :title="playerStore.isPlaying ? '暂停' : '播放'"
                    @click="playerStore.togglePlay()"
                  >
                    <Icon
                      :icon="playerStore.isPlaying ? iconPause : iconPlay"
                      width="26"
                      height="26"
                    />
                  </Button>
                  <Button variant="unstyled" size="none"
                    type="button"
                    class="lyric-transport-btn"
                    title="下一曲"
                    @click="playerStore.next()"
                  >
                    <Icon :icon="iconSkipForward" width="22" height="22" />
                  </Button>
                </div>

                <div class="flex min-w-[190px] items-center justify-end gap-3">
                  <Icon :icon="volumeIcon" width="18" height="18" class="text-white/62" />
                  <Slider
                    :model-value="volumeValue"
                    :min="0"
                    :max="100"
                    :step="1"
                    class="w-[132px]"
                    track-class="lyric-volume-track"
                    range-class="lyric-volume-range"
                    thumb-class="lyric-volume-thumb"
                    @update:model-value="handleVolumeInput"
                    @value-commit="handleVolumeInput"
                  />
                </div>
              </div>
            </div>
          </section>
        </div>
      </main>
    </div>
  </div>
</template>

<style scoped>
.lyric-view {
  animation: lyric-fade-in 0.55s ease;
}

.lyric-view__backdrop {
  background:
    linear-gradient(
      180deg,
      rgba(8, 10, 16, 0.72) 0%,
      rgba(8, 10, 16, 0.36) 24%,
      rgba(8, 10, 16, 0.58) 100%
    ),
    radial-gradient(
      circle at top left,
      color-mix(in srgb, var(--color-primary) 36%, transparent) 0%,
      transparent 36%
    ),
    radial-gradient(
      circle at bottom right,
      color-mix(in srgb, var(--color-primary) 24%, transparent) 0%,
      transparent 40%
    ),
    rgba(7, 8, 12, 0.34);
}

.lyric-view__noise {
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.06) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.045) 1px, transparent 1px);
  background-size: 24px 24px;
  mix-blend-mode: soft-light;
}

.lyric-top-btn,
.lyric-icon-btn,
.lyric-transport-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  background: rgba(255, 255, 255, 0.05);
  color: rgba(255, 255, 255, 0.88);
  backdrop-filter: blur(20px);
  transition:
    transform 0.2s ease,
    background-color 0.2s ease,
    border-color 0.2s ease;
}

.lyric-top-btn:hover,
.lyric-icon-btn:hover,
.lyric-transport-btn:hover,
.lyric-play-btn:hover {
  transform: translateY(-1px);
}

.lyric-top-btn,
.lyric-icon-btn {
  width: 44px;
  height: 44px;
  border-radius: 999px;
}

.lyric-transport-btn {
  width: 50px;
  height: 50px;
  border-radius: 999px;
}

.lyric-play-btn {
  width: 66px;
  height: 66px;
  border: none;
  border-radius: 999px;
  background: linear-gradient(
    135deg,
    color-mix(in srgb, var(--color-primary) 76%, white 18%),
    color-mix(in srgb, var(--color-primary) 92%, black 6%)
  );
  color: white;
  box-shadow: 0 18px 40px color-mix(in srgb, var(--color-primary) 34%, transparent);
  transition:
    transform 0.2s ease,
    box-shadow 0.25s ease;
}

.lyric-cover-shell {
  position: relative;
  width: min(28vw, 420px);
  aspect-ratio: 1;
}

.lyric-cover-glow {
  position: absolute;
  inset: -24px;
  border-radius: 40px;
  background:
    radial-gradient(
      circle at 50% 35%,
      color-mix(in srgb, var(--color-primary) 42%, white 10%) 0%,
      transparent 68%
    ),
    radial-gradient(circle at 50% 100%, rgba(255, 255, 255, 0.2) 0%, transparent 52%);
  filter: blur(28px);
  opacity: 0.95;
}

.lyric-cover-frame {
  position: relative;
  height: 100%;
  width: 100%;
  overflow: hidden;
  border-radius: 30px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  box-shadow:
    0 36px 80px rgba(0, 0, 0, 0.45),
    inset 0 1px 0 rgba(255, 255, 255, 0.12);
}

.lyric-scroll {
  scrollbar-width: none;
  mask-image: linear-gradient(to bottom, transparent 0%, black 13%, black 87%, transparent 100%);
}

.lyric-scroll::-webkit-scrollbar {
  display: none;
}

.lyric-line {
  border-radius: 24px;
  opacity: 0.42;
  transform: scale(0.96);
}

.lyric-line span {
  text-wrap: balance;
}

.lyric-line.is-current {
  opacity: 1;
  transform: scale(1);
  color: white;
  text-shadow: 0 12px 28px rgba(0, 0, 0, 0.3);
}

.lyric-line.is-current span {
  color: color-mix(in srgb, white 78%, var(--color-primary) 22%);
}

.lyric-line.is-current::before {
  content: '';
  position: absolute;
  inset: 6px 0;
  border-radius: 18px;
  background: linear-gradient(
    90deg,
    color-mix(in srgb, var(--color-primary) 16%, transparent),
    transparent 72%
  );
  opacity: 0.9;
  z-index: -1;
}

.lyric-line.is-past {
  opacity: 0.22;
}

.lyric-line.is-future:hover,
.lyric-line.is-past:hover {
  opacity: 0.68;
  transform: scale(0.985);
}

:deep(.lyric-progress-slider.slider-root-horizontal) {
  height: 18px;
}

:deep(.lyric-progress-track) {
  height: 5px;
  background: rgba(255, 255, 255, 0.12);
}

:deep(.lyric-progress-range) {
  background: linear-gradient(
    90deg,
    color-mix(in srgb, var(--color-primary) 92%, white 10%),
    color-mix(in srgb, var(--color-primary) 72%, white 18%)
  );
}

:deep(.lyric-progress-thumb) {
  width: 14px;
  height: 14px;
  border: none;
  background: #fff;
  box-shadow: 0 0 0 5px color-mix(in srgb, var(--color-primary) 22%, transparent);
}

:deep(.lyric-volume-track) {
  height: 4px;
  background: rgba(255, 255, 255, 0.12);
}

:deep(.lyric-volume-range) {
  background: rgba(255, 255, 255, 0.9);
}

:deep(.lyric-volume-thumb) {
  width: 11px;
  height: 11px;
  border: none;
  background: #fff;
}

@keyframes lyric-fade-in {
  from {
    opacity: 0;
    transform: scale(1.015);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}

@media (max-width: 1180px) {
  .lyric-cover-shell {
    width: min(36vw, 360px);
  }
}

@media (max-width: 980px) {
  .lyric-view {
    overflow-y: auto;
  }

  .lyric-view > .relative {
    min-height: 100vh;
  }

  .lyric-cover-shell {
    width: min(62vw, 320px);
  }

  .lyric-scroll {
    mask-image: linear-gradient(to bottom, transparent 0%, black 8%, black 92%, transparent 100%);
  }
}
</style>
