<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { usePlaylistStore } from '@/stores/playlist';
import { usePlayerStore } from '@/stores/player';
import { getNewSongs } from '@/api/music';
import { getPlaylistByCategory, getPlaylistTags, getRanks, getRankTop } from '@/api/playlist';
import {
  mapPlaylistMeta,
  parsePlaylistTracks,
  mapAlbumMeta,
  mapRankMeta,
  type PlaylistMeta,
  type AlbumMeta,
  type RankMeta,
} from '@/utils/mappers';
import type { Song } from '@/stores/playlist';
import PlaylistCard from '@/components/music/PlaylistCard.vue';
import SongList from '@/components/music/SongList.vue';
import SongListHeader from '@/components/music/SongListHeader.vue';
import ActionRow from '@/components/music/DetailPageActionRow.vue';
import BatchActionDrawer from '@/components/music/BatchActionDrawer.vue';
import Cover from '@/components/ui/Cover.vue';
import CustomTabBar from '@/components/ui/CustomTabBar.vue';
import CustomSelector from '@/components/ui/CustomSelector.vue';
import CustomPicker, { type PickerOption } from '@/components/ui/CustomPicker.vue';
import AlbumCard from '@/components/music/AlbumCard.vue';
import { getAlbumTop } from '@/api/music';
import { useRouter } from 'vue-router';
import type { SortField, SortOrder } from '@/components/music/SongListHeader.vue';
import { iconCurrentLocation, iconSearch } from '@/icons';

const playlistStore = usePlaylistStore();
const playerStore = usePlayerStore();
const router = useRouter();

const recommendedPlaylists = ref<PlaylistMeta[]>([]);
const newSongs = ref<Song[]>([]);
const loadingPlaylists = ref(true);
const loadingNewSongs = ref(true);
const activeTabIndex = ref(0);
const showPlaylistPicker = ref(false);
const showRankPicker = ref(false);
const showAlbumPicker = ref(false);

const playlistCategories = ref<PickerOption[]>([]);
const playlistCategoryLabel = ref('全部');
const playlistCategoryId = ref('0');

const ranks = ref<RankMeta[]>([]);
const rankLabel = ref('排行榜');
const rankId = ref<number | null>(null);
const rankSongs = ref<Song[]>([]);
const loadingRankSongs = ref(true);
const rankSearchQuery = ref('');
const rankSongListRef = ref<{ scrollToActive?: () => void } | null>(null);
const showRankBatchDrawer = ref(false);

const rankSortField = ref<SortField | null>(null);
const rankSortOrder = ref<SortOrder>(null);

const albumTypes = ref<PickerOption[]>([
  { id: 'all', name: '全部' },
  { id: '1', name: '华语' },
  { id: '2', name: '欧美' },
  { id: '3', name: '日本' },
  { id: '4', name: '韩国' },
]);
const albumTypeId = ref('all');
const albumTypeLabel = ref('全部');
const albums = ref<AlbumMeta[]>([]);
const loadingAlbums = ref(true);

const visibleNewSongs = computed(() => newSongs.value.slice(0, 10));
const activeRankSongId = computed(() => playerStore.currentTrackId ?? undefined);
const sortedRankSongs = computed(() => {
  const base = rankSongs.value.slice();
  if (!rankSortField.value || !rankSortOrder.value) return base;
  const compareText = (a: string, b: string) =>
    a.localeCompare(b, 'zh-Hans-CN', { sensitivity: 'base' });
  const indexMap = new Map<string, number>();
  rankSongs.value.forEach((song, index) => {
    indexMap.set(song.id, index);
  });
  const direction = rankSortOrder.value === 'asc' ? 1 : -1;

  return base.sort((a, b) => {
    switch (rankSortField.value) {
      case 'title':
        return compareText(a.title, b.title) * direction;
      case 'album':
        return compareText(a.album ?? '', b.album ?? '') * direction;
      case 'duration':
        return (a.duration - b.duration) * direction;
      case 'index':
        return ((indexMap.get(a.id) ?? 0) - (indexMap.get(b.id) ?? 0)) * direction;
      default:
        return 0;
    }
  });
});

const extractList = (payload: unknown): unknown[] => {
  if (Array.isArray(payload)) return payload;
  if (payload && typeof payload === 'object') {
    const record = payload as Record<string, unknown>;
    const data = record.data as Record<string, unknown> | undefined;
    const list = data?.special_list ?? data?.list ?? record.list ?? record.info ?? data ?? record;
    return Array.isArray(list) ? list : [];
  }
  return [];
};

const loadPlaylistCategories = async () => {
  try {
    const res = await getPlaylistTags();
    const list = extractList(res).filter((item) => typeof item === 'object' && item !== null) as Record<string, unknown>[];
    const options: PickerOption[] = [];
    list.forEach((cat) => {
      const groupName = String(cat.tag_name ?? cat.name ?? '');
      const sons = (cat.son as Record<string, unknown>[] | undefined) ?? [];
      if (sons.length === 0) return;
      sons.forEach((son) => {
        options.push({
          id: String(son.tag_id ?? son.id ?? ''),
          name: String(son.tag_name ?? son.name ?? ''),
          group: groupName,
        });
      });
    });
    playlistCategories.value = options;
    if (options.length > 0 && playlistCategoryId.value === '0') {
      const first = options[0];
      playlistCategoryId.value = first.id;
      playlistCategoryLabel.value = first.group ? `${first.group} - ${first.name}` : first.name;
    }
  } catch (error) {
    playlistCategories.value = [];
  }
};

