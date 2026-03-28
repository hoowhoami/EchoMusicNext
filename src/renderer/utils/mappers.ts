import { Song, SongRelateGood, SongArtist } from '@/stores/playlist';
import { getCoverUrl } from './music';

export interface PlaylistTrackQueryContext {
  listid?: number;
  listCreateGid?: string;
  listCreateUserid?: number;
  currentUserId?: number;
}

export interface PlaylistParseResult {
  songs: Song[];
  filteredCount: number;
}

export interface PlaylistMeta {
  id: number;
  globalCollectionId?: string;
  listCreateGid?: string;
  listCreateUserid?: number;
  listCreateListid?: number;
  listid?: number;
  musiclibId?: number;
  name: string;
  pic: string;
  intro: string;
  nickname: string;
  userPic: string;
  tags: string;
  playCount: number;
  count: number;
  isPrivate: boolean;
  heat?: number;
  publishDate?: string;
  createTime?: number;
  updateTime?: number;
  source: number;
  type?: number;
  isDefault?: boolean;
}

export interface AlbumMeta {
  id: number;
  name: string;
  pic: string;
  intro: string;
  singerName: string;
  singerId: number;
  publishTime: string;
  songCount: number;
  playCount: number;
  heat: number;
  language: string;
  type: string;
  company: string;
}

export interface ArtistMeta {
  id: number;
  name: string;
  pic: string;
  intro: string;
  songCount: number;
  albumCount: number;
  mvCount: number;
  fansCount: number;
}

export interface Comment {
  id: string | number;
  userName: string;
  avatar: string;
  content: string;
  time: string;
  likeCount: number;
}

type UnknownRecord = Record<string, unknown>;

const EMPTY_RECORD: UnknownRecord = {};

const isRecord = (value: unknown): value is UnknownRecord => {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
};

const toRecord = (value: unknown): UnknownRecord => (isRecord(value) ? value : EMPTY_RECORD);

const getRecord = (record: UnknownRecord, key: string): UnknownRecord | undefined => {
  const value = record[key];
  return isRecord(value) ? value : undefined;
};

const getArray = (value: unknown): unknown[] | undefined =>
  Array.isArray(value) ? value : undefined;

const parseIntSafe = (value: unknown): number => {
  if (value == null) return 0;
  if (typeof value === 'number') return value;
  const numericPart = String(value).match(/^\d+/)?.[0];
  if (numericPart) return Number.parseInt(numericPart, 10) || 0;
  return Number.parseInt(String(value), 10) || 0;
};

const parseOptionalInt = (value: unknown): number | undefined => {
  if (value == null) return undefined;
  if (typeof value === 'number') return value;
  const parsed = Number.parseInt(String(value), 10);
  return Number.isNaN(parsed) ? undefined : parsed;
};

const readString = (value: unknown, fallback = ''): string => {
  if (value == null) return fallback;
  return String(value);
};

const pickValue = (...values: unknown[]): unknown => {
  for (const value of values) {
    if (value === undefined || value === null) continue;
    if (typeof value === 'string' && value === '') continue;
    return value;
  }
  return undefined;
};

const toRelateGood = (item: unknown): SongRelateGood | null => {
  if (!isRecord(item)) return null;
  const hash = readString(pickValue(item.hash, item.Hash, item.file_hash, item.fileHash), '');
  const quality = readString(pickValue(item.quality, item.Quality, item.q), '');
  const level = parseOptionalInt(
    pickValue(item.level, item.quality_level, item.qualityLevel, item.quality),
  );

  const good: SongRelateGood = {};
  if (hash) good.hash = hash;
  if (quality) good.quality = quality;
  if (level !== undefined) good.level = level;

  if (!good.hash && !good.quality && good.level === undefined) return null;
  return good;
};

const buildArtists = (record: UnknownRecord, audioInfo: UnknownRecord): SongArtist[] => {
  const artists: SongArtist[] = [];
  const singerInfo = getArray(pickValue(record.singerinfo, audioInfo.singerinfo, record.authors));

  if (singerInfo) {
    for (const raw of singerInfo) {
      if (!isRecord(raw)) continue;
      const name = readString(
        pickValue(raw.name, raw.author_name, raw.singername, raw.singer),
        '',
      ).trim();
      if (!name) continue;
      const id = pickValue(raw.id, raw.author_id, raw.singerid, raw.singer_id);
      artists.push({
        id: id !== undefined && id !== null ? readString(id) : undefined,
        name,
      });
    }
  }

  if (artists.length === 0) {
    const fallbackName = readString(
      pickValue(
        record.author_name,
        record.singername,
        record.singer,
        audioInfo.author_name,
        audioInfo.singername,
      ),
      '',
    ).trim();

    if (fallbackName) {
      artists.push({ name: fallbackName });
    }
  }

  return artists;
};

