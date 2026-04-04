import { Menu, Tray, app, nativeImage } from 'electron';
import { join } from 'path';

type TrayCommand = 'togglePlayback' | 'previousTrack' | 'nextTrack' | 'togglePlayMode';
type PlayMode = 'list' | 'random' | 'single';

interface TrayContext {
  getMainWindow: () => Electron.BrowserWindow | null;
  restoreWindow: () => void;
}

interface TrayPlaybackState {
  isPlaying: boolean;
  playMode: PlayMode;
}

let appTray: Tray | null = null;
let trayContext: TrayContext | null = null;
let playbackState: TrayPlaybackState = {
  isPlaying: false,
  playMode: 'list',
};

const playModeLabelMap: Record<PlayMode, string> = {
  list: '列表循环',
  random: '随机播放',
  single: '单曲循环',
};

const resolveTrayIconPath = () => {
  const iconName = process.platform === 'darwin'
    ? 'mac_tray_icon_template.png'
    : process.platform === 'win32'
      ? 'icon.ico'
      : 'icon.png';

  if (app.isPackaged) {
    return join(process.resourcesPath, 'icons', iconName);
  }

  return join(process.cwd(), 'build/icons', iconName);
};

const createTrayImage = () => {
  const image = nativeImage.createFromPath(resolveTrayIconPath());
  if (process.platform === 'darwin') {
    image.setTemplateImage(true);
  }
  return image;
};

const forwardCommandToRenderer = (command: TrayCommand) => {
  const mainWindow = trayContext?.getMainWindow();
  if (!mainWindow) return;
  mainWindow.webContents.send('shortcut-trigger', command);
};

const rebuildTrayMenu = () => {
  if (!appTray) return;

  const contextMenu = Menu.buildFromTemplate([
    {
      label: playbackState.isPlaying ? '暂停' : '播放',
      click: () => forwardCommandToRenderer('togglePlayback'),
    },
    {
      label: '上一首',
      click: () => forwardCommandToRenderer('previousTrack'),
    },
    {
      label: '下一首',
      click: () => forwardCommandToRenderer('nextTrack'),
    },
    {
      label: `播放模式：${playModeLabelMap[playbackState.playMode]}`,
      click: () => forwardCommandToRenderer('togglePlayMode'),
    },
  ]);

  appTray.setToolTip('EchoMusic');
  appTray.setContextMenu(contextMenu);
};

export const initTray = (context: TrayContext) => {
  trayContext = context;
  if (appTray) {
    rebuildTrayMenu();
    return appTray;
  }

  appTray = new Tray(createTrayImage());
  appTray.on('click', () => {
    trayContext?.restoreWindow();
  });
  rebuildTrayMenu();
  return appTray;
};

export const destroyTray = () => {
  if (!appTray) return;
  appTray.destroy();
  appTray = null;
};

export const updateTrayPlaybackState = (nextState: Partial<TrayPlaybackState>) => {
  playbackState = {
    ...playbackState,
    ...nextState,
  };
  rebuildTrayMenu();
};
