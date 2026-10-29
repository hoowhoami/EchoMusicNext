// AGPL-3.0-only. Compare resident native runs with the unchanged pinned GPU runtime.
// Dev-only Playwright is supplied by the caller; the helper has no browser dependency.
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.FOLIA_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve('.');
const directory = path.resolve(
  process.env.FOLIA_RESIDENT_OUTPUT_DIR || 'build/audio-analysis/resident-oracle',
);
const models = path.resolve(
  process.env.FOLIA_ANALYSIS_MODELS || 'build/audio-analysis/dependencies',
);
const files = {
  '/models.mjs': path.join(root, 'tools/audio-analysis-helper/generated/source-models.mjs'),
};
for (const model of ['beat_this', 'htdemucs']) {
  files[`/${model}.bin`] = path.join(models, `${model}-wgsl.bin`);
  for (const label of ['a', 'b', 'again']) {
    files[`/${model}-${label}-native.f32`] = path.join(directory, `${model}-${label}-native.f32`);
    for (const channel of model === 'beat_this' ? ['mel'] : ['left', 'right'])
      files[`/${model}-${label}-${channel}.f32`] = path.join(
        directory,
        `${model}-${label === 'again' ? 'a' : label}-${channel}.f32`,
      );
  }
}
const server = http.createServer(async (request, response) => {
  if (request.url === '/') {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html>');
    return;
  }
  const file = files[request.url];
  if (!file) {
    response.writeHead(404);
    response.end();
    return;
  }
  response.setHeader(
    'Content-Type',
    request.url.endsWith('.mjs') ? 'text/javascript' : 'application/octet-stream',
  );
  try {
    response.end(await fs.readFile(file));
  } catch {
    response.writeHead(404);
    response.end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({
    channel: 'chrome',
    headless: true,
    args: ['--enable-unsafe-webgpu', '--use-angle=metal'],
  });
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const comparisons = [];
  for (const model of ['beat_this', 'htdemucs']) {
    const native = JSON.parse(
      await fs.readFile(path.join(directory, `${model}-resident.json`), 'utf8'),
    );
    assert.equal(native.backend, 'Metal');
    assert.equal(native.deviceCreations, 1);
    assert.equal(native.executions, 3);
    assert.equal(native.modelVerifications, 1);
    const comparison = await page.evaluate(async (model) => {
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
      if (!adapter || adapter.info.isFallbackAdapter) throw Error('Hardware GPU required');
      const device = await adapter.requestDevice();
      const source = await import('/models.mjs');
      const raw = await (await fetch(`/${model}.bin`)).arrayBuffer();
      const end = Number(new DataView(raw).getBigUint64(raw.byteLength - 8, true));
      const manifest = JSON.parse(new TextDecoder().decode(raw.slice(end, raw.byteLength - 16)));
      const weights = new Uint8Array(raw, 0, end);
      const runtime =
        model === 'beat_this'
          ? await source.BeatThisWgsl.create(device, weights, manifest)
          : await source.HtdemucsWgsl.create(device, weights, manifest, { sources: [0, 1, 2, 3] });
      const read = async (label, channel) =>
        new Float32Array(await (await fetch(`/${model}-${label}-${channel}.f32`)).arrayBuffer());
      const result = [];
      try {
        for (const label of ['a', 'b', 'again']) {
          const output =
            model === 'beat_this'
              ? await runtime.run(await read(label, 'mel'), { slice: 'stage' })
              : await runtime.run(await read(label, 'left'), await read(label, 'right'), {
                  slice: 'stage',
                });
          const expected =
            model === 'beat_this'
              ? [output.beat, output.downbeat]
              : output.stems.flatMap((stem) => [stem.left, stem.right]);
          const native = await read(label, 'native');
          let index = 0,
            max = 0,
            square = 0,
            power = 0;
          for (const channel of expected)
            for (const value of channel) {
              const delta = native[index++] - value;
              if (!Number.isFinite(delta)) throw Error('Non-finite model output');
              max = Math.max(max, Math.abs(delta));
              square += delta * delta;
              power += value * value;
            }
          if (index !== native.length) throw Error('Output shape differs');
          result.push({
            label,
            count: index,
            max,
            rmse: Math.sqrt(square / index),
            snr: square === 0 ? 1000 : 10 * Math.log10(power / square),
          });
        }
      } finally {
        runtime.destroy();
        device.destroy();
      }
      return {
        adapter: {
          vendor: adapter.info.vendor,
          architecture: adapter.info.architecture,
          isFallbackAdapter: adapter.info.isFallbackAdapter,
        },
        results: result,
      };
    }, model);
    assert.equal(comparison.adapter.isFallbackAdapter, false);
    for (const result of comparison.results)
      assert.ok(result.snr > 60, JSON.stringify({ model, ...result }));
    comparisons.push({ model, native, ...comparison });
  }
  const result = {
    chromeVersion: browser.version(),
    sourceRevision: 'a8da4437cf19723555bd75e4685ca20815fe5b82',
    gateSnrDb: 60,
    comparisons,
    scope: 'floating-point correctness; concurrent workloads exclude performance conclusions',
  };
  await fs.writeFile(
    path.join(directory, 'resident-browser-comparison.json'),
    JSON.stringify(result, null, 2) + '\n',
  );
  console.log(JSON.stringify(result, null, 2));
} finally {
  if (browser) await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
