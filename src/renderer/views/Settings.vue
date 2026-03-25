<script setup lang="ts">
import { computed, ref } from 'vue';
import { useSettingStore } from '../stores/setting';
import Select from '../components/ui/Select.vue';
import Slider from '../components/ui/Slider.vue';
import Switch from '../components/ui/Switch.vue';
import Dialog from '../components/ui/Dialog.vue';

const settingStore = useSettingStore();

type ThemeMode = 'light' | 'dark' | 'system';
type CloseBehavior = 'tray' | 'exit';
type AudioQuality = '128' | '320' | 'flac' | 'high';
type ShortcutCommand =
  | 'togglePlayback'
  | 'previousTrack'
  | 'nextTrack'
  | 'volumeUp'
  | 'volumeDown'
  | 'toggleMute'
  | 'toggleFavorite'
  | 'togglePlayMode'
  | 'toggleWindow';

interface ShortcutItem {
  command: ShortcutCommand;
  title: string;
  desc: string;
}

const shortcutItems: ShortcutItem[] = [
  { command: 'togglePlayback', title: '播放 / 暂停', desc: '切换当前歌曲的播放状态' },
  { command: 'previousTrack', title: '上一首', desc: '跳转到播放列表中的上一首歌曲' },
  { command: 'nextTrack', title: '下一首', desc: '跳转到播放列表中的下一首歌曲' },
  { command: 'volumeUp', title: '音量 +', desc: '将播放器音量提高 5%' },
  { command: 'volumeDown', title: '音量 -', desc: '将播放器音量降低 5%' },
  { command: 'toggleMute', title: '静音', desc: '切换播放器静音状态' },
  { command: 'toggleFavorite', title: '收藏当前歌曲', desc: '切换播放器当前歌曲的收藏状态' },
  { command: 'togglePlayMode', title: '切换播放模式', desc: '在列表循环、单曲循环、随机播放之间切换' },
  { command: 'toggleWindow', title: '显示 / 隐藏窗口', desc: '切换主窗口的显示和隐藏状态' },
];

const themeOptions = [
  { label: '跟随系统', value: 'system' },
  { label: '浅色模式', value: 'light' },
  { label: '深色模式', value: 'dark' },
];

const shortcutBindings = computed(() => settingStore.shortcutBindings ?? {});

const toggleShortcut = (command: ShortcutCommand) => {
  const current = shortcutBindings.value[command] ?? settingStore.defaultShortcutLabels[command];
  settingStore.shortcutBindings = {
    ...shortcutBindings.value,
    [command]: current,
  };
};

const clearShortcut = (command: ShortcutCommand) => {
  const next = { ...shortcutBindings.value };
  delete next[command];
  settingStore.shortcutBindings = next;
};

const showConfirmClear = ref(false);

const audioQualityOptions = [
  { label: 'Hi-Res品质', value: 'high' },
  { label: 'SQ无损品质', value: 'flac' },
  { label: 'HQ高品质', value: '320' },
  { label: '标准品质', value: '128' },
];

const closeBehaviorOptions = [
  { label: '最小化到托盘', value: 'tray' },
  { label: '彻底退出程序', value: 'exit' },
];

const outputDeviceOptions = computed(() =>
  settingStore.outputDevices.map((device) => ({ label: device, value: device }))
);

const versionLabel = computed(() => settingStore.appVersion || '1.0.0');
const releaseChannelLabel = computed(() =>
  settingStore.isPrerelease ? 'Prerelease' : 'Release'
);
</script>

