<script setup lang="ts">
import { computed, ref, onMounted, onUnmounted } from 'vue';
import { useSettingStore } from '@/stores/setting';
import Select from '@/components/ui/Select.vue';
import Slider from '@/components/ui/Slider.vue';
import Switch from '@/components/ui/Switch.vue';
import Dialog from '@/components/ui/Dialog.vue';
import {
  iconPlus,
  iconMinus,
  iconList,
  iconGrid,
  iconChevronUpDown,
  iconStar,
  iconShield,
  iconInfo,
  iconExternalLink,
  iconChevronRight,
} from '@/icons';

const settingStore = useSettingStore();

onMounted(() => {
  settingStore.syncCloseBehavior();
  settingStore.syncTheme();
});

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

type ShortcutScope = 'local' | 'global';

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
const globalShortcutBindings = computed(() => settingStore.globalShortcutBindings ?? {});
const isMac = computed(() => window.electron?.platform === 'darwin');
const recording = ref<{ scope: ShortcutScope; command: ShortcutCommand } | null>(null);
let removeRecorder: (() => void) | null = null;
let removeOutside: (() => void) | null = null;

const isRecording = (command: ShortcutCommand, scope: ShortcutScope) =>
  recording.value?.command === command && recording.value?.scope === scope;

const formatMainKey = (key: string, mac: boolean) => {
  if (key === ' ' || key === 'Spacebar') return 'Space';
  if (key === 'ArrowLeft') return mac ? '←' : 'Left';
  if (key === 'ArrowRight') return mac ? '→' : 'Right';
  if (key === 'ArrowUp') return mac ? '↑' : 'Up';
  if (key === 'ArrowDown') return mac ? '↓' : 'Down';
  if (key.length === 1) return key.toUpperCase();
  return key;
};

const buildShortcutLabel = (event: KeyboardEvent) => {
  const key = event.key;
  if (['Shift', 'Control', 'Alt', 'Meta'].includes(key)) return '';
  const mainKey = formatMainKey(key, isMac.value);
  if (!mainKey) return '';
  if (isMac.value) {
    const parts = [
      event.metaKey ? '⌘' : '',
      event.shiftKey ? '⇧' : '',
      event.altKey ? '⌥' : '',
      event.ctrlKey ? '⌃' : '',
      mainKey,
    ].filter(Boolean);
    return parts.join('');
  }
  const parts = [
    event.ctrlKey ? 'Ctrl' : '',
    event.shiftKey ? 'Shift' : '',
    event.altKey ? 'Alt' : '',
    event.metaKey ? 'Meta' : '',
    mainKey,
  ].filter(Boolean);
  return parts.join('+');
};

const resolveLabel = (
  binding: Record<string, string>,
  defaults: Record<string, string>,
  command: ShortcutCommand
) => {
  if (Object.prototype.hasOwnProperty.call(binding, command)) {
    return binding[command] ?? '';
  }
  return defaults[command] ?? '';
};

const getShortcutValue = (command: ShortcutCommand, scope: ShortcutScope) => {
  if (isRecording(command, scope)) return '';
  if (scope === 'global') {
    return resolveLabel(globalShortcutBindings.value, settingStore.defaultGlobalShortcutLabels, command);
  }
  return resolveLabel(shortcutBindings.value, settingStore.defaultShortcutLabels, command);
};

const getShortcutPlaceholder = (command: ShortcutCommand, scope: ShortcutScope) => {
  if (isRecording(command, scope)) return '按键盘输入快捷键';
  if (scope === 'global' && !settingStore.globalShortcutsEnabled) return '开启后可录制';
  return '点击录制';
};

const stopRecording = () => {
  recording.value = null;
  removeRecorder?.();
  removeRecorder = null;
  removeOutside?.();
  removeOutside = null;
};

