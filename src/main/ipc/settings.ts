import { BrowserWindow, ipcMain, shell } from 'electron';
import log from 'electron-log';
import { dirname } from 'path';

interface IpcContext {
  getMainWindow: () => BrowserWindow | null;
}

const openLogDirectory = async () => {
  const logFile = log.transports.file.getFile();
  const logDir = logFile?.path ? dirname(logFile.path) : '';
  if (!logDir) return;
  await shell.openPath(logDir);
};

export const registerSettingsHandlers = ({ getMainWindow }: IpcContext) => {
  ipcMain.on('open-log-directory', async () => {
    await openLogDirectory();
  });

  ipcMain.on('check-for-updates', () => {
    const win = getMainWindow();
    if (!win) return;
    win.webContents.send('update-check', { success: true });
  });

  ipcMain.on('open-external', async (_event, url: string) => {
    if (typeof url !== 'string' || !url.startsWith('http')) return;
    await shell.openExternal(url);
  });

  ipcMain.on('open-disclaimer', () => {
    const win = getMainWindow();
    if (!win) return;
    win.webContents.send('open-disclaimer');
  });
};
