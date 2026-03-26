import axios from 'axios';
import { useUserStore } from '@/stores/user';
import { useDeviceStore } from '@/stores/device';
import { logger } from './logger';

const isDev = import.meta.env.DEV;
const API_BASE_URL = isDev ? '/api' : 'http://127.0.0.1:12306';

const request = axios.create({
  baseURL: API_BASE_URL,
  timeout: 10000,
});

// 请求拦截器
request.interceptors.request.use(
  (config) => {
    const authParts: string[] = [];
    
    // 从 Pinia Store 中获取信息
    const userStore = useUserStore();
    const deviceStore = useDeviceStore();

    // 1. 注入用户信息
    if (userStore.info) {
      if (userStore.info.token) authParts.push(`token=${userStore.info.token}`);
      if (userStore.info.userid) authParts.push(`userid=${userStore.info.userid}`);
      if (userStore.info.t1) authParts.push(`t1=${userStore.info.t1}`);
    }

    // 2. 注入设备信息
    if (deviceStore.info) {
      const device = deviceStore.info;
      if (device.dfid) authParts.push(`dfid=${device.dfid}`);
      if (device.mid) authParts.push(`KUGOU_API_MID=${device.mid}`);
      if (device.uuid) authParts.push(`uuid=${device.uuid}`);
      if (device.guid) authParts.push(`KUGOU_API_GUID=${device.guid}`);
      if (device.serverDev) authParts.push(`KUGOU_API_DEV=${device.serverDev}`);
      if (device.mac) authParts.push(`KUGOU_API_MAC=${device.mac}`);
    }
    
    const auth = authParts.join(';');
    if (authParts.length > 0) {
      config.headers['Authorization'] = auth;
    }
    
    config.params = {
      ...config.params,
      t: Date.now(),
    };

    // 打印请求日志
    const fullUrl = axios.getUri(config);
    logger.info('API', `Request: [${config.method?.toUpperCase()}] ${fullUrl}`, auth ? `Auth: ${auth}` : '');
    
    return config;
  },
  (error) => {
    logger.error('API', 'Request Error:', error);
    return Promise.reject(error);
  }
);

// 响应拦截器
request.interceptors.response.use(
  (response) => {
    const data = response.data;
    
    // 打印响应日志
    let resStr = typeof data === 'object' ? JSON.stringify(data) : String(data);
    if (resStr.length > 2000) {
      resStr = resStr.substring(0, 2000) + '... (truncated)';
    }
    logger.info('API', `Response: ${response.config.url} ->`, resStr);

    if (data.error_code === 20018 || (data.msg && data.msg.includes('登录已过期'))) {
      logger.warn('API', '登录已过期');
    }
    return data;
  },
  (error) => {
    logger.error('API', `Response Error: ${error.config?.url} ->`, error.message);
    return Promise.reject(error);
  }
);

export default request;
