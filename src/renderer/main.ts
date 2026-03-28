import { createApp } from 'vue';
import { createPinia } from 'pinia';
import piniaPluginPersistedstate from 'pinia-plugin-persistedstate';
import { Icon } from '@iconify/vue';
import App from './App.vue';
import router from './router';
import { logger } from '@/utils/logger';
import './style.css';

const app = createApp(App);
const pinia = createPinia();
pinia.use(piniaPluginPersistedstate);

// Vue 全局异常处理
app.config.errorHandler = (err: any, instance, info) => {
  logger.error('App', 'Global exception catch', err, instance, info);

  // 如果是路由正在切换中，或者是正常页面逻辑错误，跳转到错误页
  // 避免在错误页本身出错导致无限循环
  if (router.currentRoute.value.name !== 'error') {
    router.push({
      name: 'error',
      query: {
        message: err.message || String(err),
        status: 'App Error',
        from: router.currentRoute.value.fullPath,
      },
    });
  }
};

app.use(pinia);
app.use(router);
app.component('Icon', Icon);
app.mount('#app');
