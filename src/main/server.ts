import { ChildProcess, spawn, execSync } from 'child_process';
import { app } from 'electron';
import path from 'path';
import fs from 'fs';

let apiProcess: ChildProcess | null = null;
const isDev = !app.isPackaged;

/**
 * 启动 API 服务器
 */
export function startApiServer() {
  return new Promise<void>((resolve, reject) => {
    let apiPath = '';
    let cwd = '';
    let args: string[] = [];

    if (isDev) {
      // 开发环境：执行 npm run dev
      cwd = path.join(process.cwd(), 'server');
      const npmCmd = process.platform === 'win32' ? 'npm.cmd' : 'npm';
      
      console.log(`[Server] 开发环境：准备在 ${cwd} 执行 npm install...`);
      try {
        // 自动化执行 npm install
        execSync(`${npmCmd} install`, { cwd, stdio: 'inherit' });
        console.log('[Server] npm install 完成');
      } catch (e) {
        console.error('[Server] npm install 失败:', e);
      }

      console.log(`[Server] 开发环境：启动 npm 进程...`);
      apiPath = npmCmd;
      args = ['run', 'dev', '--', '--port=12306', '--platform=lite'];
    } else {
      // 生产环境：运行二进制文件
      cwd = path.join(process.resourcesPath, 'server');
      switch (process.platform) {
        case 'win32':
          apiPath = path.join(cwd, 'app_win.exe');
          break;
        case 'darwin':
          apiPath = path.join(cwd, 'app_macos');
          break;
        case 'linux':
          apiPath = path.join(cwd, 'app_linux');
          break;
        default:
          return reject(new Error(`不支持的平台: ${process.platform}`));
      }
      args = ['--port=12306', '--platform=lite', '--host=127.0.0.1'];
    }

    console.log(`[Server] API 路径: ${apiPath}, CWD: ${cwd}`);

    if (isDev) {
        // 开发环境直接启动 npm
        apiProcess = spawn(apiPath, args, { cwd, windowsHide: true });
    } else {
        if (!fs.existsSync(apiPath)) {
            const error = new Error(`API 可执行文件未找到：${apiPath}`);
            console.error(error.message);
            return reject(error);
        }
        apiProcess = spawn(apiPath, args, { cwd, windowsHide: true });
    }

    apiProcess.stdout?.on('data', (data) => {
      const output = data.toString();
      console.log(`[Server] API 输出: ${output}`);
      // 匹配关键字判断就绪状态
      if (output.includes('running') || output.includes('server started') || output.includes('localhost:12306') || output.includes('127.0.0.1:12306')) {
        console.log('[Server] API 服务器已就绪');
        resolve();
      }
    });

    apiProcess.stderr?.on('data', (data) => {
      const output = data.toString();
      // 某些库的日志会输出到 stderr，不代表一定报错
      console.warn(`[Server] API 潜在错误/日志: ${output}`);
    });

    apiProcess.on('close', (code) => {
      console.log(`[Server] API 进程关闭，退出码: ${code}`);
    });

    apiProcess.on('error', (error) => {
      console.error('[Server] 启动 API 失败:', error);
      reject(error);
    });

    // 15秒超时兜底 (开发环境下编译可能较慢)
    setTimeout(() => {
        reject(new Error('API 启动超时，请检查控制台日志'));
    }, 15000);
  });
}

/**
 * 停止 API 服务器
 */
export function stopApiServer() {
  if (apiProcess && apiProcess.pid) {
    try {
      // 检查进程是否还在运行 (信号 0 不杀进程，仅检查是否存在)
      process.kill(apiProcess.pid, 0);
      
      if (process.platform === 'win32') {
        const { exec } = require('child_process');
        exec(`taskkill /F /T /PID ${apiProcess.pid}`);
      } else {
        process.kill(-apiProcess.pid, 'SIGKILL');
      }
    } catch (e) {
      // 如果进程不存在 (ESRCH)，忽略错误
      console.warn(`[Server] 停止 API 失败或进程已不存在: ${e}`);
    } finally {
      apiProcess = null;
    }
  }
}
