// AGPL-3.0-only. Source drumHits.ts OfflineAudioContext 44100 -> 22050 path.
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { sourceHalfRate } from '../signal-job.mjs';

const playwright = process.env.FOLIA_PLAYWRIGHT_MODULE;
const { chromium } = await import(
  playwright ? pathToFileURL(path.resolve(playwright)).href : 'playwright'
);
const browser = await chromium.launch({ channel: 'chrome', headless: true });
try {
  const page = await browser.newPage();
  const lengths = [1, 2, 3, 65, 127, 128, 129, 1023, 1024, 1025, 44101, 176401];
  const results = [];
  for (const length of lengths) {
    const samples = Float32Array.from(
      { length },
      (_, i) => Math.sin(i * 0.097) * 0.21 + (i % 127 === 0 ? 0.4 : 0),
    );
    const expected = sourceHalfRate(samples);
    const actual = await page.evaluate(async (input) => {
      const samples = new Float32Array(input);
      const context = new OfflineAudioContext(1, Math.ceil(samples.length / 2), 22050);
      const source = context.createBufferSource();
      source.buffer = context.createBuffer(1, samples.length, 44100);
      source.buffer.copyToChannel(samples, 0);
      source.connect(context.destination);
      source.start();
      return Array.from((await context.startRendering()).getChannelData(0));
    }, Array.from(samples));
    assert.equal(actual.length, expected.length);
    let max = 0;
    for (let i = 0; i < actual.length; ++i) max = Math.max(max, Math.abs(actual[i] - expected[i]));
    assert.equal(max, 0, `non-integer interpolation at ${length}`);
    results.push({ length, output: actual.length, max });
  }
  console.log(
    JSON.stringify(
      { chrome: browser.version(), profile: 'source OfflineAudioContext 2:1', results },
      null,
      2,
    ),
  );
} finally {
  await browser.close();
}
