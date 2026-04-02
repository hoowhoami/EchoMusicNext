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
import { getCoverUrl } from '@/utils/cover';
import Button from '@/components/ui/Button.vue';
import {
  iconChevronDown,
  iconCopy,
  iconHeart,
  iconLanguage,
  iconMinus,
  iconPause,
  iconPlay,
  iconPlus,
  iconRefreshCw,
  iconRepeat,
  iconListRestart,
  iconShuffle,
  iconSkipBack,
  iconSkipForward,
  iconTypography,
  iconVolume2,
  iconVolume1,
  iconVolumeX,
} from '@/icons';
import { isSameSong } from '@/utils/song';

const router = useRouter();
const route = useRoute();
const playerStore = usePlayerStore();
const playlistStore = usePlaylistStore();
const lyricStore = useLyricStore();

const lyricListRef = ref<HTMLElement | null>(null);
const progressValue = ref(0);
const isProgressDragging = ref(false);
const volumeValue = ref(Math.round(playerStore.volume * 100));
const copyFeedback = ref(false);

const currentTrack = computed<Song | undefined>(() => {
  const currentId = String(playerStore.currentTrackId ?? '');
  if (!currentId) return undefined;
  return (
    playlistStore.defaultList.find((song) => String(song.id) === currentId)
    || playlistStore.favorites.find((song) => String(song.id) === currentId)
    || playerStore.currentTrackSnapshot
    || undefined
  );
});

const backgroundUrl = computed(() => getCoverUrl(currentTrack.value?.coverUrl, 900));
const currentIndex = computed(() => lyricStore.currentIndex);
const hasLyrics = computed(() => lyricStore.lines.length > 0);
const currentTrackHash = computed(() => String(currentTrack.value?.hash ?? currentTrack.value?.id ?? ''));
const lyricModeLabel = computed(() => {
  if (lyricStore.lyricsMode === 'translation') return '翻译';
  if (lyricStore.lyricsMode === 'romanization') return '音译';
  return '标准';
});
const canToggleMode = computed(() => lyricStore.hasTranslation || lyricStore.hasRomanization);
const emptyStateText = computed(() => {
  if (lyricStore.isLoading) return '歌词加载中…';
  return lyricStore.tips || '暂无歌词';
});
const titleFontSize = computed(() => `clamp(${23 * lyricStore.fontScale}px, ${2.3 * lyricStore.fontScale}vw, ${36 * lyricStore.fontScale}px)`);
const secondaryFontSize = computed(() => `clamp(${12 * lyricStore.fontScale}px, ${1.0 * lyricStore.fontScale}vw, ${15 * lyricStore.fontScale}px)`);
const fontWeightLabel = computed(() => `W${lyricStore.fontWeightValue}`);

