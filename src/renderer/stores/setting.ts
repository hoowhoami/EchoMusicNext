import { defineStore } from 'pinia';

export const useSettingStore = defineStore('setting', {
  state: () => ({
    theme: 'system' as 'light' | 'dark' | 'system',
    language: 'zh-CN',
    shortcutEnabled: true,
    autoPlay: true,
  }),
  actions: {
    setTheme(theme: 'light' | 'dark' | 'system') {
      this.theme = theme;
    },
    toggleShortcuts(enabled: boolean) {
      this.shortcutEnabled = enabled;
    },
  },
  persist: true,
});
