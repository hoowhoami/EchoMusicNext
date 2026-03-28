<script setup lang="ts">
import { useRouter } from 'vue-router';
import { computed, ref, onMounted, onUnmounted } from 'vue';
import { usePlayerStore } from '@/stores/player';
import { useSettingStore } from '@/stores/setting';
import { usePlaylistStore, type Song, type SongArtist } from '@/stores/playlist';
import { SliderRoot, SliderTrack, SliderRange, SliderThumb } from 'reka-ui';
import Cover from '@/components/ui/Cover.vue';
import {
  DropdownMenuRoot,
  DropdownMenuTrigger,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuPortal,
} from 'reka-ui';
import PlayerQueueDrawer from '@/components/music/PlayerQueueDrawer.vue';
import {
  iconMusic,
  iconHeart,
  iconCloud,
  iconMessageCircle,
  iconRepeat,
  iconShuffle,
  iconListRestart,
  iconSkipBack,
  iconSkipForward,
  iconPlay,
  iconPause,
  iconVolume2,
  iconVolume1,
  iconVolumeX,
  iconVolumeOff,
  iconList,
  iconSpeedometer,
  iconPulse,
} from '@/icons';

const router = useRouter();
const player = usePlayerStore();
const playlist = usePlaylistStore();
const settingStore = useSettingStore();

const currentTrack = computed(() => {
  return (
    playlist.defaultList.find((s: Song) => s.id === player.currentTrackId) ||
    playlist.favorites.find((s: Song) => s.id === player.currentTrackId) ||
    playlist.history.find((s: Song) => s.id === player.currentTrackId)
  );
});

const isFavorite = computed(() => {
  return currentTrack.value ? playlist.favorites.some((s) => s.id === currentTrack.value?.id) : false;
});

const playbackRates = [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0];

const artistList = computed(() => {
  if (!currentTrack.value) return [];
  if (currentTrack.value.artists && currentTrack.value.artists.length > 0) return currentTrack.value.artists;
  if (!currentTrack.value.artist) return [] as SongArtist[];
  return currentTrack.value.artist
    .split(/[,/，]/)
    .map((name) => name.trim())
    .filter((name) => name.length > 0)
    .map((name) => ({ name }));
});

const formatTime = (seconds: number) => {
  if (!seconds || isNaN(seconds)) return '00:00';
  const mins = Math.floor(seconds / 60);
  const secs = Math.floor(seconds % 60);
  return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
};

const navigateToPlaying = () => {
  router.push('/playing');
};

const goToArtist = (artist: SongArtist) => {
  if (artist.id) {
    router.push({ name: 'artist-detail', params: { id: String(artist.id) } });
  } else {
    router.push({ name: 'artist-detail', params: { id: encodeURIComponent(artist.name) } });
  }
};

const goToComments = () => {
  if (!currentTrack.value) return;
  const track = currentTrack.value;
  router.push({
    name: 'comment',
    params: { id: track.mixSongId ? String(track.mixSongId) : String(track.id) },
    query: {
      type: 'music',
      title: track.title,
      artist: track.artist,
      album: track.album ?? '',
      cover: track.coverUrl ?? '',
      albumId: track.albumId ?? '',
      hash: track.hash ?? '',
      mixSongId: track.mixSongId ?? '',
    },
  });
};

const setPlaybackRate = (rate: number) => {
  if (player.playbackRate === rate) return;
  player.setPlaybackRate(rate);
};

const setAudioQuality = (quality: '128' | '320' | 'flac' | 'high') => {
  if (settingStore.audioQuality === quality) return;
  settingStore.audioQuality = quality;
};

const setAudioEffect = (effect: 'none' | 'piano' | 'acappella' | 'subwoofer' | 'ancient' | 'surnay' | 'dj' | 'viper_tape' | 'viper_atmos' | 'viper_clear') => {
  if (settingStore.audioEffect === effect) return;
  settingStore.audioEffect = effect;
};

const openQueue = () => {
  isQueueDrawerOpen.value = true;
};


const handleSeek = (value: number[] | undefined) => {
  if (value && value.length > 0) {
    player.seek(value[0]);
  }
};

const handleSeekStart = () => {
  player.notifySeekStart();
};

const handleSeekEnd = () => {
  player.notifySeekEnd();
};

const lastVolume = ref(0.8);
const isHoveringProgress = ref(false);
const isVolumeVisible = ref(false);
const volumeContainerRef = ref<HTMLElement | null>(null);
const isQueueDrawerOpen = ref(false);

