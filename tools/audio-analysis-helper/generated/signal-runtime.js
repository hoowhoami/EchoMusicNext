var __generator =
  (this && this.__generator) ||
  function (thisArg, body) {
    var f, y, t, g;
    var _ = {
      label: 0,
      sent: function () {
        if (t[0] & 1) throw t[1];
        return t[1];
      },
      trys: [],
      ops: [],
    };
    g = Object.create((typeof Iterator === 'function' ? Iterator : Object).prototype);
    return (
      (g.next = verb(0)),
      (g['throw'] = verb(1)),
      (g['return'] = verb(2)),
      typeof Symbol === 'function' &&
        (g[Symbol.iterator] = function () {
          return this;
        }),
      g
    );
    function verb(n) {
      return function (v) {
        return step([n, v]);
      };
    }
    function step(op) {
      if (f) throw new TypeError('Generator is already executing.');
      while ((g && ((g = 0), op[0] && (_ = 0)), _))
        try {
          if (
            ((f = 1),
            y &&
              (t =
                op[0] & 2
                  ? y['return']
                  : op[0]
                    ? y['throw'] || ((t = y['return']) && t.call(y), 0)
                    : y.next) &&
              !(t = t.call(y, op[1])).done)
          )
            return t;
          if (((y = 0), t)) op = [op[0] & 2, t.value];
          switch (op[0]) {
            case 0:
            case 1:
              t = op;
              break;
            case 4:
              _.label++;
              return { value: op[1], done: false };
            case 5:
              _.label++;
              y = op[1];
              op = [0];
              continue;
            case 7:
              op = _.ops.pop();
              _.trys.pop();
              continue;
            default:
              if (
                !((t = _.trys), (t = t.length > 0 && t[t.length - 1])) &&
                (op[0] === 6 || op[0] === 2)
              ) {
                _ = 0;
                continue;
              }
              if (op[0] === 3 && (!t || (op[1] > t[0] && op[1] < t[3]))) {
                _.label = op[1];
                break;
              }
              if (op[0] === 6 && _.label < t[1]) {
                _.label = t[1];
                t = op;
                break;
              }
              if (t && _.label < t[2]) {
                _.label = t[2];
                _.ops.push(op);
                break;
              }
              if (t[2]) _.ops.pop();
              _.trys.pop();
              continue;
          }
          op = body.call(thisArg, _);
        } catch (e) {
          op = [6, e];
          y = 0;
        } finally {
          f = t = 0;
        }
      if (op[0] & 5) throw op[1];
      return { value: op[0] ? op[1] : void 0, done: true };
    }
  };
var __values =
  (this && this.__values) ||
  function (o) {
    var s = typeof Symbol === 'function' && Symbol.iterator,
      m = s && o[s],
      i = 0;
    if (m) return m.call(o);
    if (o && typeof o.length === 'number')
      return {
        next: function () {
          if (o && i >= o.length) o = void 0;
          return { value: o && o[i++], done: !o };
        },
      };
    throw new TypeError(s ? 'Object is not iterable.' : 'Symbol.iterator is not defined.');
  };
var __read =
  (this && this.__read) ||
  function (o, n) {
    var m = typeof Symbol === 'function' && o[Symbol.iterator];
    if (!m) return o;
    var i = m.call(o),
      r,
      ar = [],
      e;
    try {
      while ((n === void 0 || n-- > 0) && !(r = i.next()).done) ar.push(r.value);
    } catch (error) {
      e = { error: error };
    } finally {
      try {
        if (r && !r.done && (m = i['return'])) m.call(i);
      } finally {
        if (e) throw e.error;
      }
    }
    return ar;
  };
var __spreadArray =
  (this && this.__spreadArray) ||
  function (to, from, pack) {
    if (pack || arguments.length === 2)
      for (var i = 0, l = from.length, ar; i < l; i++) {
        if (ar || !(i in from)) {
          if (!ar) ar = Array.prototype.slice.call(from, 0, i);
          ar[i] = from[i];
        }
      }
    return to.concat(ar || Array.prototype.slice.call(from));
  };
