<script setup lang="ts">
import { ref, onMounted, onUnmounted, reactive, watch } from 'vue';
import { useRouter } from 'vue-router';
import { useUserStore } from '../stores/user';
import { 
  getLoginQrKey, createLoginQr, checkLoginQr, 
  sendSmsCode, loginBySms,
  createWxLogin, checkWxLogin, loginByOpenPlat 
} from '../api/user';

// 引入封装后的 UI 组件
import Tabs from '../components/ui/Tabs.vue';
import TabsList from '../components/ui/TabsList.vue';
import TabsTrigger from '../components/ui/TabsTrigger.vue';
import TabsContent from '../components/ui/TabsContent.vue';
import Input from '../components/ui/Input.vue';
import Button from '../components/ui/Button.vue';

import OverlayHeader from '../layouts/OverlayHeader.vue';
import CommonImage from '../components/CommonImage.vue';

const router = useRouter();
const userStore = useUserStore();

// 当前选中的 Tab (0: 扫码, 1: 验证码, 2: 微信)
const activeTab = ref('0');

// --- 酷狗扫码逻辑 ---
const qrKey = ref<string | undefined>(undefined);
const qrUrl = ref<string | undefined>(undefined);
const qrStatus = ref(1); 
const isLoadingQr = ref(false);
let isPollingQr = false;

const loadQrCode = async () => {
  if (activeTab.value !== '0') return;
  isLoadingQr.value = true;
  qrUrl.value = undefined;
  isPollingQr = false;
  try {
    const keyRes: any = await getLoginQrKey();
    const currentKey = keyRes?.data?.qrcode || keyRes?.data?.key;
    if (keyRes?.status === 1 && currentKey) {
      qrKey.value = currentKey;
      qrStatus.value = 1;
      if (keyRes.data.qrcode_img) {
        qrUrl.value = keyRes.data.qrcode_img;
        startCheckStatus();
      } else {
        const createRes: any = await createLoginQr(qrKey.value!);
        if (createRes?.status === 1 && createRes?.data?.qrcode_img) {
          qrUrl.value = createRes.data.qrcode_img;
          startCheckStatus();
        }
      }
    }
  } catch (e) {
    console.error('[Login] Load QR Error:', e);
  } finally {
    isLoadingQr.value = false;
  }
};

const startCheckStatus = async () => {
  if (isPollingQr || activeTab.value !== '0') return;
  isPollingQr = true;
  console.log('[Login] Starting Kugou QR polling...');
  
  while (isPollingQr && qrKey.value && activeTab.value === '0') {
    try {
      const res: any = await checkLoginQr(qrKey.value);
      if (!isPollingQr || activeTab.value !== '0') break;
      
      if (res) {
        const status = res.data?.status ?? res.status;
        qrStatus.value = status;
        if (status === 4 && res.data) {
          isPollingQr = false;
          userStore.handleLoginSuccess(res.data);
          router.push('/main/home');
          break;
        } else if (status === 0) {
          isPollingQr = false;
          break;
        }
      }
    } catch (e) {
      console.error('[Login] Check QR Status Error:', e);
    }
    await new Promise(resolve => setTimeout(resolve, 3000));
  }
  isPollingQr = false;
  console.log('[Login] Kugou QR polling stopped.');
};

// --- 验证码登录逻辑 ---
const smsData = reactive({
  mobile: '',
  code: '',
  isSending: false,
  countdown: 0,
  error: ''
});
let smsTimer: any = null;

const startCountdown = () => {
  smsData.countdown = 60;
  smsTimer = setInterval(() => {
    smsData.countdown--;
    if (smsData.countdown <= 0) clearInterval(smsTimer);
  }, 1000);
};

const handleSendCode = async () => {
  if (!/^1[3-9]\d{9}$/.test(smsData.mobile)) {
    smsData.error = '请输入正确的手机号';
    return;
  }
  smsData.isSending = true;
  smsData.error = '';
  try {
    const res: any = await sendSmsCode(smsData.mobile);
    if (res.status === 1) startCountdown();
    else smsData.error = res.error || '发送失败';
  } catch (e) {
    smsData.error = '网络错误';
  } finally {
    smsData.isSending = false;
  }
};

