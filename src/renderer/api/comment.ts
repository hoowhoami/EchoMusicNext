import request from '../utils/request';

/**
 * 获取歌曲评论
 */
export function getMusicComments(hash: string, page = 1, pagesize = 30) {
  return request.get('/comment/music', {
    params: { hash, page, pagesize }
  });
}

/**
 * 获取歌单评论
 */
export function getPlaylistComments(id: string | number, page = 1, pagesize = 30) {
  return request.get('/comment/playlist', {
    params: { id, page, pagesize }
  });
}

/**
 * 获取专辑评论
 */
export function getAlbumComments(id: string | number, page = 1, pagesize = 30) {
  return request.get('/comment/album', {
    params: { id, page, pagesize }
  });
}
