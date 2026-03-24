export interface IElectronAPI {
  platform: string;
  ipcRenderer: {
    send: (channel: string, data: any) => void;
    on: (channel: string, func: (...args: any[]) => void) => void;
  };
  windowControl: (action: 'minimize' | 'maximize' | 'close') => void;
}

declare global {
  interface Window {
    electron: IElectronAPI;
  }
}