const handleSmsLogin = async () => {
  if (!smsData.mobile || !smsData.code) return;
  smsData.isSending = true;
  try {
    const res: any = await loginBySms(smsData.mobile, smsData.code);
    if (res.status === 1 && res.data) {
      userStore.handleLoginSuccess(res.data);
      router.push('/main/home');
    } else {
      smsData.error = res.error || '登录失败';
    }
  } catch (e) {
    smsData.error = '登录异常';
  } finally {
    smsData.isSending = false;
  }
};

// --- 微信扫码逻辑 ---
const wxQr = reactive({
  url: '',
  uuid: '',
  status: 0, // 0: 等待, 1: 扫描, 2: 确认, 3: 过期
  isLoading: false
});
let isPollingWx = false;

const loadWxQr = async () => {
  if (activeTab.value !== '2') return;
  wxQr.isLoading = true;
  wxQr.url = '';
  wxQr.status = 0;
  isPollingWx = false;
  try {
    const res: any = await createWxLogin();
    if (res?.uuid) {
      wxQr.uuid = res.uuid;
      const base64 = res.qrcode?.qrcodebase64;
      if (base64) {
        wxQr.url = base64.startsWith('data:') ? base64 : `data:image/jpeg;base64,${base64}`;
      } else {
        wxQr.url = res.qrcode?.qrcodeurl || '';
      }
      startCheckWxStatus();
    }
  } catch (e) {
    console.error('[Login] Load Wx QR Error:', e);
  } finally {
    wxQr.isLoading = false;
  }
};

const startCheckWxStatus = async () => {
  if (isPollingWx || activeTab.value !== '2') return;
  isPollingWx = true;
  console.log('[Login] Starting WeChat polling...');

  while (isPollingWx && wxQr.uuid && activeTab.value === '2') {
    try {
      // 这里的 checkWxLogin 内部可能需要针对长轮询调大 axios 的 timeout
      const res: any = await checkWxLogin(wxQr.uuid, Date.now());
      if (!isPollingWx || activeTab.value !== '2') break;
      if (res) {
        const code = res.wx_errcode || res.status;
        if (code === 405) { 
           isPollingWx = false;
           const wxCode = res.wx_code;
           if (wxCode) {
             const loginRes: any = await loginByOpenPlat(wxCode);
             if (loginRes?.status === 1 || loginRes?.code === 200) {
               userStore.handleLoginSuccess(loginRes.data || loginRes.body?.data || loginRes);
               router.push('/main/home');
             }
           }
           break;
        } else if (code === 404) {
          wxQr.status = 1;
        } else if (code === 403 || code === 402) {
          wxQr.status = 3;
          isPollingWx = false;
          break;
        } else if (code === 408) {
          wxQr.status = 0;
        }
      }
    } catch (e) {
      console.error('[Login] Check Wx Status Error:', e);
    }
    // 如果发生异常或请求结束，等待 3 秒再次尝试
    await new Promise(resolve => setTimeout(resolve, 3000));
  }
  isPollingWx = false;
  console.log('[Login] WeChat polling stopped.');
};

const stopCheckStatus = () => { 
  isPollingQr = false;
  isPollingWx = false;
};

// 监听 Tab 切换，触发对应逻辑
watch(activeTab, (newTab) => {
  console.log('[Login] Tab changed to:', newTab);
  stopCheckStatus();
  if (newTab === '0') loadQrCode();
  else if (newTab === '2') loadWxQr();
});

onMounted(() => loadQrCode());
onUnmounted(() => {
  stopCheckStatus();
  if (smsTimer) clearInterval(smsTimer);
});
</script>

