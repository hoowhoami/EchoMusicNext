import { contextBridge, ipcRenderer } from 'electron';
import log from 'electron-log/renderer';

const ipcListenerMap = new Map<string, WeakMap<(...args: any[]) => void, (...args: any[]) => void>>();

const getWrappedListener = (channel: string, func: (...args: any[]) => void) => {
  let channelMap = ipcListenerMap.get(channel);
  if (!channelMap) {
    channelMap = new WeakMap();
    ipcListenerMap.set(channel, channelMap);
  }

  const existing = channelMap.get(func);
  if (existing) return existing;

  const wrapped = (_event: Electron.IpcRendererEvent, ...args: any[]) => func(...args);
  channelMap.set(func, wrapped);
  return wrapped;
};

contextBridge.exposeInMainWorld('electron', {
  platform: process.platform,
  ipcRenderer: {
    send: (channel: string, data: any) => ipcRenderer.send(channel, data),
    on: (channel: string, func: (...args: any[]) => void) => {
      const wrapped = getWrappedListener(channel, func);
      ipcRenderer.on(channel, wrapped);
    },
    off: (channel: string, func: (...args: any[]) => void) => {
      const wrapped = ipcListenerMap.get(channel)?.get(func);
      if (wrapped) {
        ipcRenderer.removeListener(channel, wrapped);
      }
    },
  },
  shortcuts: {
    register: (payload: { enabled: boolean; shortcutMap: Record<string, string> }) =>
      ipcRenderer.send('shortcuts:register', payload),
    onTrigger: (func: (command: string) => void) => {
      const listener = (_event: Electron.IpcRendererEvent, command: string) => func(command);
      ipcRenderer.on('shortcut-trigger', listener);
      return () => ipcRenderer.removeListener('shortcut-trigger', listener);
    },
  },
  windowControl: (action: 'minimize' | 'maximize' | 'close') =>
    ipcRenderer.send('window-control', action),
  apiServer: {
    start: () => ipcRenderer.invoke('api-server:start'),
    stop: () => ipcRenderer.send('api-server:stop'),
  },
  log: log.functions
});
