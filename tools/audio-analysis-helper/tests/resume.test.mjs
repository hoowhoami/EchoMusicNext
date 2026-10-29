// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import test from 'node:test';
import { analyseDrums, decodeDrumResume } from '../signal-job.mjs';

const rate = 44100;
const pcm = (seconds = 4) =>
  Float32Array.from(
    { length: rate * seconds },
    (_, i) => 0.2 * Math.sin((i * 2 * Math.PI * 80) / rate) * Math.exp(-(i % 22050) / 3000),
  );
function executor(stopAfter = Infinity, checkpoints = []) {
  let stopped = false,
    computations = 0;
  const partials = [];
  return {
    partials,
    get computations() {
      return computations;
    },
    canceled() {
      return stopped;
    },
    progress(done) {
      if (done === stopAfter) stopped = true;
    },
    partial(value) {
      partials.push(value);
    },
    checkpoint(bytes, partial) {
      checkpoints.push({ bytes: bytes.slice(0), partial });
    },
    async compute(name, frames, a, b) {
      if (stopped) throw Error('Canceled');
      ++computations;
      assert.equal(name, 'htdemucs');
      assert.equal(frames, 1500);
      const left = new Float32Array(a),
        right = new Float32Array(b);
      const out = new Float32Array(8 * left.length);
      for (let i = 0; i < left.length; ++i) {
        out[i] = left[i] * 0.5;
        out[left.length + i] = right[i] * 0.5;
      }
      return out.buffer;
    },
  };
}

test('real segment boundary cancel restores exact source state and skips completed GPU work', async () => {
  const input = pcm(),
    fullExecutor = executor();
  const full = await analyseDrums(fullExecutor, input.buffer, input.buffer);
  const checkpoints = [],
    first = executor(1, checkpoints);
  await assert.rejects(analyseDrums(first, input.buffer, input.buffer), /Canceled/);
  assert.equal(first.computations, 1);
  assert.equal(checkpoints.length, 1);
  const saved = checkpoints[0];
  const state = decodeDrumResume(saved.bytes, input.length);
  assert.equal(state.next, 1);
  assert.equal(state.divided, 129024);
  assert.equal(state.measured, state.divided);
  assert.ok(state.drums.length < 50000, 'only the STFT margin and OLA tail survive');
  assert.ok(saved.bytes.byteLength < 2 * 1024 * 1024);
  assert.equal(saved.partial.complete, false);
  assert.ok(saved.partial.hits.until > 0);
  const second = executor();
  const resumed = await analyseDrums(second, input.buffer, input.buffer, saved.bytes);
  assert.equal(second.computations, fullExecutor.computations - first.computations);
  assert.deepEqual(resumed, full, 'Float32 OLA, energy and final hits match uninterrupted source');
  assert.deepEqual(
    second.partials[0],
    saved.partial,
    'covered hits arrive before the next segment',
  );
});

test('invalid, truncated, oversized and contradictory checkpoints reject before compute', async () => {
  const input = pcm(),
    saved = [],
    first = executor(1, saved);
  await assert.rejects(analyseDrums(first, input.buffer, input.buffer), /Canceled/);
  const corrupt = saved[0].bytes.slice(0);
  new DataView(corrupt).setUint32(12, 999, true);
  const nonfinite = saved[0].bytes.slice(0);
  new DataView(nonfinite).setFloat32(64, NaN, true);
  for (const value of [
    new ArrayBuffer(12),
    saved[0].bytes.slice(0, -4),
    new ArrayBuffer(2 * 1024 * 1024 + 1),
    corrupt,
    nonfinite,
  ]) {
    const native = executor();
    await assert.rejects(analyseDrums(native, input.buffer, input.buffer, value), /ResumeInvalid/);
    assert.equal(native.computations, 0);
  }
  assert.throws(() => decodeDrumResume(saved[0].bytes, input.length + 1), /ResumeInvalid/);
});

test('success makes no resumable complete checkpoint, repeated stop and resume stays bit exact', async () => {
  const input = pcm(8),
    reference = await analyseDrums(executor(), input.buffer, input.buffer);
  let bytes = new ArrayBuffer(0),
    count = 0;
  for (let next = 1; next <= 2; ++next) {
    const checkpoints = [],
      native = executor(next, checkpoints);
    await assert.rejects(analyseDrums(native, input.buffer, input.buffer, bytes), /Canceled/);
    count += native.computations;
    bytes = checkpoints.at(-1).bytes;
    assert.equal(decodeDrumResume(bytes, input.length).next, next);
  }
  const checkpoints = [],
    last = executor(Infinity, checkpoints);
  assert.deepEqual(await analyseDrums(last, input.buffer, input.buffer, bytes), reference);
  count += last.computations;
  assert.equal(count, 3);
  assert.ok(checkpoints.every(({ bytes }) => decodeDrumResume(bytes, input.length).next < 3));
});

// The maximum admitted track still fits the fixed state budget without truncating any band/energy.
test('600 second source state stays within the private resume byte budget', () => {
  const total = 44100 * 600,
    next = Math.ceil(total / 129024) - 1;
  const divided = next * 129024,
    measured = divided;
  const done = Math.floor(divided / 882) - 4 + 1;
  const tailStart = Math.min(Math.max(0, done - 4) * 882, measured, divided);
  const reached = Math.min(total, (next - 1) * 129024 + 172032);
  const sizes = [
    reached - tailStart,
    reached - divided,
    done,
    done,
    done,
    Math.ceil(measured / 441),
  ];
  const bytes = 64 + sizes.reduce((sum, size) => sum + size * 4, 0);
  assert.ok(bytes < 2 * 1024 * 1024);
  const buffer = new ArrayBuffer(bytes),
    view = new DataView(buffer);
  [0x31524446, 1, total, next, divided, tailStart, done, measured, ...sizes, 0, 0].forEach(
    (value, index) => view.setUint32(index * 4, value, true),
  );
  const restored = decodeDrumResume(buffer, total);
  assert.equal(restored.done, done);
  assert.equal(restored.bands[0].length, done);
  assert.equal(restored.drumEnergy.length, Math.ceil(measured / 441));
});
