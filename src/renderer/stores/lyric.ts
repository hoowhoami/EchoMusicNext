import { defineStore } from 'pinia';
import { getLyric, searchLyric } from '@/api/music';
import logger from '@/utils/logger';

export interface LyricCharacter {
  text: string;
  startTime: number;
  endTime: number;
  highlighted: boolean;
}

export interface LyricLine {
  time: number;
  text: string;
  translated?: string;
  romanized?: string;
  characters: LyricCharacter[];
}

export type LyricsMode = 'none' | 'translation' | 'romanization';

type LyricSearchCandidate = {
  id?: string | number;
  accesskey?: string;
};

type LyricSearchResponse = {
  candidates?: LyricSearchCandidate[];
  info?: LyricSearchCandidate[];
};

type LyricDetailResponse = {
  decodeContent?: string;
  lyric?: string;
};

type LyricDetailEnvelope = LyricDetailResponse & {
  data?: LyricDetailResponse;
};

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

const parsePreference = (value: unknown): LyricsMode => {
  if (value === 'translation' || value === 'romanization') return value;
  return 'none';
};

const normalizeDetailPayload = (payload: unknown): LyricDetailResponse | null => {
  if (!payload || typeof payload !== 'object') return null;
  const record = payload as LyricDetailEnvelope;
  const nested = record.data && typeof record.data === 'object' ? record.data : null;
  const decodeContent = typeof (nested?.decodeContent ?? record.decodeContent) === 'string'
    ? String(nested?.decodeContent ?? record.decodeContent)
    : '';
  const lyric = typeof (nested?.lyric ?? record.lyric) === 'string'
    ? String(nested?.lyric ?? record.lyric)
    : '';
  if (!decodeContent && !lyric) return null;
  return { decodeContent, lyric };
};

const decodeLanguageLine = (line: string): { translationLyrics?: unknown[]; romanizationLyrics?: unknown[] } => {
  try {
    const code = line.slice(10, -1);
    if (!code) return {};

    const cleanedCode = code.replace(/[^A-Za-z0-9+/=]/g, '');
    const paddingLength = (4 - (cleanedCode.length % 4)) % 4;
    const paddedCode = `${cleanedCode}${'='.repeat(paddingLength)}`;
    const decoded = window.atob(paddedCode);
    const utf8 = decodeURIComponent(
      Array.from(decoded)
        .map((char) => `%${char.charCodeAt(0).toString(16).padStart(2, '0')}`)
        .join('')
    );
    const languageData = JSON.parse(utf8) as { content?: Array<{ type?: number; lyricContent?: unknown[] }> };

    let translationLyrics: unknown[] | undefined;
    let romanizationLyrics: unknown[] | undefined;

    for (const section of languageData.content ?? []) {
      if (section.type === 1) translationLyrics = section.lyricContent;
      if (section.type === 0) romanizationLyrics = section.lyricContent;
    }

    return { translationLyrics, romanizationLyrics };
  } catch (error) {
    logger.warn('LyricStore', 'Decode lyric language line failed', error);
    return {};
  }
};

const buildFallbackCharacters = (text: string, startTime: number, duration: number): LyricCharacter[] => {
  const safeText = text.trim();
  if (!safeText) return [];
  const total = Math.max(duration, safeText.length * 120);

  return safeText.split('').map((char, index, arr) => ({
    text: char,
    startTime: startTime + Math.floor((index * total) / arr.length),
    endTime: startTime + Math.floor(((index + 1) * total) / arr.length),
    highlighted: false,
  }));
};

const getSecondaryText = (line: LyricLine, mode: LyricsMode): string => {
  if (mode === 'translation') return line.translated?.trim() ?? '';
  if (mode === 'romanization') return line.romanized?.trim() ?? '';
  return '';
};

