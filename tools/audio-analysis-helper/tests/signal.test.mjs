// AGPL-3.0-only. Arithmetic/stream contract at the model executor boundary; GPU is tested separately.
import assert from 'node:assert/strict';
import test from 'node:test';
import { analyseDrums, sourceHalfRate } from '../signal-job.mjs';
import { melSpectrogram, drumBandTracks, pickBandHits } from '../generated/source-signal.mjs';

test('drum live head is emitted after ten seconds with a safe until; complete pick matches source', async () => {
  const rate = 44100,
    input = Float32Array.from(
      { length: rate * 13 },
      (_, i) => 0.2 * Math.sin((i * 2 * Math.PI * 80) / rate) * Math.exp(-(i % 22050) / 3000),
    );
  const partials = [],
    progress = [];
  const executor = {
    progress(...args) {
      progress.push(args);
    },
    partial(value) {
      partials.push(value);
    },
    async compute(name, frames, a, b) {
      assert.equal(name, 'htdemucs');
      assert.equal(frames, 1500);
      const left = new Float32Array(a),
        right = new Float32Array(b),
        output = new Float32Array(8 * left.length);
      for (let i = 0; i < left.length; ++i) {
        output[i] = left[i] * 0.5;
        output[i + left.length] = right[i] * 0.5;
      }
      return output.buffer;
    },
  };
  const output = await analyseDrums(executor, input.buffer, input.buffer);
  assert.ok(partials.length > 0, 'source live heads must precede full separation');
  for (const partial of partials) {
    assert.equal(partial.complete, false);
    assert.equal(partial.backend, 'gpu');
    assert.ok(partial.hits.until >= 10 && partial.hits.until < 13);
    assert.ok(partial.hits.times.every((time) => time < partial.hits.until));
    assert.equal(partial.hits.times.length, partial.hits.kinds.length);
  }
  assert.equal(output.complete, true);
  assert.ok(output.coverage > 0.08);
  assert.ok(progress.length > 1);
  assert.equal(progress.at(-1)[2], 13);
  // The executor's known stem is exactly half the PCM. Source final picking must agree.
  const referenceStem = input.map((value) => value * 0.5);
  const expected = pickBandHits(
    drumBandTracks(await melSpectrogram(sourceHalfRate(referenceStem))),
    output.durationSeconds * 50 + 1,
  );
  assert.deepEqual(output.hits.times, Array.from(expected.times));
  assert.deepEqual(output.hits.kinds, Array.from(expected.kinds));
  for (let i = 0; i < expected.strengths.length; ++i)
    assert.ok(Math.abs(output.hits.strengths[i] - expected.strengths[i]) < 1e-5);
});