const buildRelateGoods = (record: UnknownRecord, audioInfo: UnknownRecord): SongRelateGood[] => {
  const goods: SongRelateGood[] = [];
  const rawGoods = getArray(
    pickValue(
      record.relate_goods,
      record.relateGoods,
      audioInfo.relate_goods,
      audioInfo.relateGoods,
    ),
  );

  if (rawGoods) {
    for (const item of rawGoods) {
      const good = toRelateGood(item);
      if (good) goods.push(good);
    }
  }

  const pushQualityHash = (hashValue: unknown, quality: string) => {
    const hash = readString(hashValue, '');
    if (hash) goods.push({ hash, quality });
  };

  pushQualityHash(pickValue(record.hash_320, audioInfo.hash_320), '320');
  pushQualityHash(pickValue(record.hash_flac, audioInfo.hash_flac), 'flac');
  pushQualityHash(pickValue(record.hash_high, audioInfo.hash_high), 'high');

  const hq = getRecord(record, 'HQ');
  const sq = getRecord(record, 'SQ');
  const res = getRecord(record, 'Res');
  if (hq) pushQualityHash(pickValue(hq.Hash, hq.hash), '320');
  if (sq) pushQualityHash(pickValue(sq.Hash, sq.hash), 'flac');
  if (res) pushQualityHash(pickValue(res.Hash, res.hash), 'high');

  return goods;
};

const formatPic = (value: unknown): string => {
  if (!value) return '';
  let pic = String(value).replaceAll('{size}', '400');
  if (pic.startsWith('//')) {
    pic = `https:${pic}`;
  }
  return pic;
};

const normalizeText = (raw: string): string => {
  return raw.replaceAll('_', ' ').replace(/\s+/g, ' ').trim();
};

const processSongTitle = (rawName: string): string => {
  if (rawName.includes(' - ')) {
    const parts = rawName.split(' - ');
    if (parts.length > 1) return parts.slice(1).join(' - ');
  }
  return rawName;
};

const cleanupAudioExtension = (value: string): string => {
  const extensions = ['.mp3', '.flac', '.wav', '.aac', '.m4a', '.ape'];
  let output = value;
  for (const ext of extensions) {
    if (output.toLowerCase().endsWith(ext)) {
      output = output.slice(0, -ext.length);
    }
  }
  return output;
};

export const resolvePlaylistTrackQueryId = (
  id: string | number,
  context: PlaylistTrackQueryContext,
): string => {
  let queryId = String(id ?? '');
  const { listid, listCreateGid, listCreateUserid, currentUserId } = context;
  if (listid && listid !== 0) {
    if (listCreateUserid && currentUserId && listCreateUserid !== currentUserId) {
      if (listCreateGid && listCreateGid !== '0' && listCreateGid !== 'null') {
        queryId = listCreateGid;
      }
    }
  }
  return queryId;
};

export const parsePlaylistTracks = (responseData: unknown): PlaylistParseResult => {
  let dataCandidate: unknown;
  if (Array.isArray(responseData)) {
    dataCandidate = responseData;
  } else if (isRecord(responseData)) {
    dataCandidate = responseData.data ?? responseData;
  }

  let list: unknown[] | undefined;
  if (Array.isArray(dataCandidate)) {
    list = dataCandidate;
  } else if (isRecord(dataCandidate)) {
    list =
      getArray(dataCandidate.songs) ??
      getArray(dataCandidate.info) ??
      getArray(dataCandidate.list) ??
      getArray(dataCandidate.songlist) ??
      getArray(dataCandidate.song_list);

    const infoMap = getRecord(dataCandidate, 'info');
    if (!list && infoMap) {
      list = getArray(infoMap.list) ?? getArray(infoMap.songs) ?? getArray(infoMap.songlist);
    }
  }

  const songsData = Array.isArray(list) ? list : [];
  const songs: Song[] = [];
  let filteredCount = 0;
  for (const rawSong of songsData) {
    if (!rawSong || typeof rawSong !== 'object') continue;
    const song = mapPlaylistSong(rawSong);
    if (isMeaninglessHashlessSong(song)) {
      filteredCount += 1;
      continue;
    }
    songs.push(song);
  }

  return { songs, filteredCount };
};

