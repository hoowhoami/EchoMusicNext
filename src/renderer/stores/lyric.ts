import { defineStore } from 'pinia';

export interface LyricLine {
  time: number;
  text: string;
}

export const useLyricStore = defineStore('lyric', {
  state: () => ({
    lines: [] as LyricLine[],
    currentIndex: -1,
    rawLyric: '',
  }),
  actions: {
    setLyric(lrc: string) {
      this.rawLyric = lrc;
      this.lines = this.parseLRC(lrc);
    },
    updateCurrentIndex(currentTime: number) {
      this.currentIndex = this.lines.findIndex((line, index) => {
        const nextLine = this.lines[index + 1];
        return currentTime >= line.time && (!nextLine || currentTime < nextLine.time);
      });
    },
    parseLRC(lrc: string): LyricLine[] {
      const lines = lrc.split('\n');
      const result: LyricLine[] = [];
      const timeRegex = /\[(\d{2}):(\d{2})\.(\d{2,3})\]/;

      lines.forEach((line) => {
        const match = timeRegex.exec(line);
        if (match) {
          const m = parseInt(match[1]);
          const s = parseInt(match[2]);
          const ms = parseInt(match[3]);
          const time = m * 60 + s + (ms > 99 ? ms / 1000 : ms / 100);
          const text = line.replace(timeRegex, '').trim();
          if (text) result.push({ time, text });
        }
      });
      return result.sort((a, b) => a.time - b.time);
    },
  },
});
