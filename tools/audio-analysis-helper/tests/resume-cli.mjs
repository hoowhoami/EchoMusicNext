// SPDX-License-Identifier: AGPL-3.0-only
// Manual real-model process cancellation/checkpoint oracle. No SDK or plugin sees these paths.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';

const output = path.resolve(
  process.env.FOLIA_RESUME_OUTPUT_DIR || 'build/audio-analysis/resume-20261028/cli',
);
const helper = path.resolve(
  process.env.FOLIA_ANALYSIS_HELPER || 'build/audio-analysis/native/echo-audio-analysis-helper',
);
const runtime = path.resolve(
  process.env.FOLIA_RESUME_RUNTIME || 'tools/audio-analysis-helper/generated',
);
const models = path.resolve(
  process.env.FOLIA_ANALYSIS_MODELS || 'build/audio-analysis/dependencies',
);
await fs.mkdir(output, { recursive: true });
const directory = await fs.mkdtemp(path.join(output, 'private-job-'));
const data = new Float32Array(4 * 44100),
  right = new Float32Array(data.length);
for (let i = 0; i < data.length; ++i) {
  data[i] = 0.1 * Math.sin(i * 0.031);
  right[i] = 0.08 * Math.sin(i * 0.041);
}
await fs.writeFile(path.join(directory, 'left.f32'), Buffer.from(data.buffer), { mode: 0o600 });
await fs.writeFile(path.join(directory, 'right.f32'), Buffer.from(right.buffer), { mode: 0o600 });
const key = crypto
  .createHash('sha256')
  .update('authorized-account:actual-source:profile')
  .digest('hex');
const argumentsFor = (extra = []) => [
  '--kind',
  'drumHits',
  '--models',
  models,
  '--runtime',
  runtime,
  '--input',
  path.join(directory, 'left.f32'),
  '--right',
  path.join(directory, 'right.f32'),
  '--checkpoint-key',
  key,
  ...extra,
];
async function run(kill = false, extra = []) {
  const child = spawn(helper, argumentsFor(extra), { stdio: ['ignore', 'pipe', 'pipe'] });
  let pending = '',
    stderr = '';
  const events = [];
  child.stderr.on('data', (value) => {
    stderr += value;
  });
  child.stdout.on('data', (chunk) => {
    pending += chunk;
    for (;;) {
      const index = pending.indexOf('\n');
      if (index < 0) break;
      const event = JSON.parse(pending.slice(0, index));
      pending = pending.slice(index + 1);
      events.push(event);
      if (kill && event.type === 'progress' && event.done === 1) child.kill('SIGKILL');
    }
  });
  const result = await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('close', (code, signal) => resolve({ code, signal, events, stderr }));
  });
  return result;
}
const first = await run(true);
assert.equal(first.signal, 'SIGKILL');
const checkpoint = first.events.find((event) => event.type === 'checkpoint');
assert.ok(checkpoint);
assert.ok(first.events.every((event) => !('path' in event)));
const checkpointPath = path.join(directory, 'drum-resume-output.bin');
const bytes = await fs.readFile(checkpointPath);
assert.equal(bytes.length, checkpoint.bytes);
assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), checkpoint.sha256);
assert.equal((await fs.stat(checkpointPath)).mode & 0o777, 0o600);
await fs.writeFile(path.join(directory, 'drum-resume-input.bin'), bytes, { mode: 0o600 });
const second = await run();
assert.equal(second.code, 0, second.stderr);
assert.deepEqual(
  second.events.filter((event) => event.type === 'progress').map((event) => event.done),
  [2],
);
assert.deepEqual(second.events.find((event) => event.type === 'partial').result, checkpoint.result);
assert.equal(second.events.at(-1).result.complete, true);
await assert.rejects(fs.stat(checkpointPath), { code: 'ENOENT' });
const full = JSON.parse(await fs.readFile(path.resolve(output, '../native/complete.json'), 'utf8'));
assert.deepEqual(
  second.events.at(-1).result,
  full,
  'actual two-process model output equals uninterrupted C++ oracle',
);
// No file from stdout is consulted. A fixed-path symlink cannot redirect writes.
const sentinel = path.join(directory, 'sentinel');
await fs.writeFile(sentinel, 'untouched');
await fs.symlink(sentinel, checkpointPath);
const rejected = await run();
assert.equal(rejected.code, 1);
assert.match(rejected.events.at(-1).error, /ResumeInvalid/);
assert.equal(await fs.readFile(sentinel, 'utf8'), 'untouched');
await fs.unlink(checkpointPath);
const changed = Buffer.from(bytes);
changed[changed.length - 1] ^= 1;
await fs.writeFile(path.join(directory, 'drum-resume-input.bin'), changed);
const corrupt = await run();
assert.equal(corrupt.code, 1);
assert.match(corrupt.events.at(-1).error, /ResumeInvalid/);
assert.equal(corrupt.events.filter((event) => event.type === 'progress').length, 0);
await fs.writeFile(
  path.join(output, 'result.json'),
  JSON.stringify(
    {
      first,
      second,
      rejected,
      corrupt,
      checkpointBytes: bytes.length,
      checkpointSha256: checkpoint.sha256,
      finalSha256: crypto.createHash('sha256').update(JSON.stringify(full)).digest('hex'),
      mathematicalOracle: 'native/complete.json uninterrupted actual Metal model',
    },
    null,
    2,
  ) + '\n',
);
console.log(
  'PASS: actual process kill at segment one, validated atomic checkpoint, resume only segment two, exact complete oracle, symlink and corruption rejected',
);
