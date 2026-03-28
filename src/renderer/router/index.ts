import { createRouter, createWebHashHistory, type RouteRecordRaw } from 'vue-router';

const routes: RouteRecordRaw[] = [
  {
    path: '/',
    name: 'loading',
    component: () => import('@/views/Loading.vue'),
    meta: { skipHistory: true },
  },
  {
    path: '/login',
    name: 'login',
    component: () => import('@/views/Login.vue'),
    meta: { skipHistory: true },
  },
  {
    path: '/playing',
    name: 'playing',
    component: () => import('@/views/Playing.vue'),
    meta: { skipHistory: true },
  },
  {
    path: '/main',
    component: () => import('@/layouts/MainLayout.vue'),
    children: [
      {
        path: 'home',
        name: 'home',
        component: () => import('@/views/Home.vue'),
      },
      {
        path: 'collection',
        name: 'collection',
        component: () => import('@/views/Collection.vue'),
      },
      {
        path: 'explore',
        name: 'explore',
        component: () => import('@/views/Explore.vue'),
      },
      {
        path: 'ranking',
        name: 'ranking',
        component: () => import('@/views/Ranking.vue'),
      },
      {
        path: 'history',
        name: 'history',
        component: () => import('@/views/History.vue'),
      },
      {
        path: 'profile',
        name: 'profile',
        component: () => import('@/views/Profile.vue'),
      },
      {
        path: 'settings',
        name: 'settings',
        component: () => import('@/views/Settings.vue'),
      },
      {
        path: 'playlist/:id',
        name: 'playlist-detail',
        component: () => import('@/views/details/PlaylistDetail.vue'),
        meta: { title: '歌单详情' },
      },
      {
        path: 'artist/:id',
        name: 'artist-detail',
        component: () => import('@/views/details/ArtistDetail.vue'),
        meta: { title: '歌手详情' },
      },
      {
        path: 'album/:id',
        name: 'album-detail',
        component: () => import('@/views/details/AlbumDetail.vue'),
        meta: { title: '专辑详情' },
      },
      {
        path: 'comment/:id',
        name: 'comment',
        component: () => import('@/views/details/CommentPage.vue'),
        meta: { title: '评论' },
      },
      {
        path: 'error',
        name: 'error',
        component: () => import('@/views/ErrorPage.vue'),
        meta: { title: '出错了' },
      },
    ],
  },
];

const router = createRouter({
  history: createWebHashHistory(),
  routes,
});

router.beforeEach((to, from) => {
  // 不需要跳过历史，直接放行
  if (!to.meta.skipHistory) return true;

  // 如果是来自同一个路由 = 已经重写过了，直接放行
  if (from.fullPath === to.fullPath) {
    return true;
  }

  // 只执行一次 replace
  return {
    ...to,
    replace: true,
  };
});

export default router;