const isMeaninglessHashlessSong = (song: Song): boolean => {
  return (
    song.hash === '' &&
    song.title.trim() === '' &&
    song.artist.trim() === '' &&
    (song.album ?? '').trim() === '' &&
    song.coverUrl.trim() === '' &&
    String(song.mixSongId ?? '') === '0'
  );
};

export const mapPlaylistSong = (json: unknown): Song => {
  const record = toRecord(json);
  const audioInfo = getRecord(record, 'audio_info') ?? EMPTY_RECORD;

  const hash = readString(pickValue(record.hash, audioInfo.hash_128, audioInfo.hash));

  const rawName = readString(
    pickValue(
      record.songname,
      record.filename,
      record.name,
      record.audio_name,
      audioInfo.songname,
      audioInfo.filename,
      audioInfo.name,
      '',
    ),
  );

  let singerName = readString(
    pickValue(
      record.author_name,
      record.singername,
      record.singer,
      audioInfo.author_name,
      audioInfo.singername,
      '',
    ),
  );

  if (!singerName && rawName.includes(' - ')) {
    singerName = rawName.split(' - ')[0];
  }

  let title = processSongTitle(rawName);
  title = cleanupAudioExtension(title);
  singerName = cleanupAudioExtension(singerName);

  const albumInfo =
    getRecord(record, 'albuminfo') ?? getRecord(record, 'album_info') ?? EMPTY_RECORD;
  const transParam = getRecord(record, 'trans_param') ?? EMPTY_RECORD;
  const cover = readString(
    pickValue(
      record.cover,
      record.pic,
      record.img,
      record.album_sizable_cover,
      audioInfo.img,
      transParam.union_cover,
      albumInfo.cover,
      albumInfo.sizable_cover,
      '',
    ),
  );

  const durationRaw = parseIntSafe(
    pickValue(record.timelen, audioInfo.duration_128, audioInfo.duration, 0),
  );

  const relateGoods = buildRelateGoods(record, audioInfo);
  const artists = buildArtists(record, audioInfo);

  const privilegeDownload =
    getRecord(record, 'privilege_download') ??
    getRecord(record, 'privilegeDownload') ??
    EMPTY_RECORD;

  const privilege = parseOptionalInt(
    pickValue(record.privilege, audioInfo.privilege, privilegeDownload.privilege, undefined),
  );

  let payType = parseOptionalInt(pickValue(record.pay_type, record.PayType, record.payType));
  if (payType === undefined) {
    const downloadList = getArray(record.download);
    const firstDownload = downloadList?.[0];
    if (isRecord(firstDownload)) {
      payType = parseOptionalInt(firstDownload.pay_type ?? firstDownload.PayType);
    }
  }

  const oldCpy = parseOptionalInt(
    pickValue(record.old_cpy, record.media_old_cpy, record.mediaOldCpy),
  );

  return {
    id: readString(pickValue(record.mixsongid, record.audio_id, audioInfo.audio_id, hash)),
    title: title || '未知歌曲',
    artist: normalizeText(singerName || '未知歌手'),
    artists,
    album: normalizeText(
      readString(
        pickValue(record.albumname, record.album_name, albumInfo.name, albumInfo.album_name, ''),
      ),
    ),
    albumId: readString(
      pickValue(albumInfo.id, albumInfo.album_id, record.album_id, record.albumid, ''),
    ),
    duration: Math.floor(durationRaw / 1000),
    coverUrl: getCoverUrl(cover, 400),
    audioUrl: '',
    hash,
    mixSongId: parseIntSafe(pickValue(record.mixsongid, record.audio_id, audioInfo.audio_id, 0)),
    relateGoods,
    privilege,
    payType,
    oldCpy,
  };
};

