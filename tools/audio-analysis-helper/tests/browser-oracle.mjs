// Real hardware compute comparison: official installed Chrome, never bundled Chromium/software GPU.
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
const playwright = process.env.FOLIA_PLAYWRIGHT_MODULE;
const { chromium } = await import(
  playwright ? pathToFileURL(path.resolve(playwright)).href : 'playwright'
);
const root = path.resolve('.');
const cache = path.join(root, 'build/audio-analysis');
const modulePath = path.join(root, 'tools/audio-analysis-helper/generated/source-models.mjs');
const runtime = path.join(root, 'tools/audio-analysis-helper/generated/graph-runtime.js');
const model = process.argv[2] || 'beat_this';
const frames = 100;
const inputs =
  model === 'beat_this'
    ? [['mel', new Float32Array(frames * 128)]]
    : [
        ['left', new Float32Array(172032)],
        ['right', new Float32Array(172032)],
      ];
for (const [name, data] of inputs) {
  for (let i = 0; i < data.length; i++)
    data[i] =
      model === 'beat_this'
        ? 3 + 2 * Math.sin(i * 0.017)
        : 0.12 * Math.sin(i * 0.013 + (name === 'right' ? 0.7 : 0)) + 0.03 * Math.sin(i * 0.071);
  await fs.writeFile(path.join(cache, `${model}-${name}.f32`), new Uint8Array(data.buffer));
}
const output = path.join(cache, `${model}-native.f32`);
const native = spawnSync(
  path.join(cache, 'native/echo-audio-analysis-helper'),
  [
    '--model',
    model,
    '--model-file',
    path.join(cache, `dependencies/${model}-wgsl.bin`),
    '--runtime',
    runtime,
    '--frames',
    String(frames),
    '--input',
    path.join(cache, `${model}-${inputs[0][0]}.f32`),
    ...(model === 'htdemucs' ? ['--right', path.join(cache, `${model}-right.f32`)] : []),
    '--output',
    output,
  ],
  { encoding: 'utf8', timeout: 120000 },
);
assert.equal(native.status, 0, native.stderr + native.stdout);
const nativeReply = JSON.parse(native.stdout);
assert.equal(nativeReply.backend, 'Metal');
const files = {
  '/models.mjs': modulePath,
  '/model.bin': path.join(cache, `dependencies/${model}-wgsl.bin`),
  '/native.f32': output,
};
for (const [name] of inputs) files[`/${name}.f32`] = path.join(cache, `${model}-${name}.f32`);
const server = http.createServer(async (req, res) => {
  if (req.url === '/') {
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end('<!doctype html><title>Native source oracle</title>');
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
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const comparison = await page.evaluate(
    async ({ model, frames }) => {
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
      if (!adapter || adapter.info.isFallbackAdapter) throw Error('real hardware WebGPU required');
      const device = await adapter.requestDevice();
      const api = await import('/models.mjs');
      const raw = await (await fetch('/model.bin')).arrayBuffer();
      const at = Number(new DataView(raw).getBigUint64(raw.byteLength - 8, true));
      const manifest = JSON.parse(new TextDecoder().decode(raw.slice(at, raw.byteLength - 16)));
      const weights = new Uint8Array(raw, 0, at);
      const runtime =
        model === 'beat_this'
          ? await api.BeatThisWgsl.create(device, weights, manifest)
          : await api.HtdemucsWgsl.create(device, weights, manifest, { sources: [0, 1, 2, 3] });
      const read = async (name) =>
        new Float32Array(await (await fetch(`/${name}.f32`)).arrayBuffer());
      const actual =
        model === 'beat_this'
          ? await runtime.run(await read('mel'), { slice: 'stage' })
          : await runtime.run(await read('left'), await read('right'), { slice: 'stage' });
      const reference =
        model === 'beat_this'
          ? [...actual.beat, ...actual.downbeat]
          : actual.stems.flatMap((stem) => [...stem.left, ...stem.right]);
      const native = await read('native');
      if (native.length !== reference.length) throw Error('output length differs');
      let max = 0,
        square = 0,
        power = 0;
      for (let i = 0; i < native.length; i++) {
        const difference = Math.abs(native[i] - reference[i]);
        if (!Number.isFinite(difference)) throw Error('non-finite output');
        max = Math.max(max, difference);
        square += difference * difference;
        power += reference[i] * reference[i];
      }
      runtime.destroy();
      device.destroy();
      return {
        adapter: {
          vendor: adapter.info.vendor,
          architecture: adapter.info.architecture,
          device: adapter.info.device,
          description: adapter.info.description,
          isFallbackAdapter: adapter.info.isFallbackAdapter,
        },
        count: native.length,
        max,
        rmse: Math.sqrt(square / native.length),
        snr: square === 0 ? Infinity : 10 * Math.log10(power / square),
      };
    },
    { model, frames },
  );
  assert.ok(comparison.snr > 60, JSON.stringify(comparison));
  console.log(
    JSON.stringify(
      { model, native: nativeReply, chromeVersion: browser.version(), comparison },
      null,
      2,
    ),
  );
} finally {
  await browser.close();
  server.close();
}