const isFavorite = computed(() => {
  if (!currentTrack.value) return false;
  return playlistStore.favorites.some(
    (song) => isSameSong(song, currentTrack.value as Song) || String(song.id) === String(currentTrack.value?.id),
  );
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
    void playlistStore.removeFavoriteSong(currentTrack.value);
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

const copyLyrics = async () => {
  const text = lyricStore.copyableText.trim();
  if (!text) return;

  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(text);
  } else {
    const textArea = document.createElement('textarea');
    textArea.value = text;
    textArea.style.position = 'fixed';
    textArea.style.opacity = '0';
    document.body.appendChild(textArea);
    textArea.select();
    document.execCommand('copy');
    document.body.removeChild(textArea);
  }

  copyFeedback.value = true;
  window.setTimeout(() => {
    copyFeedback.value = false;
  }, 1200);
};

const refreshLyrics = async () => {
  const hash = currentTrackHash.value;
  if (!hash) return;
  lyricStore.clear(hash, '歌词加载中...');
  await lyricStore.fetchLyrics(hash);
  lyricStore.updateCurrentIndex(playerStore.currentTime);
  await nextTick();
  scrollToCurrentLine(false);
};

const increaseFontWeight = () => {
  lyricStore.updateFontWeight(lyricStore.fontWeightIndex + 1);
};

const decreaseFontWeight = () => {
  lyricStore.updateFontWeight(lyricStore.fontWeightIndex - 1);
};

const toggleLyricsMode = () => {
  if (!canToggleMode.value) return;
  lyricStore.toggleLyricsMode();
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

watch(
  () => lyricStore.lyricsMode,
  async () => {
    await nextTick();
    scrollToCurrentLine(false);
  },
);

const closeLyricPage = async () => {
  await closeTransientView(router, route);
};

const handleKeydown = (event: KeyboardEvent) => {
  if (event.key === 'Escape') {
    event.preventDefault();
    void closeLyricPage();
    return;
  }

  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'c') {
    if (!hasLyrics.value) return;
    event.preventDefault();
    void copyLyrics();
  }
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
        class="absolute inset-0 bg-cover bg-center opacity-[0.16] blur-[2px] scale-[1.08]"
        :style="{ backgroundImage: backgroundUrl ? `url(${backgroundUrl})` : undefined }"
      ></div>
      <div class="absolute inset-0 lyric-view__backdrop"></div>
    </div>

    <OverlayHeader />

    <div class="relative z-10 flex h-full flex-col">
      <div class="h-12 w-full drag"></div>

      <div class="px-6 pb-3 no-drag">
        <div class="flex h-12 items-center">
          <Button variant="unstyled" size="none" type="button" class="lyric-icon-btn" title="返回" @click="closeLyricPage">
            <Icon :icon="iconChevronDown" width="22" height="22" />
          </Button>
          <div class="ml-auto flex items-center gap-2">
            <div class="lyric-weight-pill">
              <Icon :icon="iconTypography" width="13" height="13" class="text-white/62" />
              <Button variant="unstyled" size="none" type="button" class="lyric-weight-btn" :disabled="lyricStore.fontWeightIndex <= 0" @click="decreaseFontWeight">
                <Icon :icon="iconMinus" width="10" height="10" />
              </Button>
              <span class="lyric-weight-label">{{ fontWeightLabel }}</span>
              <Button variant="unstyled" size="none" type="button" class="lyric-weight-btn" :disabled="lyricStore.fontWeightIndex >= 8" @click="increaseFontWeight">
                <Icon :icon="iconPlus" width="10" height="10" />
              </Button>
            </div>
            <Button variant="unstyled" size="none" type="button" class="lyric-tool-chip" :disabled="!canToggleMode" @click="toggleLyricsMode">
              <Icon :icon="iconLanguage" width="14" height="14" />
              <span>{{ lyricModeLabel }}</span>
            </Button>
            <Button variant="unstyled" size="none" type="button" class="lyric-tool-chip" :disabled="!hasLyrics" @click="copyLyrics">
              <Icon :icon="iconCopy" width="14" height="14" />
              <span>{{ copyFeedback ? '已复制' : '复制' }}</span>
            </Button>
            <Button variant="unstyled" size="none" type="button" class="lyric-tool-chip" :disabled="!currentTrackHash" @click="refreshLyrics">
              <Icon :icon="iconRefreshCw" width="14" height="14" :class="lyricStore.isLoading ? 'animate-spin' : ''" />
              <span>刷新</span>
            </Button>
          </div>
        </div>
      </div>

      <div class="flex-1 min-h-0 px-14">
        <div class="mx-auto flex h-full max-w-[1500px] items-center gap-10">
          <section class="flex min-w-0 flex-[5] items-center justify-center">
            <div class="lyric-info-panel">
              <div class="lyric-cover-shell">
                <div class="lyric-cover-frame">
                  <Cover :url="currentTrack?.coverUrl" :size="800" class="h-full w-full" :borderRadius="24" />
                </div>
              </div>
              <h2 class="mt-12 truncate text-center text-[clamp(28px,2.6vw,34px)] font-semibold tracking-[-0.04em] text-white">
                {{ currentTrack?.title || '未在播放' }}
              </h2>
              <p class="mt-3 truncate text-center text-[clamp(16px,1.4vw,18px)] font-semibold text-white/62">
                {{ currentTrack?.artist || '等待播放中' }}
              </p>
            </div>
          </section>

          <section class="flex min-w-0 flex-[7] flex-col justify-center self-stretch">
            <div class="lyric-stage flex-1 min-h-0">
              <div ref="lyricListRef" class="lyric-scroll h-full overflow-y-auto">
                <template v-if="hasLyrics">
                  <div class="py-[14vh]">
                    <div
                      v-for="(line, index) in lyricStore.lines"
                      :key="`${line.time}-${index}`"
                      class="lyric-row"
                      :style="{ height: ((lyricStore.lyricsMode === 'translation' || lyricStore.lyricsMode === 'romanization') ? 100 : 70) * lyricStore.fontScale + 'px' }"
                    >
                      <Button
                        variant="unstyled"
                        size="none"
                        type="button"
                        :data-lyric-index="index"
                        :class="['lyric-line', currentIndex === index ? 'is-current' : 'is-idle']"
                        @click="playerStore.seek(line.time)"
                      >
                        <span
                          class="block leading-[1.24] tracking-[0.01em]"
                          :style="{ fontSize: titleFontSize, fontWeight: String(lyricStore.fontWeightValue) }"
                        >
                          <template v-if="currentIndex === index && line.characters.length > 1">
                            <span
                              v-for="(char, charIndex) in line.characters"
                              :key="`${index}-${charIndex}-${char.startTime}`"
                              class="lyric-character"
                              :class="char.highlighted ? 'is-highlighted' : ''"
                            >{{ char.text }}</span>
                          </template>
                          <template v-else>
                            {{ line.text }}
                          </template>
                        </span>
                        <span
                          v-if="(lyricStore.lyricsMode === 'translation' && line.translated) || (lyricStore.lyricsMode === 'romanization' && line.romanized)"
                          class="mt-1.5 block max-w-full truncate text-white/58"
                          :style="{ fontSize: secondaryFontSize, fontWeight: String(currentIndex === index ? Math.max(500, lyricStore.fontWeightValue - 200) : 400) }"
                        >
                          {{ lyricStore.lyricsMode === 'translation' ? line.translated : line.romanized }}
                        </span>
                      </Button>
                    </div>
                  </div>
                </template>

                <div v-else class="flex h-full items-center justify-center text-center">
                  <div class="space-y-3">
                    <p class="text-[28px] font-semibold text-white/88">
                      {{ lyricStore.isLoading ? '歌词加载中…' : '纯音乐，请欣赏' }}
                    </p>
                    <p class="text-sm font-semibold text-white/38">{{ emptyStateText }}</p>
                  </div>
                </div>
              </div>
            </div>
          </section>
        </div>
      </div>

      <div class="px-10 pb-6 pt-2 no-drag">
        <div class="lyric-bottom-bar mx-auto max-w-[1500px]">
          <div class="mb-2 flex items-center justify-between text-[11px] font-semibold text-white/42">
            <span>{{ formatDuration(isProgressDragging ? progressValue : playerStore.currentTime) }}</span>
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

          <div class="mt-4 flex items-center justify-between gap-4">
            <div class="flex items-center gap-2">
              <Button variant="unstyled" size="none" type="button" class="lyric-compact-btn" title="收藏" @click="toggleFavorite">
                <Icon :icon="iconHeart" width="16" height="16" :class="isFavorite ? 'text-[#ff6b81]' : 'text-white/66'" />
              </Button>
              <Button variant="unstyled" size="none" type="button" class="lyric-compact-btn" :title="playModeMeta.label" @click="cyclePlayMode">
                <Icon :icon="playModeMeta.icon" width="17" height="17" class="text-white/70" />
              </Button>
            </div>

            <div class="flex items-center gap-2.5">
              <Button variant="unstyled" size="none" type="button" class="lyric-transport-btn" title="上一曲" @click="playerStore.prev()">
                <Icon :icon="iconSkipBack" width="18" height="18" />
              </Button>
              <Button variant="unstyled" size="none" type="button" class="lyric-play-btn" :title="playerStore.isPlaying ? '暂停' : '播放'" @click="playerStore.togglePlay()">
                <Icon :icon="playerStore.isPlaying ? iconPause : iconPlay" width="18" height="18" />
              </Button>
              <Button variant="unstyled" size="none" type="button" class="lyric-transport-btn" title="下一曲" @click="playerStore.next()">
                <Icon :icon="iconSkipForward" width="18" height="18" />
              </Button>
            </div>

            <div class="flex min-w-[132px] items-center justify-end gap-2">
              <Icon :icon="volumeIcon" width="15" height="15" class="text-white/54" />
              <Slider
                :model-value="volumeValue"
                :min="0"
                :max="100"
                :step="1"
                class="w-[92px]"
                track-class="lyric-volume-track"
                range-class="lyric-volume-range"
                thumb-class="lyric-volume-thumb"
                @update:model-value="handleVolumeInput"
                @value-commit="handleVolumeInput"
              />
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.lyric-view__backdrop {
  background:
    linear-gradient(180deg, rgba(0, 0, 0, 0.78) 0%, rgba(0, 0, 0, 0.44) 50%, rgba(0, 0, 0, 0.82) 100%),
    radial-gradient(circle at center, rgba(255, 255, 255, 0.04) 0%, transparent 48%);
}

.lyric-icon-btn,
.lyric-tool-chip,
.lyric-weight-btn,
.lyric-compact-btn,
.lyric-transport-btn {
  width: 34px;
  height: 34px;
  border-radius: 999px;
}

.lyric-icon-btn:hover,
.lyric-tool-chip:hover,
.lyric-weight-btn:hover,
.lyric-compact-btn:hover,
.lyric-transport-btn:hover,
.lyric-play-btn:hover {
  transform: translateY(-1px);
}

.lyric-icon-btn {
  width: 36px;
  height: 36px;
  border-radius: 999px;
}

.lyric-tool-chip {
  gap: 6px;
  height: 36px;
  padding: 0 14px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 700;
}

.lyric-weight-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  height: 36px;
  padding: 0 8px 0 10px;
  border-radius: 999px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  background: rgba(255, 255, 255, 0.06);
  backdrop-filter: blur(18px);
}

.lyric-weight-btn,
.lyric-compact-btn {
  width: 26px;
  height: 26px;
  border-radius: 999px;
}

.lyric-weight-label {
  min-width: 38px;
  text-align: center;
  font-size: 11px;
  font-weight: 800;
  letter-spacing: 0.08em;
  color: rgba(255, 255, 255, 0.84);
}

.lyric-info-panel {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  width: 100%;
}

.lyric-cover-shell {
  position: relative;
  width: clamp(240px, 38vh, 400px);
  height: clamp(240px, 38vh, 400px);
}

.lyric-cover-frame {
  position: relative;
  height: 100%;
  width: 100%;
  overflow: hidden;
  border-radius: 24px;
  box-shadow: 0 20px 50px rgba(0, 0, 0, 0.45);
}

.lyric-stage {
  min-height: 0;
}

.lyric-scroll {
  height: 100%;
  scrollbar-width: none;
}

.lyric-scroll::-webkit-scrollbar {
  display: none;
}

.lyric-row {
  display: flex;
  align-items: center;
  justify-content: center;
}

.lyric-line {
  width: 100%;
  padding: 0 16px;
  text-align: center;
  transition: opacity 0.26s ease, transform 0.26s ease;
}

.lyric-line.is-idle {
  opacity: 0.38;
  transform: scale(0.9) translateY(4px);
}

.lyric-line.is-current {
  opacity: 1;
  transform: scale(1) translateY(0);
}

.lyric-character {
  transition: color 0.18s ease;
  color: rgba(255, 255, 255, 0.96);
}

.lyric-character.is-highlighted {
  color: color-mix(in srgb, var(--color-primary) 92%, white 10%);
  text-shadow: 0 0 10px color-mix(in srgb, var(--color-primary) 40%, transparent);
}

.lyric-bottom-bar {
  padding: 4px 6px 0;
}

.lyric-transport-btn {
  width: 38px;
  height: 38px;
  border-radius: 999px;
}

.lyric-play-btn {
  width: 42px;
  height: 42px;
  border: none;
  border-radius: 999px;
  background: linear-gradient(180deg, color-mix(in srgb, var(--color-primary) 96%, white 8%), color-mix(in srgb, var(--color-primary) 82%, black 8%));
  color: white;
  box-shadow: 0 8px 18px color-mix(in srgb, var(--color-primary) 24%, transparent);
  transition: transform 0.2s ease, box-shadow 0.25s ease;
}

.lyric-progress-track,
.lyric-volume-track {
  background: rgba(255, 255, 255, 0.12) !important;
}

.lyric-progress-range,
.lyric-volume-range {
  background: linear-gradient(90deg, rgba(255, 255, 255, 0.92), color-mix(in srgb, var(--color-primary) 70%, white 20%)) !important;
}

.lyric-progress-thumb,
.lyric-volume-thumb {
  width: 9px !important;
  height: 9px !important;
  background: #fff !important;
  box-shadow: 0 3px 10px rgba(0, 0, 0, 0.18);
}

.lyric-tool-chip:disabled,
.lyric-weight-btn:disabled {
  opacity: 0.38;
  cursor: not-allowed;
  transform: none !important;
}
</style>