const loadRecommendedPlaylists = async () => {
  loadingPlaylists.value = true;
  try {
    const res = await getPlaylistByCategory(playlistCategoryId.value || '0', 0, 1);
    const list = extractList(res).map((item) => mapPlaylistMeta(item));
    recommendedPlaylists.value = list;
  } catch (error) {
    recommendedPlaylists.value = [];
  } finally {
    loadingPlaylists.value = false;
  }
};

const loadNewSongs = async () => {
  loadingNewSongs.value = true;
  try {
    const res = await getNewSongs();
    const songs = parsePlaylistTracks(res).songs;
    newSongs.value = songs;
  } catch (error) {
    newSongs.value = [];
  } finally {
    loadingNewSongs.value = false;
  }
};

const loadRanks = async () => {
  try {
    const res = await getRanks();
    const list = extractList(res).map((item) => mapRankMeta(item));
    ranks.value = list;
    if (list.length > 0) {
      rankId.value = list[0].id;
      rankLabel.value = list[0].name;
      await loadRankSongs(list[0].id);
    }
  } catch (error) {
    try {
      const fallback = await getRankTop();
      const list = extractList(fallback).map((item) => mapRankMeta(item));
      ranks.value = list;
      if (list.length > 0) {
        rankId.value = list[0].id;
        rankLabel.value = list[0].name;
        await loadRankSongs(list[0].id);
      }
    } catch (_error) {
      ranks.value = [];
    }
  }
};

const loadRankSongs = async (targetId: number) => {
  loadingRankSongs.value = true;
  try {
    const res = await getRankSongs(targetId, 1, 100);
    const list = extractList(res).map((item) => mapRankSong(item));
    rankSongs.value = list;
  } catch (error) {
    rankSongs.value = [];
  } finally {
    loadingRankSongs.value = false;
  }
};

const loadAlbums = async () => {
  loadingAlbums.value = true;
  try {
    const res = await getAlbumTop(albumTypeId.value === 'all' ? '' : albumTypeId.value);
    const list = extractList(res).map((item) => mapAlbumMeta(item));
    albums.value = list;
  } catch (error) {
    albums.value = [];
  } finally {
    loadingAlbums.value = false;
  }
};

const isPlayableSong = (song: Song) => {
  const isUnavailable = song.privilege === 40;
  const isPaid = song.privilege === 10 && song.payType === 2;
  const isNoCopyright = song.privilege === 5;
  if (isUnavailable || isPaid) return false;
  if (isNoCopyright) return song.oldCpy === 1;
  return Boolean(song.hash?.trim());
};

const playSong = (song: Song) => {
  if (!isPlayableSong(song)) return;
  playlistStore.defaultList = newSongs.value.slice();
  playerStore.playTrack(song.id);
};

const playRankSongs = () => {
  if (rankSongs.value.length === 0) return;
  const playable = rankSongs.value.find((song) => isPlayableSong(song));
  if (!playable) return;
  playlistStore.defaultList = rankSongs.value.slice();
  playerStore.playTrack(playable.id);
};

const openRankBatchDrawer = () => {
  if (rankSongs.value.length === 0) return;
  showRankBatchDrawer.value = true;
};

const handleRankLocate = () => rankSongListRef.value?.scrollToActive?.();

const handleRankSort = (field: SortField) => {
  if (rankSortField.value === field) {
    if (rankSortOrder.value === 'asc') {
      rankSortOrder.value = 'desc';
    } else if (rankSortOrder.value === 'desc') {
      rankSortField.value = null;
      rankSortOrder.value = null;
    }
  } else {
    rankSortField.value = field;
    rankSortOrder.value = 'asc';
  }
};

onMounted(() => {
  void loadPlaylistCategories();
  void loadRecommendedPlaylists();
  void loadNewSongs();
  void loadRanks();
  void loadAlbums();
});

watch(
  () => playlistCategoryId.value,
  () => {
    void loadRecommendedPlaylists();
  },
);

watch(
  () => albumTypeId.value,
  () => {
    void loadAlbums();
  },
);

const handleRankNavigate = () => {
  if (!rankId.value) return;
  router.push({ name: 'ranking' });
};

const handleSelectRank = (option: PickerOption) => {
  const id = Number.parseInt(option.id, 10);
  if (Number.isNaN(id)) return;
  rankId.value = id;
  rankLabel.value = option.name;
  showRankPicker.value = false;
  void loadRankSongs(id);
};

const handleSelectPlaylistCategory = (option: PickerOption) => {
  playlistCategoryId.value = option.id;
  playlistCategoryLabel.value = option.group ? `${option.group} - ${option.name}` : option.name;
  showPlaylistPicker.value = false;
};

