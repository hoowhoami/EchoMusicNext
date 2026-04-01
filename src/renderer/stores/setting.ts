import { defineStore } from 'pinia';

type OutputDeviceOption = {
  label: string;
  value: string;
};

type OutputDeviceStatus = 'idle' | 'ready' | 'unsupported' | 'permission' | 'fallback' | 'error';

export const DEFAULT_SHORTCUT_LABELS: Record<string, string> = {
  togglePlayback: '⌘Space',
  previousTrack: '⌘←',
  nextTrack: '⌘→',
  volumeUp: '⌘↑',
  volumeDown: '⌘↓',
  toggleMute: '⌘M',
  toggleFavorite: '⌘L',
  togglePlayMode: '⌘P',
  toggleWindow: '⌘W',
};

export const DEFAULT_GLOBAL_SHORTCUT_LABELS: Record<string, string> = {
  togglePlayback: '⌘⇧Space',
  previousTrack: '⌘⇧←',
  nextTrack: '⌘⇧→',
  volumeUp: '⌘⇧↑',
  volumeDown: '⌘⇧↓',
  toggleMute: '⌘⇧M',
  toggleFavorite: '⌘⇧L',
  togglePlayMode: '⌘⇧P',
  toggleWindow: '⌘⇧W',
};

export const useSettingStore = defineStore('setting', {
  state: () => ({
    theme: 'system' as 'light' | 'dark' | 'system',
    language: 'zh-CN',
    shortcutEnabled: true,
    autoPlay: true,
    rememberWindowSize: true,
    showPlaylistCount: true,
    closeBehavior: 'tray' as 'tray' | 'exit',
    replacePlaylist: false,
    volumeFade: true,
    volumeFadeTime: 1000,
    autoNext: false,
    preventSleep: true,
    defaultAudioQuality: 'high' as '128' | '320' | 'flac' | 'high',
    compatibilityMode: true,
    globalShortcutsEnabled: false,
    shortcutBindings: {} as Record<string, string>,
    globalShortcutBindings: {} as Record<string, string>,
    defaultShortcutLabels: { ...DEFAULT_SHORTCUT_LABELS } as Record<string, string>,
    defaultGlobalShortcutLabels: { ...DEFAULT_GLOBAL_SHORTCUT_LABELS } as Record<string, string>,
    outputDevice: 'default',
    outputDevices: [
      { label: '系统默认', value: 'default' },
    ] as OutputDeviceOption[],
    outputDeviceType: 'default' as 'default' | 'wasapi',
    outputDeviceStatus: 'idle' as OutputDeviceStatus,
    outputDeviceStatusMessage: '',
    pauseOnDeviceChange: false,
    autoReceiveVip: false,
    checkPrerelease: false,
    appVersion: '1.0.0',
    isPrerelease: true,
    searchHistory: [] as string[],
    userAgreementAccepted: false,
  }),
  actions: {
    setTheme(theme: 'light' | 'dark' | 'system') {
      this.theme = theme;
      this.syncTheme();
    },
    toggleShortcuts(enabled: boolean) {
      this.shortcutEnabled = enabled;
    },
    resetShortcutDefaults() {
      this.defaultShortcutLabels = { ...DEFAULT_SHORTCUT_LABELS };
      this.defaultGlobalShortcutLabels = { ...DEFAULT_GLOBAL_SHORTCUT_LABELS };
    },
    openLogDirectory() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('open-log-directory', null);
      }
    },
    clearAppData() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('clear-app-data', null);
      }
      localStorage.clear();
      sessionStorage.clear();
      this.$reset();
      window.setTimeout(() => {
        window.location.reload();
      }, 80);
    },
    checkForUpdates() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('check-for-updates', {
          prerelease: this.checkPrerelease,
        });
      }
    },
    openRepo() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('open-external', 'https://github.com/hoowhoami/EchoMusic');
      }
    },
    openDisclaimer() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('open-disclaimer', null);
      }
    },
    syncCloseBehavior() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('update-close-behavior', this.closeBehavior);
      }
    },
    syncTheme() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('update-theme', this.theme);
      }
    },
    syncRememberWindowSize() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('update-remember-window-size', this.rememberWindowSize);
      }
    },
    syncPreventSleep(isPlaying = false) {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('update-power-save-blocker', {
          enabled: this.preventSleep,
          isPlaying,
        });
      }
    },
    setOutputDeviceStatus(status: OutputDeviceStatus, message = '') {
      this.outputDeviceStatus = status;
      this.outputDeviceStatusMessage = message;
    },
    addToSearchHistory(keyword: string) {
      const normalized = keyword.trim();
      if (!normalized) return;
      this.searchHistory = [normalized, ...this.searchHistory.filter((item) => item !== normalized)].slice(0, 20);
    },
    removeFromSearchHistory(keyword: string) {
      this.searchHistory = this.searchHistory.filter((item) => item !== keyword);
    },
    clearSearchHistory() {
      this.searchHistory = [];
    },
    acceptUserAgreement() {
      this.userAgreementAccepted = true;
    },
  },
  persist: true,
});
