/// <reference path="../electron.d.ts" />

/**
 * 接入 electron-log 的日志工具，支持开发环境回退到 console
 */

interface LoggerFunctions {
  info: (...args: any[]) => void;
  warn: (...args: any[]) => void;
  error: (...args: any[]) => void;
  debug: (...args: any[]) => void;
  verbose: (...args: any[]) => void;
}

const isDev = import.meta.env.DEV;

// 确保在渲染进程中使用，window 应该是可用的
const getElectronLog = (): LoggerFunctions | undefined => {
  try {
    // 使用 any 绕过可能的类型合并延迟
    const electron = (window as any).electron;
    return electron?.log;
  } catch (e) {
    return undefined;
  }
};

const electronLog = getElectronLog();

// 创建一个符合 Logger 接口的对象
export const logger = {
  info: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.info(...args);
    console.info('[INFO]', ...args);
  },
  warn: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.warn(...args);
    console.warn('[WARN]', ...args);
  },
  error: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.error(...args);
    console.error('[ERROR]', ...args);
  },
  debug: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.debug(...args);
    if (isDev) console.debug('[DEBUG]', ...args);
  },
  verbose: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.verbose(...args);
    if (isDev) console.log('[VERBOSE]', ...args);
  },
  // 保持 log 兼容性，映射到 info
  log: (...args: any[]) => {
    if (electronLog && !isDev) electronLog.info(...args);
    console.log('[LOG]', ...args);
  }
};

export default logger;