<template>
  <div class="login-page fixed inset-0 overflow-hidden bg-bg-main text-text-main transition-colors duration-500 select-none flex flex-col">
    <!-- 装饰背景 -->
    <div class="absolute inset-0 bg-gradient-to-br from-bg-sidebar via-bg-main to-bg-sidebar opacity-60 z-0"></div>
    <div class="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[800px] h-[800px] rounded-full bg-primary/[0.03] blur-[120px] pointer-events-none z-0"></div>

    <OverlayHeader class="relative z-20" />

    <div class="flex-1 relative overflow-hidden flex items-center justify-center p-6 z-10">
      <div class="absolute top-4 left-6 z-[100]">
        <button @click="router.back()" class="no-drag w-10 h-10 flex items-center justify-center rounded-full text-text-main dark:text-white transition-all duration-300 bg-transparent hover:bg-black/[0.05] dark:hover:bg-white/[0.1]">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" class="opacity-40 group-hover:opacity-100"><path d="m15 18-6-6 6-6"/></svg>
        </button>
      </div>

      <!-- 设置 activationMode="manual" 防止焦点自动切换导致意外 Tab 跳转 -->
      <Tabs v-model="activeTab" activationMode="manual" class="w-full max-w-[420px] max-h-full">
        <div class="bg-bg-card/70 dark:bg-[#1C1C1E]/80 backdrop-blur-3xl border border-black/[0.05] dark:border-white/[0.05] rounded-[36px] shadow-[0_40px_100px_rgba(0,0,0,0.1)] transition-all duration-500 overflow-hidden flex flex-col h-[510px]">
          
          <div class="px-10 pt-8 flex-1 flex flex-col items-center justify-center">
            
            <!-- 1. 扫码登录 -->
            <TabsContent value="0" class="w-full animate-fade-in flex flex-col items-center">
              <div class="text-center mb-4">
                <h1 class="text-[26px] font-black tracking-tight leading-tight mb-1">扫码登录</h1>
                <p class="text-[13px] opacity-40 font-bold uppercase tracking-[1.5px]">使用酷狗概念版扫码</p>
              </div>
              <div class="relative w-48 h-48 bg-white p-3.5 rounded-[28px] shadow-[0_12px_40px_rgba(0,0,0,0.06)] border border-black/[0.02]">
                  <CommonImage :src="qrUrl" className="w-full h-full rounded-xl" />
                  <div v-if="qrStatus === 0" class="absolute inset-0 bg-white/95 rounded-2xl flex flex-col items-center justify-center space-y-4 z-30">
                      <span class="text-[13px] font-black opacity-60">二维码已过期</span>
                      <button @click="loadQrCode" class="text-[13px] text-primary font-black hover:opacity-80 transition-all active:scale-95">重新加载</button>
                  </div>
                  <div v-if="qrStatus === 2" class="absolute inset-0 bg-white/98 rounded-2xl flex flex-col items-center justify-center space-y-5 z-30">
                      <div class="w-14 h-14 bg-green-500 rounded-full flex items-center justify-center text-white"><svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4"><path d="M20 6L9 17l-5-5"/></svg></div>
                      <p class="text-[14px] font-black opacity-80">请在手机端确认</p>
                  </div>
              </div>
              <div class="mt-6 text-[11px] font-black opacity-20 uppercase tracking-[3px]">等待扫码中</div>
            </TabsContent>

            <!-- 2. 验证码登录 -->
            <TabsContent value="1" class="w-full animate-fade-in pb-2">
              <div class="text-center mb-4">
                <h1 class="text-[26px] font-black mb-1">验证码登录</h1>
                <p class="text-[13px] opacity-40 font-bold uppercase tracking-[1.5px]">无需密码，快捷安全</p>
              </div>
              <div class="flex flex-col">
                <Input 
                  v-model="smsData.mobile" 
                  type="tel" 
                  placeholder="手机号码" 
                  class="mb-4"
                />
                <div class="flex gap-3 mb-1">
                  <Input 
                    v-model="smsData.code" 
                    placeholder="验证码" 
                    class="flex-1"
                    inputClass="pr-10"
                  />
                  <Button 
                    variant="secondary" 
                    class="shrink-0 whitespace-nowrap"
                    :disabled="smsData.countdown > 0"
                    @click="handleSendCode"
                  >
                    {{ smsData.countdown > 0 ? `${smsData.countdown}s` : '获取验证码' }}
                  </Button>
                </div>
                <div class="h-5 flex items-center px-2 mb-1">
                  <p v-if="smsData.error" class="text-[11px] text-red-500 font-bold">{{ smsData.error }}</p>
                </div>
                <Button 
                  class="w-full" 
                  :loading="smsData.isSending" 
                  @click="handleSmsLogin"
                >
                  立即登录
                </Button>
              </div>
            </TabsContent>

            <!-- 3. 微信扫码 -->
            <TabsContent value="2" class="w-full animate-fade-in flex flex-col items-center">
              <div class="text-center mb-4">
                <h1 class="text-[26px] font-black mb-1">微信登录</h1>
                <p class="text-[13px] opacity-40 font-bold uppercase tracking-[1.5px]">请使用微信扫描二维码</p>
              </div>
              <div class="relative w-48 h-48 bg-white p-3.5 rounded-[28px] shadow-[0_12px_40px_rgba(0,0,0,0.06)] border border-black/[0.02]">
                  <CommonImage :src="wxQr.url" className="w-full h-full rounded-xl" />
                  <div v-if="wxQr.status === 3" class="absolute inset-0 bg-white/95 rounded-2xl flex flex-col items-center justify-center space-y-4 z-30">
                      <span class="text-[13px] font-black opacity-60">二维码已过期</span>
                      <button @click="loadWxQr" class="text-[13px] text-[#07C160] font-black hover:opacity-80 transition-all active:scale-95">重新加载</button>
                  </div>
                  <div v-if="wxQr.status === 1" class="absolute inset-0 bg-white/98 rounded-2xl flex flex-col items-center justify-center space-y-5 z-30">
                      <div class="w-14 h-14 bg-[#07C160] rounded-full flex items-center justify-center text-white"><svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4"><path d="M20 6L9 17l-5-5"/></svg></div>
                      <p class="text-[14px] font-black opacity-80">请在手机端确认</p>
                  </div>
              </div>
              <div class="mt-6 text-[11px] font-black opacity-20 uppercase tracking-[3px]">等待微信扫码</div>
            </TabsContent>
          </div>

          <!-- 底部：其他方式 -->
          <div class="px-10 pb-8">
            <div class="pt-6 border-t border-black/[0.03] dark:border-white/[0.03] flex flex-col items-center space-y-4">
              <span class="text-[12px] font-black opacity-30 uppercase tracking-[4px]">其他登录方式</span>
              <TabsList class="gap-10">
                  <TabsTrigger value="0" class="group data-[state=active]:hidden">
                    <div class="w-14 h-14 rounded-full border border-border-light flex items-center justify-center text-primary/60 group-hover:text-primary transition-all group-active:scale-90 group-hover:bg-primary/5">
                      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M3 7V5a2 2 0 0 1 2-2h2m10 0h2a2 2 0 0 1 2 2v2m0 10v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2M7 7h10v10H7z"/></svg>
                    </div>
                  </TabsTrigger>
                  <TabsTrigger value="1" class="group data-[state=active]:hidden">
                    <div class="w-14 h-14 rounded-full border border-border-light flex items-center justify-center text-text-main/50 group-hover:text-primary transition-all group-active:scale-90 group-hover:bg-primary/5">
                      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 18h.01M8 21h8a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2H8a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2z"/></svg>
                    </div>
                  </TabsTrigger>
                  <TabsTrigger value="2" class="group data-[state=active]:hidden">
                    <div class="w-14 h-14 rounded-full border border-border-light flex items-center justify-center text-[#07C160]/60 group-hover:text-[#07C160] transition-all group-active:scale-90 group-hover:bg-[#07C160]/5">
                      <svg width="26" height="26" viewBox="0 0 24 24" fill="currentColor"><path d="M12 2C6.48 2 2 5.92 2 10.77c0 2.76 1.45 5.2 3.73 6.84l-.54 1.93c-.04.14.07.28.21.28.06 0 .13-.02.18-.06l2.31-1.34c.67.19 1.37.29 2.11.29 5.52 0 10-3.92 10-8.77S17.52 2 12 2zm4.1 8.56c-.22 0-.4-.18-.4-.4V8.43c0-.22.18-.4.4-.4s.4.18.4.4v1.73c0 .22-.18.4-.4.4zm-2.6 0c-.22 0-.4-.18-.4-.4V8.43c0-.22.18-.4.4-.4s.4.18.4.4v1.73c0 .22-.18.4-.4.4zm-4.93-.4c0-.22.18-.4.4-.4s.4.18.4.4v1.73c0 .22-.18.4-.4.4s-.4-.18-.4-.4V10.16zm2.6 0c0-.22.18-.4.4-.4s.4.18.4.4v1.73c0 .22-.18.4-.4.4s-.4-.18-.4-.4V10.16z"/></svg>
                    </div>
                  </TabsTrigger>
              </TabsList>
            </div>
          </div>

        </div>
      </Tabs>
    </div>
  </div>
</template>

<style scoped>
.animate-fade-in {
  animation: fade-in 0.6s cubic-bezier(0.2, 0.8, 0.2, 1) forwards;
}
@keyframes fade-in {
  from { opacity: 0; transform: translateY(30px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
