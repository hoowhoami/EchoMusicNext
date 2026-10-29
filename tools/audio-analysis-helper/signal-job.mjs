// SPDX-License-Identifier: AGPL-3.0-only
// Exact pinned pure math; binary ArrayBuffers cross the native process bridge.
import './typed-array-compat.mjs';
import {
  melSpectrogram,
  splitForModel,
  aggregateChunks,
  beatsFromLogits,
  computeBeatAccents,
  resolveBeatConfidence,
  stemSegmentStarts,
  stemSegmentWindow,
  drumBandTracks,
  pickBandHits,
  drumCoverage,
  energyFrames,
  createDrumBandStream,
  liveTailFrames,
} from './generated/source-signal.mjs';

function checked(buffer, rate) {
  const data = new Float32Array(buffer);
  if (data.length < 1024 || data.length > rate * 600)
    throw Error('PCM duration budget: 1024 samples..600 seconds');
  for (let i = 0; i < data.length; i++)
    if (!Number.isFinite(data[i]) || Math.abs(data[i]) > 16)
      throw Error('non-finite/out-of-range PCM');
  return data;
}

export function sourceHalfRate(signal) {
  // Source uses OfflineAudioContext AudioBufferSource with exactly 2:1 sample-rate ratio.
  // Chromium's linear source interpolation lands on integer indices: Chrome155 corpus byte-exact.
  const output = new Float32Array(Math.ceil(signal.length / 2));
  // Chromium's AudioBufferSource returns silence when there are fewer than two input frames.
  if (signal.length < 2) return output;
  for (let i = 0; i < output.length; ++i) output[i] = signal[i * 2];
  return output;
}

export async function analyseBeats(native, buffer) {
  const mono = checked(buffer, 22050);
  const mel = await melSpectrogram(mono);
  const chunks = splitForModel(mel);
  const predictions = [];
  for (let i = 0; i < chunks.length; i++) {
    const chunk = chunks[i];
    const out = new Float32Array(
      await native.compute('beat_this', chunk.frames, chunk.data.buffer, new ArrayBuffer(0)),
    );
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
  }
  const track = aggregateChunks(predictions, mel.frames);
  const grid = beatsFromLogits(track.beat, track.downbeat);
  const energy = new Float32Array(
    await native.compute('percussive', mel.frames, mel.data.buffer, new ArrayBuffer(0)),
  );
  if (energy.length !== mel.frames * 2) throw Error('HPSS output shape');
  const percussive = new Float32Array(mel.frames),
    total = new Float32Array(mel.frames);
  for (let i = 0; i < mel.frames; i++) {
    percussive[i] = energy[2 * i];
    total[i] = energy[2 * i + 1];
  }
  const accents = grid.beats.length
    ? computeBeatAccents(
        mel,
        grid.beats,
        grid.downbeats,
        { percussive, total },
        { confidence: resolveBeatConfidence(track.beat, grid.beats) },
      )
    : null;
  return {
    version: 1,
    beats: grid.beats,
    downbeats: grid.downbeats,
    accents,
    complete: true,
    durationSeconds: mono.length / 22050,
    frames: mel.frames,
    source: 'beat_this',
    backend: 'gpu',
  };
}

const RESUME_MAGIC = 0x31524446; // FDR1, little endian.
const RESUME_LIMIT = 2 * 1024 * 1024;
const SEGMENT = 172032;
const FRAME_SAMPLES = 882;

// Private process checkpoint; source Float32 state is copied bit for bit, never JSON-rounded.
function encodeDrumResume(state) {
  const arrays = [state.drums, state.weight, ...state.bands, state.drumEnergy];
  const bytes = 64 + arrays.reduce((sum, value) => sum + value.length * 4, 0);
  if (bytes > RESUME_LIMIT) return null;
  const buffer = new ArrayBuffer(bytes),
    view = new DataView(buffer);
  const header = [
    RESUME_MAGIC,
    1,
    state.total,
    state.next,
    state.divided,
    state.tailStart,
    state.done,
    state.measured,
    ...arrays.map((value) => value.length),
    0,
    0,
  ];
  header.forEach((value, index) => view.setUint32(index * 4, value, true));
  let at = 64;
  for (const array of arrays)
    for (const value of array) {
      view.setFloat32(at, value, true);
      at += 4;
    }
  return buffer;
}