const startRecording = (command: ShortcutCommand, scope: ShortcutScope) => {
  if (scope === 'global' && !settingStore.globalShortcutsEnabled) {
    return;
  }
  if (isRecording(command, scope)) return;
  if (recording.value && !isRecording(command, scope)) {
    stopRecording();
  }
  recording.value = { command, scope };
  removeRecorder?.();
  const handler = (event: KeyboardEvent) => {
    if (!recording.value) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    if (event.key === 'Backspace' || event.key === 'Delete') {
      clearShortcut(recording.value.command, recording.value.scope);
      stopRecording();
      return;
    }
    const label = buildShortcutLabel(event);
    if (!label) return;
    const hasModifier = event.metaKey || event.ctrlKey || event.altKey || event.shiftKey;
    if (!hasModifier) return;
    if (recording.value.scope === 'global') {
      settingStore.globalShortcutBindings = {
        ...globalShortcutBindings.value,
        [recording.value.command]: label,
      };
    } else {
      settingStore.shortcutBindings = {
        ...shortcutBindings.value,
        [recording.value.command]: label,
      };
    }
    stopRecording();
  };
  window.addEventListener('keydown', handler, true);
  removeRecorder = () => window.removeEventListener('keydown', handler, true);

  const outsideHandler = (event: MouseEvent) => {
    if (!recording.value) return;
    const target = event.target as HTMLElement | null;
    if (target?.closest('.shortcut-input')) return;
    stopRecording();
  };
  window.addEventListener('mousedown', outsideHandler, true);
  removeOutside = () => window.removeEventListener('mousedown', outsideHandler, true);
};

const clearShortcut = (command: ShortcutCommand, scope: ShortcutScope) => {
  if (scope === 'global') {
    const next = { ...globalShortcutBindings.value, [command]: '' };
    settingStore.globalShortcutBindings = next;
  } else {
    const next = { ...shortcutBindings.value, [command]: '' };
    settingStore.shortcutBindings = next;
  }
  if (isRecording(command, scope)) stopRecording();
};

