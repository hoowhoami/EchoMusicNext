import request from '../utils/request';

/**
 * 获取歌手详情
 */
export function getArtistDetail(id: string | number) {
  return request.post('/artist/detail', { id });
}

/**
 * 获取歌手单曲
 */
export function getArtistSongs(id: string | number, page = 1, pagesize = 30) {
  return request.post('/artist/audios', { id, page, pagesize });
}

/**
 * 获取歌手专辑
 */
export function getArtistAlbums(id: string | number, page = 1, pagesize = 30) {
  return request.post('/artist/albums', { id, page, pagesize });
}
