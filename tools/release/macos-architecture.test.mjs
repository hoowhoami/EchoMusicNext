import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import {
  macosBinaries,
  thinMacosBinaries,
  verifyMacosArchitecture,
} from './macos-architecture.mjs';

function fixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-macos-arch-'));
  const source = path.join(root, 'value.c');
  fs.writeFileSync(source, 'int value(void) { return 42; }\n');
  const run = (command, args) => {
    const result = spawnSync(command, args, { encoding: 'utf8' });
    assert.equal(result.status, 0, result.error?.message || result.stderr);
  };
  for (const arch of ['arm64', 'x86_64'])
    run('clang', ['-arch', arch, '-c', source, '-o', path.join(root, arch)]);
  run('lipo', [
    '-create',
    path.join(root, 'arm64'),
    path.join(root, 'x86_64'),
    '-output',
    path.join(root, 'universal'),
  ]);
  return root;
}

for (const arch of ['arm64', 'x64'])
  test(
    `deployed ${arch} keeps only its slice in frameworks, plugins and helpers`,
    {
      skip: process.platform !== 'darwin',
    },
    () => {
      const root = fixture();
      try {
        const app = path.join(root, 'app');
        const paths = [
          'Contents/Frameworks/QtExample.framework/Versions/A/QtExample',
          'Contents/PlugIns/media.dylib',
          'Contents/Resources/qml/Example/plugin',
          'Contents/Helpers/worker',
        ];
        const original = fs.readFileSync(path.join(root, 'universal'));
        for (const name of paths) {
          const file = path.join(app, name);
          fs.mkdirSync(path.dirname(file), { recursive: true });
          fs.writeFileSync(file, original, { mode: 0o755 });
        }
        const thin = path.join(app, 'Contents/Helpers/already-thin');
        fs.copyFileSync(path.join(root, arch === 'x64' ? 'x86_64' : arch), thin);
        const version = path.join(app, 'Contents/Frameworks/QtExample.framework/Versions/Current');
        fs.symlinkSync('A', version);
        fs.symlinkSync(path.join(root, 'universal'), path.join(app, 'SDK-alias'));
        fs.writeFileSync(path.join(app, 'resource.txt'), 'Retain this resource.');
        const result = thinMacosBinaries(app, arch);
        assert.equal(result.binaries, paths.length + 1);
        assert.equal(result.thinned, paths.length);
        assert.ok(result.bytesAfter < result.bytesBefore);
        for (const file of macosBinaries(app)) verifyMacosArchitecture(file, arch);
        assert.equal(fs.statSync(path.join(app, paths[3])).mode & 0o777, 0o755);
        assert.equal(fs.readlinkSync(version), 'A');
        assert.deepEqual(fs.readFileSync(path.join(root, 'universal')), original);
        assert.equal(
          fs.readFileSync(path.join(app, 'resource.txt'), 'utf8'),
          'Retain this resource.',
        );
        const repeated = thinMacosBinaries(app, arch);
        assert.equal(repeated.thinned, 0);
        assert.equal(repeated.bytesBefore, result.bytesAfter);
        assert.equal(repeated.bytesAfter, result.bytesAfter);
      } finally {
        fs.rmSync(root, { recursive: true, force: true });
      }
    },
  );

test(
  'a missing target slice or failed lipo stops packaging without replacing the input',
  {
    skip: process.platform !== 'darwin',
  },
  () => {
    const root = fixture();
    try {
      const app = path.join(root, 'app');
      fs.mkdirSync(app);
      const file = path.join(app, 'plugin');
      const original = fs.readFileSync(path.join(root, 'x86_64'));
      fs.writeFileSync(file, original);
      assert.throws(() => thinMacosBinaries(app, 'arm64'), /architecture mismatch/);
      assert.deepEqual(fs.readFileSync(file), original);
      const invalid = Buffer.alloc(512);
      invalid.writeUInt32BE(0xcafebabe);
      invalid.writeUInt32BE(2, 4);
      fs.writeFileSync(file, invalid);
      assert.throws(() => thinMacosBinaries(app, 'arm64'), /Cannot retain arm64/);
      assert.deepEqual(fs.readFileSync(file), invalid);
      assert.deepEqual(fs.readdirSync(app), ['plugin']);
      assert.throws(() => thinMacosBinaries(app, 'invalid'), /Unsupported macOS architecture/);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  },
);