<template>
  <div class="settings-view p-8 space-y-12 max-w-4xl transition-colors duration-300">
    <header>
      <h1 class="text-3xl font-black tracking-tight text-text-main">偏好设置</h1>
    </header>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 3v18"/><path d="M3 12h18"/></svg>
        </div>
        <h2 class="text-lg font-bold">外观与界面</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">主题模式</h3>
            <p class="text-sm text-text-secondary">选择您喜欢的主题外观</p>
          </div>
          <Select
            class="min-w-[180px]"
            :model-value="settingStore.theme"
            :options="themeOptions"
            @update:model-value="settingStore.setTheme($event as ThemeMode)"
          />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">记住窗口大小</h3>
            <p class="text-sm text-text-secondary">在下次启动时自动恢复窗口大小和位置</p>
          </div>
          <Switch v-model="settingStore.rememberWindowSize" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">播放列表计数</h3>
            <p class="text-sm text-text-secondary">在播放器播放列表图标上显示计数</p>
          </div>
          <Switch v-model="settingStore.showPlaylistCount" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">关闭行为</h3>
            <p class="text-sm text-text-secondary">点击窗口关闭按钮时的应用行为</p>
          </div>
          <Select
            class="min-w-[180px]"
            :model-value="settingStore.closeBehavior"
            :options="closeBehaviorOptions"
            @update:model-value="settingStore.closeBehavior = $event as CloseBehavior"
          />
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M5 12h14"/><path d="M12 5v14"/></svg>
        </div>
        <h2 class="text-lg font-bold">播放体验</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">播放替换队列</h3>
            <p class="text-sm text-text-secondary">双击播放单曲时用当前列表替换播放队列</p>
          </div>
          <Switch v-model="settingStore.replacePlaylist" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">淡入淡出播放</h3>
            <p class="text-sm text-text-secondary">启用歌曲切换时的过渡效果</p>
          </div>
          <Switch v-model="settingStore.volumeFade" />
        </div>
        <div v-if="settingStore.volumeFade" class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">淡入淡出时长</h3>
            <p class="text-sm text-text-secondary">{{ (settingStore.volumeFadeTime / 1000).toFixed(1) }}s</p>
          </div>
          <Slider
            class="w-48"
            :model-value="settingStore.volumeFadeTime"
            :min="100"
            :max="3000"
            :step="100"
            @update:model-value="settingStore.volumeFadeTime = $event"
          />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">自动跳过错误</h3>
            <p class="text-sm text-text-secondary">歌曲加载失败时自动尝试下一首</p>
          </div>
          <Switch v-model="settingStore.autoNext" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">防止系统休眠</h3>
            <p class="text-sm text-text-secondary">播放音乐时阻止系统进入睡眠</p>
          </div>
          <Switch v-model="settingStore.preventSleep" />
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M4 12h16"/><path d="M4 6h16"/><path d="M4 18h16"/></svg>
        </div>
        <h2 class="text-lg font-bold">播放音质</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">首选音质</h3>
            <p class="text-sm text-text-secondary">根据网络环境选择播放音质</p>
          </div>
          <Select
            class="min-w-[180px]"
            :model-value="settingStore.audioQuality"
            :options="audioQualityOptions"
            @update:model-value="settingStore.audioQuality = $event as AudioQuality"
          />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">智能兼容模式</h3>
            <p class="text-sm text-text-secondary">首选音质不可用时自动尝试备选</p>
          </div>
          <Switch v-model="settingStore.compatibilityMode" />
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M7 7h10v10H7z"/><path d="M3 12h4"/><path d="M17 12h4"/><path d="M12 3v4"/><path d="M12 17v4"/></svg>
        </div>
        <h2 class="text-lg font-bold">全局快捷键</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">启用全局快捷键</h3>
            <p class="text-sm text-text-secondary">允许应用在后台响应系统级快捷键</p>
          </div>
          <Switch v-model="settingStore.globalShortcutsEnabled" />
        </div>
        <div class="settings-divider"></div>
        <div v-for="item in shortcutItems" :key="item.command" class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">{{ item.title }}</h3>
            <p class="text-sm text-text-secondary">{{ item.desc }}</p>
          </div>
          <div class="flex items-center gap-2">
            <button class="shortcut-badge" @click="toggleShortcut(item.command)">
              {{ shortcutBindings[item.command] || settingStore.defaultShortcutLabels[item.command] }}
            </button>
            <button class="shortcut-reset" @click="clearShortcut(item.command)">恢复默认</button>
          </div>
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 3v18"/><path d="M5 12h14"/></svg>
        </div>
        <h2 class="text-lg font-bold">音频设备</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">输出设备</h3>
            <p class="text-sm text-text-secondary">选择音频播放输出设备</p>
          </div>
          <Select
            class="min-w-[180px]"
            :model-value="settingStore.outputDevice"
            :options="outputDeviceOptions"
            @update:model-value="settingStore.outputDevice = String($event)"
          />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">设备断开时暂停</h3>
            <p class="text-sm text-text-secondary">检测到输出设备断开时自动暂停播放</p>
          </div>
          <Switch v-model="settingStore.pauseOnDeviceChange" />
        </div>
        <div v-if="settingStore.outputDeviceType === 'wasapi'" class="settings-warning">
          该设备为 WASAPI 驱动，可能导致部分音效不可用
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 2l3 7h7l-5.5 4 2 7-6-4.5-6 4.5 2-7L2 9h7z"/></svg>
        </div>
        <h2 class="text-lg font-bold">实验性功能</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">自动领取 VIP</h3>
            <p class="text-sm text-text-secondary">每次启动自动领取每日 VIP (需要登录)</p>
          </div>
          <Switch v-model="settingStore.autoReceiveVip" />
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M12 2l8 4v6c0 5-3.5 9-8 10-4.5-1-8-5-8-10V6l8-4z"/></svg>
        </div>
        <h2 class="text-lg font-bold">数据与安全</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">查看运行日志</h3>
            <p class="text-sm text-text-secondary">打开本地日志目录以供排查问题</p>
          </div>
          <button class="settings-button" @click="settingStore.openLogDirectory()">立即查看</button>
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">清除应用数据</h3>
            <p class="text-sm text-text-secondary">移除所有持久化设置及缓存信息</p>
          </div>
          <button class="settings-button danger" @click="showConfirmClear = true">立即清除</button>
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><circle cx="12" cy="12" r="9"/><path d="M12 7v6"/><path d="M12 16h.01"/></svg>
        </div>
        <h2 class="text-lg font-bold">关于 EchoMusic</h2>
      </div>
      <div class="settings-card">
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">检查预发布版本</h3>
            <p class="text-sm text-text-secondary">开启后可收到 Alpha/Beta/RC 版本更新推送</p>
          </div>
          <Switch v-model="settingStore.checkPrerelease" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">当前版本</h3>
            <p class="text-sm text-text-secondary">Version v{{ versionLabel }} {{ releaseChannelLabel }}</p>
          </div>
          <button class="text-primary text-sm font-semibold" @click="settingStore.checkForUpdates()">检查更新</button>
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">项目源码</h3>
            <p class="text-sm text-text-secondary">开源共享于 GitHub</p>
          </div>
          <button class="text-text-secondary" @click="settingStore.openRepo()">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M7 7h10v10"/><path d="M7 17L17 7"/></svg>
          </button>
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">免责声明</h3>
            <p class="text-sm text-text-secondary">查看法律条款与免责声明</p>
          </div>
          <button class="text-text-secondary" @click="settingStore.openDisclaimer()">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
          </button>
        </div>
      </div>
    </section>

    <Dialog
      v-model:open="showConfirmClear"
      title="清除应用数据"
      description="此操作将移除所有持久化设置与缓存，无法撤销。"
    >
      <template #footer>
        <button class="settings-button" @click="showConfirmClear = false">取消</button>
        <button class="settings-button danger" @click="settingStore.clearAppData(); showConfirmClear = false;">确认清除</button>
      </template>
    </Dialog>
  </div>