export const mapArtistSong = (_artistId: string | number, json: unknown): Song => {
  const record = toRecord(json);
  const transParam = getRecord(record, 'trans_param') ?? EMPTY_RECORD;
  const hash = readString(record.hash, '');
  const audioInfo = getRecord(record, 'audio_info') ?? EMPTY_RECORD;
  const artists = buildArtists(record, audioInfo);
  const privilegeDownload =
    getRecord(record, 'privilege_download') ??
    getRecord(record, 'privilegeDownload') ??
    EMPTY_RECORD;

  return {
    id: readString(pickValue(record.mixsongid, record.album_audio_id, hash)),
    title: readString(record.audio_name, '未知歌曲'),
    artist: normalizeText(readString(record.author_name, '未知歌手')),
    artists,
    album: normalizeText(readString(record.album_name, '')),
    albumId: readString(record.album_id, ''),
    duration: Math.floor(parseIntSafe(pickValue(record.timelength, 0)) / 1000),
    coverUrl: getCoverUrl(readString(transParam.union_cover, ''), 400),
    audioUrl: '',
    hash,
    mixSongId: parseIntSafe(pickValue(record.mixsongid, record.album_audio_id, 0)),
    relateGoods: buildRelateGoods(record, audioInfo),
    privilege: parseOptionalInt(
      pickValue(record.privilege, privilegeDownload.privilege, undefined),
    ),
    payType: parseOptionalInt(pickValue(record.pay_type, record.PayType, record.payType)),
    oldCpy: parseOptionalInt(pickValue(record.old_cpy, record.media_old_cpy)),
  };
};

export const mapAlbumSong = (json: unknown): Song => {
  const record = toRecord(json);
  const base = getRecord(record, 'base') ?? EMPTY_RECORD;
  const audioInfo = getRecord(record, 'audio_info') ?? EMPTY_RECORD;
  const albumInfo = getRecord(record, 'album_info') ?? EMPTY_RECORD;
  const transParam = getRecord(record, 'trans_param') ?? EMPTY_RECORD;
  const copyright = getRecord(record, 'copyright') ?? EMPTY_RECORD;
  const artists = buildArtists(record, audioInfo);

  const cover = readString(
    pickValue(record.pic, record.img, audioInfo.img, albumInfo.cover, transParam.union_cover, ''),
  );

  return {
    id: readString(pickValue(base.audio_id, record.audio_id, audioInfo.audio_id, '')),
    title: processSongTitle(readString(pickValue(base.audio_name, record.songname, '未知歌曲'))),
    artist: normalizeText(readString(pickValue(base.author_name, record.author_name, '未知歌手'))),
    artists,
    album: normalizeText(readString(pickValue(albumInfo.album_name, record.album_name, ''))),
    albumId: readString(pickValue(base.album_id, record.album_id, '')),
    duration: Math.floor(parseIntSafe(pickValue(audioInfo.duration, record.timelength, 0)) / 1000),
    coverUrl: getCoverUrl(cover, 400),
    audioUrl: '',
    hash: readString(pickValue(audioInfo.hash, record.hash, '')),
    mixSongId: parseIntSafe(pickValue(base.audio_id, record.audio_id, 0)),
    relateGoods: buildRelateGoods(record, audioInfo),
    privilege: parseOptionalInt(pickValue(record.privilege, copyright.privilege, undefined)),
    payType: parseOptionalInt(pickValue(record.pay_type, record.PayType, record.payType)),
    oldCpy: parseOptionalInt(pickValue(record.old_cpy, record.media_old_cpy)),
  };
};

