import request from '../utils/request';

/**
 * 获取专辑详情
 */
export function getAlbumDetail(id: string | number) {
  return request.post('/album/detail', { id });
}

/**
 * 获取专辑歌曲
 */
export function getAlbumSongs(id: string | number, page = 1, pagesize = 30) {
  return request.post('/album/songs', { id, page, pagesize });
}