const handleSelectAlbumType = (option: PickerOption) => {
  albumTypeId.value = option.id;
  albumTypeLabel.value = option.name;
  showAlbumPicker.value = false;
};
</script>

<template>
  <div class="explore-view px-10 pt-5 pb-10">
    <div class="text-[24px] font-semibold text-text-main tracking-tight">发现音乐</div>

    <div class="mt-4">
      <CustomTabBar
        v-model="activeTabIndex"
        :tabs="['歌单', '排行榜', '新碟上架', '新歌速递']"
      />
    </div>

    <div v-if="activeTabIndex === 0" class="mt-6">
      <div class="explore-toolbar">
        <CustomSelector :label="playlistCategoryLabel" @click="showPlaylistPicker = true" />
      </div>
      <div v-if="loadingPlaylists" class="h-[220px] flex items-center justify-center text-[12px] text-text-secondary/70">加载中...</div>
      <div v-else class="playlist-grid">
        <div
          v-for="entry in recommendedPlaylists"
          :key="String(entry.id)"
          class="playlist-grid-item"
        >
          <PlaylistCard
            :id="entry.listCreateGid || entry.globalCollectionId || entry.listCreateListid || entry.id"
            :name="entry.name"
            :coverUrl="entry.pic"
            :creator="entry.nickname"
            :songCount="entry.count"
            :coverRadius="14"
            :showShadow="true"
            layout="grid"
          />
        </div>
      </div>
    </div>

    <div v-else-if="activeTabIndex === 1" class="mt-6">
      <div class="explore-toolbar">
        <CustomSelector :label="rankLabel" @click="showRankPicker = true" />
      </div>
      <div class="ranking-embed" @click="handleRankNavigate">
        <div class="ranking-embed-title">进入排行榜</div>
        <div class="ranking-embed-sub">当前：{{ rankLabel }}</div>
      </div>
    </div>

    <div v-else-if="activeTabIndex === 2" class="mt-6">
      <div class="explore-toolbar">
        <CustomSelector :label="albumTypeLabel" @click="showAlbumPicker = true" />
      </div>
      <div v-if="loadingAlbums" class="h-[230px] flex items-center justify-center text-[12px] text-text-secondary/70">加载中...</div>
      <div v-else class="album-grid">
        <AlbumCard
          v-for="album in albums"
          :key="String(album.id)"
          :id="album.id"
          :name="album.name"
          :coverUrl="album.pic"
          :artist="album.singerName"
          :publishTime="album.publishTime"
        />
      </div>
    </div>

    <div v-else class="mt-6">
      <div v-if="loadingNewSongs" class="h-[210px] flex items-center justify-center text-[12px] text-text-secondary/70">加载中...</div>
      <div v-else class="new-song-grid">
        <button
          v-for="song in visibleNewSongs"
          :key="song.id"
          class="new-song-card"
          @click="playSong(song)"
        >
          <div class="new-song-cover">
            <Cover :url="song.coverUrl" :size="200" class="w-full h-full" :borderRadius="16" />
          </div>
          <div class="mt-2 text-[13px] font-semibold text-text-main truncate">{{ song.title }}</div>
          <div class="text-[11px] text-text-secondary truncate">{{ song.artist }}</div>
        </button>
      </div>
    </div>

    <CustomPicker
      v-model:open="showPlaylistPicker"
      title="歌单分类"
      :options="playlistCategories"
      :selectedId="playlistCategoryId"
      @select="handleSelectPlaylistCategory"
    />

    <CustomPicker
      v-model:open="showRankPicker"
      title="排行榜选择"
      :options="ranks.map((rank) => ({ id: String(rank.id), name: rank.name }))"
      :selectedId="rankId ? String(rankId) : ''"
      @select="handleSelectRank"
    />

    <CustomPicker
      v-model:open="showAlbumPicker"
      title="专辑类型"
      :options="albumTypes"
      :selectedId="albumTypeId"
      @select="handleSelectAlbumType"
      :maxWidth="360"
    />
  </div>
</template>

<style scoped>
@reference "@/style.css";

.explore-toolbar {
  display: flex;
  align-items: center;
  padding: 0 0 10px 0;
}

.playlist-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 20px;
}

.playlist-grid-item :deep(.card-container) {
  height: 230px;
}

.playlist-grid-item :deep(.cover-wrapper) {
  height: 170px;
  aspect-ratio: auto;
}

.ranking-embed {
  height: 180px;
  border-radius: 16px;
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
  border: 1px solid color-mix(in srgb, var(--color-primary) 30%, transparent);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  cursor: pointer;
}

.ranking-embed-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--color-primary);
}

.ranking-embed-sub {
  font-size: 12px;
  color: color-mix(in srgb, var(--color-text-main) 60%, transparent);
}

.album-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 20px;
}

.new-song-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, 200px);
  grid-auto-rows: 210px;
  gap: 20px;
  justify-content: start;
}

.new-song-card {
  text-align: left;
  height: 210px;
  display: flex;
  flex-direction: column;
  padding: 0;
  border: none;
  background: transparent;
}

.new-song-cover {
  position: relative;
  flex: 1;
  border-radius: 16px;
  overflow: hidden;
}
</style>
