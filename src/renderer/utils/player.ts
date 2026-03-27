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
  private audio: HTMLAudioElement;
  private events: PlayerEngineEvents;

  constructor() {
    this.audio = new Audio();
    this.events = {};
    this.bindEvents();
  }

  private bindEvents(): void {
    this.audio.ontimeupdate = () => {
      this.events.timeUpdate?.(this.audio.currentTime);
    };

    this.audio.ondurationchange = () => {
      this.events.durationChange?.(this.audio.duration);
    };

    this.audio.onended = () => {
      this.events.ended?.();
    };

    this.audio.onplay = () => {
      this.events.play?.();
    };

    this.audio.onpause = () => {
      this.events.pause?.();
    };

    this.audio.onerror = (event) => {
      this.events.error?.(event);
    };
  }

  setEvents(events: PlayerEngineEvents): void {
    this.events = events;
  }

  setVolume(value: number): number {
    const next = clamp(value, 0, 1);
    this.audio.volume = next;
    return next;
  }

  setPlaybackRate(rate: number): number {
    const next = clamp(rate, 0.5, 2);
    this.audio.playbackRate = next;
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
    session.setActionHandler('seekto', handlers.seekto ? (details) => handlers.seekto?.(details.seekTime ?? 0) : null);
    session.setActionHandler('seekbackward', handlers.seekbackward ? (details) => handlers.seekbackward?.(details.seekOffset ?? 10) : null);
    session.setActionHandler('seekforward', handlers.seekforward ? (details) => handlers.seekforward?.(details.seekOffset ?? 10) : null);
  }

  setSource(url: string): void {
    if (!url || this.audio.src === url) return;
    this.audio.src = url;
    this.audio.load();
  }

  async play(): Promise<void> {
    await this.audio.play();
  }

  pause(): void {
    this.audio.pause();
  }

  seek(time: number): void {
    this.audio.currentTime = time;
  }

  reset(): void {
    this.audio.pause();
    this.audio.currentTime = 0;
  }

  get source(): string {
    return this.audio.src;
  }

  get currentTime(): number {
    return this.audio.currentTime;
  }

  get duration(): number {
    return this.audio.duration;
  }

  get volume(): number {
    return this.audio.volume;
  }

  get playbackRate(): number {
    return this.audio.playbackRate;
  }
}
