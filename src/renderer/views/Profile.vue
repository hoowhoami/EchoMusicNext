<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { useUserStore } from '@/stores/user';
import { 
  getUserDetail, getUserVipDetail, 
  claimDayVip, upgradeDayVip, getVipMonthRecord 
} from '@/api/user';
import Avatar from '@/components/ui/Avatar.vue';
import logger from '@/utils/logger';

const router = useRouter();
const userStore = useUserStore();
const userInfo = computed(() => userStore.info);

const isLoading = ref(false);

// 提取详细信息
const detail = computed(() => userInfo.value?.extendsInfo?.detail || {});
const vipInfo = computed(() => userInfo.value?.extendsInfo?.vip || {});
const busiVip = computed(() => vipInfo.value?.busi_vip || []);

const tvip = computed(() => busiVip.value.find((v: any) => v.product_type === 'tvip' && v.is_vip === 1));
const svip = computed(() => busiVip.value.find((v: any) => v.product_type === 'svip' && v.is_vip === 1));

const gender = computed(() => {
  const g = detail.value?.gender;
  return g === 1 ? '男' : (g === 0 ? '女' : '保密');
});

// 格式化逻辑
const formatLeLing = (rtime: any) => {
  if (!rtime) return '未知';
  const start = new Date(parseInt(rtime) * 1000);
  const diff = Date.now() - start.getTime();
  const days = Math.floor(diff / (1000 * 60 * 60 * 24));
  if (days > 365) return `${Math.floor(days / 365)} 年`;
  if (days > 30) return `${Math.floor(days / 30)} 个月`;
  return `${days} 天`;
};

const formatDuration = (minutes: any) => {
  if (!minutes) return '0 小时';
  const m = parseInt(minutes) || 0;
  if (m > 60) return `${Math.floor(m / 60)} 小时 ${m % 60} 分钟`;
  return `${m} 分钟`;
};

const getVipExpireText = (vipData: any) => {
  if (!vipData?.vip_end_time) return null;
  try {
    const expireDate = new Date(vipData.vip_end_time);
    const now = new Date();
    const diff = expireDate.getTime() - now.getTime();
    if (diff < 0) return '已过期';
    const days = Math.floor(diff / (1000 * 60 * 60 * 24));
    if (days > 365) return `${Math.floor(days / 365)}年后到期`;
    if (days > 30) return `${Math.floor(days / 30)}个月后到期`;
    if (days > 0) return `${days}天后到期`;
    return '即将到期';
  } catch (e) { return null; }
};

const loadData = async () => {
  if (!userStore.isLoggedIn) return;
  isLoading.value = true;
  try {
    // 并发获取基础信息、VIP信息、领取记录
    const [detailRes, vipRes, recordRes] = await Promise.all([
      getUserDetail(),
      getUserVipDetail(),
      getVipMonthRecord()
    ]);

    if (detailRes.status === 1) {
      userStore.handleLoginSuccess({ ...userInfo.value, extendsInfo: { detail: detailRes.data } });
    }
    if (vipRes.status === 1) {
      userStore.handleLoginSuccess({ 
        ...userInfo.value, 
        extendsInfo: { 
          detail: detail.value, 
          vip: vipRes.data 
        } 
      });
    }

    // 同步领取状态
    const today = new Date().toISOString().split('T')[0];
    const recordList = recordRes?.data?.list || [];
    const isTvipClaimed = recordList.some((item: any) => item.day === today);
    
    // 检查 SVIP 是否已通过 extendsInfo 存在
    const isSvipActive = !!svip.value;
    
    userStore.setClaimStatus(isTvipClaimed, isSvipActive);

  } catch (e) {
    logger.error('Profile', 'Load Data Error:', e);
  } finally {
    isLoading.value = false;
  }
};

const handleClaimTvip = async () => {
  if (userStore.isTvipClaimedToday) return;
  isLoading.value = true;
  try {
    const today = new Date().toISOString().split('T')[0];
    const res = await claimDayVip(today);
    if (res.status === 1) {
      userStore.setClaimStatus(true, userStore.isSvipClaimedToday);
      await loadData(); // 刷新
    }
  } catch (e) { logger.error('Profile', 'Claim TVIP error:', e); }
  finally { isLoading.value = false; }
};

const handleUpgradeSvip = async () => {
  if (userStore.isSvipClaimedToday || !userStore.isTvipClaimedToday) return;
  isLoading.value = true;
  try {
    const res = await upgradeDayVip();
    if (res.status === 1 || res.error_code === 297002) {
      userStore.setClaimStatus(userStore.isTvipClaimedToday, true);
      await loadData(); // 刷新
    }
  } catch (e) { logger.error('Profile', 'Upgrade SVIP error:', e); }
  finally { isLoading.value = false; }
};

