<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { getRanks } from '@/api/playlist';
import Cover from '@/components/ui/Cover.vue';

const router = useRouter();
const loading = ref(true);
const ranks = ref<any[]>([]);

const fetchRanks = async () => {
  loading.value = true;
  try {
    const res: any = await getRanks();
    if (res.status === 1) {
      ranks.value = res.data?.info || res.info || [];
    }
  } catch (e) {
    console.error('Fetch ranks error:', e);
  } finally {
    loading.value = false;
  }
};

onMounted(fetchRanks);

const goToPlaylist = (rank: any) => {
  router.push({
    name: 'playlist-detail',
    params: { id: rank.rankid },
    query: { type: 'rank' }
  });
};
</script>

<template>
  <div class="ranking-view p-8 space-y-8">
    <div class="space-y-2">
      <h1 class="text-3xl font-black text-text-main">排行榜</h1>
      <p class="text-sm text-text-secondary opacity-60">发现最热门的音乐</p>
    </div>

    <div v-if="loading" class="flex items-center justify-center py-20">
      <div class="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
    </div>

    <div v-else class="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-6">
      <div
        v-for="rank in ranks"
        :key="rank.rankid"
        class="rank-card group cursor-pointer"
        @click="goToPlaylist(rank)"
      >
        <div class="relative aspect-square rounded-xl overflow-hidden shadow-md bg-bg-card transition-all duration-300 group-hover:shadow-xl group-hover:-translate-y-1">
          <Cover :url="rank.imgurl" :size="400" class="transition-transform duration-500 group-hover:scale-110" />

          <div class="absolute inset-0 bg-gradient-to-t from-black/60 via-transparent to-transparent"></div>

          <div class="absolute bottom-3 left-3 right-3">
            <div class="text-white text-xs font-bold opacity-80">{{ rank.rankname }}</div>
          </div>
        </div>

        <div class="mt-3 space-y-1">
          <h4 class="font-bold text-[14px] text-text-main truncate group-hover:text-primary transition-colors">
            {{ rank.rankname }}
          </h4>
        </div>
      </div>
    </div>
  </div>
</template>
