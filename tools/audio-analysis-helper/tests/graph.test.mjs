import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import test from 'node:test';
import { recordModelGraph } from '../graph-recorder.mjs';

const cache = path.resolve('build/audio-analysis/dependencies');
async function model(name) {
  const data = await fs.readFile(path.join(cache, `${name}-wgsl.bin`));
  const offset = Number(data.readBigUInt64LE(data.length - 8));
  const manifest = JSON.parse(data.subarray(offset, data.length - 16));
  const rope = manifest.tensors['rope.freqs'];
  return {
    manifest,
    rope: rope
      ? Array.from({ length: 16 }, (_, i) => data.readFloatLE(rope.byte_offset + i * 4))
      : [],
  };
}

test('real source BeatThis graph keeps WGSL, tensor offsets, chunk shape and readback', async () => {
  const graph = await recordModelGraph('beat_this', await model('beat_this'), 100);
  assert.equal(graph.model, 'beat_this');
  assert.equal(graph.frames, 100);
  assert.equal(graph.pipelines.length, 6);
  assert.ok(graph.groups.length > 20);
  assert.ok(graph.batches.flat().filter((value) => value.kind === 'dispatch').length > 50);
  assert.ok(graph.writes.some((value) => value.modelOffset === 0 && value.size > 40_000_000));
  assert.ok(graph.writes.some((value) => value.input === 'mel' && value.size === 100 * 128 * 4));
  assert.equal(graph.outputs.at(-1).size, 100 * 2 * 4);
  for (const pipeline of graph.pipelines) assert.match(pipeline.code, /@compute/);
});

test('real source HTDemucs graph retains all four stems and full ordered layers', async () => {
  const graph = await recordModelGraph('htdemucs', await model('htdemucs'));
  assert.equal(graph.model, 'htdemucs');
  assert.ok(graph.batches.flat().filter((value) => value.kind === 'dispatch').length > 100);
  assert.equal(graph.outputs.at(-1).size, 4 * 2 * 172032 * 4);
  assert.equal(
    graph.writes.filter((value) => value.input === 'left' || value.input === 'right').length,
    2,
  );
  assert.ok(graph.totalBytes < 1024 * 1024 * 1024);
});

test('no synthetic weight bytes are materialized and graph changes with the exact frame count', async () => {
  const input = await model('beat_this');
  const short = await recordModelGraph('beat_this', input, 99);
  const long = await recordModelGraph('beat_this', input, 100);
  assert.notDeepEqual(short.writes, long.writes);
  assert.equal(short.outputs.at(-1).size, 99 * 2 * 4);
  assert.ok(short.writes.every((value) => !value.data || value.data.length < 2_000_000));
  await assert.rejects(() => recordModelGraph('beat_this', input, 0), /frames/);
  await assert.rejects(() => recordModelGraph('beat_this', input, 1501), /frames/);
});
