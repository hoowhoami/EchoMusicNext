// AGPL-3.0-only. Full pinned signal pipeline + actual native/browser GPU, same PCM.
import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import http from 'node:http';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
const playwright = process.env.FOLIA_PLAYWRIGHT_MODULE;
const { chromium } = await import(
  playwright ? pathToFileURL(path.resolve(playwright)).href : 'playwright'
);
const root = path.resolve('.'),
  base = path.join(root, 'tools/audio-analysis-helper');
const cache = path.join(root, 'build/audio-analysis');
const kind = process.argv[2] || 'beats';
const seconds = Number(process.argv[3] || 4);
assert.ok(seconds >= 1 && seconds <= 20);
const rate = kind === 'beats' ? 22050 : 44100;
const channels = kind === 'beats' ? ['left'] : ['left', 'right'];
for (const name of channels) {
  const data = new Float32Array(rate * seconds);
  for (let i = 0; i < data.length; ++i) {
    const local = (i % (rate / 2)) / rate;
    const envelope = Math.exp(-local * 50);
    data[i] =
      0.22 * Math.sin((2 * Math.PI * 80 * i) / rate) * envelope +
      0.015 * Math.sin((2 * Math.PI * 660 * i) / rate + (name === 'right' ? 0.3 : 0));
  }
  await fs.writeFile(path.join(cache, `profile-${kind}-${name}.f32`), new Uint8Array(data.buffer));
}
const reply = spawnSync(
  path.join(cache, 'native/echo-audio-analysis-helper'),
  [
    '--kind',
    kind,
    '--models',
    path.join(cache, 'dependencies'),
    '--runtime',
    path.join(base, 'generated'),
    '--input',
    path.join(cache, `profile-${kind}-left.f32`),
    ...(kind === 'drumHits' ? ['--right', path.join(cache, `profile-${kind}-right.f32`)] : []),
  ],
  { encoding: 'utf8', timeout: 120000, maxBuffer: 4 * 1024 * 1024 },
);
assert.equal(reply.status, 0, reply.stderr + reply.stdout);
const messages = reply.stdout
  .trim()
  .split('\n')
  .map((line) => JSON.parse(line));
const native = messages.at(-1),
  nativePartials = messages
    .filter((value) => value.type === 'partial')
    .map((value) => value.result);
