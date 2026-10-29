var __async = (__this, __arguments, generator) => {
  return new Promise((resolve, reject) => {
    var fulfilled = (value) => {
      try {
        step(generator.next(value));
      } catch (e) {
        reject(e);
      }
    };
    var rejected = (value) => {
      try {
        step(generator.throw(value));
      } catch (e) {
        reject(e);
      }
    };
    var step = (x) =>
      x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
    step((generator = generator.apply(__this, __arguments)).next());
  });
};

// ../../../../Work/mine/folia-major/src/services/automix/signalAnalysis.ts
var bitReversal = /* @__PURE__ */ new Map();
var bitReversalOf = (n) => {
  let table = bitReversal.get(n);
  if (table) return table;
  table = new Int32Array(n);
  for (let i = 1, j = 0; i < n; i += 1) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    table[i] = j;
  }
  bitReversal.set(n, table);
  return table;
};
var fft = (real, imag) => {
  const n = real.length;
  const reversed = bitReversalOf(n);
  for (let i = 1; i < n; i += 1) {
    const j = reversed[i];
    if (i < j) {
      let swap = real[i];
      real[i] = real[j];
      real[j] = swap;
      swap = imag[i];
      imag[i] = imag[j];
      imag[j] = swap;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const angle = (-2 * Math.PI) / len;
    const wReal = Math.cos(angle);
    const wImag = Math.sin(angle);
    for (let i = 0; i < n; i += len) {
      let curReal = 1;
      let curImag = 0;
      for (let k = 0; k < len / 2; k += 1) {
        const aReal = real[i + k];
        const aImag = imag[i + k];
        const bReal = real[i + k + len / 2] * curReal - imag[i + k + len / 2] * curImag;
        const bImag = real[i + k + len / 2] * curImag + imag[i + k + len / 2] * curReal;
        real[i + k] = aReal + bReal;
        imag[i + k] = aImag + bImag;
        real[i + k + len / 2] = aReal - bReal;
        imag[i + k + len / 2] = aImag - bImag;
        const nextReal = curReal * wReal - curImag * wImag;
        curImag = curReal * wImag + curImag * wReal;
        curReal = nextReal;
      }
    }
  }
};
var TONE_EDGE_HZ = [250, 4e3];
var TONE_MID_HZ = Math.round(Math.sqrt(TONE_EDGE_HZ[0] * TONE_EDGE_HZ[1]));

// ../../../../Work/mine/folia-major/src/services/automix/uiYield.ts
var YIELD_BUDGET_MS = 8;
var paints = typeof document !== 'undefined';
var yieldToUi = () =>
  paints
    ? new Promise((resolve) => {
        setTimeout(resolve, 0);
      })
    : Promise.resolve();

// ../../../../Work/mine/folia-major/src/services/analysis/beatThis.ts
var BEAT_THIS_SAMPLE_RATE = 22050;
var N_FFT = 1024;
var HOP = 441;
var BEAT_THIS_FPS = BEAT_THIS_SAMPLE_RATE / HOP;
var N_MELS = 128;
var F_MIN = 30;
var F_MAX = 11e3;
var LOG_MULTIPLIER = 1e3;
var AMIN = 1e-10;
var hzToMel = (hz) => {
  const fSp = 200 / 3;
  if (hz < 1e3) return hz / fSp;
  return 1e3 / fSp + Math.log(hz / 1e3) / (Math.log(6.4) / 27);
};
var melToHz = (mel) => {
  const fSp = 200 / 3;
  const minLogMel = 1e3 / fSp;
  if (mel < minLogMel) return fSp * mel;
  return 1e3 * Math.exp((Math.log(6.4) / 27) * (mel - minLogMel));
};
var buildFilterbank = () => {
  const bins = N_FFT / 2 + 1;
  const melMin = hzToMel(F_MIN);
  const melMax = hzToMel(F_MAX);
  const corners = Array.from({ length: N_MELS + 2 }, (_, index) =>
    melToHz(melMin + ((melMax - melMin) * index) / (N_MELS + 1)),
  );
  return Array.from({ length: N_MELS }, (_, band) => {
    const row = new Float64Array(bins);
    const [left, centre, right] = [corners[band], corners[band + 1], corners[band + 2]];
    for (let bin = 0; bin < bins; bin += 1) {
      const hz = (bin * BEAT_THIS_SAMPLE_RATE) / N_FFT;
      const rising = centre === left ? 0 : (hz - left) / (centre - left);
      const falling = right === centre ? 0 : (right - hz) / (right - centre);
      row[bin] = Math.max(0, Math.min(rising, falling));
    }
    let start = 0;
    while (start < bins && row[start] === 0) start += 1;
    let end = bins;
    while (end > start && row[end - 1] === 0) end -= 1;
    return { weights: row, start, end };
  });
};
var melBandCentresHz = () => {
  const melMin = hzToMel(F_MIN);
  const melMax = hzToMel(F_MAX);
  return Array.from({ length: N_MELS }, (_, band) =>
    melToHz(melMin + ((melMax - melMin) * (band + 1)) / (N_MELS + 1)),
  );
};
var MEL_BANDS = N_MELS;
var filterbank = null;
var hann = null;
var melSpectrogram = (mono) =>
  __async(null, null, function* () {
    if (!filterbank) filterbank = buildFilterbank();
    if (!hann) {
      hann = new Float64Array(N_FFT);
      for (let index = 0; index < N_FFT; index += 1) {
        hann[index] = 0.5 * (1 - Math.cos((2 * Math.PI * index) / N_FFT));
      }
    }
    const pad = N_FFT / 2;
    const padded = new Float64Array(mono.length + 2 * pad);
    for (let index = 0; index < pad; index += 1) {
      padded[index] = mono[Math.min(pad - index, mono.length - 1)];
      padded[padded.length - 1 - index] = mono[Math.max(0, mono.length - 1 - (pad - index))];
    }
    padded.set(mono, pad);
    const frames = Math.floor((padded.length - N_FFT) / HOP) + 1;
    if (frames <= 0) return { data: new Float32Array(0), frames: 0 };
    const data = new Float32Array(frames * N_MELS);
    const real = new Float64Array(N_FFT);
    const imag = new Float64Array(N_FFT);
    const magnitude = new Float64Array(N_FFT / 2 + 1);
    const scale = 1 / Math.sqrt(N_FFT);
    let yieldBy = performance.now() + YIELD_BUDGET_MS;
    for (let frame = 0; frame < frames; frame += 1) {
      if (performance.now() >= yieldBy) {
        yield yieldToUi();
        yieldBy = performance.now() + YIELD_BUDGET_MS;
      }
      const start = frame * HOP;
      for (let index = 0; index < N_FFT; index += 1) {
        real[index] = padded[start + index] * hann[index];
        imag[index] = 0;
      }
      fft(real, imag);
      for (let bin = 0; bin < magnitude.length; bin += 1) {
        magnitude[bin] = Math.sqrt(real[bin] * real[bin] + imag[bin] * imag[bin]) * scale;
      }
      for (let band = 0; band < N_MELS; band += 1) {
        const { weights, start: from, end: to } = filterbank[band];
        let energy = 0;
        for (let bin = from; bin < to; bin += 1) energy += magnitude[bin] * weights[bin];
        data[frame * N_MELS + band] = Math.log1p(LOG_MULTIPLIER * Math.max(energy, AMIN));
      }
    }
    return { data, frames };
  });
var deduplicate = (peaks, width) => {
  if (!peaks.length) return [];
  const merged = [];
  let mean = peaks[0];
  let count = 1;
  for (let index = 1; index < peaks.length; index += 1) {
    const peak = peaks[index];
    if (peak - mean <= width) {
      count += 1;
      mean += (peak - mean) / count;
    } else {
      merged.push(Math.round(mean));
      mean = peak;
      count = 1;
    }
  }
  merged.push(Math.round(mean));
  return merged;
};
var beatsFromLogits = (beatLogits, downbeatLogits, fps = BEAT_THIS_FPS) => {
  const pick = (logits) => {
    const peaks = [];
    for (let index = 0; index < logits.length; index += 1) {
      const value = logits[index];
      if (!(value > 0)) continue;
      let isPeak = true;
      for (let offset = -3; offset <= 3 && isPeak; offset += 1) {
        const other = index + offset;
        if (other >= 0 && other < logits.length && logits[other] > value) isPeak = false;
      }
      if (isPeak) peaks.push(index);
    }
    return deduplicate(peaks, 1);
  };
  const beats = pick(beatLogits).map((frame) => frame / fps);
  const downbeats = pick(downbeatLogits).map((frame) => frame / fps);
  if (!beats.length) return { beats, downbeats: [] };
  const snapped = downbeats.map((time) => {
    let best = beats[0];
    for (const beat of beats) {
      if (Math.abs(beat - time) < Math.abs(best - time)) best = beat;
    }
    return best;
  });
  return { beats, downbeats: [...new Set(snapped)].sort((a, b) => a - b) };
};
var MODEL_CHUNK_FRAMES = 1500;
var BORDER_FRAMES = 6;
var chunkStarts = (frames) => {
  const stride = MODEL_CHUNK_FRAMES - 2 * BORDER_FRAMES;
  const starts = [];
  for (let start = -BORDER_FRAMES; start < frames - BORDER_FRAMES; start += stride) {
    starts.push(start);
  }
  if (!starts.length) return [-BORDER_FRAMES];
  if (frames > stride) starts[starts.length - 1] = frames - (MODEL_CHUNK_FRAMES - BORDER_FRAMES);
  return starts;
};
var splitForModel = (mel) => {
  if (!mel.frames) return [];
  return chunkStarts(mel.frames).map((start) => {
    const from = Math.max(0, start);
    const to = Math.min(start + MODEL_CHUNK_FRAMES, mel.frames);
    const leftPad = Math.max(0, -start);
    const rightPad = Math.max(0, Math.min(BORDER_FRAMES, start + MODEL_CHUNK_FRAMES - mel.frames));
    const frames = leftPad + Math.max(0, to - from) + rightPad;
    const data = new Float32Array(frames * 128);
    if (to > from) data.set(mel.data.subarray(from * 128, to * 128), leftPad * 128);
    return { data, frames, start };
  });
};
var aggregateChunks = (predictions, frames) => {
  const beat = new Float32Array(frames).fill(-1e3);
  const downbeat = new Float32Array(frames).fill(-1e3);
  for (let index = predictions.length - 1; index >= 0; index -= 1) {
    const chunk = predictions[index];
    const wide = chunk.frames >= 2 * BORDER_FRAMES;
    const from = wide ? BORDER_FRAMES : 0;
    const to = wide ? chunk.frames - BORDER_FRAMES : chunk.frames;
    for (let offset = from; offset < to; offset += 1) {
      const target = chunk.start + offset;
      if (target < 0 || target >= frames) continue;
      beat[target] = chunk.beat[offset];
      downbeat[target] = chunk.downbeat[offset];
    }
  }
  return { beat, downbeat };
};

// ../../../../Work/mine/folia-major/src/services/analysis/beatAccents.ts
var BEAT_ACCENTS_VERSION = 10;
var LOW_MAX_HZ = 150;
var MID_MAX_HZ = 2e3;
var PEAK_BEFORE = 1;
var PEAK_AFTER = 2;
var BASE_FROM = 6;
var BASE_TO = 2;
var IMPACT_WEIGHTS = { low: 0.5, mid: 0.3, high: 0.2 };
var LOUDNESS_HALF = 12;
var IMPACT_RISE_SHARE = 0.4;
var NORMALISE_LOW_PERCENTILE = 0.1;
var NORMALISE_HIGH_PERCENTILE = 0.9;
var IMPACT_CURVE = 0.7;
var MIN_BEATS = 8;
var TRACK_SCALE_MIN = 0.45;
var TRACK_SCALE_RISE_LOW = 0.1;
var TRACK_SCALE_RISE_HIGH = 0.35;
var PERCUSSIVE_GATE = 0.45;
var MIN_BARS = 4;
var FALLBACK_BAR_BEATS = 4;
var PULSE_GROUPS = [
  { size: 3, ratio: 1.2, riseRatio: 1.2, minBpm: 120 },
  // 0: the rise is not asked (夢灯籠's head rises 0.83 against 0.93 / 0.94, its logit 1.0 above).
  { size: 2, ratio: 0, riseRatio: 1.5, minBpm: 90 },
];
var WHOLE_BAR_GROUP = { size: 3, ratio: 1.2, riseRatio: 1.5, minBpm: 90 };
var MIN_PULSE_SPACING = 0.7;
var MIN_GATED_SHARE = 0.15;
var UNSURE_SHARE = 0.55;
var DRIVEN_TRACK_SHARE = 0.3;
var DRIVEN_OFFBEAT_SHARE = 0.6;
var DRIVEN_HALF = 8;
var UNSURE_HALF = 4;
var UNSURE_RUN = 8;
var PULSE_CONFIDENCE_GAP = 0.7;
var MIN_PULSE_BARS = 8;
var CONFIDENCE_HALF = 2;
var bandOf = null;
var resolveBandOf = () => {
  if (bandOf) return bandOf;
  bandOf = Uint8Array.from(melBandCentresHz(), (hz) =>
    hz < LOW_MAX_HZ ? 0 : hz < MID_MAX_HZ ? 1 : 2,
  );
  return bandOf;
};
var splitBands = (mel) => {
  const of = resolveBandOf();
  const counts = [0, 0, 0];
  of.forEach((band) => {
    counts[band] += 1;
  });
  const tracks = {
    low: new Float32Array(mel.frames),
    mid: new Float32Array(mel.frames),
    high: new Float32Array(mel.frames),
    all: new Float32Array(mel.frames),
  };
  const rows = [tracks.low, tracks.mid, tracks.high];
  for (let frame = 0; frame < mel.frames; frame += 1) {
    const offset = frame * MEL_BANDS;
    let low = 0;
    let mid = 0;
    let high = 0;
    for (let band = 0; band < MEL_BANDS; band += 1) {
      const value = mel.data[offset + band];
      const which = of[band];
      if (which === 0) low += value;
      else if (which === 1) mid += value;
      else high += value;
    }
    rows[0][frame] = counts[0] ? low / counts[0] : 0;
    rows[1][frame] = counts[1] ? mid / counts[1] : 0;
    rows[2][frame] = counts[2] ? high / counts[2] : 0;
    tracks.all[frame] = (low + mid + high) / MEL_BANDS;
  }
  return tracks;
};
var riseAt = (track, frame) => {
  const last = track.length - 1;
  if (last < 0) return 0;
  let peak = Number.NEGATIVE_INFINITY;
  for (let at = frame - PEAK_BEFORE; at <= frame + PEAK_AFTER; at += 1) {
    peak = Math.max(peak, track[Math.min(last, Math.max(0, at))]);
  }
  let base = 0;
  for (let at = frame - BASE_FROM; at <= frame - BASE_TO; at += 1)
    base += track[Math.min(last, Math.max(0, at))];
  base /= BASE_FROM - BASE_TO + 1;
  return Math.max(0, peak - base);
};
var loudnessAt = (track, frame) => {
  const last = track.length - 1;
  if (last < 0) return 0;
  let sum = 0;
  for (let at = frame - LOUDNESS_HALF; at <= frame + LOUDNESS_HALF; at += 1)
    sum += track[Math.min(last, Math.max(0, at))];
  return sum / (2 * LOUDNESS_HALF + 1);
};
var percentile = (sorted, p) => {
  if (!sorted.length) return 0;
  const at = Math.min(sorted.length - 1, Math.max(0, Math.round(p * (sorted.length - 1))));
  return sorted[at];
};
var normaliser = (sorted) => {
  const floor = percentile(sorted, NORMALISE_LOW_PERCENTILE);
  const ceiling = percentile(sorted, NORMALISE_HIGH_PERCENTILE);
  if (!(ceiling - floor > 1e-6)) return null;
  return (value) => Math.min(1, Math.max(0, (value - floor) / (ceiling - floor)));
};
var median = (values) =>
  percentile(
    [...values].sort((a, b) => a - b),
    0.5,
  );
var round3 = (value) => Math.round(value * 1e3) / 1e3;
var resolveBarStarts = (beats, downbeats) => {
  const downbeatSet = new Set(downbeats);
  const starts = [];
  if (downbeats.length >= 2) {
    beats.forEach((time, index) => {
      if (index === 0 || downbeatSet.has(time)) starts.push(index);
    });
    return starts;
  }
  for (let index = 0; index < beats.length; index += FALLBACK_BAR_BEATS) starts.push(index);
  return starts;
};
var resolveTrackPercussiveShare = (energy) => {
  if (!energy) return null;
  let hit = 0;
  let all = 0;
  for (let frame = 0; frame < energy.total.length; frame += 1) {
    hit += energy.percussive[frame];
    all += energy.total[frame];
  }
  return all > 0 ? hit / all : null;
};
var resolveBarPercussiveShares = (energy, beats, downbeats) => {
  const starts = resolveBarStarts(beats, downbeats);
  if (starts.length < MIN_BARS) return { starts, shares: [] };
  const { percussive, total } = energy;
  const stepOf = (time) => Math.min(total.length, Math.max(0, Math.floor(time * BEAT_THIS_FPS)));
  const lastGap = beats.length > 1 ? beats[beats.length - 1] - beats[beats.length - 2] : 0.5;
  const shares = starts.map((first, bar) => {
    const from = stepOf(beats[first]);
    const until =
      bar + 1 < starts.length
        ? stepOf(beats[starts[bar + 1]])
        : stepOf(beats[beats.length - 1] + lastGap);
    let hit = 0;
    let all = 0;
    for (let frame = from; frame < until; frame += 1) {
      hit += percussive[frame];
      all += total[frame];
    }
    return all > 0 ? hit / all : 0;
  });
  return { starts, shares };
};
var resolveRhythmicBeats = (energy, beats, downbeats) => {
  const rhythmic = beats.map(() => 1);
  if (!energy) return rhythmic;
  const { starts, shares } = resolveBarPercussiveShares(energy, beats, downbeats);
  if (!shares.length) return rhythmic;
  const usual = median(shares);
  if (usual < MIN_GATED_SHARE) return rhythmic;
  const threshold = usual * PERCUSSIVE_GATE;
  starts.forEach((first, bar) => {
    if (shares[bar] >= threshold) return;
    const end = bar + 1 < starts.length ? starts[bar + 1] : beats.length;
    for (let index = first; index < end; index += 1) rhythmic[index] = 0;
  });
  return rhythmic;
};
var resolveBeatConfidence = (logits, beats) =>
  beats.map((time) => {
    const frame = Math.round(time * BEAT_THIS_FPS);
    let peak = Number.NEGATIVE_INFINITY;
    for (let at = frame - CONFIDENCE_HALF; at <= frame + CONFIDENCE_HALF; at += 1) {
      if (at >= 0 && at < logits.length) peak = Math.max(peak, logits[at]);
    }
    return Number.isFinite(peak) ? round3(peak) : 0;
  });
var resolveDownbeatStarts = (beats, downbeats) => {
  const downbeatSet = new Set(downbeats);
  const starts = [];
  beats.forEach((time, index) => {
    if (downbeatSet.has(time)) starts.push(index);
  });
  return starts;
};
var readBeatMeter = (rises, beats, downbeats, confidence = null) => {
  var _a;
  const gaps = beats.slice(1).map((time, index) => time - beats[index]);
  const gap = median(gaps);
  const meter = {
    bpm: gap > 0 ? Math.round(60 / gap) : 0,
    barLengths: [],
    usual: 0,
    byPosition: [],
    confidenceByPosition: [],
    group: 1,
    unsure: 0,
    driven: 0,
  };
  const starts = resolveDownbeatStarts(beats, downbeats);
  if (starts.length < 2) return meter;
  const counts = /* @__PURE__ */ new Map();
  for (let bar = 0; bar + 1 < starts.length; bar += 1) {
    const length = starts[bar + 1] - starts[bar];
    counts.set(length, ((_a = counts.get(length)) != null ? _a : 0) + 1);
  }
  meter.barLengths = [...counts].sort((a, b) => b[1] - a[1]);
  const [usual, bars] = meter.barLengths[0];
  meter.usual = usual;
  meter.byPosition = Array.from({ length: usual }, () => 0);
  const logits = Array.from({ length: usual }, () => []);
  for (let bar = 0; bar + 1 < starts.length; bar += 1) {
    if (starts[bar + 1] - starts[bar] !== usual) continue;
    for (let position = 0; position < usual; position += 1) {
      meter.byPosition[position] += rises[starts[bar] + position] / bars;
      if (confidence) logits[position].push(confidence[starts[bar] + position]);
    }
  }
  if (confidence) meter.confidenceByPosition = logits.map(median);
  if (bars < MIN_PULSE_BARS) return meter;
  const { byPosition, confidenceByPosition } = meter;
  const standsOut = (head, needed) => {
    const before = (head + usual - 1) % usual;
    const after = (head + 1) % usual;
    if (
      needed > 0 &&
      !(byPosition[head] > needed * Math.max(byPosition[before], byPosition[after]))
    )
      return false;
    return (
      !confidence ||
      confidenceByPosition[head] -
        Math.max(confidenceByPosition[before], confidenceByPosition[after]) >
        PULSE_CONFIDENCE_GAP
    );
  };
  const found = [...PULSE_GROUPS, WHOLE_BAR_GROUP].find(({ size, ratio, riseRatio, minBpm }) => {
    if (usual % size !== 0 || meter.bpm < minBpm) return false;
    const needed = confidence ? ratio : riseRatio;
    if (usual === size) return standsOut(0, needed);
    for (let head = size; head < usual; head += size) {
      if (!standsOut(head, needed)) return false;
    }
    return true;
  });
  if (found) meter.group = found.size;
  return meter;
};
var resolveGroupHeads = (meter, beats, downbeats) => {
  const pulse = beats.map(() => 1);
  const { group } = meter;
  if (group === 1) return pulse;
  const starts = resolveDownbeatStarts(beats, downbeats);
  for (let index = 0; index < starts[0]; index += 1)
    pulse[index] = (starts[0] - index) % group === 0 ? 1 : 0;
  starts.forEach((start, bar) => {
    const end = bar + 1 < starts.length ? starts[bar + 1] : beats.length;
    for (let index = start; index < end; index += 1)
      pulse[index] = (index - start) % group === 0 ? 1 : 0;
  });
  return pulse;
};
var resolvePulseFromMeter = (meter, beats, downbeats) => {
  const pulse = resolveGroupHeads(meter, beats, downbeats);
  const gap = median(beats.slice(1).map((time, index) => time - beats[index]));
  if (!(gap > 0) || beats.length < MIN_BEATS) return pulse;
  const spacing = MIN_PULSE_SPACING * gap * meter.group;
  const downbeatSet = new Set(downbeats);
  let last = -1;
  beats.forEach((time, index) => {
    if (!pulse[index]) return;
    if (last < 0 || time - beats[last] >= spacing) {
      last = index;
      return;
    }
    if (downbeatSet.has(time) && !downbeatSet.has(beats[last])) {
      pulse[last] = 0;
      last = index;
    } else {
      pulse[index] = 0;
    }
  });
  return pulse;
};
var resolveUnsureBeats = (confidence, pulse) => {
  const unsure = pulse.map(() => 0);
  if (!confidence) return unsure;
  const felt = pulse.map((_, index) => index).filter((index) => pulse[index]);
  if (felt.length < UNSURE_RUN) return unsure;
  const logits = felt.map((index) => confidence[index]);
  const sorted = [...logits].sort((a, b) => a - b);
  const floor = UNSURE_SHARE * percentile(sorted, 0.75);
  const low = logits.map((_, at) => {
    const around = logits.slice(Math.max(0, at - UNSURE_HALF), at + UNSURE_HALF + 1);
    return around.reduce((sum, value) => sum + value, 0) / around.length < floor;
  });
  let start = -1;
  for (let at = 0; at <= low.length; at += 1) {
    if (at < low.length && low[at]) {
      if (start < 0) start = at;
      continue;
    }
    if (start >= 0 && at - start >= UNSURE_RUN) {
      for (let index = felt[start]; index <= felt[at - 1]; index += 1) unsure[index] = 1;
    }
    start = -1;
  }
  return unsure;
};
var restoreDrivenBeats = (pulse, group, confidence, percussiveShare, unsure = []) => {
  if (
    group !== 2 ||
    !confidence ||
    percussiveShare === null ||
    percussiveShare < DRIVEN_TRACK_SHARE
  )
    return 0;
  const felt = pulse.map((_, index) => index).filter((index) => pulse[index]);
  const groups = felt
    .slice(0, -1)
    .map((head, at) => {
      const between = [];
      for (let index = head + 1; index < felt[at + 1]; index += 1) between.push(index);
      return { head, between };
    })
    .filter(({ head }) => !unsure[head]);
  const mean = (values) => values.reduce((sum, value) => sum + value, 0) / values.length;
  const driven = groups.map((_, at) => {
    const around = groups.slice(Math.max(0, at - DRIVEN_HALF), at + DRIVEN_HALF + 1);
    const offbeats = around.flatMap((entry) => entry.between.map((index) => confidence[index]));
    if (!offbeats.length) return false;
    const heads = mean(around.map((entry) => confidence[entry.head]));
    return heads > 0 && mean(offbeats) >= DRIVEN_OFFBEAT_SHARE * heads;
  });
  let restored = 0;
  groups.forEach(({ between }, at) => {
    if (!driven[at]) return;
    between.forEach((index) => {
      pulse[index] = 1;
      restored += 1;
    });
  });
  return restored;
};
var measure = (tracks, time) => {
  const frame = Math.round(time * BEAT_THIS_FPS);
  const low = riseAt(tracks.low, frame);
  const mid = riseAt(tracks.mid, frame);
  const high = riseAt(tracks.high, frame);
  return {
    low,
    mid,
    high,
    raw: low * IMPACT_WEIGHTS.low + mid * IMPACT_WEIGHTS.mid + high * IMPACT_WEIGHTS.high,
    loudness: loudnessAt(tracks.all, frame),
  };
};
var resolveTrackScale = (medianBeatRise) => {
  if (!(medianBeatRise > 0)) return TRACK_SCALE_MIN;
  const amount =
    (medianBeatRise - TRACK_SCALE_RISE_LOW) / (TRACK_SCALE_RISE_HIGH - TRACK_SCALE_RISE_LOW);
  return round3(TRACK_SCALE_MIN + (1 - TRACK_SCALE_MIN) * Math.min(1, Math.max(0, amount)));
};
var computeBeatAccents = (
  mel,
  beats,
  downbeats = [],
  energy = null,
  { confidence = null, onMeter } = {},
) => {
  const tracks = splitBands(mel);
  const rises = beats.map((time) => measure(tracks, time));
  const meter = readBeatMeter(
    rises.map((entry) => entry.raw),
    beats,
    downbeats,
    confidence,
  );
  const accents = {
    v: BEAT_ACCENTS_VERSION,
    impact: [],
    low: [],
    mid: [],
    high: [],
    rhythmic: resolveRhythmicBeats(energy, beats, downbeats),
    pulse: resolvePulseFromMeter(meter, beats, downbeats),
    trackScale: 1,
  };
  const sortedRaw = rises.map((rise2) => rise2.raw).sort((a, b) => a - b);
  const rise = normaliser(sortedRaw);
  const loudness = normaliser(rises.map((entry) => entry.loudness).sort((a, b) => a - b));
  const flat = beats.length < MIN_BEATS || (!rise && !loudness);
  const riseShare = !loudness ? 1 : !rise ? 0 : IMPACT_RISE_SHARE;
  rises.forEach((entry) => {
    const mixed =
      riseShare * (rise ? rise(entry.raw) : 0) +
      (1 - riseShare) * (loudness ? loudness(entry.loudness) : 0);
    accents.impact.push(flat ? 1 : round3(mixed ** IMPACT_CURVE));
    const { low, mid, high } = entry;
    const sum = low + mid + high;
    accents.low.push(sum > 0 ? round3(low / sum) : round3(1 / 3));
    accents.mid.push(sum > 0 ? round3(mid / sum) : round3(1 / 3));
    accents.high.push(sum > 0 ? round3(high / sum) : round3(1 / 3));
  });
  if (beats.length >= MIN_BEATS) accents.trackScale = resolveTrackScale(median(sortedRaw));
  const unsure = resolveUnsureBeats(confidence, accents.pulse);
  unsure.forEach((value, index) => {
    if (value) accents.rhythmic[index] = 0;
  });
  meter.unsure = unsure.filter(Boolean).length;
  meter.driven = restoreDrivenBeats(
    accents.pulse,
    meter.group,
    confidence,
    resolveTrackPercussiveShare(energy),
    unsure,
  );
  onMeter == null ? void 0 : onMeter(meter, accents.pulse);
  return accents;
};

// ../../../../Work/mine/folia-major/src/services/analysis/drumHits.ts
var PEAK_BEFORE2 = 1;
var PEAK_AFTER2 = 2;
var BASE_FROM2 = 6;
var BASE_TO2 = 2;
var HIT_TOP = 0.99;
var HIT_ONSET_FLOOR = 0.15;
var PEAK_HALF = 3;
var MIN_HIT_GAP = 0.09;
var KICK_MAX_HZ = 150;
var SNARE_MAX_HZ = 4e3;
var HAT_MIN_HZ = 6e3;
var ENERGY_HOP = 441;
var SHARE_WINDOW = 25;
var SHARE_AUDIBLE = 0.05;
var DRUMS_PLAYING_SHARE = 0.03;
var liveTailFrames = () => PEAK_AFTER2 + PEAK_HALF + Math.ceil(MIN_HIT_GAP * BEAT_THIS_FPS);
var quantile = (values, q) => {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
};
var bandTrack = (mel, fromHz, toHz) => {
  const bands = melBandCentresHz()
    .map((hz, band) => (hz >= fromHz && hz < toHz ? band : -1))
    .filter((band) => band >= 0);
  const out = new Float32Array(mel.frames);
  for (let frame = 0; frame < mel.frames; frame += 1) {
    let sum = 0;
    for (const band of bands) sum += mel.data[frame * MEL_BANDS + band];
    out[frame] = bands.length ? sum / bands.length : 0;
  }
  return out;
};
var riseAt2 = (track, frame) => {
  let peak = 0;
  for (let at = frame - PEAK_BEFORE2; at <= frame + PEAK_AFTER2; at += 1) {
    if (at >= 0 && at < track.length) peak = Math.max(peak, track[at]);
  }
  let base = 0;
  let count = 0;
  for (let at = frame - BASE_FROM2; at <= frame - BASE_TO2; at += 1) {
    if (at >= 0 && at < track.length) {
      base += track[at];
      count += 1;
    }
  }
  return Math.max(0, peak - (count ? base / count : 0));
};
var riseShares = (track) => {
  const rise = new Float32Array(track.length);
  for (let frame = 0; frame < track.length; frame += 1) rise[frame] = riseAt2(track, frame);
  const loud = rise.filter((value) => value > 0).sort();
  const top =
    (loud.length ? loud[Math.min(loud.length - 1, Math.floor(HIT_TOP * loud.length))] : 0) || 1;
  return rise.map((value) => Math.min(1, value / top));
};
var drumBandTracks = (mel) => [
  bandTrack(mel, 0, KICK_MAX_HZ),
  bandTrack(mel, KICK_MAX_HZ, SNARE_MAX_HZ),
  bandTrack(mel, HAT_MIN_HZ, Infinity),
];
var pickBandHits = (tracks, frames) => {
  const bands = tracks.map((track) => riseShares(track.subarray(0, frames)));
  const top = new Float32Array(frames);
  const kindOf = new Uint8Array(frames);
  for (let frame = 0; frame < frames; frame += 1) {
    for (let kind = 0; kind < bands.length; kind += 1) {
      if (bands[kind][frame] > top[frame]) {
        top[frame] = bands[kind][frame];
        kindOf[frame] = kind;
      }
    }
  }
  const times = [];
  const strengths = [];
  const kinds = [];
  for (let frame = 0; frame < frames; frame += 1) {
    const share = top[frame];
    if (share < HIT_ONSET_FLOOR) continue;
    let peak = true;
    for (
      let at = Math.max(0, frame - PEAK_HALF);
      at <= Math.min(frames - 1, frame + PEAK_HALF) && peak;
      at += 1
    ) {
      if (top[at] > share || (top[at] === share && at < frame)) peak = false;
    }
    if (!peak) continue;
    const time = frame / BEAT_THIS_FPS;
    const last = times.length - 1;
    if (last >= 0 && time - times[last] < MIN_HIT_GAP) {
      if (share > strengths[last]) {
        times[last] = time;
        strengths[last] = share;
        kinds[last] = kindOf[frame];
      }
      continue;
    }
    times.push(time);
    strengths.push(share);
    kinds.push(kindOf[frame]);
  }
  return {
    times: Float64Array.from(times),
    strengths: Float32Array.from(strengths),
    kinds: Uint8Array.from(kinds),
  };
};
var energyFrames = (signal) => {
  const out = new Float32Array(Math.ceil(signal.length / ENERGY_HOP));
  for (let at = 0; at < signal.length; at += 1)
    out[Math.floor(at / ENERGY_HOP)] += signal[at] * signal[at];
  return out;
};
var drumCoverage = (drumEnergy, mixEnergy) => {
  var _a;
  const windows = [];
  for (let from = 0; from < mixEnergy.length; from += SHARE_WINDOW) {
    let drum = 0;
    let mix = 0;
    for (let at = from; at < Math.min(mixEnergy.length, from + SHARE_WINDOW); at += 1) {
      drum += (_a = drumEnergy[at]) != null ? _a : 0;
      mix += mixEnergy[at];
    }
    windows.push({ drum, mix });
  }
  const floor =
    quantile(
      windows.map((window2) => window2.mix),
      0.5,
    ) * SHARE_AUDIBLE;
  const audible = windows.filter((window2) => window2.mix > floor && window2.mix > 0);
  if (!audible.length) return 0;
  return (
    audible.filter((window2) => window2.drum >= DRUMS_PLAYING_SHARE * window2.mix).length /
    audible.length
  );
};

// ../../../../Work/mine/folia-major/src/services/analysis/drumHitStream.ts
var STEM_RATE = 44100;
var MARGIN_FRAMES = 4;
var createDrumBandStream = (total, steps, from) => {
  const frameSamples = STEM_RATE / BEAT_THIS_FPS;
  const frames = Math.floor(Math.ceil(total / 2) / (frameSamples / 2)) + 1;
  const bands = [new Float32Array(frames), new Float32Array(frames), new Float32Array(frames)];
  let done = 0;
  if (from) {
    if (
      from.done > frames ||
      from.bands.some((band) => band.length < from.done || band.length > frames)
    ) {
      throw new Error(
        `drum band stream: cannot carry on from ${from.done} frames of ${from.bands.map((band) => band.length).join('/')} into ${frames}`,
      );
    }
    from.bands.forEach((band, index) => bands[index].set(band));
    done = from.done;
  }
  return {
    frames,
    bands,
    get done() {
      return done;
    },
    advance: (drums, final, end) =>
      __async(null, null, function* () {
        var _a;
        const target = end
          ? frames
          : Math.min(frames, Math.floor(final / frameSamples) - MARGIN_FRAMES + 1);
        if (target <= done) return;
        const first = Math.max(0, done - MARGIN_FRAMES);
        const from2 = first * frameSamples;
        const to = end ? total : (target - 1 + MARGIN_FRAMES) * frameSamples;
        const read = steps.bands(yield steps.mel(yield steps.resample(drums.subarray(from2, to))));
        for (let frame = done; frame < target; frame += 1) {
          const at = frame - first;
          for (let band = 0; band < 3; band += 1)
            bands[band][frame] = (_a = read[band][at]) != null ? _a : 0;
        }
        done = target;
      }),
  };
};

// ../../../../Work/mine/folia-major/src/services/analysis/stemRange.ts
var HTDEMUCS_GPU_SEGMENT = 172032;
var stemSegmentStarts = (total, segment = HTDEMUCS_GPU_SEGMENT) => {
  const stride = segment - Math.floor(segment / 4);
  const count = Math.max(1, Math.ceil(total / stride));
  const starts = [];
  for (let c = 0; c < count; c += 1) {
    const start = c * stride;
    if (start >= total && c > 0) break;
    starts.push(start);
  }
  return starts;
};
var stemSegmentWindow = (segment) => {
  const overlap = Math.floor(segment / 4);
  const win = new Float32Array(segment).fill(1);
  for (let i = 0; i < overlap; i += 1) {
    const ramp = i / (overlap - 1);
    win[i] = ramp;
    win[segment - 1 - i] = ramp;
  }
  return win;
};
export {
  aggregateChunks,
  beatsFromLogits,
  computeBeatAccents,
  createDrumBandStream,
  drumBandTracks,
  drumCoverage,
  energyFrames,
  liveTailFrames,
  melSpectrogram,
  pickBandHits,
  resolveBeatConfidence,
  splitForModel,
  stemSegmentStarts,
  stemSegmentWindow,
};