export function decodeDrumResume(buffer, total) {
  const fail = () => {
    throw Error('ResumeInvalid: checkpoint shape/state');
  };
  if (
    !(buffer instanceof ArrayBuffer) ||
    buffer.byteLength < 64 ||
    buffer.byteLength > RESUME_LIMIT
  )
    fail();
  if (!Number.isInteger(total) || total < 1024 || total > 44100 * 600) fail();
  const view = new DataView(buffer),
    header = [];
  for (let i = 0; i < 16; ++i) header.push(view.getUint32(i * 4, true));
  const [magic, version, length, next, divided, tailStart, done, measured] = header;
  const starts = stemSegmentStarts(total, SEGMENT);
  const reached = Math.min(total, (starts[next - 1] ?? total) + SEGMENT);
  const frames = Math.floor(Math.ceil(total / 2) / 441) + 1;
  const expectedDone = Math.min(frames, Math.floor(divided / FRAME_SAMPLES) - 4 + 1);
  const expectedTail = Math.min(Math.max(0, done - 4) * FRAME_SAMPLES, measured, divided);
  const sizes = [
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
    sizes.some((size, index) => size < 0 || size !== header[8 + index]) ||
    64 + sizes.reduce((sum, size) => sum + size * 4, 0) !== buffer.byteLength
  )
    fail();
  let at = 64;
  const arrays = sizes.map((size) => {
    const array = new Float32Array(size);
    for (let i = 0; i < size; ++i) {
      array[i] = view.getFloat32(at, true);
      at += 4;
      if (!Number.isFinite(array[i])) fail();
    }
    return array;
  });
  if (arrays[1].some((value) => value < 0) || arrays[5].some((value) => value < 0)) fail();
  return {
    total,
    next,
    divided,
    tailStart,
    done,
    measured,
    drums: arrays[0],
    weight: arrays[1],
    bands: arrays.slice(2, 5),
    drumEnergy: arrays[5],
  };
}

