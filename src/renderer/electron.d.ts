export interface IElectronAPI {
  platform: string;
  ipcRenderer: {
    send: (channel: string, data: any) => void;
    on: (channel: string, func: (...args: any[]) => void) => void;
  };
  windowControl: (action: 'minimize' | 'maximize' | 'close') => void;
  apiServer: {
    start: () => Promise<{ success: boolean; error?: string }>;
    stop: () => void;
  };
  log: {
    info: (...args: any[]) => void;
    warn: (...args: any[]) => void;
    error: (...args: any[]) => void;
    debug: (...args: any[]) => void;
    verbose: (...args: any[]) => void;
  };
}

declare global {
  interface Window {
    electron: IElectronAPI;
  }
}
