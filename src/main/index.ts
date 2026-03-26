import { app, BrowserWindow } from 'electron';
import { release } from 'os';
import { initLogger } from './logger';
import { startApiServer, stopApiServer } from './server';
import { registerIpcHandlers } from './ipc';
import { createWindow, getMainWindow, restoreWindow } from './window';

// --- 初始化日志 ---
initLogger();

app.commandLine.appendSwitch('enable-smooth-scrolling'); // 平滑滚动
app.commandLine.appendSwitch('ignore-gpu-blacklist'); // 强制启用GPU
app.commandLine.appendSwitch('enable-hardware-overlays'); // 硬件叠加层（滚动神器）
app.commandLine.appendSwitch('enable-features', 'VaapiVideoDecoder'); // 硬件解码加速
app.commandLine.appendSwitch('disable-frame-rate-limit'); // 解锁帧率上限

if (process.platform === 'win32') app.setAppUserModelId(app.getName());

// --- 保持应用单例运行 ---
const gotTheLock = app.requestSingleInstanceLock();
if (!gotTheLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    restoreWindow();
  });
}

// 注册 IPC 处理器
registerIpcHandlers({
  getMainWindow,
});

app.whenReady().then(async () => {
  try {
    // 启动 API 服务
    await startApiServer();
  } catch (err) {
    console.error('[Main] Failed to start API server:', err);
  }
  // 创建主窗口
  createWindow();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  const mainWindow = getMainWindow();

  if (mainWindow) {
    if (mainWindow.isMinimized()) {
      mainWindow.restore();
    }
    mainWindow.show();
    mainWindow.focus();
  } else {
    createWindow();
  }
});

app.on('before-quit', () => {
  stopApiServer();
});
