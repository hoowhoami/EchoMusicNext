/**
 * 接入 electron-log 的日志工具
 */

export const logger = window.electron.log;

// 为 console 挂载相同的函数
Object.assign(console, logger);

export default logger;
