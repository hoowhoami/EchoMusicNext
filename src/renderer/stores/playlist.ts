import { defineStore } from 'pinia';

export interface Song {
  id: string;
  title: string;
  artist: string;
  album?: string;
  duration: number;
  coverUrl: string;
  audioUrl: string;
  lyric?: string;
}

const sampleLrc = `[00:00.00]梦中的婚礼
[00:02.00]作曲 : Paul de Senneville
[00:04.00]编曲 : Olivier Toussaint
[00:06.00]演奏 : Richard Clayderman
[00:08.00]
[00:10.00](这是一首纯音乐，请欣赏)
[00:40.00]音符在指尖跳跃
[01:10.00]旋律在心中回荡
[01:40.00]梦境与现实交织
[02:10.00]在这浪漫的夜晚
[02:40.00]EchoMusic 陪伴着你`;

export const usePlaylistStore = defineStore('playlist', {
  state: () => ({
    // 默认内置列表 (写死模拟数据)
    defaultList: [
      {
        id: '1',
        title: '梦中的婚礼',
        artist: 'Richard Clayderman',
        duration: 167,
        coverUrl: 'https://p2.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=431795921.mp3',
        lyric: sampleLrc,
      },
      {
        id: '2',
        title: '克罗地亚狂想曲',
        artist: 'Maksim Mrvica',
        duration: 205,
        coverUrl: 'https://p2.music.126.net/79V_93P_0a-v-93P_0a-vA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=210815.mp3',
      },
      {
        id: '3',
        title: 'Canon in D',
        artist: 'Johann Pachelbel',
        duration: 310,
        coverUrl: 'https://p1.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=1413142871.mp3',
      },
      {
        id: '4',
        title: 'Summer',
        artist: 'Joe Hisaishi',
        duration: 223,
        coverUrl: 'https://p2.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=441491.mp3',
      },
      {
        id: '5',
        title: 'River Flows in You',
        artist: 'Yiruma',
        duration: 188,
        coverUrl: 'https://p2.music.126.net/6y-U_9D-x7Pz1B6T0o2_oA==/109951165034938827.jpg',
        audioUrl: 'https://music.163.com/song/media/outer/url?id=26237342.mp3',
      },
    ] as Song[],
    favorites: [] as Song[],
    history: [] as Song[],
  }),
  actions: {
    addToFavorites(song: Song) {
      if (!this.favorites.find((s) => s.id === song.id)) {
        this.favorites.push(song);
      }
    },
    removeFromFavorites(id: string) {
      this.favorites = this.favorites.filter((s) => s.id !== id);
    },
    addToHistory(song: Song) {
      this.history = [song, ...this.history.filter((s) => s.id !== song.id)].slice(0, 100);
    },
  },
  persist: true,
});
