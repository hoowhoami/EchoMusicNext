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
  router.push({
    name: 'comment',
    params: { id: currentTrack.value.mixSongId ? String(currentTrack.value.mixSongId) : String(currentTrack.value.id) },
    query: { type: 'music' },
  });
};

const setPlaybackRate = (rate: number) => {
  player.setPlaybackRate(rate);
};

const setAudioQuality = (quality: '128' | '320' | 'flac' | 'high') => {
  settingStore.audioQuality = quality;
};

const setAudioEffect = (effect: 'none' | 'piano' | 'acappella' | 'subwoofer' | 'ancient' | 'surnay' | 'dj' | 'viper_tape' | 'viper_atmos' | 'viper_clear') => {
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
  const content = document.querySelector('.main-content') as HTMLElement | null;
  if (content) {
    const width = Math.floor(content.clientWidth);
    const left = Math.floor(content.getBoundingClientRect().left);
    document.documentElement.style.setProperty('--drawer-content-width', `${width}px`);
    document.documentElement.style.setProperty('--drawer-content-left', `${left}px`);
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
      class="player-bar w-full h-[80px] bg-bg-card/80 backdrop-blur-md border border-border-light/40 rounded-[16px] shadow-[0_8px_30px_rgb(0,0,0,0.08)] dark:shadow-[0_8px_30px_rgb(0,0,0,0.2)] flex items-center justify-between px-4 gap-8 select-none no-drag transition-all duration-300"
    >
      <!-- 1. 左侧：歌曲信息 - 弹性增长 -->
      <div class="flex-1 flex items-center gap-3 min-w-[120px] max-w-[320px] overflow-hidden">
        <div
          class="relative w-[56px] h-[56px] shrink-0 cursor-pointer group rounded-xl overflow-hidden bg-black/[0.04] dark:bg-white/[0.04]"
          @click="navigateToPlaying"
        >
          <Cover
            v-if="currentTrack"
            :url="currentTrack.coverUrl"
            :size="200"
            :width="56"
            :height="56"
            :borderRadius="12"
            class="transition-transform duration-500 group-hover:scale-110"
          />
          <div v-else class="w-full h-full flex items-center justify-center text-text-main/20">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
              <path d="M9 18V5l12-2v13" />
              <circle cx="6" cy="18" r="3" />
              <circle cx="18" cy="16" r="3" />
            </svg>
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
              <span v-if="currentTrack" class="text-[14px] text-text-main/40 mx-0.5">-</span>
              <div v-if="currentTrack" class="flex items-center">
                <template v-for="(artist, index) in artistList" :key="index">
                  <span
                    class="text-[13px] text-text-main/60 hover:text-primary cursor-pointer transition-colors"
                    @click="goToArtist(artist)"
                  >
                    {{ artist.name }}
                  </span>
                  <span v-if="index < artistList.length - 1" class="text-[13px] text-text-main/30 mx-0.5">/</span>
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
              <svg
                width="16"
                height="16"
                viewBox="0 0 24 24"
                :fill="isFavorite ? 'currentColor' : 'none'"
                stroke="currentColor"
                stroke-width="2"
              >
                <path
                  d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l8.78-8.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"
                />
              </svg>
            </button>

            <!-- 云盘标识 -->
            <div v-if="currentTrack?.privilege === 10" class="text-primary/60" title="云盘歌曲">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M17.5 19c2.5 0 4.5-2 4.5-4.5 0-2.3-1.7-4.2-3.9-4.5-1.1-3.5-4.4-6-8.1-6-3.8 0-7.1 2.5-8.1 6C-0.3 10.3-2 12.2-2 14.5c0 2.5 2 4.5 4.5 4.5" />
              </svg>
            </div>

            <button
              @click="navigateToPlaying"
              class="p-0.5 text-text-main/25 hover:text-primary transition-all hover:scale-110"
              title="详情及评论"
            >
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z" />
                <path d="M12 7v5" stroke-width="2" />
                <path d="M12 14v.01" stroke-width="2.5" />
              </svg>
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
            class="p-2 text-text-main/30 hover:text-primary transition-all hover:scale-110 active:scale-90"
          >
            <svg v-if="player.playMode === 'list'" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M17 2l4 4-4 4M3 11v-1a4 4 0 014-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 01-4 4H3"/></svg>
            <svg v-else-if="player.playMode === 'random'" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M16 3h5v5M4 20L21 3M21 16v5h-5M15 15l6 6M4 4l5 5"/></svg>
            <svg v-else width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M17 2l4 4-4 4M3 11v-1a4 4 0 014-4h14M7 22l-4-4 4-4M21 13v1a4 4 0 01-4 4H3"/><path d="M11 15h2v-4h-2"/></svg>
          </button>

          <button @click="player.prev" class="p-2 text-text-main/60 hover:text-primary transition-all hover:scale-110 active:scale-90">
            <svg width="26" height="26" viewBox="0 0 24 24" fill="currentColor"><path d="M6 6h2v12H6zm3.5 6l8.5 6V6z" /></svg>
          </button>

          <button
            @click="player.togglePlay"
            class="player-toggle w-[42px] h-[42px] rounded-full bg-black/[0.04] flex items-center justify-center hover:scale-110 active:scale-95 transition-all border border-black/5"
          >
            <svg
              v-if="!player.isPlaying"
              width="24"
              height="24"
              viewBox="0 0 24 24"
              fill="currentColor"
              class="ml-0.5"
            >
              <path d="M8 5v14l11-7z" />
            </svg>
            <svg v-else width="22" height="22" viewBox="0 0 24 24" fill="currentColor">
              <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z" />
            </svg>
          </button>

          <button @click="player.next" class="p-2 text-text-main/60 hover:text-primary transition-all hover:scale-110 active:scale-90">
            <svg width="26" height="26" viewBox="0 0 24 24" fill="currentColor"><path d="M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z" /></svg>
          </button>

          <!-- 音量控制 - 点击弹出 -->
          <div 
            ref="volumeContainerRef"
            class="relative flex items-center group/vol" 
          >
            <button @click="toggleVolume" class="p-2 text-text-main/30 hover:text-primary transition-all hover:scale-110 active:scale-90" :class="{ 'text-primary': isVolumeVisible }">
              <svg v-if="player.volume > 0.5" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2">
                <path d="M11 5L6 9H2v6h4l5 4V5zM19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07" />
              </svg>
              <svg v-else-if="player.volume > 0" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2">
                <path d="M11 5L6 9H2v6h4l5 4V5zM15.54 8.46a5 5 0 0 1 0 7.07" />
              </svg>
              <svg v-else width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2">
                <path d="M11 5L6 9H2v6h4l5 4V5zM23 9l-6 6M17 9l6 6" />
              </svg>
            </button>
            
            <Transition name="volume-pop">
              <div 
                v-show="isVolumeVisible" 
                class="absolute bottom-[100%] left-1/2 -translate-x-1/2 pb-4"
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
                  
                  <button @click="toggleMute" class="mt-2 p-1 text-text-main/40 hover:text-primary transition-colors">
                     <svg v-if="player.volume > 0" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M11 5L6 9H2v6h4l5 4V5z"/></svg>
                     <svg v-else width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M11 5L6 9H2v6h4l5 4V5zM23 9l-6 6M17 9l6 6"/></svg>
                  </button>
                  
                  <span class="mt-1 text-[10px] font-bold text-text-main/40 tabular-nums">{{ Math.round(player.volume * 100) }}</span>

                  <!-- 三角箭头 -->
                  <div class="absolute -bottom-1.5 left-1/2 -translate-x-1/2 w-3 h-3 bg-bg-card/95 rotate-45 border-r border-b border-border-light/40"></div>
                </div>
              </div>
            </Transition>
          </div>
        </div>

        <!-- 进度条系统 - 动态伸缩至最大值 -->
        <div class="w-full max-w-[480px] flex items-center gap-3 px-1 h-[14px] min-w-0">
          <span class="text-[10px] font-medium text-text-main/30 w-9 shrink-0 text-right tabular-nums">{{ formatTime(player.currentTime) }}</span>
          <SliderRoot
            :model-value="[player.currentTime]"
            :max="player.duration || 100"
            :step="0.1"
            class="relative flex items-center select-none touch-none flex-1 min-w-0 h-4 group/progress"
            @update:model-value="handleSeek"
            @mouseenter="isHoveringProgress = true"
            @mouseleave="isHoveringProgress = false"
          >
            <SliderTrack class="player-progress-track bg-black/[0.08] relative grow rounded-full h-[3px]">
              <SliderRange class="absolute bg-primary rounded-full h-full" />
            </SliderTrack>
            <SliderThumb
              class="block w-2.5 h-2.5 bg-white border border-black/10 rounded-full shadow-md focus:outline-none transition-all duration-200"
              :class="[isHoveringProgress ? 'opacity-100 scale-125' : 'opacity-0 scale-50']"
            />
          </SliderRoot>
          <span class="text-[10px] font-medium text-text-main/30 w-9 shrink-0 text-left tabular-nums">{{ formatTime(player.duration) }}</span>
        </div>
      </div>

      <!-- 3. 右侧：功能选项 - 弹性增长 -->
      <div class="player-actions flex-1 flex justify-end items-center gap-1 min-w-[120px] max-w-[320px]">
        <DropdownMenuRoot>
          <DropdownMenuTrigger as-child>
            <button class="p-2 text-text-main/30 hover:text-primary transition-all hover:scale-110 active:scale-90" title="播放倍速">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <circle cx="12" cy="12" r="9" />
                <path d="M12 7v5l3 2" />
              </svg>
            </button>
          </DropdownMenuTrigger>
          <DropdownMenuPortal>
            <DropdownMenuContent class="player-dropdown" align="end" side="top" :side-offset="10">
              <div class="player-dropdown-title">播放倍速</div>
              <DropdownMenuItem
                v-for="rate in playbackRates"
                :key="rate"
                class="player-dropdown-item"
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
            <button class="p-2 text-text-main/30 hover:text-primary transition-all hover:scale-110 active:scale-90" title="音质">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M12 20v-8m0-4V4m-5 12v-2m0-4v-2m10 10v-6m0-4V8" />
              </svg>
            </button>
          </DropdownMenuTrigger>
          <DropdownMenuPortal>
            <DropdownMenuContent class="player-dropdown" align="end" side="top" :side-offset="10">
              <div class="player-dropdown-title">音质</div>
              <DropdownMenuItem class="player-dropdown-item" @select="setAudioQuality('high')">
                <span>Hi-Res</span>
                <span v-if="settingStore.audioQuality === 'high'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem class="player-dropdown-item" @select="setAudioQuality('flac')">
                <span>SQ 无损</span>
                <span v-if="settingStore.audioQuality === 'flac'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem class="player-dropdown-item" @select="setAudioQuality('320')">
                <span>HQ 高品质</span>
                <span v-if="settingStore.audioQuality === '320'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <DropdownMenuItem class="player-dropdown-item" @select="setAudioQuality('128')">
                <span>标准</span>
                <span v-if="settingStore.audioQuality === '128'" class="player-dropdown-check">✓</span>
              </DropdownMenuItem>
              <div class="player-dropdown-divider"></div>
              <div class="player-dropdown-title">音效</div>
              <div class="player-dropdown-scroll">
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('none')">
                  <span>无音效</span>
                  <span v-if="settingStore.audioEffect === 'none'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('piano')">
                  <span>钢琴</span>
                  <span v-if="settingStore.audioEffect === 'piano'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('acappella')">
                  <span>清唱</span>
                  <span v-if="settingStore.audioEffect === 'acappella'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('subwoofer')">
                  <span>重低音</span>
                  <span v-if="settingStore.audioEffect === 'subwoofer'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('ancient')">
                  <span>古风</span>
                  <span v-if="settingStore.audioEffect === 'ancient'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('surnay')">
                  <span>唢呐</span>
                  <span v-if="settingStore.audioEffect === 'surnay'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('dj')">
                  <span>DJ</span>
                  <span v-if="settingStore.audioEffect === 'dj'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('viper_tape')">
                  <span>蝰蛇母带</span>
                  <span v-if="settingStore.audioEffect === 'viper_tape'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('viper_atmos')">
                  <span>蝰蛇全景声</span>
                  <span v-if="settingStore.audioEffect === 'viper_atmos'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
                <DropdownMenuItem class="player-dropdown-item" @select="setAudioEffect('viper_clear')">
                  <span>蝰蛇超清</span>
                  <span v-if="settingStore.audioEffect === 'viper_clear'" class="player-dropdown-check">✓</span>
                </DropdownMenuItem>
              </div>
              <div class="player-dropdown-arrow"></div>
            </DropdownMenuContent>
          </DropdownMenuPortal>
        </DropdownMenuRoot>

        <button
          class="p-2 text-text-main/30 hover:text-primary transition-all hover:scale-110 active:scale-90"
          title="播放队列"
          @click="openQueue"
        >
          <svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01" /></svg>
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
  padding: 8px;
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

:deep(.player-dropdown-title) {
  font-size: 11px;
  font-weight: 700;
  color: var(--color-text-secondary);
  padding: 2px 6px 6px;
}

:deep(.player-dropdown-item) {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 8px;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  color: var(--color-text-main);
  cursor: pointer;
  transition: all 0.2s ease;
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
  margin: 4px 6px;
  background-color: var(--color-border-light);
}

:deep(.player-dropdown-scroll) {
  max-height: 168px;
  overflow-y: auto;
  padding-right: 2px;
}

:deep(.player-dropdown-scroll::-webkit-scrollbar) {
  width: 4px;
}

:deep(.player-dropdown-scroll::-webkit-scrollbar-thumb) {
  background: var(--color-border-light);
  border-radius: 999px;
}

:deep(.player-dropdown-arrow) {
  position: absolute;
  bottom: -6px;
  right: 26px;
  width: 12px;
  height: 12px;
  background: var(--color-bg-card);
  border-right: 1px solid var(--color-border-light);
  border-bottom: 1px solid var(--color-border-light);
  transform: rotate(45deg);
}

</style>