assert.equal(native.ok, true);
const files = {
  '/models.mjs': path.join(base, 'generated/source-models.mjs'),
  '/signal-job.mjs': path.join(base, 'signal-job.mjs'),
  '/typed-array-compat.mjs': path.join(base, 'typed-array-compat.mjs'),
  '/generated/source-signal.mjs': path.join(base, 'generated/source-signal.mjs'),
  '/left.f32': path.join(cache, `profile-${kind}-left.f32`),
  '/right.f32': path.join(cache, `profile-${kind}-right.f32`),
  '/beat_this.bin': path.join(cache, 'dependencies/beat_this-wgsl.bin'),
  '/htdemucs.bin': path.join(cache, 'dependencies/htdemucs-wgsl.bin'),
};
const server = http.createServer(async (req, res) => {
  if (req.url === '/') {
    res.setHeader('Content-Type', 'text/html');
    res.end('<!doctype html>');
    return;
  }
  if (!files[req.url]) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.setHeader(
    'Content-Type',
    req.url.endsWith('.mjs') ? 'text/javascript' : 'application/octet-stream',
  );
  res.end(await fs.readFile(files[req.url]));
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu', '--use-angle=metal'],
});
try {
  const session = await browser.newBrowserCDPSession();
  const system = await session.send('SystemInfo.getInfo');
  assert.match(JSON.stringify(system.gpu.devices), /Apple/);
  assert.doesNotMatch(JSON.stringify(system.gpu.devices), /SwiftShader|llvmpipe/i);
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const expected = await page.evaluate(async (kind) => {
    const source = await import('/signal-job.mjs'),
      gpu = await import('/models.mjs');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter || adapter.info.isFallbackAdapter) throw Error('hardware adapter required');
    const device = await adapter.requestDevice();
    const runtimes = new Map();
    async function runtime(name) {
      if (runtimes.has(name)) return runtimes.get(name);
      const raw = await (await fetch(`/${name}.bin`)).arrayBuffer();
      const at = Number(new DataView(raw).getBigUint64(raw.byteLength - 8, true));
      const manifest = JSON.parse(new TextDecoder().decode(raw.slice(at, raw.byteLength - 16)));
      const weights = new Uint8Array(raw, 0, at);
      const result =
        name === 'beat_this'
          ? await gpu.BeatThisWgsl.create(device, weights, manifest)
          : await gpu.HtdemucsWgsl.create(device, weights, manifest, { sources: [0, 1, 2, 3] });
      runtimes.set(name, result);
      return result;
    }
    const partials = [];
    const bridge = {
      progress() {},
      partial(value) {
        partials.push(value);
      },
      async compute(name, frames, a, b) {
        if (name === 'percussive') {
          const value = await gpu.runPercussiveEnergy(device, new Float32Array(a), frames);
          const data = new Float32Array(2 * frames);
          for (let i = 0; i < frames; i++) {
            data[2 * i] = value.percussive[i];
            data[2 * i + 1] = value.total[i];
          }
          return data.buffer;
        }
        const value =
          name === 'beat_this'
            ? await (await runtime(name)).run(new Float32Array(a), { slice: 'stage' })
            : await (
                await runtime(name)
              ).run(new Float32Array(a), new Float32Array(b), { slice: 'stage' });
        if (name === 'htdemucs') return value.stems[0].left.buffer;
        const output = new Float32Array(frames * 2);
        output.set(value.beat);
        output.set(value.downbeat, frames);
        return output.buffer;
      },
    };
    const left = await (await fetch('/left.f32')).arrayBuffer();
    const output =
      kind === 'beats'
        ? await source.analyseBeats(bridge, left)
        : await source.analyseDrums(bridge, left, await (await fetch('/right.f32')).arrayBuffer());
    for (const item of runtimes.values()) item.destroy();
    device.destroy();
    return JSON.parse(JSON.stringify({ result: output, partials }));
  }, kind);
  if (kind === 'beats') {
    assert.deepEqual(native.result.beats, expected.result.beats);
    assert.deepEqual(native.result.downbeats, expected.result.downbeats);
    assert.deepEqual(native.result.accents, expected.result.accents);
  } else {
    assert.equal(native.result.drumless, expected.result.drumless);
    assert.deepEqual(native.result.hits.times, expected.result.hits.times);
    assert.deepEqual(native.result.hits.kinds, expected.result.hits.kinds);
    for (let i = 0; i < expected.result.hits.strengths.length; i++)
      assert.ok(
        Math.abs(native.result.hits.strengths[i] - expected.result.hits.strengths[i]) < 1e-4,
      );
    if (seconds >= 13) {
      assert.ok(nativePartials.length > 0);
      assert.ok(expected.partials.length > 0);
      const first = nativePartials[0],
        source = expected.partials[0];
      assert.equal(first.complete, false);
      assert.equal(first.hits.until, source.hits.until);
      assert.deepEqual(first.hits.times, source.hits.times);
      assert.deepEqual(first.hits.kinds, source.hits.kinds);
      for (let i = 0; i < source.hits.strengths.length; i++)
        assert.ok(Math.abs(first.hits.strengths[i] - source.hits.strengths[i]) < 1e-4);
    }
  }
  console.log(
    JSON.stringify(
      {
        kind,
        chrome: browser.version(),
        hardware: system.gpu.devices,
        nativeMs: native.elapsedMs,
        beats: native.result.beats?.length,
        hits: native.result.hits?.times.length,
        drumless: native.result.drumless,
        coverage: native.result.coverage,
        partialHeads: nativePartials.map((value) => ({
          until: value.hits.until,
          hits: value.hits.times.length,
        })),
        result: 'source full profile agrees',
      },
      null,
      2,
    ),
  );
} finally {
  await browser.close();
  server.close();
}
