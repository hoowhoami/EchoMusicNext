import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { verifyAudioAnalysisMaterials } from './verify-stage.mjs';
const source = fileURLToPath(new URL('../audio-analysis-helper/', import.meta.url));
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'analysis-stage materials '));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  fs.cpSync(source, path.join(directory, 'source'), {
    recursive: true,
    filter: (file) => !path.relative(source, file).split(path.sep).includes('tests'),
  });
  fs.mkdirSync(path.join(directory, 'runtime'));
  for (const name of ['graph-runtime.js', 'signal-runtime.js', 'provenance.json'])
    fs.copyFileSync(path.join(source, 'generated', name), path.join(directory, 'runtime', name));
  fs.mkdirSync(path.join(directory, 'licenses'));
  for (const name of ['LICENSE', 'THIRD_PARTY_NOTICES.md'])
    fs.copyFileSync(path.join(source, name), path.join(directory, 'licenses', name));
  fs.cpSync(path.join(source, 'third_party'), path.join(directory, 'licenses/third_party'), {
    recursive: true,
  });
  return directory;
}
test('actual preferred source materials pass without models or native execution', (t) => {
  const directory = fixture(t);
  assert.doesNotThrow(() => verifyAudioAnalysisMaterials(directory));
});
for (const name of ['graph-runtime.js', 'signal-runtime.js'])
  test(`tampered deployed ${name} is rejected despite a correct source snapshot`, (t) => {
    const directory = fixture(t);
    fs.appendFileSync(path.join(directory, 'runtime', name), '\n// damaged stage\n');
    assert.throws(
      () => verifyAudioAnalysisMaterials(directory),
      /artifact.*digest|runtime.*digest/i,
    );
  });
for (const name of [
  'graph-runtime.js',
  'signal-runtime.js',
  'source-models.mjs',
  'source-signal.mjs',
])
  test(`tampered preferred generated ${name} is rejected`, (t) => {
    const directory = fixture(t);
    fs.appendFileSync(
      path.join(directory, 'source/generated', name),
      '\n// stale generated source\n',
    );
    assert.throws(
      () => verifyAudioAnalysisMaterials(directory),
      /artifact.*digest|runtime.*digest/i,
    );
  });
test('two packaged provenance documents must be identical', (t) => {
  const directory = fixture(t),
    file = path.join(directory, 'source/generated/provenance.json');
  const value = JSON.parse(fs.readFileSync(file, 'utf8'));
  value.artifacts['signal-runtime.js'] = '0'.repeat(64);
  fs.writeFileSync(file, JSON.stringify(value));
  assert.throws(() => verifyAudioAnalysisMaterials(directory), /provenance.*differ/i);
});
for (const file of [
  'beat-this-LICENSE.txt',
  'demucs-LICENSE.txt',
  'wgpu-native-LICENSE.MIT.txt',
  'wgpu-native-LICENSE.APACHE.txt',
  'typescript-LICENSE.txt',
  'esbuild-LICENSE.md',
])
  test(`missing bundled ${file} is rejected`, (t) => {
    const directory = fixture(t);
    fs.rmSync(path.join(directory, 'licenses/third_party', file), { force: true });
    assert.throws(() => verifyAudioAnalysisMaterials(directory), /distribution material|license/i);
  });
for (const location of ['licenses/third_party', 'source/third_party'])
  test(`changed model copyright/license in ${location} is rejected`, (t) => {
    const directory = fixture(t),
      file = path.join(directory, location, 'beat-this-LICENSE.txt');
    fs.writeFileSync(file, 'MIT\nremoved original author attribution\n');
    assert.throws(() => verifyAudioAnalysisMaterials(directory), /license.*digest/i);
  });
for (const name of [
  'build-source.mjs',
  'SignalProfile.cpp',
  'NativeGraph.h',
  'typed-array-compat.mjs',
])
  test(`missing preferred implementation ${name} is rejected`, (t) => {
    const directory = fixture(t);
    fs.unlinkSync(path.join(directory, 'source', name));
    assert.throws(() => verifyAudioAnalysisMaterials(directory), /distribution material/i);
  });
for (const [name, target] of [
  ['runtime', 'generated'],
  ['source/upstream', 'upstream'],
  ['licenses/third_party', 'third_party'],
])
  test(`external ${name} directory cannot masquerade as installed materials`, (t) => {
    const directory = fixture(t),
      location = path.join(directory, name);
    fs.rmSync(location, { recursive: true });
    fs.symlinkSync(
      path.join(source, target),
      location,
      process.platform === 'win32' ? 'junction' : 'dir',
    );
    assert.throws(() => verifyAudioAnalysisMaterials(directory), /outside|escape/i);
  });
