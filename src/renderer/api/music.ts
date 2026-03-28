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
 * 获取歌曲播放权限/音质信息
 */
export function getSongPrivilegeLite(hash: string, albumId?: string | number) {
  return request.get('/privilege/lite', {
    params: {
      hash,
      album_id: albumId,
    },
  });
}

/**
 * 获取云盘歌曲播放地址
 */
export async function getCloudSongUrl(
  hash: string,
  audioId?: string | number,
  albumAudioId?: string | number
): Promise<string | null> {
  const res = await request.get('/user/cloud/url', {
    params: {
      hash,
      audio_id: audioId,
      album_audio_id: albumAudioId,
    },
  });
  if (res && typeof res === 'object') {
    const record = res as { status?: number; data?: { url?: string } };
    if (record.status === 1 && record.data?.url) return record.data.url;
  }
  return null;
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
