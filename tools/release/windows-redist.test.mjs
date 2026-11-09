import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { verifyMicrosoftRuntime } from './windows-redist.mjs';

function fixture() {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo runtime 验签 '));
  return {
    directory,
    cleanup() {
      fs.rmSync(directory, { recursive: true, force: true });
    },
  };
}

test('native verification returns the exact file version without invoking a shell', () => {
  const f = fixture();
  const calls = [];
  try {
    const file = path.join(f.directory, 'vc redist.arm64.exe');
    const version = verifyMicrosoftRuntime(file, f.directory, {
      run(command, args, options) {
        calls.push({ command, args, options });
        return {
          status: 0,
          stdout: calls.length === 1 ? 'MSVC build output' : '14.51.36231.0\r\n',
        };
      },
    });
    assert.equal(version, '14.51.36231.0');
    assert.equal(calls.length, 2);
    assert.equal(calls[0].command, 'cl.exe');
    assert.ok(calls[0].args.includes('/MT'));
    assert.ok(calls[0].args.includes('wintrust.lib'));
    assert.equal(calls[1].command, calls[0].args.find((arg) => arg.startsWith('/Fe')).slice(3));
    assert.deepEqual(calls[1].args, [file]);
    assert.equal(calls[1].options.shell, undefined);
    assert.ok(calls[1].options.timeout > 0);
    assert.deepEqual(fs.readdirSync(f.directory), []);
  } finally {
    f.cleanup();
  }
});

for (const failure of [
  { status: 1, stderr: 'Runtime Authenticode verification failed (0x80096010)' },
  { status: 1, stderr: 'Runtime signer must be Microsoft Corporation' },
  { status: null, signal: 'SIGTERM' },
  { status: null, error: new Error('spawn ETIMEDOUT') },
])
  test(`failed native verification stops preparation: ${failure.stderr || failure.signal || failure.error.message}`, () => {
    const f = fixture();
    let calls = 0;
    try {
      assert.throws(
        () =>
          verifyMicrosoftRuntime('redist.exe', f.directory, {
            run() {
              calls += 1;
              return calls === 1 ? { status: 0, stdout: '' } : failure;
            },
          }),
        /Microsoft runtime signature validation failed/,
      );
      assert.equal(calls, 2);
      assert.deepEqual(fs.readdirSync(f.directory), []);
    } finally {
      f.cleanup();
    }
  });

test('missing MSVC stops before verification and removes temporary build products', () => {
  const f = fixture();
  let calls = 0;
  try {
    assert.throws(
      () =>
        verifyMicrosoftRuntime('redist.exe', f.directory, {
          run() {
            calls += 1;
            return { status: null, error: new Error('spawn cl.exe ENOENT') };
          },
        }),
      /Cannot build native Microsoft runtime verifier.*ENOENT/,
    );
    assert.equal(calls, 1);
    assert.deepEqual(fs.readdirSync(f.directory), []);
  } finally {
    f.cleanup();
  }
});

for (const output of ['', '15.0.1.0', '14.51.1.0\nextra output'])
  test(`invalid version output is rejected: ${JSON.stringify(output)}`, () => {
    const f = fixture();
    try {
      assert.throws(
        () =>
          verifyMicrosoftRuntime('redist.exe', f.directory, {
            run: () => ({ status: 0, stdout: output }),
          }),
        /Invalid Microsoft runtime version/,
      );
      assert.deepEqual(fs.readdirSync(f.directory), []);
    } finally {
      f.cleanup();
    }
  });

test(
  'Windows rejects unsigned input using the actual native verifier',
  { skip: process.platform !== 'win32' },
  () => {
    const f = fixture();
    try {
      const file = path.join(f.directory, 'unsigned.exe');
      fs.writeFileSync(file, 'Not a signed executable');
      assert.throws(
        () => verifyMicrosoftRuntime(file, f.directory),
        /Authenticode verification failed/,
      );
      assert.deepEqual(fs.readdirSync(f.directory), ['unsigned.exe']);
    } finally {
      f.cleanup();
    }
  },
);

test(
  'Windows rejects the Node.js signer as a runtime publisher',
  { skip: process.platform !== 'win32' },
  () => {
    const f = fixture();
    try {
      assert.throws(
        () => verifyMicrosoftRuntime(process.execPath, f.directory),
        /Runtime signer must be Microsoft Corporation/,
      );
      assert.deepEqual(fs.readdirSync(f.directory), []);
    } finally {
      f.cleanup();
    }
  },
);

const redist = process.env.ECHO_TEST_REDIST;
test(
  'Windows accepts an official runtime and rejects modified signed bytes',
  {
    skip: process.platform !== 'win32' || !redist,
  },
  () => {
    const f = fixture();
    try {
      assert.match(verifyMicrosoftRuntime(redist, f.directory), /^14\.\d+\.\d+\.\d+$/);
      const changed = path.join(f.directory, 'tampered.exe');
      const bytes = fs.readFileSync(redist);
      // The DOS stub is covered by Authenticode; keep the PE headers parseable.
      bytes[0x40] ^= 1;
      fs.writeFileSync(changed, bytes);
      assert.throws(
        () => verifyMicrosoftRuntime(changed, f.directory),
        /Authenticode verification failed/,
      );
    } finally {
      f.cleanup();
    }
  },
);
