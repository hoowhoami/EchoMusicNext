import { contextBridge, ipcRenderer } from 'electron';
import log from 'electron-log/renderer';

contextBridge.exposeInMainWorld('electron', {
  platform: process.platform,
  ipcRenderer: {
    send: (channel: string, data: any) => ipcRenderer.send(channel, data),
    on: (channel: string, func: (...args: any[]) => void) =>
      ipcRenderer.on(channel, (event, ...args) => func(...args)),
  },
  windowControl: (action: 'minimize' | 'maximize' | 'close') =>
    ipcRenderer.send('window-control', action),
  apiServer: {
    start: () => ipcRenderer.invoke('api-server:start'),
    stop: () => ipcRenderer.send('api-server:stop'),
  },
  log: log.functions
});
