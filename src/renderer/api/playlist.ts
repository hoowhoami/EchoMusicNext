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

/**
 * 向歌单添加歌曲
 * @param listid 歌单 ID
 * @param data 歌曲数据，格式: name|hash|albumid|mixsongid
 */
export function addPlaylistTrack(listid: string | number, data: string) {
  return request.get('/playlist/tracks/add', {
    params: { listid, data }
  });
}

/**
 * 从歌单删除歌曲
 * @param listid 歌单 ID
 * @param fileids 歌曲文件 ID (mixsongid)，多个用逗号隔开
 */
export function deletePlaylistTrack(listid: string | number, fileids: string) {
  return request.get('/playlist/tracks/del', {
    params: { listid, fileids }
  });
}