</template>

<style scoped>
@reference "../style.css";

.settings-view {
  animation: fade-in 0.6s cubic-bezier(0.4, 0, 0.2, 1);
}

.settings-card {
  @apply bg-bg-sidebar rounded-2xl p-6 space-y-6 transition-colors duration-300 border border-border-light/40 overflow-visible;
}

.settings-item {
  @apply flex items-center justify-between gap-6;
}

.settings-divider {
  @apply h-px bg-border-light/40;
}

.settings-select {
  @apply bg-bg-main text-text-main border border-border-light rounded-lg px-3 py-1.5 text-sm font-semibold focus:outline-none min-w-[160px];
}


.shortcut-badge {
  @apply px-3 py-1.5 rounded-lg bg-bg-main border border-border-light text-[12px] font-semibold;
}

.shortcut-reset {
  @apply text-[11px] text-text-secondary hover:text-primary;
}

.settings-warning {
  @apply mt-3 text-[12px] text-amber-600 bg-amber-500/10 rounded-lg px-3 py-2;
}

.settings-button {
  @apply px-4 py-1.5 rounded-full bg-primary/10 text-primary text-[12px] font-semibold hover:bg-primary/20 transition-colors;
}

.settings-button.danger {
  @apply bg-red-500/10 text-red-500 hover:bg-red-500/20;
}


@keyframes fade-in {
  from { opacity: 0; transform: translateY(10px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