const handleLogout = () => {
  userStore.logout();
  router.push('/main/home');
};

onMounted(() => loadData());
</script>

<template>
  <div class="profile-page h-full overflow-y-auto px-10 py-6 select-none bg-bg-main">
    <div v-if="userStore.isLoggedIn && userInfo" class="max-w-[900px] mx-auto animate-in slide-in-from-bottom-4 duration-500">
      
      <!-- 1. Header -->
      <header class="flex items-center justify-between mb-8">
        <h1 class="text-[24px] font-black tracking-tight">个人中心</h1>
        <button 
          @click="handleLogout"
          class="w-10 h-10 flex items-center justify-center rounded-full border border-border-light hover:bg-red-500/10 hover:text-red-500 transition-all active:scale-90"
          title="退出登录"
        >
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
        </button>
      </header>

      <!-- 2. User Profile Card -->
      <div class="user-card relative overflow-hidden p-8 rounded-[32px] bg-gradient-to-br from-primary/10 via-primary/5 to-transparent border border-primary/20 mb-8">
        <div class="flex items-center gap-8 relative z-10">
          <div class="p-1 rounded-full border-2 border-primary/30 shrink-0">
             <Avatar :src="userInfo.pic" class="w-[88px] h-[88px] rounded-full" />
          </div>
          <div class="flex-1 min-w-0">
            <div class="flex items-center gap-3 mb-2">
              <h2 class="text-[22px] font-black truncate">{{ userInfo.nickname }}</h2>
              <div v-if="tvip" class="px-1.5 py-0.5 rounded-md bg-gradient-to-r from-[#07C160] to-[#07C160]/80 text-white text-[9px] font-black shadow-sm">畅听</div>
              <div v-if="svip" class="px-1.5 py-0.5 rounded-md bg-gradient-to-r from-orange-500 to-orange-500/80 text-white text-[9px] font-black shadow-sm">概念</div>
            </div>
            <p v-if="detail.descri" class="text-[13px] opacity-60 font-medium line-clamp-2 mb-4">{{ detail.descri }}</p>
            
            <div class="flex items-center gap-8">
              <div class="flex flex-col">
                <span class="text-[16px] font-black">Lv.{{ detail.p_grade || 0 }}</span>
                <span class="text-[10px] opacity-40 uppercase font-bold tracking-wider">等级</span>
              </div>
              <div class="w-[1px] h-5 bg-black/5 dark:bg-white/5"></div>
              <div class="flex flex-col">
                <span class="text-[16px] font-black">{{ detail.follows || 0 }}</span>
                <span class="text-[10px] opacity-40 uppercase font-bold tracking-wider">关注</span>
              </div>
              <div class="w-[1px] h-5 bg-black/5 dark:bg-white/5"></div>
              <div class="flex flex-col">
                <span class="text-[16px] font-black">{{ detail.fans || 0 }}</span>
                <span class="text-[10px] opacity-40 uppercase font-bold tracking-wider">粉丝</span>
              </div>
              <div class="w-[1px] h-5 bg-black/5 dark:bg-white/5"></div>
              <div class="flex flex-col">
                <span class="text-[16px] font-black">{{ detail.nvisitors || 0 }}</span>
                <span class="text-[10px] opacity-40 uppercase font-bold tracking-wider">访客</span>
              </div>
            </div>
          </div>
        </div>
        <!-- 装饰背景 -->
        <div class="absolute -right-10 -bottom-10 w-40 h-40 bg-primary/5 rounded-full blur-3xl pointer-events-none"></div>
      </div>

      <div class="grid grid-cols-1 md:grid-cols-5 gap-8">
        <!-- 3. Account Archives -->
        <div class="md:col-span-3">
          <div class="flex items-center gap-2 mb-4">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" class="text-primary"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
            <h3 class="text-[16px] font-black">账号档案</h3>
          </div>
          <div class="space-y-1 p-2 rounded-[24px] bg-black/[0.03] dark:bg-white/[0.03]">
             <div class="flex items-center justify-between p-4">
               <span class="text-[13px] opacity-40 font-bold">用户 ID</span>
               <span class="text-[13px] font-black">{{ userInfo.userid }}</span>
             </div>
             <div class="flex items-center justify-between p-4">
               <span class="text-[13px] opacity-40 font-bold">性别</span>
               <span class="text-[13px] font-black">{{ gender }}</span>
             </div>
             <div class="flex items-center justify-between p-4">
               <span class="text-[13px] opacity-40 font-bold">乐龄</span>
               <span class="text-[13px] font-black">{{ formatLeLing(detail.rtime) }}</span>
             </div>
             <div class="flex items-center justify-between p-4">
               <span class="text-[13px] opacity-40 font-bold">累计听歌</span>
               <span class="text-[13px] font-black">{{ formatDuration(detail.duration) }}</span>
             </div>
             <div class="flex items-center justify-between p-4">
               <span class="text-[13px] opacity-40 font-bold">所在地区</span>
               <span class="text-[13px] font-black">{{ detail.loc || '未知' }} {{ detail.city || '' }}</span>
             </div>
          </div>
        </div>

        <!-- 4. Daily Benefits -->
        <div class="md:col-span-2">
           <div class="flex items-center gap-2 mb-4">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" class="text-primary"><path d="M20 12V8H4v4"/><rect width="16" height="12" x="4" y="8" rx="2"/><path d="M12 22V8"/><path d="M12 8H4.4a2.4 2.4 0 0 0 0 4.8h15.2a2.4 2.4 0 0 0 0-4.8H12Z"/></svg>
            <h3 class="text-[16px] font-black">每日权益</h3>
          </div>
          <div class="space-y-3">
             <!-- TVIP -->
             <div 
               @click="handleClaimTvip"
               :class="[
                 'flex items-center gap-4 p-4 rounded-[20px] transition-all border',
                 userStore.isTvipClaimedToday 
                  ? 'bg-green-500/10 border-green-500/20' 
                  : 'bg-black/[0.03] dark:bg-white/[0.03] border-transparent hover:bg-black/[0.06] dark:hover:bg-white/[0.06] cursor-pointer'
               ]"
             >
               <div :class="['w-10 h-10 rounded-full flex items-center justify-center shrink-0', userStore.isTvipClaimedToday ? 'bg-green-500/20 text-green-500' : 'bg-black/5 dark:bg-white/5 opacity-40']">
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
               </div>
               <div class="flex-1">
                 <h4 :class="['text-[14px] font-black', userStore.isTvipClaimedToday ? 'text-green-500' : '']">领取畅听会员</h4>
                 <p v-if="tvip" class="text-[10px] opacity-40 font-bold uppercase">{{ getVipExpireText(tvip) }}</p>
               </div>
               <div v-if="userStore.isTvipClaimedToday" class="text-green-500">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><polyline points="20 6 9 17 4 12"/></svg>
               </div>
               <div v-else class="opacity-20"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><path d="m9 18 6-6-6-6"/></svg></div>
             </div>

             <!-- SVIP -->
             <div 
               @click="handleUpgradeSvip"
               :class="[
                 'flex items-center gap-4 p-4 rounded-[20px] transition-all border',
                 userStore.isSvipClaimedToday || svip
                  ? 'bg-orange-500/10 border-orange-500/20' 
                  : (userStore.isTvipClaimedToday ? 'bg-black/[0.03] dark:bg-white/[0.03] border-transparent hover:bg-black/[0.06] dark:hover:bg-white/[0.06] cursor-pointer' : 'bg-black/[0.01] dark:bg-white/[0.01] opacity-40 cursor-not-allowed')
               ]"
             >
               <div :class="['w-10 h-10 rounded-full flex items-center justify-center shrink-0', (userStore.isSvipClaimedToday || svip) ? 'bg-orange-500/20 text-orange-500' : 'bg-black/5 dark:bg-white/5 opacity-40']">
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.71-2.13.71-2.13l-4.42-.87zM21 3s-9 0-12 9c-1.2 3.6 0 7 0 7l3-3c0 0-1-1 0-3s3-3 3-3l3-3s1 1 3 0l-3-3z"/></svg>
               </div>
               <div class="flex-1">
                 <h4 :class="['text-[14px] font-black', (userStore.isSvipClaimedToday || svip) ? 'text-orange-500' : '']">升级概念会员</h4>
                 <p v-if="svip" class="text-[10px] opacity-40 font-bold uppercase">{{ getVipExpireText(svip) }}</p>
               </div>
               <div v-if="userStore.isSvipClaimedToday || svip" class="text-orange-500">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><polyline points="20 6 9 17 4 12"/></svg>
               </div>
               <div v-else class="opacity-20"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><path d="m9 18 6-6-6-6"/></svg></div>
             </div>
          </div>
        </div>
      </div>

    </div>
    
    <div v-else class="h-full flex flex-col items-center justify-center opacity-30 italic animate-in fade-in duration-500">
       <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" class="mb-4"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
       <span class="text-[16px] font-bold">请先登录以查看个人中心</span>
       <button @click="router.push('/login')" class="mt-6 px-8 py-2.5 rounded-full bg-primary text-white text-[14px] font-black not-italic opacity-100 hover:scale-105 active:scale-95 transition-all">立即登录</button>
    </div>
  </div>
</template>

<style scoped>
.user-card {
  box-shadow: 0 20px 60px -10px rgba(var(--color-primary-rgb), 0.15);
}
</style>
