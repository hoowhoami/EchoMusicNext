import request from '@/utils/request';

/**
 * 获取歌曲播放地址
 */
export function getSongUrl(hash: string, quality = '') {
  return request.get('/song/url', {
    params: { hash, quality },
  });
}

/**
 * 搜索歌词
 */
export function searchLyric(hash: string) {
  return request.get('/search/lyric', {
    params: { hash },
  });
}

/**
 * 获取歌词详情
 */
export function getLyric(id: string, accesskey: string) {
  return request.get('/lyric', {
    params: { id, accesskey, decode: 'true', fmt: 'krc' },
  });
}

/**
 * 获取新歌榜
 */
export function getNewSongs() {
  return request.get('/top/song');
}

/**
 * 获取每日推荐歌曲
 */
export function getEverydayRecommend() {
  return request.get('/everyday/recommend');
}

/**
 * 获取歌曲高潮片段
 */
export function getSongClimax(hash: string) {
  return request.get('/song/climax', {
    params: { hash },
  });
}