export const mapPlaylistMeta = (json: unknown): PlaylistMeta => {
  const record = toRecord(json);
  const isUserPlaylist =
    Object.prototype.hasOwnProperty.call(record, 'list_create_userid') &&
    (Object.prototype.hasOwnProperty.call(record, 'listid') ||
      Object.prototype.hasOwnProperty.call(record, 'specialid'));

  if (isUserPlaylist) {
    const typeValue = parseOptionalInt(record.type);
    const isDefault = record.is_def === 1 || record.is_default === 1 || record.is_def === 2;
    console.log(record.musiclib_id);
    return {
      id: parseIntSafe(pickValue(record.listid, record.specialid, 0)),
      listid: parseIntSafe(record.listid),
      globalCollectionId: readString(
        pickValue(record.global_collection_id, record.gid, record.specialid, ''),
      ),
      listCreateGid: readString(record.list_create_gid, ''),
      listCreateUserid: parseOptionalInt(record.list_create_userid),
      listCreateListid: parseOptionalInt(record.list_create_listid),
      musiclibId: parseOptionalInt(record.musiclib_id),
      name: readString(pickValue(record.name, record.specialname, '')),
      pic: formatPic(pickValue(record.pic, record.imgurl, record.cover, record.img, '')),
      intro: readString(pickValue(record.intro, record.description, record.desc, ''), ''),
      nickname: readString(
        pickValue(record.nickname, record.username, record.list_create_username, ''),
      ),
      userPic: formatPic(
        pickValue(record.user_pic, record.avatar, record.create_user_pic, record.pic, ''),
      ),
      tags: readString(record.tags, ''),
      playCount: parseIntSafe(
        pickValue(record.play_count, record.playcount, record.count, record.play_total, 0),
      ),
      count: parseIntSafe(pickValue(record.song_count, record.songcount, record.count, 0)),
      isPrivate: record.is_pri === 1 || record.is_private === 1,
      heat: parseOptionalInt(
        pickValue(record.collectcount, record.collect_count, record.collect_total),
      ),
      publishDate: readString(pickValue(record.publishtime, record.publish_time, '')).split(' ')[0],
      createTime: parseOptionalInt(pickValue(record.create_time, record.addtime)),
      updateTime: parseOptionalInt(record.update_time),
      source: parseIntSafe(pickValue(record.source, 1)),
      type: typeValue,
      isDefault,
    };
  }

  return {
    id: parseIntSafe(pickValue(record.specialid, record.listid, record.global_collection_id, 0)),
    globalCollectionId: readString(
      pickValue(record.global_collection_id, record.gid, record.specialid, ''),
    ),
    listCreateGid: readString(
      pickValue(
        record.list_create_gid,
        record.global_collection_id,
        record.gid,
        record.specialid,
        '',
      ),
    ),
    listCreateUserid: parseOptionalInt(pickValue(record.list_create_userid, record.userid)),
    listCreateListid: parseOptionalInt(pickValue(record.list_create_listid, record.specialid)),
    name: readString(pickValue(record.specialname, record.name, '')),
    pic: formatPic(pickValue(record.flexible_cover, record.pic, record.imgurl, record.img, '')),
    intro: readString(pickValue(record.intro, record.description, record.desc, ''), ''),
    nickname: readString(
      pickValue(record.nickname, record.username, record.author, record.list_create_username, ''),
    ),
    userPic: formatPic(
      pickValue(record.user_pic, record.avatar, record.create_user_pic, record.author_pic, ''),
    ),
    tags: readString(record.tags, ''),
    playCount: parseIntSafe(
      pickValue(record.playcount, record.play_count, record.count, record.play_total, 0),
    ),
    count: parseIntSafe(pickValue(record.song_count, record.songcount, record.count, 0)),
    isPrivate: false,
    heat: parseOptionalInt(
      pickValue(record.collectcount, record.collect_count, record.collect_total),
    ),
    publishDate: readString(pickValue(record.publishtime, record.publish_time, '')).split(' ')[0],
    createTime: parseOptionalInt(pickValue(record.create_time, record.addtime)),
    updateTime: parseOptionalInt(record.update_time),
    source: parseIntSafe(pickValue(record.source, 1)),
  };
};

export const mapAlbumMeta = (json: unknown): AlbumMeta => {
  const record = toRecord(json);
  return {
    id: parseIntSafe(pickValue(record.AlbumId, record.albumid, record.album_id, record.id, 0)),
    name: readString(
      pickValue(record.AlbumName, record.albumname, record.album_name, record.name, ''),
    ),
    pic: formatPic(
      pickValue(
        record.img,
        record.Image,
        record.imgurl,
        record.sizable_cover,
        record.pic,
        record.cover,
        '',
      ),
    ),
    intro: readString(pickValue(record.intro, record.album_intro, '')),
    singerName: readString(
      pickValue(
        record.SingerName,
        record.singername,
        record.singer_name,
        record.author_name,
        record.singer,
        '',
      ),
    ),
    singerId: parseIntSafe(
      pickValue(record.SingerId, record.singerid, record.author_id, record.singer_id, 0),
    ),
    publishTime: readString(
      pickValue(
        record.PublishTime,
        record.publishtime,
        record.publish_time,
        record.publish_date,
        '',
      ),
    ).split(' ')[0],
    songCount: parseIntSafe(
      pickValue(
        record.SongCount,
        record.song_count,
        record.count,
        record.songcount,
        record.total_count,
        0,
      ),
    ),
    playCount: parseIntSafe(pickValue(record.play_count, record.play_times, record.playcount, 0)),
    heat: parseIntSafe(pickValue(record.heat, record.collect_count, record.collectcount, 0)),
    language: readString(record.language, ''),
    type: readString(record.type, ''),
    company: readString(record.company, ''),
  };
};