export async function analyseDrums(
  native,
  leftBuffer,
  rightBuffer,
  resumeBuffer = new ArrayBuffer(0),
) {
  const left = checked(leftBuffer, 44100),
    right = checked(rightBuffer, 44100);
  if (left.length !== right.length) throw Error('stereo PCM lengths differ');
  const segment = SEGMENT,
    total = left.length;
  const starts = stemSegmentStarts(total, segment),
    win = stemSegmentWindow(segment);
  const drums = new Float32Array(total),
    weight = new Float32Array(total),
    mix = new Float32Array(total);
  for (let i = 0; i < total; i++) mix[i] = (left[i] + right[i]) / 2;
  const mixEnergy = energyFrames(mix),
    drumEnergy = new Float32Array(mixEnergy.length);
  const from = resumeBuffer.byteLength ? decodeDrumResume(resumeBuffer, total) : null;
  if (from) {
    drums.set(from.drums, from.tailStart);
    weight.set(from.weight, from.divided);
    drumEnergy.set(from.drumEnergy);
  }
  const stream = createDrumBandStream(
    total,
    {
      resample: async (signal) => sourceHalfRate(signal),
      mel: melSpectrogram,
      bands: drumBandTracks,
    },
    from ? { bands: from.bands, done: from.done } : undefined,
  );
  let divided = from ? from.divided : 0,
    measured = from ? from.measured : 0,
    liveAt = -Infinity;
  const advance = async (final) => {
    for (; measured < final; measured++)
      drumEnergy[Math.floor(measured / 441)] += drums[measured] * drums[measured];
    await stream.advance(drums, final, false);
  };
  const partialResult = (final) => {
    const windows = Math.floor(final / 441 / 25) * 25;
    const coverage = drumCoverage(drumEnergy.subarray(0, windows), mixEnergy.subarray(0, windows));
    const until = Math.max(0, stream.done - liveTailFrames()) / 50;
    const picked = pickBandHits(stream.bands, stream.done);
    const after = picked.times.findIndex((time) => time >= until);
    const head = coverage < 0.08 ? 0 : after < 0 ? picked.times.length : after;
    return {
      version: 1,
      complete: false,
      durationSeconds: total / 44100,
      source: 'htdemucs',
      backend: 'gpu',
      coverage,
      drumless: coverage < 0.08,
      hits: {
        times: Array.from(picked.times.slice(0, head)),
        strengths: Array.from(picked.strengths.slice(0, head)),
        kinds: Array.from(picked.kinds.slice(0, head)),
        until,
      },
    };
  };
  if (from && typeof native.partial === 'function') native.partial(partialResult(divided));
  for (let index = from ? from.next : 0; index < starts.length; index++) {
    if (typeof native.canceled === 'function' && native.canceled())
      throw Error('Canceled: analysis retired');
    const start = starts[index],
      count = Math.min(segment, total - start);
    const a = new Float32Array(segment),
      b = new Float32Array(segment);
    a.set(left.subarray(start, start + count));
    b.set(right.subarray(start, start + count));
    const stems = new Float32Array(await native.compute('htdemucs', 1500, a.buffer, b.buffer));
    if (stems.length !== 4 * 2 * segment) throw Error('stem output shape');
    for (let i = 0; i < count; i++) {
      drums[start + i] += ((stems[i] + stems[segment + i]) / 2) * win[i];
      weight[start + i] += win[i];
    }
    const final = index + 1 < starts.length ? starts[index + 1] : total;
    for (let i = divided; i < final; i++) drums[i] /= Math.max(weight[i], 1e-8);
    divided = final;
    if (
      typeof native.partial === 'function' &&
      final >= 10 * 44100 &&
      performance.now() - liveAt >= 1000
    ) {
      liveAt = performance.now();
      await advance(final);
      native.partial(partialResult(final));
    }
    if (
      typeof native.checkpoint === 'function' &&
      native.checkpointEnabled !== false &&
      index + 1 < starts.length
    ) {
      await advance(final);
      const reached = Math.min(total, start + segment);
      const tailStart = Math.min(Math.max(0, stream.done - 4) * FRAME_SAMPLES, measured, divided);
      const state = {
        total,
        next: index + 1,
        divided,
        tailStart,
        done: stream.done,
        measured,
        drums: drums.slice(tailStart, reached),
        weight: weight.slice(divided, reached),
        bands: stream.bands.map((band) => band.slice(0, stream.done)),
        drumEnergy: drumEnergy.slice(0, Math.ceil(measured / 441)),
      };
      const payload = encodeDrumResume(state);
      if (payload) native.checkpoint(payload, partialResult(final));
    }
    native.progress(index + 1, starts.length, divided / 44100);
  }
  for (; measured < total; measured++)
    drumEnergy[Math.floor(measured / 441)] += drums[measured] * drums[measured];
  await stream.advance(drums, total, true);
  const hits = pickBandHits(stream.bands, stream.frames);
  const coverage = drumCoverage(drumEnergy, mixEnergy);
  return {
    version: 1,
    hits: {
      times: Array.from(hits.times),
      strengths: Array.from(hits.strengths),
      kinds: Array.from(hits.kinds),
    },
    drumless: coverage < 0.08,
    coverage,
    drumEnergy: Array.from(drumEnergy),
    mixEnergy: Array.from(mixEnergy),
    complete: true,
    durationSeconds: total / 44100,
    source: 'htdemucs',
    backend: 'gpu',
  };
}