const resetAllShortcuts = () => {
  settingStore.resetShortcutDefaults();
  settingStore.shortcutBindings = { ...settingStore.defaultShortcutLabels };
  settingStore.globalShortcutBindings = { ...settingStore.defaultGlobalShortcutLabels };
  stopRecording();
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

onUnmounted(() => {
  stopRecording();
});
</script>

<template>
  <div class="settings-view p-8 space-y-12 max-w-4xl transition-colors duration-300">
    <header>
      <h1 class="text-3xl font-black tracking-tight text-text-main">偏好设置</h1>
    </header>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <Icon :icon="iconPlus" width="18" height="18" />
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
            @update:model-value="settingStore.closeBehavior = $event as CloseBehavior; settingStore.syncCloseBehavior()"
          />
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <Icon :icon="iconMinus" width="18" height="18" />
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
          <Icon :icon="iconList" width="18" height="18" />
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
          <Icon :icon="iconGrid" width="18" height="18" />
        </div>
        <h2 class="text-lg font-bold">快捷键设置</h2>
      </div>
      <div class="settings-card">
        <div class="shortcut-grid-header">
          <div>功能说明</div>
          <div class="shortcut-col-title">快捷键</div>
          <div class="shortcut-col-title">全局快捷键</div>
        </div>
        <div class="settings-divider"></div>
        <div class="shortcut-list">
          <div v-for="item in shortcutItems" :key="item.command" class="shortcut-grid-row">
            <div class="space-y-1">
              <h3 class="font-semibold">{{ item.title }}</h3>
              <p class="text-sm text-text-secondary">{{ item.desc }}</p>
            </div>
            <div class="shortcut-cell shortcut-cell-offset">
              <input
                class="shortcut-input"
                :class="{ recording: isRecording(item.command, 'local') }"
                :value="getShortcutValue(item.command, 'local')"
                :placeholder="getShortcutPlaceholder(item.command, 'local')"
                readonly
                @click="startRecording(item.command, 'local')"
                @focus="startRecording(item.command, 'local')"
              />
            </div>
            <div class="shortcut-cell shortcut-cell-offset">
              <input
                class="shortcut-input"
                :class="{
                  recording: isRecording(item.command, 'global'),
                  'shortcut-input-disabled': !settingStore.globalShortcutsEnabled,
                }"
                :value="getShortcutValue(item.command, 'global')"
                :placeholder="getShortcutPlaceholder(item.command, 'global')"
                :disabled="!settingStore.globalShortcutsEnabled"
                readonly
                @click="startRecording(item.command, 'global')"
                @focus="startRecording(item.command, 'global')"
              />
            </div>
          </div>
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">启用全局快捷键</h3>
            <p class="text-sm text-text-secondary">允许应用在后台响应系统级快捷键</p>
          </div>
          <Switch v-model="settingStore.globalShortcutsEnabled" />
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">恢复默认</h3>
            <p class="text-sm text-text-secondary">恢复所有快捷键为默认</p>
          </div>
          <button class="settings-button" @click="resetAllShortcuts">恢复默认</button>
        </div>
      </div>
    </section>

    <section class="space-y-6">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
          <Icon :icon="iconChevronUpDown" width="18" height="18" />
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
          <Icon :icon="iconStar" width="18" height="18" />
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
          <Icon :icon="iconShield" width="18" height="18" />
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
          <Icon :icon="iconInfo" width="18" height="18" />
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
            <Icon :icon="iconExternalLink" width="18" height="18" />
          </button>
        </div>
        <div class="settings-divider"></div>
        <div class="settings-item">
          <div class="space-y-1">
            <h3 class="font-semibold">免责声明</h3>
            <p class="text-sm text-text-secondary">查看法律条款与免责声明</p>
          </div>
          <button class="text-text-secondary" @click="settingStore.openDisclaimer()">
            <Icon :icon="iconChevronRight" width="16" height="16" />
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
@reference "@/style.css";

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


.shortcut-input {
  @apply w-full max-w-[220px] px-3 py-2 rounded-lg bg-bg-main border border-border-light text-[12px] font-semibold text-left;
  outline: none;
}


.shortcut-input.recording {
  border-color: #0071e3;
  color: #0071e3;
  background-color: rgba(0, 113, 227, 0.12);
  box-shadow: 0 0 0 2px rgba(0, 113, 227, 0.25);
}

.dark .shortcut-input.recording {
  border-color: #4aa3ff;
  color: #8cc6ff;
  background-color: rgba(0, 113, 227, 0.24);
  box-shadow: 0 0 0 2px rgba(0, 113, 227, 0.4);
}

.shortcut-input:focus,
.shortcut-input:focus-visible {
  border-color: #0071e3;
  box-shadow: 0 0 0 2px rgba(0, 113, 227, 0.25);
}

.dark .shortcut-input:focus,
.dark .shortcut-input:focus-visible {
  border-color: #4aa3ff;
  box-shadow: 0 0 0 2px rgba(0, 113, 227, 0.4);
}

.shortcut-input::placeholder {
  color: rgba(0, 0, 0, 0.45);
}

.dark .shortcut-input::placeholder {
  color: rgba(255, 255, 255, 0.5);
}

.shortcut-input-disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.shortcut-grid-header,
.shortcut-grid-row,
.shortcut-grid-footer {
  @apply grid items-center gap-4;
  grid-template-columns: minmax(200px, 1fr) minmax(180px, 240px) minmax(180px, 240px);
}

.shortcut-grid-header {
  @apply text-[12px] text-text-secondary font-semibold tracking-wide text-left;
}

.shortcut-col-title {
  @apply text-left pl-10;
}

.shortcut-grid-row {
  @apply py-1.5;
}

.shortcut-cell {
  @apply flex items-center justify-start;
}

.shortcut-cell-offset {
  @apply pl-10;
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
