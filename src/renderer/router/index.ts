import { createRouter, createWebHashHistory, type RouteRecordRaw } from 'vue-router';

const routes: RouteRecordRaw[] = [
  {
    path: '/',
    name: 'loading',
    component: () => import('../views/Loading.vue'),
  },
  {
    path: '/login',
    name: 'login',
    component: () => import('../views/Login.vue'),
  },
  {
    path: '/playing',
    name: 'playing',
    component: () => import('../views/Playing.vue'),
  },
  {
    path: '/main',
    component: () => import('../layouts/MainLayout.vue'),
    children: [
      {
        path: 'home',
        name: 'home',
        component: () => import('../views/Home.vue'),
      },
      {
        path: 'collection',
        name: 'collection',
        component: () => import('../views/Collection.vue'),
      },
      {
        path: 'explore',
        name: 'explore',
        component: () => import('../views/Explore.vue'),
      },
      {
        path: 'ranking',
        name: 'ranking',
        component: () => import('../views/Ranking.vue'),
      },
      {
        path: 'history',
        name: 'history',
        component: () => import('../views/History.vue'),
      },
      {
        path: 'profile',
        name: 'profile',
        component: () => import('../views/Profile.vue'),
      },
      {
        path: 'settings',
        name: 'settings',
        component: () => import('../views/Settings.vue'),
      },
    ],
  },
];

const router = createRouter({
  history: createWebHashHistory(),
  routes,
});

export default router;
