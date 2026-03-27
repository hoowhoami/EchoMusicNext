import { contextBridge, ipcRenderer } from 'electron';
import log from 'electron-log/renderer';

contextBridge.exposeInMainWorld('electron', {
  platform: process.platform,
  ipcRenderer: {
    send: (channel: string, data: any) => ipcRenderer.send(channel, data),
    on: (channel: string, func: (...args: any[]) => void) =>
      ipcRenderer.on(channel, (event, ...args) => func(...args)),
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
