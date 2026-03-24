import { app, BrowserWindow, shell, ipcMain } from 'electron';
import { release } from 'os';
import { join } from 'path';
import { startApiServer, stopApiServer } from './server';

if (release().startsWith('6.1')) app.disableHardwareAcceleration();
if (process.platform === 'win32') app.setAppUserModelId(app.getName());

// --- 保持应用单例运行 ---
const gotTheLock = app.requestSingleInstanceLock();
if (!gotTheLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (win) {
      if (win.isMinimized()) win.restore();
      win.focus();
    }
  });
}

let win: BrowserWindow | null = null;
// 根据您的构建输出确认是 .js 还是 .cjs
const preload = join(__dirname, '../preload/index.js');
const url = process.env.VITE_DEV_SERVER_URL;
const indexHtml = join(__dirname, '../../dist/index.html');

async function createWindow() {
  win = new BrowserWindow({
    title: 'EchoMusic',
    icon: join(__dirname, '../../public/favicon.ico'),
    width: 1100,
    height: 750,
    minWidth: 1050,
    minHeight: 700,
    frame: false,
    titleBarStyle: 'hidden',
    trafficLightPosition: { x: 18, y: 18 },
    webPreferences: {
      preload,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      webSecurity: false, // 禁用 CORS 限制
      allowRunningInsecureContent: true, // 允许混合内容
      zoomFactor: 1.0,
    },
  });

  // 窗口控制 IPC
  ipcMain.on('window-control', (event, action: 'minimize' | 'maximize' | 'close') => {
    const browserWindow = BrowserWindow.fromWebContents(event.sender);
    if (!browserWindow) return;
    if (action === 'minimize') browserWindow.minimize();
    else if (action === 'maximize') {
      if (browserWindow.isMaximized()) browserWindow.unmaximize();
      else browserWindow.maximize();
    } else if (action === 'close') browserWindow.close();
  });

  // API 服务器控制 (由 Loading.vue 调用)
  ipcMain.handle('api-server:start', async () => {
    try {
      await startApiServer();
      return { success: true };
    } catch (error) {
      return { success: false, error: String(error) };
    }
  });

  ipcMain.on('api-server:stop', () => {
    stopApiServer();
  });

  if (url) {
    win.loadURL(url);
  } else {
    win.loadFile(indexHtml);
  }

  win.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('https:')) shell.openExternal(url);
    return { action: 'deny' };
  });
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  win = null;
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  const allWindows = BrowserWindow.getAllWindows();
  if (allWindows.length) allWindows[0].focus();
  else createWindow();
});

app.on('before-quit', () => {
  stopApiServer();
});