export const useLyricStore = defineStore('lyric', {
  state: () => ({
    lines: [] as LyricLine[],
    currentIndex: -1,
    rawLyric: '',
    loadedHash: '',
    tips: '暂无歌词',
    isLoading: false,
    lyricsMode: 'none' as LyricsMode,
    preferredMode: 'none' as LyricsMode,
    hasTranslation: false,
    hasRomanization: false,
    fontScale: 1,
    fontWeightIndex: 8,
    requestSerial: 0,
  }),
  getters: {
    showTranslation: (state) => state.lyricsMode === 'translation' && state.hasTranslation,
    showRomanization: (state) => state.lyricsMode === 'romanization' && state.hasRomanization,
    currentLine: (state) => (state.currentIndex >= 0 ? state.lines[state.currentIndex] ?? null : null),
    activeSecondaryText: (state) => {
      const line = state.currentIndex >= 0 ? state.lines[state.currentIndex] ?? null : null;
      return line ? getSecondaryText(line, state.lyricsMode) : '';
    },
    fontWeightValue: (state) => {
      const weights = [100, 200, 300, 400, 500, 600, 700, 800, 900] as const;
      return weights[clamp(state.fontWeightIndex, 0, 8)] ?? 900;
    },
    copyableText: (state) => {
      const mode = state.lyricsMode;
      return state.lines
        .map((line) => {
          const primary = line.text.trim();
          const secondary = getSecondaryText(line, mode);
          return secondary ? `${primary}\n${secondary}` : primary;
        })
        .filter(Boolean)
        .join('\n');
    },
  },
  actions: {
    resetLyricsState(payload?: { hash?: string; tips?: string }) {
      this.lines = [];
      this.currentIndex = -1;
      this.rawLyric = '';
      this.loadedHash = payload?.hash ?? '';
      this.tips = payload?.tips ?? '暂无歌词';
      this.isLoading = false;
      this.lyricsMode = 'none';
      this.hasTranslation = false;
      this.hasRomanization = false;
    },
    clear(hash = '', tips = '暂无歌词') {
      this.requestSerial += 1;
      this.resetLyricsState({ hash, tips });
    },
    beginLoading(hash = '') {
      this.resetLyricsState({ hash, tips: '歌词加载中...' });
      this.isLoading = true;
    },
    applyPreferredMode() {
      const preferred = parsePreference(this.preferredMode);
      if (preferred === 'translation' && this.hasTranslation) {
        this.lyricsMode = 'translation';
        return;
      }
      if (preferred === 'romanization' && this.hasRomanization) {
        this.lyricsMode = 'romanization';
        return;
      }
      this.lyricsMode = 'none';
    },
    toggleLyricsMode() {
      if (this.lyricsMode === 'none') {
        if (this.hasTranslation) {
          this.lyricsMode = 'translation';
        } else if (this.hasRomanization) {
          this.lyricsMode = 'romanization';
        }
      } else if (this.lyricsMode === 'translation') {
        this.lyricsMode = this.hasRomanization ? 'romanization' : 'none';
      } else {
        this.lyricsMode = 'none';
      }
      this.preferredMode = this.lyricsMode;
    },
    updateFontScale(scale: number) {
      this.fontScale = clamp(Number(scale) || 1, 0.7, 1.4);
    },
    updateFontWeight(index: number) {
      this.fontWeightIndex = clamp(Math.round(index), 0, 8);
    },
    setLyric(content: string, hash = '') {
      this.parseLyricContent({ decodeContent: content }, hash);
    },
    parseLyricContent(payload: LyricDetailResponse, hash = '') {
      this.resetLyricsState({ hash, tips: '暂无歌词' });
      const content = String(payload.decodeContent ?? payload.lyric ?? '').trim();
      this.rawLyric = content;
      this.loadedHash = hash;
      this.isLoading = false;

      if (!content) {
        this.tips = '暂无歌词';
        return;
      }

      const sourceLines = content.split('\n');
      const languageLine = sourceLines.find((line) => line.startsWith('[language:')) ?? '';
      const { translationLyrics, romanizationLyrics } = languageLine ? decodeLanguageLine(languageLine) : {};
      const charRegex = /<(\d+),(\d+),\d+>([^<]+)/g;
      const parsedLines: LyricLine[] = [];

      for (const sourceLine of sourceLines) {
        const krcMatch = sourceLine.match(/^\[(\d+),(\d+)\](.*)$/);
        if (krcMatch) {
          const lineStart = Number.parseInt(krcMatch[1], 10);
          const lineDuration = Number.parseInt(krcMatch[2], 10);
          const lineContent = krcMatch[3] ?? '';
          const characters: LyricCharacter[] = [];
          const matches = Array.from(lineContent.matchAll(charRegex));

          if (matches.length > 0) {
            for (const match of matches) {
              const text = match[3] ?? '';
              const duration = Number.parseInt(match[2] ?? '0', 10);
              const startTime = lineStart + Number.parseInt(match[1] ?? '0', 10);
              characters.push({
                text,
                startTime,
                endTime: startTime + duration,
                highlighted: false,
              });
            }
          } else {
            const text = lineContent.replace(/<.*?>/g, '').trim();
            characters.push(...buildFallbackCharacters(text, lineStart, lineDuration));
          }

          const text = characters.map((char) => char.text).join('').trim();
          if (text) {
            parsedLines.push({
              time: lineStart / 1000,
              text,
              characters,
            });
          }
          continue;
        }

        const lrcMatch = sourceLine.match(/^\[(\d+):(\d+(?:\.\d+)?)\](.*)$/);
        if (!lrcMatch) continue;

        const minutes = Number.parseInt(lrcMatch[1], 10);
        const seconds = Number.parseFloat(lrcMatch[2]);
        const text = (lrcMatch[3] ?? '').trim();
        const startTime = Math.round((minutes * 60 + seconds) * 1000);
        const characters = buildFallbackCharacters(text, startTime, 3000);
        if (!text || characters.length === 0) continue;

        parsedLines.push({
          time: startTime / 1000,
          text,
          characters,
        });
      }

      this.lines = parsedLines.map((line, index) => {
        let translated = '';
        let romanized = '';

        const translationLine = translationLyrics?.[index];
        if (Array.isArray(translationLine) && translationLine.length > 0) {
          translated = String(translationLine[0] ?? '').trim();
        }

        const romanizationLine = romanizationLyrics?.[index];
        if (Array.isArray(romanizationLine) && romanizationLine.length > 0) {
          romanized = romanizationLine.join('').trim();
        }

        if (translated) this.hasTranslation = true;
        if (romanized) this.hasRomanization = true;

        return {
          ...line,
          translated: translated || undefined,
          romanized: romanized || undefined,
        };
      });

      this.tips = this.lines.length > 0 ? '歌词已加载' : '暂无歌词';
      this.applyPreferredMode();
    },
    updateCurrentIndex(currentTime: number) {
      if (this.lines.length === 0) {
        this.currentIndex = -1;
        return;
      }

      const currentTimeMs = Math.round(currentTime * 1000);
      let nextIndex = -1;
      const startSearchIndex = this.currentIndex >= 0 && currentTime >= this.lines[this.currentIndex].time
        ? this.currentIndex
        : 0;

      for (let index = startSearchIndex; index < this.lines.length; index += 1) {
        const currentLine = this.lines[index];
        const nextLine = this.lines[index + 1];
        const start = currentLine.characters[0]?.startTime ?? Math.round(currentLine.time * 1000);
        const nextStart = nextLine?.characters[0]?.startTime ?? Number.POSITIVE_INFINITY;

        if (currentTimeMs >= start && currentTimeMs < nextStart) {
          nextIndex = index;
          break;
        }
      }

      if (nextIndex === -1 && currentTimeMs >= (this.lines[0]?.characters[0]?.startTime ?? 0)) {
        nextIndex = this.lines.length - 1;
      }

      if (this.currentIndex !== nextIndex) {
        const previousLine = this.currentIndex >= 0 ? this.lines[this.currentIndex] : null;
        previousLine?.characters.forEach((char) => {
          char.highlighted = false;
        });
        this.currentIndex = nextIndex;
      }

      if (this.currentIndex < 0) return;
      const currentLine = this.lines[this.currentIndex];
      currentLine.characters.forEach((char) => {
        char.highlighted = currentTimeMs >= char.startTime;
      });
    },
    async fetchLyrics(hash: string) {
      const normalizedHash = String(hash ?? '').trim();
      if (!normalizedHash) {
        this.clear('', '暂无歌词');
        return;
      }

      if (this.loadedHash === normalizedHash && this.lines.length > 0) return;

      const requestSerial = this.requestSerial + 1;
      this.requestSerial = requestSerial;
      this.beginLoading(normalizedHash);

      try {
        const searchResult = await searchLyric(normalizedHash) as LyricSearchResponse | null;
        if (requestSerial !== this.requestSerial) return;

        const target = (Array.isArray(searchResult?.candidates) && searchResult.candidates.length > 0
          ? searchResult.candidates[0]
          : Array.isArray(searchResult?.info) && searchResult.info.length > 0
            ? searchResult.info[0]
            : null) ?? null;

        if (!target?.id || !target.accesskey) {
          this.clear(normalizedHash, '暂无歌词');
          return;
        }

        const lyricData = normalizeDetailPayload(await getLyric(String(target.id), String(target.accesskey)));
        if (requestSerial !== this.requestSerial) return;

        if (!lyricData) {
          this.clear(normalizedHash, '暂无歌词');
          return;
        }

        this.parseLyricContent(lyricData, normalizedHash);
      } catch (error) {
        if (requestSerial !== this.requestSerial) return;
        logger.error('LyricStore', 'Fetch lyrics failed', error, { hash: normalizedHash });
        this.clear(normalizedHash, '歌词加载失败');
      }
    },
  },
  persist: {
    pick: ['preferredMode', 'fontScale', 'fontWeightIndex'],
  },
});
