import { Howl } from 'howler';

export interface PlayerEngineEvents {
  timeUpdate?: (currentTime: number) => void;
  durationChange?: (duration: number) => void;
  ended?: () => void;
  play?: () => void;
  pause?: () => void;
  error?: (event: Event) => void;
}

export interface MediaSessionMeta {
  title: string;
  artist: string;
  album?: string;
  artwork?: Array<{ src: string; sizes: string; type: string }>;
}

export interface MediaSessionState {
  isPlaying: boolean;
  duration: number;
  currentTime: number;
  playbackRate: number;
}

const clamp = (value: number, min: number, max: number): number => {
  return Math.min(max, Math.max(min, value));
};

export class PlayerEngine {
  private howl: Howl | null;
  private events: PlayerEngineEvents;
  private sourceUrl: string;
  private volumeValue: number;
  private playbackRateValue: number;
  private durationValue: number;
  private timeUpdateTimer: number | null;
  private lastTimeValue: number;

  constructor() {
    this.howl = null;
    this.events = {};
    this.sourceUrl = '';
    this.volumeValue = 1;
    this.playbackRateValue = 1;
    this.durationValue = 0;
    this.timeUpdateTimer = null;
    this.lastTimeValue = -1;
  }

  private emitDurationChange(): void {
    const duration = this.duration;
    if (!Number.isFinite(duration)) return;
    if (duration === this.durationValue) return;
    this.durationValue = duration;
    this.events.durationChange?.(duration);
  }

  private emitTimeUpdate(): void {
    const current = this.currentTime;
    if (!Number.isFinite(current)) return;
    if (current === this.lastTimeValue) return;
    this.lastTimeValue = current;
    this.events.timeUpdate?.(current);
    this.emitDurationChange();
  }

  private startTimeUpdates(): void {
    this.stopTimeUpdates();
    this.emitTimeUpdate();
    this.timeUpdateTimer = window.setInterval(() => {
      this.emitTimeUpdate();
    }, 250);
  }

  private stopTimeUpdates(): void {
    if (this.timeUpdateTimer !== null) {
      window.clearInterval(this.timeUpdateTimer);
      this.timeUpdateTimer = null;
    }
  }

  private handleError(payload?: unknown): void {
    const errorEvent = new Event('error');
    if (payload && typeof payload === 'object') {
      (errorEvent as Event & { detail?: unknown }).detail = payload;
    }
    this.events.error?.(errorEvent);
  }

  private cleanupHowl(): void {
    this.stopTimeUpdates();
    if (!this.howl) return;
    this.howl.off();
    this.howl.stop();
    this.howl.unload();
    this.howl = null;
  }

  private buildHowl(url: string): void {
    this.cleanupHowl();
    this.howl = new Howl({
      src: [url],
      html5: true,
      volume: this.volumeValue,
      rate: this.playbackRateValue,
      onload: () => {
        this.emitDurationChange();
      },
      onplay: () => {
        this.events.play?.();
        this.startTimeUpdates();
      },
      onpause: () => {
        this.events.pause?.();
        this.stopTimeUpdates();
      },
      onstop: () => {
        this.events.pause?.();
        this.stopTimeUpdates();
      },
      onend: () => {
        this.stopTimeUpdates();
        this.events.ended?.();
      },
      onloaderror: (_id, error) => {
        this.handleError(error);
      },
      onplayerror: (_id, error) => {
        this.handleError(error);
      },
    });
  }

  setEvents(events: PlayerEngineEvents): void {
    this.events = events;
  }

  setVolume(value: number): number {
    const next = clamp(value, 0, 1);
    this.volumeValue = next;
    this.howl?.volume(next);
    return next;
  }

  fadeTo(value: number, durationMs = 0): Promise<void> {
    const next = clamp(value, 0, 1);
    if (!this.howl || durationMs <= 0) {
      this.setVolume(next);
      return Promise.resolve();
    }
    const from = this.volumeValue;
    this.howl.fade(from, next, durationMs);
    return new Promise((resolve) => {
      window.setTimeout(() => {
        this.volumeValue = next;
        resolve();
      }, durationMs);
    });
  }

