import { defineStore } from 'pinia';

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
    audioQuality: 'high' as '128' | '320' | 'flac' | 'high',
    compatibilityMode: true,
    audioEffect: 'none' as 'none' | 'piano' | 'acappella' | 'subwoofer' | 'ancient' | 'surnay' | 'dj' | 'viper_tape' | 'viper_atmos' | 'viper_clear',
    globalShortcutsEnabled: false,
    shortcutBindings: {} as Record<string, string>,
    globalShortcutBindings: {} as Record<string, string>,
    defaultShortcutLabels: { ...DEFAULT_SHORTCUT_LABELS } as Record<string, string>,
    defaultGlobalShortcutLabels: { ...DEFAULT_GLOBAL_SHORTCUT_LABELS } as Record<string, string>,
    outputDevice: '自动',
    outputDevices: ['自动', '系统默认'] as string[],
    outputDeviceType: 'default' as 'default' | 'wasapi',
    pauseOnDeviceChange: false,
    autoReceiveVip: false,
    checkPrerelease: false,
    appVersion: '1.0.0',
    isPrerelease: true,
    searchHistory: [] as string[],
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
      this.$reset();
    },
    checkForUpdates() {
      if (window.electron?.ipcRenderer) {
        window.electron.ipcRenderer.send('check-for-updates', null);
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
  },
  persist: true,
});