const toggleVolume = (e: Event) => {
  e.stopPropagation();
  isVolumeVisible.value = !isVolumeVisible.value;
};

const handleVolumeChange = (value: number[] | undefined) => {
  if (value && value.length > 0) {
    player.setVolume(value[0] / 100);
  }
};

const toggleMute = (e: Event) => {
  e.stopPropagation();
  if (player.volume > 0) {
    lastVolume.value = player.volume;
    player.setVolume(0);
  } else {
    player.setVolume(lastVolume.value || 0.8);
  }
};

const handleClickOutside = (e: MouseEvent) => {
  if (isVolumeVisible.value && volumeContainerRef.value && !volumeContainerRef.value.contains(e.target as Node)) {
    isVolumeVisible.value = false;
  }
};

const toggleFavorite = (e: Event) => {
  e.stopPropagation();
  if (!currentTrack.value) return;
  if (isFavorite.value) {
    playlist.removeFromFavorites(currentTrack.value.id);
  } else {
    playlist.addToFavorites(currentTrack.value);
  }
};

const updateDrawerWidth = () => {
  const content = document.querySelector('.view-port') as HTMLElement | null;
  const fallback = document.querySelector('.main-content') as HTMLElement | null;
  const target = content ?? fallback;
  if (target) {
    const rect = target.getBoundingClientRect();
    const width = Math.floor(target.clientWidth);
    const left = Math.floor(rect.left);
    const top = Math.floor(rect.top);
    const height = Math.floor(target.clientHeight);
    document.documentElement.style.setProperty('--drawer-content-width', `${width}px`);
    document.documentElement.style.setProperty('--drawer-content-left', `${left}px`);
    document.documentElement.style.setProperty('--drawer-content-top', `${top}px`);
    document.documentElement.style.setProperty('--drawer-content-height', `${height}px`);
  }
  const playerBar = document.querySelector('.player-bar-container') as HTMLElement | null;
  if (playerBar) {
    const offset = Math.floor(playerBar.offsetHeight + 8);
    document.documentElement.style.setProperty('--drawer-bottom-offset', `${offset}px`);
  }
};

// Marquee logic
const songInfoRef = ref<HTMLElement | null>(null);
const isMarqueeActive = ref(false);

const checkMarquee = () => {
  if (songInfoRef.value) {
    const container = songInfoRef.value.parentElement;
    if (container) {
      isMarqueeActive.value = songInfoRef.value.scrollWidth > container.clientWidth;
    }
  }
};

onMounted(() => {
  window.addEventListener('resize', checkMarquee);
  window.addEventListener('click', handleClickOutside);
  window.addEventListener('resize', updateDrawerWidth);
  checkMarquee();
  updateDrawerWidth();
});

onUnmounted(() => {
  window.removeEventListener('resize', checkMarquee);
  window.removeEventListener('click', handleClickOutside);
  window.removeEventListener('resize', updateDrawerWidth);
});
</script>

