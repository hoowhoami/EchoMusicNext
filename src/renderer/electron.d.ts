export interface IElectronAPI {
  platform: string;
  ipcRenderer: {
    send: (channel: string, data: unknown) => void;
    on: (channel: string, func: (...args: unknown[]) => void) => void;
  };
  shortcuts: {
    register: (payload: { enabled: boolean; shortcutMap: Record<string, string> }) => void;
    onTrigger: (func: (command: string) => void) => () => void;
  };
  windowControl: (action: 'minimize' | 'maximize' | 'close') => void;
  apiServer: {
    start: () => Promise<{ success: boolean; error?: string }>;
    stop: () => void;
  };
  log: {
    info: (...args: unknown[]) => void;
    warn: (...args: unknown[]) => void;
    error: (...args: unknown[]) => void;
    debug: (...args: unknown[]) => void;
    verbose: (...args: unknown[]) => void;
  };
}

declare global {
  interface Window {
    electron: IElectronAPI;
  }
}

// 确保这是一个模块
export {};