var AnalysisSignal = (function () {
  var e_1, _b;
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = function (target, all) {
    for (var name in all) __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = function (to, from, except, desc) {
    var e_2, _b;
    if ((from && typeof from === 'object') || typeof from === 'function') {
      var _loop_1 = function (key) {
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, {
            get: function () {
              return from[key];
            },
            enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable,
          });
      };
      try {
        for (var _c = __values(__getOwnPropNames(from)), _d = _c.next(); !_d.done; _d = _c.next()) {
          var key = _d.value;
          _loop_1(key);
        }
      } catch (e_2_1) {
        e_2 = { error: e_2_1 };
      } finally {
        try {
          if (_d && !_d.done && (_b = _c.return)) _b.call(_c);
        } finally {
          if (e_2) throw e_2.error;
        }
      }
    }
    return to;
  };
  var __toCommonJS = function (mod) {
    return __copyProps(__defProp({}, '__esModule', { value: true }), mod);
  };
  var __async = function (__this, __arguments, generator) {
    return new Promise(function (resolve, reject) {
      var step;
      var fulfilled = function (value) {
        try {
          step(generator.next(value));
        } catch (e) {
          reject(e);
        }
      };
      var rejected = function (value) {
        try {
          step(generator.throw(value));
        } catch (e) {
          reject(e);
        }
      };
      step = function (x) {
        return x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
      };
      step((generator = generator.apply(__this, __arguments)).next());
    });
  };
  // tools/audio-analysis-helper/signal-job.mjs
  var signal_job_exports = {};
  __export(signal_job_exports, {
    analyseBeats: function () {
      return analyseBeats;
    },
    analyseDrums: function () {
      return analyseDrums;
    },
    decodeDrumResume: function () {
      return decodeDrumResume;
    },
    sourceHalfRate: function () {
      return sourceHalfRate;
    },
  });
  try {
    // tools/audio-analysis-helper/typed-array-compat.mjs
    for (
      var _c = __values([Float32Array, Float64Array]), _d = _c.next();
      !_d.done;
      _d = _c.next()
    ) {
      var Constructor = _d.value;
      if (typeof Constructor.prototype.sort === 'function') continue;
      Object.defineProperty(Constructor.prototype, 'sort', {
        configurable: true,
        writable: true,
        value: function (compare) {
          var values = Array.from(this);
          var numeric = function (a, b) {
            if (Number.isNaN(a)) return Number.isNaN(b) ? 0 : 1;
            if (Number.isNaN(b)) return -1;
            if (a === 0 && b === 0) return 1 / a < 1 / b ? -1 : 1 / a > 1 / b ? 1 : 0;
            return a - b;
          };
          values.sort(compare === void 0 ? numeric : compare);
          this.set(values);
          return this;
        },
      });
    }
  } catch (e_1_1) {
    e_1 = { error: e_1_1 };
  } finally {
    try {
      if (_d && !_d.done && (_b = _c.return)) _b.call(_c);
    } finally {
      if (e_1) throw e_1.error;
    }
  }
  // tools/audio-analysis-helper/generated/source-signal.mjs
  var __async2 = function (__this, __arguments, generator) {
    return new Promise(function (resolve, reject) {
      var step;
      var fulfilled = function (value) {
        try {
          step(generator.next(value));
        } catch (e) {
          reject(e);
        }
      };
      var rejected = function (value) {
        try {
          step(generator.throw(value));
        } catch (e) {
          reject(e);
        }
      };
      step = function (x) {
        return x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected);
      };
      step((generator = generator.apply(__this, __arguments)).next());
    });
  };
  var bitReversal = /* @__PURE__ */ new Map();
  var bitReversalOf = function (n) {
    var table = bitReversal.get(n);
    if (table) return table;
    table = new Int32Array(n);
    for (var i = 1, j = 0; i < n; i += 1) {
      var bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      table[i] = j;
    }
    bitReversal.set(n, table);
    return table;
  };
  var fft = function (real, imag) {
    var n = real.length;
    var reversed = bitReversalOf(n);
    for (var i = 1; i < n; i += 1) {
      var j = reversed[i];
      if (i < j) {
        var swap = real[i];
        real[i] = real[j];
        real[j] = swap;
        swap = imag[i];
        imag[i] = imag[j];
        imag[j] = swap;
      }
    }
    for (var len = 2; len <= n; len <<= 1) {
      var angle = (-2 * Math.PI) / len;
      var wReal = Math.cos(angle);
      var wImag = Math.sin(angle);
      for (var i = 0; i < n; i += len) {
        var curReal = 1;
        var curImag = 0;
        for (var k = 0; k < len / 2; k += 1) {
          var aReal = real[i + k];
          var aImag = imag[i + k];
          var bReal = real[i + k + len / 2] * curReal - imag[i + k + len / 2] * curImag;
          var bImag = real[i + k + len / 2] * curImag + imag[i + k + len / 2] * curReal;
          real[i + k] = aReal + bReal;
          imag[i + k] = aImag + bImag;
          real[i + k + len / 2] = aReal - bReal;
          imag[i + k + len / 2] = aImag - bImag;
          var nextReal = curReal * wReal - curImag * wImag;
          curImag = curReal * wImag + curImag * wReal;
          curReal = nextReal;
        }
      }
    }
  };
  var TONE_EDGE_HZ = [250, 4e3];
  var TONE_MID_HZ = Math.round(Math.sqrt(TONE_EDGE_HZ[0] * TONE_EDGE_HZ[1]));
  var YIELD_BUDGET_MS = 8;
  var paints = typeof document !== 'undefined';
  var yieldToUi = function () {
    return paints
      ? new Promise(function (resolve) {
          setTimeout(resolve, 0);
        })
      : Promise.resolve();
  };
  var BEAT_THIS_SAMPLE_RATE = 22050;
  var N_FFT = 1024;
  var HOP = 441;
  var BEAT_THIS_FPS = BEAT_THIS_SAMPLE_RATE / HOP;
  var N_MELS = 128;
  var F_MIN = 30;
  var F_MAX = 11e3;
  var LOG_MULTIPLIER = 1e3;
  var AMIN = 1e-10;
  var hzToMel = function (hz) {
    var fSp = 200 / 3;
    if (hz < 1e3) return hz / fSp;
    return 1e3 / fSp + Math.log(hz / 1e3) / (Math.log(6.4) / 27);
  };
  var melToHz = function (mel) {
    var fSp = 200 / 3;
    var minLogMel = 1e3 / fSp;
    if (mel < minLogMel) return fSp * mel;
    return 1e3 * Math.exp((Math.log(6.4) / 27) * (mel - minLogMel));
  };
  var buildFilterbank = function () {
    var bins = N_FFT / 2 + 1;
    var melMin = hzToMel(F_MIN);
    var melMax = hzToMel(F_MAX);
    var corners = Array.from({ length: N_MELS + 2 }, function (_, index) {
      return melToHz(melMin + ((melMax - melMin) * index) / (N_MELS + 1));
    });
    return Array.from({ length: N_MELS }, function (_, band) {
      var row = new Float64Array(bins);
      var _b = __read([corners[band], corners[band + 1], corners[band + 2]], 3),
        left = _b[0],
        centre = _b[1],
        right = _b[2];
      for (var bin = 0; bin < bins; bin += 1) {
        var hz = (bin * BEAT_THIS_SAMPLE_RATE) / N_FFT;
        var rising = centre === left ? 0 : (hz - left) / (centre - left);
        var falling = right === centre ? 0 : (right - hz) / (right - centre);
        row[bin] = Math.max(0, Math.min(rising, falling));
      }
      var start = 0;
      while (start < bins && row[start] === 0) start += 1;
      var end = bins;
      while (end > start && row[end - 1] === 0) end -= 1;
      return { weights: row, start: start, end: end };
    });
  };
  var melBandCentresHz = function () {
    var melMin = hzToMel(F_MIN);
    var melMax = hzToMel(F_MAX);
    return Array.from({ length: N_MELS }, function (_, band) {
      return melToHz(melMin + ((melMax - melMin) * (band + 1)) / (N_MELS + 1));
    });
  };
  var MEL_BANDS = N_MELS;
  var filterbank = null;
  var hann = null;
  var melSpectrogram = function (mono) {
    return __async2(null, null, function () {
      var index,
        pad,
        padded,
        index,
        frames,
        data,
        real,
        imag,
        magnitude,
        scale,
        yieldBy,
        frame,
        start,
        index,
        bin,
        band,
        _b,
        weights,
        from,
        to,
        energy,
        bin;
      return __generator(this, function (_c) {
        switch (_c.label) {
          case 0:
            if (!filterbank) filterbank = buildFilterbank();
            if (!hann) {
              hann = new Float64Array(N_FFT);
              for (index = 0; index < N_FFT; index += 1) {
                hann[index] = 0.5 * (1 - Math.cos((2 * Math.PI * index) / N_FFT));
              }
            }
            pad = N_FFT / 2;
            padded = new Float64Array(mono.length + 2 * pad);
            for (index = 0; index < pad; index += 1) {
              padded[index] = mono[Math.min(pad - index, mono.length - 1)];
              padded[padded.length - 1 - index] =
                mono[Math.max(0, mono.length - 1 - (pad - index))];
            }
            padded.set(mono, pad);
            frames = Math.floor((padded.length - N_FFT) / HOP) + 1;
            if (frames <= 0) return [2 /*return*/, { data: new Float32Array(0), frames: 0 }];
            data = new Float32Array(frames * N_MELS);
            real = new Float64Array(N_FFT);
            imag = new Float64Array(N_FFT);
            magnitude = new Float64Array(N_FFT / 2 + 1);
            scale = 1 / Math.sqrt(N_FFT);
            yieldBy = performance.now() + YIELD_BUDGET_MS;
            frame = 0;
            _c.label = 1;
          case 1:
            if (!(frame < frames)) return [3 /*break*/, 5];
            if (!(performance.now() >= yieldBy)) return [3 /*break*/, 3];
            return [4 /*yield*/, yieldToUi()];
          case 2:
            _c.sent();
            yieldBy = performance.now() + YIELD_BUDGET_MS;
            _c.label = 3;
          case 3:
            start = frame * HOP;
            for (index = 0; index < N_FFT; index += 1) {
              real[index] = padded[start + index] * hann[index];
              imag[index] = 0;
            }
            fft(real, imag);
            for (bin = 0; bin < magnitude.length; bin += 1) {
              magnitude[bin] = Math.sqrt(real[bin] * real[bin] + imag[bin] * imag[bin]) * scale;
            }
            for (band = 0; band < N_MELS; band += 1) {
              ((_b = filterbank[band]), (weights = _b.weights), (from = _b.start), (to = _b.end));
              energy = 0;
              for (bin = from; bin < to; bin += 1) energy += magnitude[bin] * weights[bin];
              data[frame * N_MELS + band] = Math.log1p(LOG_MULTIPLIER * Math.max(energy, AMIN));
            }
            _c.label = 4;
          case 4:
            frame += 1;
            return [3 /*break*/, 1];
          case 5:
            return [2 /*return*/, { data: data, frames: frames }];
        }
      });
    });
  };
  var deduplicate = function (peaks, width) {
    if (!peaks.length) return [];
    var merged = [];
    var mean = peaks[0];
    var count = 1;
    for (var index = 1; index < peaks.length; index += 1) {
      var peak = peaks[index];
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
  var beatsFromLogits = function (beatLogits, downbeatLogits, fps) {
    if (fps === void 0) {
      fps = BEAT_THIS_FPS;
    }
    var pick = function (logits) {
      var peaks = [];
      for (var index = 0; index < logits.length; index += 1) {
        var value = logits[index];
        if (!(value > 0)) continue;
        var isPeak = true;
        for (var offset = -3; offset <= 3 && isPeak; offset += 1) {
          var other = index + offset;
          if (other >= 0 && other < logits.length && logits[other] > value) isPeak = false;
        }
        if (isPeak) peaks.push(index);
      }
      return deduplicate(peaks, 1);
    };
    var beats = pick(beatLogits).map(function (frame) {
      return frame / fps;
    });
    var downbeats = pick(downbeatLogits).map(function (frame) {
      return frame / fps;
    });
    if (!beats.length) return { beats: beats, downbeats: [] };
    var snapped = downbeats.map(function (time) {
      var e_3, _b;
      var best = beats[0];
      try {
        for (
          var beats_1 = __values(beats), beats_1_1 = beats_1.next();
          !beats_1_1.done;
          beats_1_1 = beats_1.next()
        ) {
          var beat = beats_1_1.value;
          if (Math.abs(beat - time) < Math.abs(best - time)) best = beat;
        }
      } catch (e_3_1) {
        e_3 = { error: e_3_1 };
      } finally {
        try {
          if (beats_1_1 && !beats_1_1.done && (_b = beats_1.return)) _b.call(beats_1);
        } finally {
          if (e_3) throw e_3.error;
        }
      }
      return best;
    });
    return {
      beats: beats,
      downbeats: __spreadArray([], __read(new Set(snapped)), false).sort(function (a, b) {
        return a - b;
      }),
    };
  };
  var MODEL_CHUNK_FRAMES = 1500;
  var BORDER_FRAMES = 6;
  var chunkStarts = function (frames) {
    var stride = MODEL_CHUNK_FRAMES - 2 * BORDER_FRAMES;
    var starts = [];
    for (var start = -BORDER_FRAMES; start < frames - BORDER_FRAMES; start += stride) {
      starts.push(start);
    }
    if (!starts.length) return [-BORDER_FRAMES];
    if (frames > stride) starts[starts.length - 1] = frames - (MODEL_CHUNK_FRAMES - BORDER_FRAMES);
    return starts;
  };
  var splitForModel = function (mel) {
    if (!mel.frames) return [];
    return chunkStarts(mel.frames).map(function (start) {
      var from = Math.max(0, start);
      var to = Math.min(start + MODEL_CHUNK_FRAMES, mel.frames);
      var leftPad = Math.max(0, -start);
      var rightPad = Math.max(0, Math.min(BORDER_FRAMES, start + MODEL_CHUNK_FRAMES - mel.frames));
      var frames = leftPad + Math.max(0, to - from) + rightPad;
      var data = new Float32Array(frames * 128);
      if (to > from) data.set(mel.data.subarray(from * 128, to * 128), leftPad * 128);
      return { data: data, frames: frames, start: start };
    });
  };
  var aggregateChunks = function (predictions, frames) {
    var beat = new Float32Array(frames).fill(-1e3);
    var downbeat = new Float32Array(frames).fill(-1e3);
    for (var index = predictions.length - 1; index >= 0; index -= 1) {
      var chunk = predictions[index];
      var wide = chunk.frames >= 2 * BORDER_FRAMES;
      var from = wide ? BORDER_FRAMES : 0;
      var to = wide ? chunk.frames - BORDER_FRAMES : chunk.frames;
      for (var offset = from; offset < to; offset += 1) {
        var target = chunk.start + offset;
        if (target < 0 || target >= frames) continue;
        beat[target] = chunk.beat[offset];
        downbeat[target] = chunk.downbeat[offset];
      }
    }
    return { beat: beat, downbeat: downbeat };
  };
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
  var resolveBandOf = function () {
    if (bandOf) return bandOf;
    bandOf = Uint8Array.from(melBandCentresHz(), function (hz) {
      return hz < LOW_MAX_HZ ? 0 : hz < MID_MAX_HZ ? 1 : 2;
    });
    return bandOf;
  };
  var splitBands = function (mel) {
    var of = resolveBandOf();
    var counts = [0, 0, 0];
    of.forEach(function (band) {
      counts[band] += 1;
    });
    var tracks = {
      low: new Float32Array(mel.frames),
      mid: new Float32Array(mel.frames),
      high: new Float32Array(mel.frames),
      all: new Float32Array(mel.frames),
    };
    var rows = [tracks.low, tracks.mid, tracks.high];
    for (var frame = 0; frame < mel.frames; frame += 1) {
      var offset = frame * MEL_BANDS;
      var low = 0;
      var mid = 0;
      var high = 0;
      for (var band = 0; band < MEL_BANDS; band += 1) {
        var value = mel.data[offset + band];
        var which = of[band];
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
  var riseAt = function (track, frame) {
    var last = track.length - 1;
    if (last < 0) return 0;
    var peak = Number.NEGATIVE_INFINITY;
    for (var at = frame - PEAK_BEFORE; at <= frame + PEAK_AFTER; at += 1) {
      peak = Math.max(peak, track[Math.min(last, Math.max(0, at))]);
    }
    var base = 0;
    for (var at = frame - BASE_FROM; at <= frame - BASE_TO; at += 1)
      base += track[Math.min(last, Math.max(0, at))];
    base /= BASE_FROM - BASE_TO + 1;
    return Math.max(0, peak - base);
  };
  var loudnessAt = function (track, frame) {
    var last = track.length - 1;
    if (last < 0) return 0;
    var sum = 0;
    for (var at = frame - LOUDNESS_HALF; at <= frame + LOUDNESS_HALF; at += 1)
      sum += track[Math.min(last, Math.max(0, at))];
    return sum / (2 * LOUDNESS_HALF + 1);
  };
  var percentile = function (sorted, p) {
    if (!sorted.length) return 0;
    var at = Math.min(sorted.length - 1, Math.max(0, Math.round(p * (sorted.length - 1))));
    return sorted[at];
  };
  var normaliser = function (sorted) {
    var floor = percentile(sorted, NORMALISE_LOW_PERCENTILE);
    var ceiling = percentile(sorted, NORMALISE_HIGH_PERCENTILE);
    if (!(ceiling - floor > 1e-6)) return null;
    return function (value) {
      return Math.min(1, Math.max(0, (value - floor) / (ceiling - floor)));
    };
  };
  var median = function (values) {
    return percentile(
      __spreadArray([], __read(values), false).sort(function (a, b) {
        return a - b;
      }),
      0.5,
    );
  };
  var round3 = function (value) {
    return Math.round(value * 1e3) / 1e3;
  };
  var resolveBarStarts = function (beats, downbeats) {
    var downbeatSet = new Set(downbeats);
    var starts = [];
    if (downbeats.length >= 2) {
      beats.forEach(function (time, index) {
        if (index === 0 || downbeatSet.has(time)) starts.push(index);
      });
      return starts;
    }
    for (var index = 0; index < beats.length; index += FALLBACK_BAR_BEATS) starts.push(index);
    return starts;
  };
  var resolveTrackPercussiveShare = function (energy) {
    if (!energy) return null;
    var hit = 0;
    var all = 0;
    for (var frame = 0; frame < energy.total.length; frame += 1) {
      hit += energy.percussive[frame];
      all += energy.total[frame];
    }
    return all > 0 ? hit / all : null;
  };
  var resolveBarPercussiveShares = function (energy, beats, downbeats) {
    var starts = resolveBarStarts(beats, downbeats);
    if (starts.length < MIN_BARS) return { starts: starts, shares: [] };
    var percussive = energy.percussive,
      total = energy.total;
    var stepOf = function (time) {
      return Math.min(total.length, Math.max(0, Math.floor(time * BEAT_THIS_FPS)));
    };
    var lastGap = beats.length > 1 ? beats[beats.length - 1] - beats[beats.length - 2] : 0.5;
    var shares = starts.map(function (first, bar) {
      var from = stepOf(beats[first]);
      var until =
        bar + 1 < starts.length
          ? stepOf(beats[starts[bar + 1]])
          : stepOf(beats[beats.length - 1] + lastGap);
      var hit = 0;
      var all = 0;
      for (var frame = from; frame < until; frame += 1) {
        hit += percussive[frame];
        all += total[frame];
      }
      return all > 0 ? hit / all : 0;
    });
    return { starts: starts, shares: shares };
  };
  var resolveRhythmicBeats = function (energy, beats, downbeats) {
    var rhythmic = beats.map(function () {
      return 1;
    });
    if (!energy) return rhythmic;
    var _b = resolveBarPercussiveShares(energy, beats, downbeats),
      starts = _b.starts,
      shares = _b.shares;
    if (!shares.length) return rhythmic;
    var usual = median(shares);
    if (usual < MIN_GATED_SHARE) return rhythmic;
    var threshold = usual * PERCUSSIVE_GATE;
    starts.forEach(function (first, bar) {
      if (shares[bar] >= threshold) return;
      var end = bar + 1 < starts.length ? starts[bar + 1] : beats.length;
      for (var index = first; index < end; index += 1) rhythmic[index] = 0;
    });
    return rhythmic;
  };
  var resolveBeatConfidence = function (logits, beats) {
    return beats.map(function (time) {
      var frame = Math.round(time * BEAT_THIS_FPS);
      var peak = Number.NEGATIVE_INFINITY;
      for (var at = frame - CONFIDENCE_HALF; at <= frame + CONFIDENCE_HALF; at += 1) {
        if (at >= 0 && at < logits.length) peak = Math.max(peak, logits[at]);
      }
      return Number.isFinite(peak) ? round3(peak) : 0;
    });
  };
  var resolveDownbeatStarts = function (beats, downbeats) {
    var downbeatSet = new Set(downbeats);
    var starts = [];
    beats.forEach(function (time, index) {
      if (downbeatSet.has(time)) starts.push(index);
    });
    return starts;
  };
  var readBeatMeter = function (rises, beats, downbeats, confidence) {
    if (confidence === void 0) {
      confidence = null;
    }
    var _a;
    var gaps = beats.slice(1).map(function (time, index) {
      return time - beats[index];
    });
    var gap = median(gaps);
    var meter = {
      bpm: gap > 0 ? Math.round(60 / gap) : 0,
      barLengths: [],
      usual: 0,
      byPosition: [],
      confidenceByPosition: [],
      group: 1,
      unsure: 0,
      driven: 0,
    };
    var starts = resolveDownbeatStarts(beats, downbeats);
    if (starts.length < 2) return meter;
    var counts = /* @__PURE__ */ new Map();
    for (var bar = 0; bar + 1 < starts.length; bar += 1) {
      var length = starts[bar + 1] - starts[bar];
      counts.set(length, ((_a = counts.get(length)) != null ? _a : 0) + 1);
    }
    meter.barLengths = __spreadArray([], __read(counts), false).sort(function (a, b) {
      return b[1] - a[1];
    });
    var _b = __read(meter.barLengths[0], 2),
      usual = _b[0],
      bars = _b[1];
    meter.usual = usual;
    meter.byPosition = Array.from({ length: usual }, function () {
      return 0;
    });
    var logits = Array.from({ length: usual }, function () {
      return [];
    });
    for (var bar = 0; bar + 1 < starts.length; bar += 1) {
      if (starts[bar + 1] - starts[bar] !== usual) continue;
      for (var position = 0; position < usual; position += 1) {
        meter.byPosition[position] += rises[starts[bar] + position] / bars;
        if (confidence) logits[position].push(confidence[starts[bar] + position]);
      }
    }
    if (confidence) meter.confidenceByPosition = logits.map(median);
    if (bars < MIN_PULSE_BARS) return meter;
    var byPosition = meter.byPosition,
      confidenceByPosition = meter.confidenceByPosition;
    var standsOut = function (head, needed) {
      var before = (head + usual - 1) % usual;
      var after = (head + 1) % usual;
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
    var found = __spreadArray(
      __spreadArray([], __read(PULSE_GROUPS), false),
      [WHOLE_BAR_GROUP],
      false,
    ).find(function (_b) {
      var size = _b.size,
        ratio = _b.ratio,
        riseRatio = _b.riseRatio,
        minBpm = _b.minBpm;
      if (usual % size !== 0 || meter.bpm < minBpm) return false;
      var needed = confidence ? ratio : riseRatio;
      if (usual === size) return standsOut(0, needed);
      for (var head = size; head < usual; head += size) {
        if (!standsOut(head, needed)) return false;
      }
      return true;
    });
    if (found) meter.group = found.size;
    return meter;
  };
  var resolveGroupHeads = function (meter, beats, downbeats) {
    var pulse = beats.map(function () {
      return 1;
    });
    var group = meter.group;
    if (group === 1) return pulse;
    var starts = resolveDownbeatStarts(beats, downbeats);
    for (var index = 0; index < starts[0]; index += 1)
      pulse[index] = (starts[0] - index) % group === 0 ? 1 : 0;
    starts.forEach(function (start, bar) {
      var end = bar + 1 < starts.length ? starts[bar + 1] : beats.length;
      for (var index = start; index < end; index += 1)
        pulse[index] = (index - start) % group === 0 ? 1 : 0;
    });
    return pulse;
  };
  var resolvePulseFromMeter = function (meter, beats, downbeats) {
    var pulse = resolveGroupHeads(meter, beats, downbeats);
    var gap = median(
      beats.slice(1).map(function (time, index) {
        return time - beats[index];
      }),
    );
    if (!(gap > 0) || beats.length < MIN_BEATS) return pulse;
    var spacing = MIN_PULSE_SPACING * gap * meter.group;
    var downbeatSet = new Set(downbeats);
    var last = -1;
    beats.forEach(function (time, index) {
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
  var resolveUnsureBeats = function (confidence, pulse) {
    var unsure = pulse.map(function () {
      return 0;
    });
    if (!confidence) return unsure;
    var felt = pulse
      .map(function (_, index) {
        return index;
      })
      .filter(function (index) {
        return pulse[index];
      });
    if (felt.length < UNSURE_RUN) return unsure;
    var logits = felt.map(function (index) {
      return confidence[index];
    });
    var sorted = __spreadArray([], __read(logits), false).sort(function (a, b) {
      return a - b;
    });
    var floor = UNSURE_SHARE * percentile(sorted, 0.75);
    var low = logits.map(function (_, at) {
      var around = logits.slice(Math.max(0, at - UNSURE_HALF), at + UNSURE_HALF + 1);
      return (
        around.reduce(function (sum, value) {
          return sum + value;
        }, 0) /
          around.length <
        floor
      );
    });
    var start = -1;
    for (var at = 0; at <= low.length; at += 1) {
      if (at < low.length && low[at]) {
        if (start < 0) start = at;
        continue;
      }
      if (start >= 0 && at - start >= UNSURE_RUN) {
        for (var index = felt[start]; index <= felt[at - 1]; index += 1) unsure[index] = 1;
      }
      start = -1;
    }
    return unsure;
  };
  var restoreDrivenBeats = function (pulse, group, confidence, percussiveShare, unsure) {
    if (unsure === void 0) {
      unsure = [];
    }
    if (
      group !== 2 ||
      !confidence ||
      percussiveShare === null ||
      percussiveShare < DRIVEN_TRACK_SHARE
    )
      return 0;
    var felt = pulse
      .map(function (_, index) {
        return index;
      })
      .filter(function (index) {
        return pulse[index];
      });
    var groups = felt
      .slice(0, -1)
      .map(function (head, at) {
        var between = [];
        for (var index = head + 1; index < felt[at + 1]; index += 1) between.push(index);
        return { head: head, between: between };
      })
      .filter(function (_b) {
        var head = _b.head;
        return !unsure[head];
      });
    var mean = function (values) {
      return (
        values.reduce(function (sum, value) {
          return sum + value;
        }, 0) / values.length
      );
    };
    var driven = groups.map(function (_, at) {
      var around = groups.slice(Math.max(0, at - DRIVEN_HALF), at + DRIVEN_HALF + 1);
      var offbeats = around.flatMap(function (entry) {
        return entry.between.map(function (index) {
          return confidence[index];
        });
      });
      if (!offbeats.length) return false;
      var heads = mean(
        around.map(function (entry) {
          return confidence[entry.head];
        }),
      );
      return heads > 0 && mean(offbeats) >= DRIVEN_OFFBEAT_SHARE * heads;
    });
    var restored = 0;
    groups.forEach(function (_b, at) {
      var between = _b.between;
      if (!driven[at]) return;
      between.forEach(function (index) {
        pulse[index] = 1;
        restored += 1;
      });
    });
    return restored;
  };
  var measure = function (tracks, time) {
    var frame = Math.round(time * BEAT_THIS_FPS);
    var low = riseAt(tracks.low, frame);
    var mid = riseAt(tracks.mid, frame);
    var high = riseAt(tracks.high, frame);
    return {
      low: low,
      mid: mid,
      high: high,
      raw: low * IMPACT_WEIGHTS.low + mid * IMPACT_WEIGHTS.mid + high * IMPACT_WEIGHTS.high,
      loudness: loudnessAt(tracks.all, frame),
    };
  };
  var resolveTrackScale = function (medianBeatRise) {
    if (!(medianBeatRise > 0)) return TRACK_SCALE_MIN;
    var amount =
      (medianBeatRise - TRACK_SCALE_RISE_LOW) / (TRACK_SCALE_RISE_HIGH - TRACK_SCALE_RISE_LOW);
    return round3(TRACK_SCALE_MIN + (1 - TRACK_SCALE_MIN) * Math.min(1, Math.max(0, amount)));
  };
  var computeBeatAccents = function (mel, beats, downbeats, energy, _b) {
    if (downbeats === void 0) {
      downbeats = [];
    }
    if (energy === void 0) {
      energy = null;
    }
    var _c = _b === void 0 ? {} : _b,
      _d = _c.confidence,
      confidence = _d === void 0 ? null : _d,
      onMeter = _c.onMeter;
    var tracks = splitBands(mel);
    var rises = beats.map(function (time) {
      return measure(tracks, time);
    });
    var meter = readBeatMeter(
      rises.map(function (entry) {
        return entry.raw;
      }),
      beats,
      downbeats,
      confidence,
    );
    var accents = {
      v: BEAT_ACCENTS_VERSION,
      impact: [],
      low: [],
      mid: [],
      high: [],
      rhythmic: resolveRhythmicBeats(energy, beats, downbeats),
      pulse: resolvePulseFromMeter(meter, beats, downbeats),
      trackScale: 1,
    };
    var sortedRaw = rises
      .map(function (rise2) {
        return rise2.raw;
      })
      .sort(function (a, b) {
        return a - b;
      });
    var rise = normaliser(sortedRaw);
    var loudness = normaliser(
      rises
        .map(function (entry) {
          return entry.loudness;
        })
        .sort(function (a, b) {
          return a - b;
        }),
    );
    var flat = beats.length < MIN_BEATS || (!rise && !loudness);
    var riseShare = !loudness ? 1 : !rise ? 0 : IMPACT_RISE_SHARE;
    rises.forEach(function (entry) {
      var mixed =
        riseShare * (rise ? rise(entry.raw) : 0) +
        (1 - riseShare) * (loudness ? loudness(entry.loudness) : 0);
      accents.impact.push(flat ? 1 : round3(Math.pow(mixed, IMPACT_CURVE)));
      var low = entry.low,
        mid = entry.mid,
        high = entry.high;
      var sum = low + mid + high;
      accents.low.push(sum > 0 ? round3(low / sum) : round3(1 / 3));
      accents.mid.push(sum > 0 ? round3(mid / sum) : round3(1 / 3));
      accents.high.push(sum > 0 ? round3(high / sum) : round3(1 / 3));
    });
    if (beats.length >= MIN_BEATS) accents.trackScale = resolveTrackScale(median(sortedRaw));
    var unsure = resolveUnsureBeats(confidence, accents.pulse);
    unsure.forEach(function (value, index) {
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
  var liveTailFrames = function () {
    return PEAK_AFTER2 + PEAK_HALF + Math.ceil(MIN_HIT_GAP * BEAT_THIS_FPS);
  };
  var quantile = function (values, q) {
    if (!values.length) return 0;
    var sorted = __spreadArray([], __read(values), false).sort(function (a, b) {
      return a - b;
    });
    return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
  };
  var bandTrack = function (mel, fromHz, toHz) {
    var e_4, _b;
    var bands = melBandCentresHz()
      .map(function (hz, band) {
        return hz >= fromHz && hz < toHz ? band : -1;
      })
      .filter(function (band) {
        return band >= 0;
      });
    var out = new Float32Array(mel.frames);
    for (var frame = 0; frame < mel.frames; frame += 1) {
      var sum = 0;
      try {
        for (
          var bands_1 = ((e_4 = void 0), __values(bands)), bands_1_1 = bands_1.next();
          !bands_1_1.done;
          bands_1_1 = bands_1.next()
        ) {
          var band = bands_1_1.value;
          sum += mel.data[frame * MEL_BANDS + band];
        }
      } catch (e_4_1) {
        e_4 = { error: e_4_1 };
      } finally {
        try {
          if (bands_1_1 && !bands_1_1.done && (_b = bands_1.return)) _b.call(bands_1);
        } finally {
          if (e_4) throw e_4.error;
        }
      }
      out[frame] = bands.length ? sum / bands.length : 0;
    }
    return out;
  };
  var riseAt2 = function (track, frame) {
    var peak = 0;
    for (var at = frame - PEAK_BEFORE2; at <= frame + PEAK_AFTER2; at += 1) {
      if (at >= 0 && at < track.length) peak = Math.max(peak, track[at]);
    }
    var base = 0;
    var count = 0;
    for (var at = frame - BASE_FROM2; at <= frame - BASE_TO2; at += 1) {
      if (at >= 0 && at < track.length) {
        base += track[at];
        count += 1;
      }
    }
    return Math.max(0, peak - (count ? base / count : 0));
  };
  var riseShares = function (track) {
    var rise = new Float32Array(track.length);
    for (var frame = 0; frame < track.length; frame += 1) rise[frame] = riseAt2(track, frame);
    var loud = rise
      .filter(function (value) {
        return value > 0;
      })
      .sort();
    var top =
      (loud.length ? loud[Math.min(loud.length - 1, Math.floor(HIT_TOP * loud.length))] : 0) || 1;
    return rise.map(function (value) {
      return Math.min(1, value / top);
    });
  };
  var drumBandTracks = function (mel) {
    return [
      bandTrack(mel, 0, KICK_MAX_HZ),
      bandTrack(mel, KICK_MAX_HZ, SNARE_MAX_HZ),
      bandTrack(mel, HAT_MIN_HZ, Infinity),
    ];
  };
  var pickBandHits = function (tracks, frames) {
    var bands = tracks.map(function (track) {
      return riseShares(track.subarray(0, frames));
    });
    var top = new Float32Array(frames);
    var kindOf = new Uint8Array(frames);
    for (var frame = 0; frame < frames; frame += 1) {
      for (var kind = 0; kind < bands.length; kind += 1) {
        if (bands[kind][frame] > top[frame]) {
          top[frame] = bands[kind][frame];
          kindOf[frame] = kind;
        }
      }
    }
    var times = [];
    var strengths = [];
    var kinds = [];
    for (var frame = 0; frame < frames; frame += 1) {
      var share = top[frame];
      if (share < HIT_ONSET_FLOOR) continue;
      var peak = true;
      for (
        var at = Math.max(0, frame - PEAK_HALF);
        at <= Math.min(frames - 1, frame + PEAK_HALF) && peak;
        at += 1
      ) {
        if (top[at] > share || (top[at] === share && at < frame)) peak = false;
      }
      if (!peak) continue;
      var time = frame / BEAT_THIS_FPS;
      var last = times.length - 1;
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
  var energyFrames = function (signal) {
    var out = new Float32Array(Math.ceil(signal.length / ENERGY_HOP));
    for (var at = 0; at < signal.length; at += 1)
      out[Math.floor(at / ENERGY_HOP)] += signal[at] * signal[at];
    return out;
  };
  var drumCoverage = function (drumEnergy, mixEnergy) {
    var _a;
    var windows = [];
    for (var from = 0; from < mixEnergy.length; from += SHARE_WINDOW) {
      var drum = 0;
      var mix = 0;
      for (var at = from; at < Math.min(mixEnergy.length, from + SHARE_WINDOW); at += 1) {
        drum += (_a = drumEnergy[at]) != null ? _a : 0;
        mix += mixEnergy[at];
      }
      windows.push({ drum: drum, mix: mix });
    }
    var floor =
      quantile(
        windows.map(function (window2) {
          return window2.mix;
        }),
        0.5,
      ) * SHARE_AUDIBLE;
    var audible = windows.filter(function (window2) {
      return window2.mix > floor && window2.mix > 0;
    });
    if (!audible.length) return 0;
    return (
      audible.filter(function (window2) {
        return window2.drum >= DRUMS_PLAYING_SHARE * window2.mix;
      }).length / audible.length
    );
  };
  var STEM_RATE = 44100;
  var MARGIN_FRAMES = 4;
  var createDrumBandStream = function (total, steps, from) {
    var frameSamples = STEM_RATE / BEAT_THIS_FPS;
    var frames = Math.floor(Math.ceil(total / 2) / (frameSamples / 2)) + 1;
    var bands = [new Float32Array(frames), new Float32Array(frames), new Float32Array(frames)];
    var done = 0;
    if (from) {
      if (
        from.done > frames ||
        from.bands.some(function (band) {
          return band.length < from.done || band.length > frames;
        })
      ) {
        throw new Error(
          'drum band stream: cannot carry on from '
            .concat(from.done, ' frames of ')
            .concat(
              from.bands
                .map(function (band) {
                  return band.length;
                })
                .join('/'),
              ' into ',
            )
            .concat(frames),
        );
      }
      from.bands.forEach(function (band, index) {
        return bands[index].set(band);
      });
      done = from.done;
    }
    return {
      frames: frames,
      bands: bands,
      get done() {
        return done;
      },
      advance: function (drums, final, end) {
        return __async2(null, null, function () {
          var _a, target, first, from2, to, read, _b, _c, _d, _e, frame, at, band;
          return __generator(this, function (_f) {
            switch (_f.label) {
              case 0:
                target = end
                  ? frames
                  : Math.min(frames, Math.floor(final / frameSamples) - MARGIN_FRAMES + 1);
                if (target <= done) return [2 /*return*/];
                first = Math.max(0, done - MARGIN_FRAMES);
                from2 = first * frameSamples;
                to = end ? total : (target - 1 + MARGIN_FRAMES) * frameSamples;
                _c = (_b = steps).bands;
                _e = (_d = steps).mel;
                return [4 /*yield*/, steps.resample(drums.subarray(from2, to))];
              case 1:
                return [4 /*yield*/, _e.apply(_d, [_f.sent()])];
              case 2:
                read = _c.apply(_b, [_f.sent()]);
                for (frame = done; frame < target; frame += 1) {
                  at = frame - first;
                  for (band = 0; band < 3; band += 1)
                    bands[band][frame] = (_a = read[band][at]) != null ? _a : 0;
                }
                done = target;
                return [2 /*return*/];
            }
          });
        });
      },
    };
  };
  var HTDEMUCS_GPU_SEGMENT = 172032;
  var stemSegmentStarts = function (total, segment) {
    if (segment === void 0) {
      segment = HTDEMUCS_GPU_SEGMENT;
    }
    var stride = segment - Math.floor(segment / 4);
    var count = Math.max(1, Math.ceil(total / stride));
    var starts = [];
    for (var c = 0; c < count; c += 1) {
      var start = c * stride;
      if (start >= total && c > 0) break;
      starts.push(start);
    }
    return starts;
  };
  var stemSegmentWindow = function (segment) {
    var overlap = Math.floor(segment / 4);
    var win = new Float32Array(segment).fill(1);
    for (var i = 0; i < overlap; i += 1) {
      var ramp = i / (overlap - 1);
      win[i] = ramp;
      win[segment - 1 - i] = ramp;
    }
    return win;
  };
  // tools/audio-analysis-helper/signal-job.mjs
  function checked(buffer, rate) {
    var data = new Float32Array(buffer);
    if (data.length < 1024 || data.length > rate * 600)
      throw Error('PCM duration budget: 1024 samples..600 seconds');
    for (var i = 0; i < data.length; i++)
      if (!Number.isFinite(data[i]) || Math.abs(data[i]) > 16)
        throw Error('non-finite/out-of-range PCM');
    return data;
  }
  function sourceHalfRate(signal) {
    var output = new Float32Array(Math.ceil(signal.length / 2));
    if (signal.length < 2) return output;
    for (var i = 0; i < output.length; ++i) output[i] = signal[i * 2];
    return output;
  }
  function analyseBeats(native, buffer) {
    return __async(this, null, function () {
      var mono,
        mel,
        chunks,
        predictions,
        i,
        chunk,
        out,
        _b,
        track,
        grid,
        energy,
        _c,
        percussive,
        total,
        i,
        accents;
      return __generator(this, function (_d) {
        switch (_d.label) {
          case 0:
            mono = checked(buffer, 22050);
            return [4 /*yield*/, melSpectrogram(mono)];
          case 1:
            mel = _d.sent();
            chunks = splitForModel(mel);
            predictions = [];
            i = 0;
            _d.label = 2;
          case 2:
            if (!(i < chunks.length)) return [3 /*break*/, 5];
            chunk = chunks[i];
            _b = Float32Array.bind;
            return [
              4 /*yield*/,
              native.compute('beat_this', chunk.frames, chunk.data.buffer, new ArrayBuffer(0)),
            ];
          case 3:
            out = new (_b.apply(Float32Array, [void 0, _d.sent()]))();
            if (out.length !== chunk.frames * 2) throw Error('beat model output shape');
            predictions.push({
              start: chunk.start,
              frames: chunk.frames,
              beat: out.subarray(0, chunk.frames),
              downbeat: out.subarray(chunk.frames),
            });
            native.progress(
              i + 1,
              chunks.length,
              Math.min(mono.length / 22050, (chunk.start + chunk.frames - 6) / 50),
            );
            _d.label = 4;
          case 4:
            i++;
            return [3 /*break*/, 2];
          case 5:
            track = aggregateChunks(predictions, mel.frames);
            grid = beatsFromLogits(track.beat, track.downbeat);
            _c = Float32Array.bind;
            return [
              4 /*yield*/,
              native.compute('percussive', mel.frames, mel.data.buffer, new ArrayBuffer(0)),
            ];
          case 6:
            energy = new (_c.apply(Float32Array, [void 0, _d.sent()]))();
            if (energy.length !== mel.frames * 2) throw Error('HPSS output shape');
            ((percussive = new Float32Array(mel.frames)), (total = new Float32Array(mel.frames)));
            for (i = 0; i < mel.frames; i++) {
              percussive[i] = energy[2 * i];
              total[i] = energy[2 * i + 1];
            }
            accents = grid.beats.length
              ? computeBeatAccents(
                  mel,
                  grid.beats,
                  grid.downbeats,
                  { percussive: percussive, total: total },
                  { confidence: resolveBeatConfidence(track.beat, grid.beats) },
                )
              : null;
            return [
              2 /*return*/,
              {
                version: 1,
                beats: grid.beats,
                downbeats: grid.downbeats,
                accents: accents,
                complete: true,
                durationSeconds: mono.length / 22050,
                frames: mel.frames,
                source: 'beat_this',
                backend: 'gpu',
              },
            ];
        }
      });
    });
  }
  var RESUME_MAGIC = 827475014;
  var RESUME_LIMIT = 2 * 1024 * 1024;
  var SEGMENT = 172032;
  var FRAME_SAMPLES = 882;
  function encodeDrumResume(state) {
    var e_5, _b, e_6, _c;
    var arrays = __spreadArray(
      __spreadArray([state.drums, state.weight], __read(state.bands), false),
      [state.drumEnergy],
      false,
    );
    var bytes =
      64 +
      arrays.reduce(function (sum, value) {
        return sum + value.length * 4;
      }, 0);
    if (bytes > RESUME_LIMIT) return null;
    var buffer = new ArrayBuffer(bytes),
      view = new DataView(buffer);
    var header = __spreadArray(
      __spreadArray(
        [
          RESUME_MAGIC,
          1,
          state.total,
          state.next,
          state.divided,
          state.tailStart,
          state.done,
          state.measured,
        ],
        __read(
          arrays.map(function (value) {
            return value.length;
          }),
        ),
        false,
      ),
      [0, 0],
      false,
    );
    header.forEach(function (value, index) {
      return view.setUint32(index * 4, value, true);
    });
    var at = 64;
    try {
      for (
        var arrays_1 = __values(arrays), arrays_1_1 = arrays_1.next();
        !arrays_1_1.done;
        arrays_1_1 = arrays_1.next()
      ) {
        var array = arrays_1_1.value;
        try {
          for (
            var array_1 = ((e_6 = void 0), __values(array)), array_1_1 = array_1.next();
            !array_1_1.done;
            array_1_1 = array_1.next()
          ) {
            var value = array_1_1.value;
            view.setFloat32(at, value, true);
            at += 4;
          }
        } catch (e_6_1) {
          e_6 = { error: e_6_1 };
        } finally {
          try {
            if (array_1_1 && !array_1_1.done && (_c = array_1.return)) _c.call(array_1);
          } finally {
            if (e_6) throw e_6.error;
          }
        }
      }
    } catch (e_5_1) {
      e_5 = { error: e_5_1 };
    } finally {
      try {
        if (arrays_1_1 && !arrays_1_1.done && (_b = arrays_1.return)) _b.call(arrays_1);
      } finally {
        if (e_5) throw e_5.error;
      }
    }
    return buffer;
  }
  function decodeDrumResume(buffer, total) {
    var _a;
    var fail = function () {
      throw Error('ResumeInvalid: checkpoint shape/state');
    };
    if (
      !(buffer instanceof ArrayBuffer) ||
      buffer.byteLength < 64 ||
      buffer.byteLength > RESUME_LIMIT
    )
      fail();
    if (!Number.isInteger(total) || total < 1024 || total > 44100 * 600) fail();
    var view = new DataView(buffer),
      header = [];
    for (var i = 0; i < 16; ++i) header.push(view.getUint32(i * 4, true));
    var _b = __read(header, 8),
      magic = _b[0],
      version = _b[1],
      length = _b[2],
      next = _b[3],
      divided = _b[4],
      tailStart = _b[5],
      done = _b[6],
      measured = _b[7];
    var starts = stemSegmentStarts(total, SEGMENT);
    var reached = Math.min(total, ((_a = starts[next - 1]) != null ? _a : total) + SEGMENT);
    var frames = Math.floor(Math.ceil(total / 2) / 441) + 1;
    var expectedDone = Math.min(frames, Math.floor(divided / FRAME_SAMPLES) - 4 + 1);
    var expectedTail = Math.min(Math.max(0, done - 4) * FRAME_SAMPLES, measured, divided);
    var sizes = [
      reached - tailStart,
      reached - divided,
      done,
      done,
      done,
      Math.ceil(measured / 441),
    ];
    if (
      magic !== RESUME_MAGIC ||
      version !== 1 ||
      total !== length ||
      next < 1 ||
      next >= starts.length ||
      divided !== starts[next] ||
      measured !== divided ||
      done !== expectedDone ||
      tailStart !== expectedTail ||
      header[14] ||
      header[15] ||
      sizes.some(function (size, index) {
        return size < 0 || size !== header[8 + index];
      }) ||
      64 +
        sizes.reduce(function (sum, size) {
          return sum + size * 4;
        }, 0) !==
        buffer.byteLength
    )
      fail();
    var at = 64;
    var arrays = sizes.map(function (size) {
      var array = new Float32Array(size);
      for (var i = 0; i < size; ++i) {
        array[i] = view.getFloat32(at, true);
        at += 4;
        if (!Number.isFinite(array[i])) fail();
      }
      return array;
    });
    if (
      arrays[1].some(function (value) {
        return value < 0;
      }) ||
      arrays[5].some(function (value) {
        return value < 0;
      })
    )
      fail();
    return {
      total: total,
      next: next,
      divided: divided,
      tailStart: tailStart,
      done: done,
      measured: measured,
      drums: arrays[0],
      weight: arrays[1],
      bands: arrays.slice(2, 5),
      drumEnergy: arrays[5],
    };
  }
  function analyseDrums(_0, _1, _2) {
    return __async(this, arguments, function (native, leftBuffer, rightBuffer, resumeBuffer) {
      var left,
        right,
        segment,
        total,
        starts,
        win,
        drums,
        weight,
        mix,
        i,
        mixEnergy,
        drumEnergy,
        from,
        stream,
        divided,
        measured,
        liveAt,
        advance,
        partialResult,
        index,
        start,
        count,
        a,
        b,
        stems,
        _b,
        i,
        final,
        i,
        reached,
        tailStart,
        state,
        payload,
        hits,
        coverage;
      if (resumeBuffer === void 0) {
        resumeBuffer = new ArrayBuffer(0);
      }
      return __generator(this, function (_c) {
        switch (_c.label) {
          case 0:
            ((left = checked(leftBuffer, 44100)), (right = checked(rightBuffer, 44100)));
            if (left.length !== right.length) throw Error('stereo PCM lengths differ');
            ((segment = SEGMENT), (total = left.length));
            ((starts = stemSegmentStarts(total, segment)), (win = stemSegmentWindow(segment)));
            ((drums = new Float32Array(total)),
              (weight = new Float32Array(total)),
              (mix = new Float32Array(total)));
            for (i = 0; i < total; i++) mix[i] = (left[i] + right[i]) / 2;
            ((mixEnergy = energyFrames(mix)), (drumEnergy = new Float32Array(mixEnergy.length)));
            from = resumeBuffer.byteLength ? decodeDrumResume(resumeBuffer, total) : null;
            if (from) {
              drums.set(from.drums, from.tailStart);
              weight.set(from.weight, from.divided);
              drumEnergy.set(from.drumEnergy);
            }
            stream = createDrumBandStream(
              total,
              {
                resample: function (signal) {
                  return __async(null, null, function () {
                    return __generator(this, function (_b) {
                      return [2 /*return*/, sourceHalfRate(signal)];
                    });
                  });
                },
                mel: melSpectrogram,
                bands: drumBandTracks,
              },
              from ? { bands: from.bands, done: from.done } : void 0,
            );
            ((divided = from ? from.divided : 0),
              (measured = from ? from.measured : 0),
              (liveAt = -Infinity));
            advance = function (final) {
              return __async(null, null, function () {
                return __generator(this, function (_b) {
                  switch (_b.label) {
                    case 0:
                      for (; measured < final; measured++)
                        drumEnergy[Math.floor(measured / 441)] += drums[measured] * drums[measured];
                      return [4 /*yield*/, stream.advance(drums, final, false)];
                    case 1:
                      _b.sent();
                      return [2 /*return*/];
                  }
                });
              });
            };
            partialResult = function (final) {
              var windows = Math.floor(final / 441 / 25) * 25;
              var coverage2 = drumCoverage(
                drumEnergy.subarray(0, windows),
                mixEnergy.subarray(0, windows),
              );
              var until = Math.max(0, stream.done - liveTailFrames()) / 50;
              var picked = pickBandHits(stream.bands, stream.done);
              var after = picked.times.findIndex(function (time) {
                return time >= until;
              });
              var head = coverage2 < 0.08 ? 0 : after < 0 ? picked.times.length : after;
              return {
                version: 1,
                complete: false,
                durationSeconds: total / 44100,
                source: 'htdemucs',
                backend: 'gpu',
                coverage: coverage2,
                drumless: coverage2 < 0.08,
                hits: {
                  times: Array.from(picked.times.slice(0, head)),
                  strengths: Array.from(picked.strengths.slice(0, head)),
                  kinds: Array.from(picked.kinds.slice(0, head)),
                  until: until,
                },
              };
            };
            if (from && typeof native.partial === 'function')
              native.partial(partialResult(divided));
            index = from ? from.next : 0;
            _c.label = 1;
          case 1:
            if (!(index < starts.length)) return [3 /*break*/, 8];
            if (typeof native.canceled === 'function' && native.canceled())
              throw Error('Canceled: analysis retired');
            ((start = starts[index]), (count = Math.min(segment, total - start)));
            ((a = new Float32Array(segment)), (b = new Float32Array(segment)));
            a.set(left.subarray(start, start + count));
            b.set(right.subarray(start, start + count));
            _b = Float32Array.bind;
            return [4 /*yield*/, native.compute('htdemucs', 1500, a.buffer, b.buffer)];
          case 2:
            stems = new (_b.apply(Float32Array, [void 0, _c.sent()]))();
            if (stems.length !== 4 * 2 * segment) throw Error('stem output shape');
            for (i = 0; i < count; i++) {
              drums[start + i] += ((stems[i] + stems[segment + i]) / 2) * win[i];
              weight[start + i] += win[i];
            }
            final = index + 1 < starts.length ? starts[index + 1] : total;
            for (i = divided; i < final; i++) drums[i] /= Math.max(weight[i], 1e-8);
            divided = final;
            if (!(
              typeof native.partial === 'function' &&
              final >= 10 * 44100 &&
              performance.now() - liveAt >= 1e3
            ))
              return [3 /*break*/, 4];
            liveAt = performance.now();
            return [4 /*yield*/, advance(final)];
          case 3:
            _c.sent();
            native.partial(partialResult(final));
            _c.label = 4;
          case 4:
            if (!(
              typeof native.checkpoint === 'function' &&
              native.checkpointEnabled !== false &&
              index + 1 < starts.length
            ))
              return [3 /*break*/, 6];
            return [4 /*yield*/, advance(final)];
          case 5:
            _c.sent();
            reached = Math.min(total, start + segment);
            tailStart = Math.min(Math.max(0, stream.done - 4) * FRAME_SAMPLES, measured, divided);
            state = {
              total: total,
              next: index + 1,
              divided: divided,
              tailStart: tailStart,
              done: stream.done,
              measured: measured,
              drums: drums.slice(tailStart, reached),
              weight: weight.slice(divided, reached),
              bands: stream.bands.map(function (band) {
                return band.slice(0, stream.done);
              }),
              drumEnergy: drumEnergy.slice(0, Math.ceil(measured / 441)),
            };
            payload = encodeDrumResume(state);
            if (payload) native.checkpoint(payload, partialResult(final));
            _c.label = 6;
          case 6:
            native.progress(index + 1, starts.length, divided / 44100);
            _c.label = 7;
          case 7:
            index++;
            return [3 /*break*/, 1];
          case 8:
            for (; measured < total; measured++)
              drumEnergy[Math.floor(measured / 441)] += drums[measured] * drums[measured];
            return [4 /*yield*/, stream.advance(drums, total, true)];
          case 9:
            _c.sent();
            hits = pickBandHits(stream.bands, stream.frames);
            coverage = drumCoverage(drumEnergy, mixEnergy);
            return [
              2 /*return*/,
              {
                version: 1,
                hits: {
                  times: Array.from(hits.times),
                  strengths: Array.from(hits.strengths),
                  kinds: Array.from(hits.kinds),
                },
                drumless: coverage < 0.08,
                coverage: coverage,
                drumEnergy: Array.from(drumEnergy),
                mixEnergy: Array.from(mixEnergy),
                complete: true,
                durationSeconds: total / 44100,
                source: 'htdemucs',
                backend: 'gpu',
              },
            ];
        }
      });
    });
  }
  return __toCommonJS(signal_job_exports);
})();
