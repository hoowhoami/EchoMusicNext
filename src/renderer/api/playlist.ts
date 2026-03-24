import request from '../utils/request';

/**
 * 获取推荐歌单
 */
export function getRecommendPlaylists() {
  return request.get('/playlist/recommend');
}

/**
 * 获取歌单详情
 */
export function getPlaylistDetail(ids: string) {
  return request.get('/playlist/detail', {
    params: { ids }
  });
}

/**
 * 获取歌单所有歌曲
 */
export function getPlaylistTracks(id: string, page = 1, pagesize = 30) {
  return request.get('/playlist/track/all', {
    params: { id, page, pagesize }
  });
}

/**
 * 获取用户歌单
 */
export function getUserPlaylists(page = 1, pagesize = 30) {
  return request.get('/user/playlist', {
    params: { page, pagesize }
  });
}

/**
 * 获取排行榜列表
 */
export function getRanks() {
  return request.get('/rank/list');
}

/**
 * 获取排行榜歌曲
 */
export function getRankSongs(rankid: number, page = 1, pagesize = 100) {
  return request.get('/rank/audio', {
    params: { rankid, page, pagesize }
  });
}
