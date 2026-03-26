import { Song } from '@/stores/playlist';
import { mapPlaylistSong } from './mappers';

/**
 * 处理封面图地址
 * 1:1 复刻 EchoMusic 的处理逻辑
 */
export function getCoverUrl(url: string | undefined, size: number = 400): string {
  if (!url || url === '') {
    return 'https://imge.kugou.com/soft/collection/default.jpg';
  }
  // 1. 强制 HTTPS
  // 2. 替换尺寸占位符 {size}
  // 3. 域名收拢到 imge.kugou.com
  let cover = url.replace('http://', 'https://');
  return cover
    .replace('{size}', size.toString())
    .replace('c1.kgimg.com', 'imge.kugou.com');
}

/**
 * 将酷狗 API 的原始歌曲数据映射为标准的 Song 对象
 * 复刻 Flutter 版本的核心逻辑：兼容 audio_info 嵌套、多种命名字段等
 */
export function mapSong(item: unknown): Song {
  return mapPlaylistSong(item);
}
