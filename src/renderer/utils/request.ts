import axios from 'axios';
import { useUserStore } from '../stores/user';
import { useDeviceStore } from '../stores/device';

const isDev = process.env.NODE_ENV === 'development';
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
    }

    // 2. 注入设备信息
    if (deviceStore.info) {
      const device = deviceStore.info;
      if (device.dfid) authParts.push(`dfid=${device.dfid}`);
      if (device.mid) authParts.push(`mid=${device.mid}`);
      if (device.uuid) authParts.push(`uuid=${device.uuid}`);
    }
    
    if (authParts.length > 0) {
      config.headers['Authorization'] = authParts.join(';');
    }
    
    config.params = {
      ...config.params,
      t: Date.now(),
    };
    
    return config;
  },
  (error) => Promise.reject(error)
);

// 响应拦截器
request.interceptors.response.use(
  (response) => {
    const data = response.data;
    if (data.error_code === 20018 || (data.msg && data.msg.includes('登录已过期'))) {
      console.warn('[API] 登录已过期');
    }
    return data;
  },
  (error) => {
    console.error('[API] 请求失败', error);
    return Promise.reject(error);
  }
);

export default request;