export const mapAlbumDetailMeta = (json: unknown): AlbumMeta => {
  const record = toRecord(json);
  return {
    id: parseIntSafe(pickValue(record.album_id, record.albumid, record.id, 0)),
    name: readString(pickValue(record.album_name, record.albumname, record.name, '')),
    pic: formatPic(
      pickValue(record.sizable_cover, record.imgurl, record.img, record.pic, record.cover, ''),
    ),
    intro: readString(record.intro, ''),
    singerName: readString(pickValue(record.author_name, record.singername, record.singer, '')),
    singerId: parseIntSafe(pickValue(record.author_id, record.singerid, 0)),
    publishTime: readString(
      pickValue(record.publish_date, record.publishtime, record.publish_time, ''),
    ).split(' ')[0],
    songCount: parseIntSafe(pickValue(record.song_count, record.songcount, 0)),
    playCount: parseIntSafe(pickValue(record.play_count, record.play_times, record.playcount, 0)),
    heat: parseIntSafe(pickValue(record.heat, record.collect_count, record.collectcount, 0)),
    language: readString(record.language, ''),
    type: readString(record.type, ''),
    company: readString(record.company, ''),
  };
};

export const mapArtistMeta = (json: unknown): ArtistMeta => {
  const record = toRecord(json);
  return {
    id: parseIntSafe(
      pickValue(record.AuthorId, record.singerid, record.id, record.author_id, record.singer_id, 0),
    ),
    name: readString(
      pickValue(
        record.AuthorName,
        record.singername,
        record.author_name,
        record.name,
        record.singer_name,
        '',
      ),
    ),
    pic: formatPic(
      pickValue(
        record.Avatar,
        record.imgurl,
        record.avatar,
        record.pic,
        record.image,
        record.singer_img,
        record.sizable_avatar,
        '',
      ),
    ),
    intro: readString(
      pickValue(record.intro, record.long_intro, record.profile, record.singer_intro, ''),
    ),
    songCount: parseIntSafe(
      pickValue(
        record.AudioCount,
        record.song_count,
        record.songcount,
        record.audio_count,
        record.song_num,
        0,
      ),
    ),
    albumCount: parseIntSafe(
      pickValue(record.AlbumCount, record.album_count, record.albumcount, record.album_num, 0),
    ),
    mvCount: parseIntSafe(
      pickValue(record.VideoCount, record.mv_count, record.mvcount, record.mv_num, 0),
    ),
    fansCount: parseIntSafe(
      pickValue(
        record.FansNum,
        record.fansnums,
        record.fans_count,
        record.fans,
        record.fans_num,
        record.fanscount,
        0,
      ),
    ),
  };
};

export const mapArtistDetailMeta = (json: unknown): ArtistMeta => {
  const record = toRecord(json);
  let introStr = '';
  if (Array.isArray(record.long_intro)) {
    introStr = record.long_intro
      .map((entry) => readString(isRecord(entry) ? entry.content : '', ''))
      .join('\n\n');
  } else {
    introStr = readString(pickValue(record.long_intro, record.intro, record.profile, ''), '');
  }

  return {
    id: parseIntSafe(pickValue(record.AuthorId, record.author_id, record.singerid, record.id, 0)),
    name: readString(
      pickValue(record.AuthorName, record.author_name, record.singername, record.name, ''),
    ),
    pic: formatPic(pickValue(record.sizable_avatar, record.Avatar, record.imgurl, record.pic, '')),
    intro: introStr,
    songCount: parseIntSafe(
      pickValue(
        record.AudioCount,
        record.song_count,
        record.songcount,
        record.audio_count,
        record.song_num,
        0,
      ),
    ),
    albumCount: parseIntSafe(
      pickValue(record.AlbumCount, record.album_count, record.albumcount, record.album_num, 0),
    ),
    mvCount: parseIntSafe(
      pickValue(record.VideoCount, record.mv_count, record.mvcount, record.mv_num, 0),
    ),
    fansCount: parseIntSafe(
      pickValue(
        record.FansNum,
        record.fansnums,
        record.fanscount,
        record.fans_count,
        record.fans_num,
        0,
      ),
    ),
  };
};

export const mapCommentItem = (item: unknown): Comment => {
  const record = toRecord(item);
  return {
    id: readString(record.comment_id ?? record.id ?? ''),
    userName: readString(record.user_name ?? record.nickname ?? '匿名用户'),
    avatar: readString(record.user_img ?? record.avatar ?? ''),
    content: readString(record.content ?? ''),
    time: readString(record.add_time ?? record.time ?? ''),
    likeCount: parseIntSafe(record.like_count ?? record.count ?? 0),
  };
};
