// Build-time only. The native helper never launches Node/Electron/a browser.
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const here = path.dirname(fileURLToPath(import.meta.url));
const reference = path.resolve(process.argv[2] || path.join(here, 'upstream'));
const commit = 'a8da4437cf19723555bd75e4685ca20815fe5b82';
let bundled = null;
try {
  bundled = JSON.parse(await fs.readFile(path.join(reference, 'SOURCE.json'), 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
if (bundled) {
  if (
    bundled.commit !== commit ||
    bundled.repository !== 'https://github.com/chthollyphile/folia-major.git'
  )
    throw Error('Bundled Folia source identity differs from the pinned reference');
} else if (
  execFileSync('git', ['rev-parse', 'HEAD'], { cwd: reference, encoding: 'utf8' }).trim() !== commit
) {
  throw Error('Folia reference HEAD is not the pinned commit');
}
// Validate preferred inputs before replacing any generated artifact. The
// original tsconfig affects class-field semantics and is part of the source.
const recorded =
  bundled || JSON.parse(await fs.readFile(path.join(here, 'generated/provenance.json'), 'utf8'));
for (const name of new Set([...Object.keys(recorded.hashes), 'LICENSE', 'tsconfig.json'])) {
  const bytes = await fs.readFile(path.join(reference, name));
  if (bundled) {
    if (crypto.createHash('sha256').update(bytes).digest('hex') !== bundled.hashes[name])
      throw Error(`modified bundled source: ${name}`);
  } else {
    const original = execFileSync('git', ['show', `${commit}:${name}`], {
      cwd: reference,
      maxBuffer: 4 * 1024 * 1024,
    });
    if (!bytes.equals(original)) throw Error(`modified pinned source: ${name}`);
  }
}
// Reuse the existing locked author-tool dependencies; no machine-specific path.
// Run from the plugin workspace, or pass its package.json as the third argument.
const toolsPackage = path.resolve(process.argv[3] || path.join(process.cwd(), 'package.json'));
const require = createRequire(toolsPackage);
const esbuild = require('esbuild');
const ts = require('typescript');
const prettier = require('prettier');
if (esbuild.version !== '0.28.1' || ts.version !== '5.9.3' || prettier.version !== '3.9.10')
  throw Error('Use the locked author tools: esbuild0.28.1 / TypeScript5.9.3 / Prettier3.9.10');
const formatConfig = JSON.parse(await fs.readFile(path.join(here, 'source-format.json'), 'utf8'));
async function formatGenerated(name) {
  const file = path.join(here, 'generated', name);
  const code = await prettier.format(await fs.readFile(file, 'utf8'), {
    ...formatConfig,
    parser: 'babel',
  });
  await fs.writeFile(file + '.generated-tmp', code);
  await fs.rename(file + '.generated-tmp', file);
}
const source = [
  'src/services/analysis/beatThisWgsl/runtime.ts',
  'src/services/analysis/beatThisWgsl/kernels.ts',
  'src/services/analysis/htdemucsWgsl/runtime.ts',
  'src/services/analysis/htdemucsWgsl/kernels.ts',
];
const hashes = {};
for (const name of source)
  hashes[name] = crypto
    .createHash('sha256')
    .update(await fs.readFile(path.join(reference, name)))
    .digest('hex');
await fs.mkdir(path.join(here, 'generated'), { recursive: true });
const entry = `export {BeatThisWgsl} from ${JSON.stringify(path.join(reference, source[0]))};\nexport {HtdemucsWgsl} from ${JSON.stringify(path.join(reference, source[2]))};`;
const gpuEntry =
  entry +
  `\nexport {runPercussiveEnergy} from ${JSON.stringify(path.join(reference, 'src/services/analysis/beatThisWgsl/percussive.ts'))};`;
const gpuBuild = await esbuild.build({
  tsconfig: path.join(reference, 'tsconfig.json'),
  stdin: { contents: gpuEntry, resolveDir: reference },
  bundle: true,
  format: 'esm',
  platform: 'neutral',
  metafile: true,
  target: 'es2016',
  outfile: path.join(here, 'generated/source-models.mjs'),
  banner: {
    js: '// Folia a8da4437; AGPL-3.0-only. Generated without algorithm changes; see ../LICENSE.',
  },
});
const signalDir = path.join(reference, 'src/services/analysis');
const signalEntry = `export {melSpectrogram,splitForModel,aggregateChunks,beatsFromLogits} from ${JSON.stringify(path.join(signalDir, 'beatThis.ts'))};
export {computeBeatAccents,resolveBeatConfidence} from ${JSON.stringify(path.join(signalDir, 'beatAccents.ts'))};
export {drumBandTracks,pickBandHits,drumCoverage,energyFrames,liveTailFrames} from ${JSON.stringify(path.join(signalDir, 'drumHits.ts'))};
export {createDrumBandStream} from ${JSON.stringify(path.join(signalDir, 'drumHitStream.ts'))};
export {stemSegmentStarts,stemSegmentWindow} from ${JSON.stringify(path.join(signalDir, 'stemRange.ts'))};`;
const signalBuild = await esbuild.build({
  tsconfig: path.join(reference, 'tsconfig.json'),
  stdin: { contents: signalEntry, resolveDir: reference },
  bundle: true,
  format: 'esm',
  metafile: true,
  platform: 'neutral',
  target: 'es2016',
  outfile: path.join(here, 'generated/source-signal.mjs'),
  plugins: [
    {
      name: 'pure-analysis-boundary',
      setup(build) {
        build.onLoad(
          { filter: /\/(beatThis|drumHits|stemRange)\.ts$/ },
          async ({ path: filename }) => {
            let contents = await fs.readFile(filename, 'utf8');
            hashes[path.relative(reference, filename)] = crypto
              .createHash('sha256')
              .update(contents)
              .digest('hex');
            if (filename.endsWith('/beatThis.ts')) {
              contents = contents.replace(/^import[\s\S]*?;\n/gm, (value) =>
                value.includes("'../automix/signalAnalysis'") ||
                value.includes("'../automix/uiYield'")
                  ? value
                  : '',
              );
            } else if (filename.endsWith('/drumHits.ts')) {
              contents = contents
                .slice(0, contents.indexOf('const isDrumAnalysis'))
                .replace(/^import[\s\S]*?;\n/gm, '')
                .replace('const liveTailFrames =', 'export const liveTailFrames =');
              contents =
                `import {BEAT_THIS_FPS,BEAT_THIS_SAMPLE_RATE,MEL_BANDS,melBandCentresHz} from './beatThis';\n` +
                contents;
            } else {
              contents = contents
                .slice(0, contents.indexOf('// --- the segment cache ---'))
                .replace(/^import[\s\S]*?;\n/gm, '');
              contents = 'const HTDEMUCS_GPU_SEGMENT=172032;\n' + contents;
            }
            return { contents, loader: 'ts', resolveDir: path.dirname(filename) };
          },
        );
      },
    },
  ],
});
const output = await esbuild.build({
  entryPoints: [path.join(here, 'graph-recorder.mjs')],
  bundle: true,
  write: false,
  format: 'iife',
  globalName: 'AnalysisGraph',
  platform: 'neutral',
  target: 'es2016',
});
// QV4 complex native generators have independently reproduced lifetime faults.
// Standard TypeScript iterator lowering preserves each source await/yield boundary.
let lowered = ts.transpileModule(output.outputFiles[0].text, {
  compilerOptions: {
    target: ts.ScriptTarget.ES5,
    module: ts.ModuleKind.None,
    downlevelIteration: true,
  },
}).outputText;
// Explicitly hoist generated helper locals; QV4 warns on the legal TS comma declarations.
lowered = lowered
  .replace(/var _ =([^\n]+), f, y, t, g = ([^\n]+);/, 'var f, y, t, g; var _ =$1; g = $2;')
  .replaceAll('var fulfilled = function', 'var step;\n            var fulfilled = function')
  .replaceAll('var step = function', 'step = function');
await fs.writeFile(path.join(here, 'generated/graph-runtime.js'), lowered);
const signalOutput = await esbuild.build({
  entryPoints: [path.join(here, 'signal-job.mjs')],
  bundle: true,
  write: false,
  format: 'iife',
  globalName: 'AnalysisSignal',
  platform: 'neutral',
  target: 'es2016',
});
let signalCode = ts.transpileModule(signalOutput.outputFiles[0].text, {
  compilerOptions: {
    target: ts.ScriptTarget.ES5,
    module: ts.ModuleKind.None,
    downlevelIteration: true,
  },
}).outputText;
signalCode = signalCode
  .replace(/var _ =([^\n]+), f, y, t, g = ([^\n]+);/, 'var f, y, t, g; var _ =$1; g = $2;')
  .replaceAll('var fulfilled = function', 'var step;\n            var fulfilled = function')
  .replaceAll('var step = function', 'step = function');
await fs.writeFile(path.join(here, 'generated/signal-runtime.js'), signalCode);
for (const value of [
  ...Object.keys(gpuBuild.metafile.inputs),
  ...Object.keys(signalBuild.metafile.inputs),
]) {
  const absolute = path.resolve(value);
  if (!absolute.startsWith(path.resolve(reference) + '/')) continue;
  const relative = path.relative(reference, absolute);
  const current = await fs.readFile(absolute);
  if (bundled) {
    if (crypto.createHash('sha256').update(current).digest('hex') !== bundled.hashes[relative])
      throw Error(`modified bundled source: ${relative}`);
  } else {
    const pinned = execFileSync('git', ['show', `${commit}:${relative}`], {
      cwd: reference,
      maxBuffer: 4 * 1024 * 1024,
    });
    if (!current.equals(pinned)) throw Error(`modified pinned source: ${relative}`);
  }
  hashes[relative] = crypto.createHash('sha256').update(current).digest('hex');
}
for (const name of [
  'graph-runtime.js',
  'signal-runtime.js',
  'source-models.mjs',
  'source-signal.mjs',
])
  await formatGenerated(name);
const artifacts = {};
for (const name of [
  'graph-runtime.js',
  'signal-runtime.js',
  'source-models.mjs',
  'source-signal.mjs',
])
  artifacts[name] = crypto
    .createHash('sha256')
    .update(await fs.readFile(path.join(here, 'generated', name)))
    .digest('hex');
await fs.mkdir(path.join(here, 'third_party'), { recursive: true });
await fs.copyFile(
  require.resolve('typescript/LICENSE.txt'),
  path.join(here, 'third_party/typescript-LICENSE.txt'),
);
await fs.copyFile(
  require.resolve('esbuild/LICENSE.md'),
  path.join(here, 'third_party/esbuild-LICENSE.md'),
);
await fs.writeFile(
  path.join(here, 'generated/provenance.json'),
  JSON.stringify(
    {
      commit,
      hashes: Object.fromEntries(Object.entries(hashes).sort(([a], [b]) => a.localeCompare(b))),
      artifacts,
      license: 'AGPL-3.0-only',
      adaptation:
        'GPU API recorder; native graph execution, f32 source option; standard ES5 iterator lowering; bounded drum checkpoint/restore wrapper, pinned signal/WGSL math unchanged',
    },
    null,
    2,
  ) + '\n',
);

// Ship the actual preferred TypeScript inputs alongside the native bridge and
// generator. Generated JavaScript plus a remote URL is not a source snapshot.
const upstream = path.join(here, 'upstream');
await fs.mkdir(upstream, { recursive: true });
const sourceHashes = { ...hashes };
for (const name of [...Object.keys(hashes), 'LICENSE', 'tsconfig.json']) {
  const from = path.join(reference, name),
    to = path.join(upstream, name);
  const bytes = await fs.readFile(from);
  sourceHashes[name] = crypto.createHash('sha256').update(bytes).digest('hex');
  if (bundled && sourceHashes[name] !== bundled.hashes[name])
    throw Error(`modified bundled source: ${name}`);
  await fs.mkdir(path.dirname(to), { recursive: true });
  if (path.resolve(from) !== path.resolve(to)) await fs.copyFile(from, to);
}
await fs.writeFile(
  path.join(upstream, 'SOURCE.json'),
  JSON.stringify(
    {
      repository: 'https://github.com/chthollyphile/folia-major.git',
      commit,
      hashes: Object.fromEntries(
        Object.entries(sourceHashes).sort(([a], [b]) => a.localeCompare(b)),
      ),
    },
    null,
    2,
  ) + '\n',
);