<template>
  <div class="player-bar-container w-full px-2 pb-[5px] z-[1000]">
    <footer
      class="player-bar w-full h-[84px] bg-bg-card/80 backdrop-blur-md border border-border-light/40 rounded-[12px] shadow-[0_8px_30px_rgb(0,0,0,0.08)] dark:shadow-[0_8px_30px_rgb(0,0,0,0.2)] flex items-center justify-between px-4 py-1 gap-8 select-none no-drag transition-all duration-300"
    >
      <!-- 1. 左侧：歌曲信息 - 弹性增长 -->
      <div class="flex-1 flex items-center gap-3 min-w-[120px] max-w-[320px] overflow-hidden">
        <div
          class="relative w-[56px] h-[56px] shrink-0 cursor-pointer group rounded-[10px] overflow-hidden bg-black/[0.04] dark:bg-white/[0.04]"
          @click="navigateToPlaying"
        >
          <Cover
            v-if="currentTrack"
            :url="currentTrack.coverUrl"
            :size="200"
            :width="56"
            :height="56"
            :borderRadius="10"
            class="transition-transform duration-500 group-hover:scale-110"
          />
          <div v-else class="w-full h-full flex items-center justify-center text-text-main/30">
            <Icon :icon="iconMusic" width="24" height="24" />
          </div>
        </div>

        <div class="flex flex-col min-w-0 flex-1 h-full py-1">
          <div class="relative w-full overflow-hidden h-6 flex items-center">
            <div
              ref="songInfoRef"
              class="whitespace-nowrap transition-transform flex items-center gap-1"
              :class="{ 'marquee-animation': isMarqueeActive }"
              @mouseenter="checkMarquee"
            >
              <span
                class="text-[14px] font-bold text-text-main hover:text-primary cursor-pointer transition-colors"
                @click="navigateToPlaying"
              >
                {{ currentTrack ? currentTrack.title : '未在播放' }}
              </span>
              <span v-if="currentTrack" class="text-[14px] text-text-main/60 mx-0.5">-</span>
              <div v-if="currentTrack" class="flex items-center">
                <template v-for="(artist, index) in artistList" :key="index">
                  <span
                    class="text-[13px] text-text-main/60 hover:text-primary cursor-pointer transition-colors"
                    @click="goToArtist(artist)"
                  >
                    {{ artist.name }}
                  </span>
                  <span v-if="index < artistList.length - 1" class="text-[13px] text-text-main/50 mx-0.5">/</span>
                </template>
              </div>
            </div>
          </div>

          <div class="flex items-center gap-2.5 mt-1.5 h-6">
            <button
              @click="toggleFavorite"
              class="p-0.5 transition-all hover:scale-110 active:scale-90"
              :class="isFavorite ? 'text-red-500' : 'text-text-main/25 hover:text-primary'"
              title="收藏"
            >
              <Icon :icon="iconHeart" width="20" height="20" />
            </button>

            <!-- 云盘标识 -->
            <div v-if="currentTrack?.privilege === 10" class="text-primary/60" title="云盘歌曲">
              <Icon :icon="iconCloud" width="16" height="16" />
            </div>

            <button
              @click="goToComments"
              class="p-0.5 text-text-main/25 hover:text-primary transition-all hover:scale-110"
              title="详情及评论"
            >
              <Icon :icon="iconMessageCircle" width="18" height="18" />
            </button>
          </div>
        </div>
      </div>

      <!-- 2. 中间：播放控制 & 进度条 - 核心弹性区域 -->
      <div class="flex-[1.5] flex flex-col items-center justify-center gap-1 min-w-[150px]">
        <div class="flex items-center justify-center gap-4 h-10">
          <!-- 播放模式 -->
          <button
            @click="player.setPlayMode(player.playMode === 'list' ? 'random' : player.playMode === 'random' ? 'single' : 'list')"
            class="p-2 text-text-main/50 hover:text-primary transition-all hover:scale-110 active:scale-90"
          >
            <Icon v-if="player.playMode === 'list'" :icon="iconRepeat" width="22" height="22" />
            <Icon v-else-if="player.playMode === 'random'" :icon="iconShuffle" width="22" height="22" />
            <Icon v-else :icon="iconListRestart" width="22" height="22" />
          </button>

          <button @click="player.prev" class="p-2 text-text-main/60 hover:text-primary transition-all hover:scale-110 active:scale-90">
            <Icon :icon="iconSkipBack" width="22" height="22" />
          </button>

          <button
            @click="player.togglePlay"
            class="player-toggle w-[38px] h-[38px] rounded-full bg-black/[0.04] flex items-center justify-center hover:scale-110 active:scale-95 transition-all border border-black/5"
          >
            <Icon
              v-if="!player.isPlaying"
              :icon="iconPlay"
              width="16"
              height="16"
              class="ml-0.5"
            />
            <Icon v-else :icon="iconPause" width="20" height="20" />
          </button>

          <button @click="player.next" class="p-2 text-text-main/60 hover:text-primary transition-all hover:scale-110 active:scale-90">
            <Icon :icon="iconSkipForward" width="22" height="22" />
          </button>

          <!-- 音量控制 - 点击弹出 -->
          <div 
            ref="volumeContainerRef"
            class="relative flex items-center group/vol" 
          >
            <button @click="toggleVolume" class="p-2 text-text-main/50 hover:text-primary transition-all hover:scale-110 active:scale-90" :class="{ 'text-primary': isVolumeVisible }">
              <Icon v-if="player.volume > 0.5" :icon="iconVolume2" width="22" height="22" />
              <Icon v-else-if="player.volume > 0" :icon="iconVolume1" width="22" height="22" />
              <Icon v-else :icon="iconVolumeX" width="22" height="22" />
            </button>
            
            <Transition name="volume-pop">
              <div 
                v-show="isVolumeVisible" 
                class="absolute bottom-[100%] left-1/2 -translate-x-1/2 pb-2"
                @click.stop
              >
                <div class="relative bg-bg-card/95 backdrop-blur-md border border-border-light/40 p-3 rounded-2xl shadow-xl h-40 flex flex-col items-center">
                  <SliderRoot
                    :model-value="[player.volume * 100]"
                    :max="100"
                    orientation="vertical"
                    class="relative flex flex-col items-center select-none touch-none w-5 h-full"
                    @update:model-value="handleVolumeChange"
                  >
                    <SliderTrack class="bg-black/5 dark:bg-white/10 relative grow rounded-full w-[3px]">
                      <SliderRange class="absolute bg-primary rounded-full w-full" />
                    </SliderTrack>
                    <SliderThumb class="block w-3 h-3 bg-white border border-black/10 rounded-full shadow-sm focus:outline-none" />
                  </SliderRoot>
                  
                  <button @click="toggleMute" class="mt-2 p-1 text-text-main/60 hover:text-primary transition-colors">
                     <Icon v-if="player.volume > 0" :icon="iconVolumeOff" width="20" height="20" />
                     <Icon v-else :icon="iconVolumeX" width="20" height="20" />
                  </button>
                  
                  <span class="mt-1 text-[10px] font-bold text-text-main/60 tabular-nums">{{ Math.round(player.volume * 100) }}</span>

                  <!-- 三角箭头 -->
                  <div class="absolute -bottom-1.5 left-1/2 -translate-x-1/2 w-3 h-3 bg-bg-card/95 rotate-45 border-r border-b border-border-light/40"></div>
                </div>
              </div>
            </Transition>
          </div>
        </div>

        <!-- 进度条系统 - 动态伸缩至最大值 -->
        <div class="w-full max-w-[480px] flex items-center gap-3 px-1 h-[14px] min-w-0">
          <span class="text-[10px] font-medium text-text-main/50 w-9 shrink-0 text-right tabular-nums">{{ formatTime(player.currentTime) }}</span>
          <SliderRoot
            :model-value="[player.currentTime]"
            :max="player.duration || 100"
            :step="0.1"
            class="relative flex items-center select-none touch-none flex-1 min-w-0 h-4 group/progress"
            @update:model-value="handleSeek"
            @pointerdown="handleSeekStart"
            @pointerup="handleSeekEnd"
            @pointercancel="handleSeekEnd"
            @mouseenter="isHoveringProgress = true"
            @mouseleave="isHoveringProgress = false"
          >
            <SliderTrack class="player-progress-track bg-black/[0.08] relative grow rounded-full h-[3px]">
              <div class="climax-mark-layer">
                <template v-for="(mark, index) in player.climaxMarks" :key="`${mark.start}-${index}`">
                  <span
                    class="climax-tick"
                    :style="{ left: `calc(${(mark.start * 100).toFixed(3)}% - 1px)` }"
                  ></span>
                  <span
                    v-if="mark.end > mark.start"
                    class="climax-tick"
                    :style="{ left: `calc(${(mark.end * 100).toFixed(3)}% - 1px)` }"
                  ></span>
                </template>
              </div>
              <SliderRange class="absolute bg-primary rounded-full h-full" />
            </SliderTrack>
            <SliderThumb
              class="block w-2.5 h-2.5 bg-white border border-black/10 rounded-full shadow-md focus:outline-none transition-all duration-200"
              :class="[isHoveringProgress ? 'opacity-100 scale-125' : 'opacity-0 scale-50']"
            />
          </SliderRoot>
          <span class="text-[10px] font-medium text-text-main/50 w-9 shrink-0 text-left tabular-nums">{{ formatTime(player.duration) }}</span>
        </div>
      </div>

      <!-- 3. 右侧：功能选项 - 弹性增长 -->
      <div class="player-actions flex-1 flex justify-end items-center gap-1 min-w-[120px] max-w-[320px]">
        <DropdownMenuRoot>
          <DropdownMenuTrigger as-child>
            <button
              class="p-2 transition-all hover:scale-110 active:scale-90"
              :class="player.playbackRate !== 1 ? 'text-primary' : 'text-text-main/50 hover:text-primary'"
              title="播放倍速"
            >
              <Icon :icon="iconSpeedometer" width="20" height="20" />
            </button>
          </DropdownMenuTrigger>
          <DropdownMenuPortal>
            <DropdownMenuContent class="player-dropdown player-dropdown--narrow" align="center" side="top" :side-offset="8" :align-offset="0">
              <div class="player-dropdown-title">播放倍速</div>
              <DropdownMenuItem
                v-for="rate in playbackRates"
                :key="rate"
                class="player-dropdown-item"
                :class="{ 'is-active': player.playbackRate === rate }"
                @select="setPlaybackRate(rate)"
              >
                <span>{{ rate.toFixed(2).replace(/\.00$/, '') }}x</span>
                <span v-if="player.playbackRate === rate" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <div class="player-dropdown-arrow"></div>
            </DropdownMenuContent>
          </DropdownMenuPortal>
        </DropdownMenuRoot>

        <DropdownMenuRoot>
          <DropdownMenuTrigger as-child>
            <button
              class="p-2 transition-all hover:scale-110 active:scale-90"
              :class="settingStore.audioQuality !== 'high' || settingStore.audioEffect !== 'none'
                ? 'text-primary'
                : 'text-text-main/50 hover:text-primary'"
              title="音质"
            >
              <Icon :icon="iconPulse" width="20" height="20" />
            </button>
          </DropdownMenuTrigger>
          <DropdownMenuPortal>
            <DropdownMenuContent class="player-dropdown player-dropdown--narrow" align="center" side="top" :side-offset="8" :align-offset="0">
              <div class="player-dropdown-title">音质</div>
              <DropdownMenuItem
                class="player-dropdown-item"
                :class="{ 'is-active': settingStore.audioQuality === 'high' }"
                @select="setAudioQuality('high')"
              >
                <span>Hi-Res</span>
                <span v-if="settingStore.audioQuality === 'high'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem
                class="player-dropdown-item"
                :class="{ 'is-active': settingStore.audioQuality === 'flac' }"
                @select="setAudioQuality('flac')"
              >
                <span>SQ 无损</span>
                <span v-if="settingStore.audioQuality === 'flac'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem
                class="player-dropdown-item"
                :class="{ 'is-active': settingStore.audioQuality === '320' }"
                @select="setAudioQuality('320')"
              >
                <span>HQ 高品质</span>
                <span v-if="settingStore.audioQuality === '320'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem
                class="player-dropdown-item"
                :class="{ 'is-active': settingStore.audioQuality === '128' }"
                @select="setAudioQuality('128')"
              >
                <span>标准</span>
                <span v-if="settingStore.audioQuality === '128'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <div class="player-dropdown-divider"></div>
              <div class="player-dropdown-title">音效</div>
              <div class="player-dropdown-scroll">
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'none' }"
                  @select="setAudioEffect('none')"
                >
                  <span>无音效</span>
                  <span v-if="settingStore.audioEffect === 'none'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'piano' }"
                  @select="setAudioEffect('piano')"
                >
                  <span>钢琴</span>
                  <span v-if="settingStore.audioEffect === 'piano'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'acappella' }"
                  @select="setAudioEffect('acappella')"
                >
                  <span>清唱</span>
                  <span v-if="settingStore.audioEffect === 'acappella'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'subwoofer' }"
                  @select="setAudioEffect('subwoofer')"
                >
                  <span>重低音</span>
                  <span v-if="settingStore.audioEffect === 'subwoofer'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'ancient' }"
                  @select="setAudioEffect('ancient')"
                >
                  <span>古风</span>
                  <span v-if="settingStore.audioEffect === 'ancient'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'surnay' }"
                  @select="setAudioEffect('surnay')"
                >
                  <span>唢呐</span>
                  <span v-if="settingStore.audioEffect === 'surnay'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'dj' }"
                  @select="setAudioEffect('dj')"
                >
                  <span>DJ</span>
                  <span v-if="settingStore.audioEffect === 'dj'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'viper_tape' }"
                  @select="setAudioEffect('viper_tape')"
                >
                  <span>蝰蛇母带</span>
                  <span v-if="settingStore.audioEffect === 'viper_tape'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'viper_atmos' }"
                  @select="setAudioEffect('viper_atmos')"
                >
                  <span>蝰蛇全景声</span>
                  <span v-if="settingStore.audioEffect === 'viper_atmos'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem
                  class="player-dropdown-item"
                  :class="{ 'is-active': settingStore.audioEffect === 'viper_clear' }"
                  @select="setAudioEffect('viper_clear')"
                >
                  <span>蝰蛇超清</span>
                  <span v-if="settingStore.audioEffect === 'viper_clear'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
              </div>
              <div class="player-dropdown-arrow"></div>
            </DropdownMenuContent>
          </DropdownMenuPortal>
        </DropdownMenuRoot>

        <button
          class="p-2 text-text-main/50 hover:text-primary transition-all hover:scale-110 active:scale-90"
          title="播放队列"
          @click="openQueue"
        >
          <Icon :icon="iconList" width="22" height="22" />
        </button>
      </div>
    </footer>
  </div>

  <PlayerQueueDrawer v-model:open="isQueueDrawerOpen" />
