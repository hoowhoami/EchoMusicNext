import { BrowserWindow, shell, app, nativeTheme, ipcMain } from 'electron';
import Conf from 'conf';
import { join } from 'path';

type CloseBehavior = 'tray' | 'exit';
type ThemeMode = 'light' | 'dark' | 'system';

type AppSettings = {
  closeBehavior: CloseBehavior;
  theme: ThemeMode;
};

const settingsStore = new Conf<AppSettings>({
  projectName: app.getName(),
  defaults: {
    closeBehavior: 'tray',
    theme: 'system',
  },
});

let closeBehavior: CloseBehavior = settingsStore.get('closeBehavior', 'tray');
let currentTheme: ThemeMode = settingsStore.get('theme', 'system');

let win: BrowserWindow | null = null;
let isQuitting = false;

// 监听应用准备退出
app.on('before-quit', () => {
  isQuitting = true;
});

// 监听关闭行为更新
ipcMain.on('update-close-behavior', (_event, behavior: CloseBehavior) => {
  closeBehavior = behavior;
  settingsStore.set('closeBehavior', behavior);
});

// 监听主题更新，用于同步主进程背景色判断
ipcMain.on('update-theme', (_event, theme: ThemeMode) => {
  currentTheme = theme;
  settingsStore.set('theme', theme);
});

export function getMainWindow() {
  return win;
}

export async function createWindow() {
  const preload = join(__dirname, '../preload/index.js');
  const url = process.env.VITE_DEV_SERVER_URL;
  const indexHtml = join(__dirname, '../../dist/index.html');

  // 根据设置或系统偏好决定初始背景色
  let initialBgColor = '#ffffff';
  if (currentTheme === 'dark') {
    initialBgColor = '#1a1a1c';
  } else if (currentTheme === 'light') {
    initialBgColor = '#ffffff';
  } else {
    // 跟随系统
    initialBgColor = nativeTheme.shouldUseDarkColors ? '#1a1a1c' : '#ffffff';
  }

  win = new BrowserWindow({
    title: 'EchoMusic',
    icon: join(__dirname, '../../public/favicon.ico'),
    width: 1100,
    height: 750,
    minWidth: 1050,
    minHeight: 700,
    show: false, // 初始不显示，防止白屏
    backgroundColor: initialBgColor, // 动态设置背景色
    frame: false,
    transparent: false,
    hasShadow: false,
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

  // 当窗口准备好显示时再展示，优雅解决启动白屏
  win.once('ready-to-show', () => {
    win?.show();
  });

  win.webContents.setFrameRate(60); // 锁定60帧，滚动更稳

  if (url) {
    win.loadURL(url);
  } else {
    win.loadFile(indexHtml);
  }

  win.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('https:')) shell.openExternal(url);
    return { action: 'deny' };
  });

  // 拦截关闭事件
  win.on('close', (event) => {
    if (isQuitting) return;

    if (closeBehavior === 'tray') {
      event.preventDefault();
      win?.hide();
    }
  });

  win.on('closed', () => {
    win = null;
  });

  return win;
}

export function restoreWindow() {
  const mainWindow = getMainWindow();
  if (!mainWindow) return;

  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }
  mainWindow.show();
  mainWindow.focus();
}
