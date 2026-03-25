import { BrowserWindow, ipcMain } from 'electron';
import { registerApiServerHandlers } from './server';
import { registerWindowHandlers } from './window';
import { registerSettingsHandlers } from './settings';

interface IpcContext {
  getMainWindow: () => BrowserWindow | null;
}

let registered = false;

export const registerIpcHandlers = (context: IpcContext) => {
  if (registered) return;
  registerWindowHandlers(context);
  registerApiServerHandlers();
  registerSettingsHandlers(context);
  registered = true;
};

export const unregisterIpcHandlers = () => {
  ipcMain.removeAllListeners('window-control');
  ipcMain.removeAllListeners('api-server:stop');
  ipcMain.removeAllListeners('open-log-directory');
  ipcMain.removeAllListeners('check-for-updates');
  ipcMain.removeAllListeners('open-external');
  ipcMain.removeAllListeners('open-disclaimer');
  ipcMain.removeHandler('api-server:start');
};