  setPlaybackRate(rate: number): number {
    const next = clamp(rate, 0.5, 2);
    this.playbackRateValue = next;
    this.howl?.rate(next);
    return next;
  }

  updateMediaMetadata(meta: MediaSessionMeta): void {
    if (typeof navigator === 'undefined' || !('mediaSession' in navigator)) return;
    const session = navigator.mediaSession;
    try {
      session.metadata = new MediaMetadata({
        title: meta.title,
        artist: meta.artist,
        album: meta.album ?? '',
        artwork: meta.artwork ?? [],
      });
    } catch {
      // ignore MediaMetadata errors
    }
  }

  updateMediaPlaybackState(state: MediaSessionState): void {
    if (typeof navigator === 'undefined' || !('mediaSession' in navigator)) return;
    const session = navigator.mediaSession;
    session.playbackState = state.isPlaying ? 'playing' : 'paused';
    if (typeof session.setPositionState === 'function') {
      try {
        session.setPositionState({
          duration: state.duration || 0,
          playbackRate: state.playbackRate || 1,
          position: state.currentTime || 0,
        });
      } catch {
        // ignore unsupported browsers
      }
    }
  }

  setMediaSessionHandlers(handlers: {
    play?: () => void;
    pause?: () => void;
    previoustrack?: () => void;
    nexttrack?: () => void;
    seekto?: (time: number) => void;
    seekbackward?: (offset: number) => void;
    seekforward?: (offset: number) => void;
  }): void {
    if (typeof navigator === 'undefined' || !('mediaSession' in navigator)) return;
    const session = navigator.mediaSession;
    session.setActionHandler('play', handlers.play ?? null);
    session.setActionHandler('pause', handlers.pause ?? null);
    session.setActionHandler('previoustrack', handlers.previoustrack ?? null);
    session.setActionHandler('nexttrack', handlers.nexttrack ?? null);
    session.setActionHandler(
      'seekto',
      handlers.seekto ? (details) => handlers.seekto?.(details.seekTime ?? 0) : null,
    );
    session.setActionHandler(
      'seekbackward',
      handlers.seekbackward ? (details) => handlers.seekbackward?.(details.seekOffset ?? 10) : null,
    );
    session.setActionHandler(
      'seekforward',
      handlers.seekforward ? (details) => handlers.seekforward?.(details.seekOffset ?? 10) : null,
    );
  }

  setSource(url: string): void {
    if (!url || this.sourceUrl === url) return;
    this.sourceUrl = url;
    this.durationValue = 0;
    this.lastTimeValue = -1;
    this.events.durationChange?.(0);
    this.buildHowl(url);
  }

  async play(): Promise<void> {
    if (!this.howl) return;
    try {
      this.howl.play();
    } catch (error) {
      this.handleError(error);
      throw error;
    }
  }

  pause(): void {
    this.howl?.pause();
  }

  seek(time: number): void {
    if (!this.howl) return;
    this.howl.seek(time);
    this.emitTimeUpdate();
  }

  reset(): void {
    this.cleanupHowl();
    this.sourceUrl = '';
    this.durationValue = 0;
    this.lastTimeValue = -1;
    this.events.durationChange?.(0);
    this.events.timeUpdate?.(0);
  }

  get source(): string {
    return this.sourceUrl;
  }

  get currentTime(): number {
    if (!this.howl) return 0;
    const value = this.howl.seek();
    return typeof value === 'number' && Number.isFinite(value) ? value : 0;
  }

  get duration(): number {
    if (!this.howl) return 0;
    const value = this.howl.duration();
    return Number.isFinite(value) ? value : 0;
  }

  get volume(): number {
    return this.volumeValue;
  }

  get playbackRate(): number {
    return this.playbackRateValue;
  }
}