</template>

<style scoped>
.marquee-animation {
  animation: marquee 15s linear infinite;
  padding-left: 0;
}

@keyframes marquee {
  0% { transform: translateX(0); }
  10% { transform: translateX(0); }
  90% { transform: translateX(calc(-100% + 180px)); }
  100% { transform: translateX(calc(-100% + 180px)); }
}

.volume-pop-enter-active,
.volume-pop-leave-active {
  transition: all 0.4s cubic-bezier(0.34, 1.56, 0.64, 1);
}

.volume-pop-enter-from,
.volume-pop-leave-to {
  opacity: 0;
  transform: translateY(14px) scale(0.92);
}

.player-bar {
  transition: background-color 0.3s ease;
}

.climax-mark-layer {
  position: absolute;
  inset: 0;
  pointer-events: none;
}

.climax-tick {
  position: absolute;
  top: calc(50% - 3px);
  width: 2px;
  height: 6px;
  border-radius: 1px;
  background: rgba(0, 113, 227, 0.8);
}

.dark .climax-tick {
  background: rgba(0, 113, 227, 0.65);
}

.player-actions {
  padding-right: 6px;
}

.player-toggle {
  background-color: rgba(0, 0, 0, 0.04);
  border-color: transparent;
}

.dark .player-toggle {
  background-color: rgba(245, 245, 247, 0.22);
  border-color: transparent;
  box-shadow: none;
}

