import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { verifyStage } from './verify-stage.mjs';

function fixture(platform, arch) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-stage-'));
  const stage = path.join(root, 'stage');
  const bin = path.join(stage, platform === 'macOS' ? 'echomusicnext.app/Contents/MacOS' : 'bin');
  const helpers = platform === 'macOS' ? path.join(bin, '../Helpers') : bin;
  const tools = path.join(root, 'tools');
  fs.mkdirSync(bin, { recursive: true });
  fs.mkdirSync(helpers, { recursive: true });
  fs.mkdirSync(tools);
  const bytes = Buffer.alloc(512);
  if (platform === 'Linux') {
    bytes.write('\x7fELF');
    bytes[4] = 2;
    bytes.writeUInt16LE(arch === 'arm64' ? 183 : 62, 18);
  } else if (platform === 'Windows') {
    bytes.writeUInt32LE(64, 0x3c);
    bytes.write('PE\0\0', 64);
    bytes.writeUInt16LE(arch === 'arm64' ? 0xaa64 : 0x8664, 68);
  } else {
    bytes.writeUInt32LE(0xfeedfacf);
    bytes.writeUInt32LE(arch === 'arm64' ? 0x100000c : 0x1000007, 4);
  }
  const suffix = platform === 'Windows' ? '.exe' : '';
  fs.writeFileSync(path.join(bin, 'echomusicnext' + suffix), bytes);
  for (const name of [
    'echo-update-helper',
    'crashpad_handler',
    'echo-theme-image-worker',
    'echo-plugin-asset-worker',
  ])
    fs.writeFileSync(path.join(helpers, name + suffix), bytes);
  if (platform === 'Windows') fs.writeFileSync(path.join(bin, 'crashpad_wer.dll'), bytes);
  fs.writeFileSync(
    path.join(tools, 'otool'),
    '#!/bin/sh\nprintf "binary:\\n\\t%s (compatibility version 1.0.0)\\n" "${ECHO_TEST_DEPENDENCY:-/usr/lib/libSystem.B.dylib}"\n',
    { mode: 0o700 },
  );
  fs.writeFileSync(
    path.join(tools, 'xvfb-run'),
    `#!/usr/bin/env node
const fs = require('node:fs');
fs.writeFileSync(process.env.ECHO_TEST_MARKER, JSON.stringify({args: process.argv.slice(2), pluginPath: process.env.QT_PLUGIN_PATH}));
if (process.env.ECHO_TEST_DEGRADED) console.error('Crash diagnostics degraded: fixture');
setTimeout(() => process.exit(Number(process.env.ECHO_TEST_STATUS || 0)), Number(process.env.ECHO_TEST_DELAY || 0));
`,
    { mode: 0o700 },
  );
  const previous = process.env.PATH;
  process.env.PATH = tools + path.delimiter + previous;
  return {
    root,
    stage,
    bin,
    cleanup() {
      process.env.PATH = previous;
      for (const key of [
        'ECHO_TEST_MARKER',
        'ECHO_TEST_DELAY',
        'ECHO_TEST_STATUS',
        'ECHO_TEST_DEPENDENCY',
        'ECHO_TEST_DEGRADED',
      ])
        delete process.env[key];
      fs.rmSync(root, { recursive: true, force: true });
    },
  };
}

for (const platform of ['Linux', 'Windows', 'macOS'])
  for (const arch of ['x64', 'arm64'])
    test(
      `default packaging checks ${platform}/${arch} without launching the non-executable fixture`,
      { skip: platform === 'macOS' && process.platform === 'win32' },
      () => {
        const f = fixture(platform, arch);
        try {
          verifyStage(f.stage, platform, arch);
        } finally {
          f.cleanup();
        }
      },
    );

test('static validation still rejects a mismatched architecture and unexpected runtime', () => {
  const f = fixture('Linux', 'arm64');
  try {
    assert.throws(() => verifyStage(f.stage, 'Linux', 'x64'), /architecture mismatch/);
    fs.writeFileSync(path.join(f.bin, 'node'), 'unexpected runtime');
    assert.throws(() => verifyStage(f.stage, 'Linux', 'arm64'), /Unexpected runtime/);
  } finally {
    f.cleanup();
  }
});

test(
  'macOS static validation still rejects external dependencies',
  { skip: process.platform === 'win32' },
  () => {
    const f = fixture('macOS', 'x64');
    try {
      process.env.ECHO_TEST_DEPENDENCY = '/opt/homebrew/opt/libiodbc/lib/libiodbc.2.dylib';
      assert.throws(() => verifyStage(f.stage, 'macOS', 'x64'), /Unbundled dependency/);
    } finally {
      f.cleanup();
    }
  },
);

test(
  'explicit smoke allows shutdown beyond 30 seconds and removes SDK plugin paths',
  { skip: process.platform === 'win32', timeout: 55000 },
  () => {
    const f = fixture('Linux', 'x64');
    const previous = process.env.QT_PLUGIN_PATH;
    try {
      process.env.ECHO_TEST_MARKER = path.join(f.root, 'invocation.json');
      process.env.ECHO_TEST_DELAY = '31000';
      process.env.QT_PLUGIN_PATH = '/host/sdk/plugins';
      verifyStage(f.stage, 'Linux', 'x64', { smoke: true });
      const invocation = JSON.parse(fs.readFileSync(process.env.ECHO_TEST_MARKER));
      assert.ok(invocation.args.includes('--smoke-test'));
      assert.equal(invocation.pluginPath, undefined);
    } finally {
      if (previous === undefined) delete process.env.QT_PLUGIN_PATH;
      else process.env.QT_PLUGIN_PATH = previous;
      f.cleanup();
    }
  },
);

test(
  'explicit smoke failures and degraded Crashpad remain errors',
  { skip: process.platform === 'win32' },
  () => {
    const f = fixture('Linux', 'x64');
    try {
      process.env.ECHO_TEST_MARKER = path.join(f.root, 'invocation.json');
      process.env.ECHO_TEST_STATUS = '9';
      assert.throws(() => verifyStage(f.stage, 'Linux', 'x64', { smoke: true }), /smoke failed: 9/);
      process.env.ECHO_TEST_STATUS = '0';
      process.env.ECHO_TEST_DEGRADED = '1';
      assert.throws(
        () => verifyStage(f.stage, 'Linux', 'x64', { smoke: true }),
        /Crashpad initialization failed/,
      );
    } finally {
      f.cleanup();
    }
  },
);
