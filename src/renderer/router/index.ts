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
      {
        path: 'playlist/:id',
        name: 'playlist-detail',
        component: () => import('../views/details/PlaylistDetail.vue'),
        meta: { title: '歌单详情' }
      },
      {
        path: 'artist/:id',
        name: 'artist-detail',
        component: () => import('../views/details/ArtistDetail.vue'),
        meta: { title: '歌手详情' }
      },
      {
        path: 'album/:id',
        name: 'album-detail',
        component: () => import('../views/details/AlbumDetail.vue'),
        meta: { title: '专辑详情' }
      },
      {
        path: 'comment/:id',
        name: 'comment',
        component: () => import('../views/details/CommentPage.vue'),
        meta: { title: '评论' }
      },
    ],
  },
];

const router = createRouter({
  history: createWebHashHistory(),
  routes,
});

export default router;