.dark .player-progress-track {
  background-color: rgba(245, 245, 247, 0.4);
}

:deep(.player-dropdown) {
  min-width: 168px;
  padding: 8px 0 8px 8px;
  border-radius: 12px;
  background: var(--color-bg-card);
  border: 1px solid var(--color-border-light);
  box-shadow: 0 12px 28px rgba(0, 0, 0, 0.12);
  display: flex;
  flex-direction: column;
  gap: 4px;
  z-index: 1200;
  position: relative;
}

:deep(.player-dropdown--narrow) {
  min-width: 136px;
}

:deep(.player-dropdown-title) {
  font-size: 11px;
  font-weight: 700;
  color: var(--color-text-secondary);
  padding: 2px 8px 6px 6px;
}

:deep(.player-dropdown-item) {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 8px;
  margin-right: 8px;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-main);
  cursor: pointer;
  transition: all 0.2s ease;
}

:deep(.player-dropdown-scroll .player-dropdown-item) {
  margin-right: 4px;
}

:deep(.player-dropdown-item.is-active) {
  background-color: rgba(0, 113, 227, 0.12);
  color: var(--color-primary);
}

:deep(.dark .player-dropdown-item.is-active) {
  background-color: rgba(0, 113, 227, 0.2);
}

:deep(.player-dropdown-item:hover) {
  background-color: rgba(0, 0, 0, 0.05);
  color: var(--color-primary);
}

:deep(.dark .player-dropdown-item:hover) {
  background-color: rgba(255, 255, 255, 0.08);
}

:deep(.player-dropdown-check) {
  font-size: 12px;
  color: var(--color-primary);
}

:deep(.player-dropdown-divider) {
  height: 1px;
  margin: 4px 8px 4px 6px;
  background-color: var(--color-border-light);
}

:deep(.player-dropdown-scroll) {
  max-height: 168px;
  overflow-y: auto;
  padding-right: 1px;
}

:deep(.player-dropdown-arrow) {
  position: absolute;
  bottom: -6px;
  left: 50%;
  width: 12px;
  height: 12px;
  background: var(--color-bg-card);
  border-right: 1px solid var(--color-border-light);
  border-bottom: 1px solid var(--color-border-light);
  transform: translateX(-50%) rotate(45deg);
}

</style>
