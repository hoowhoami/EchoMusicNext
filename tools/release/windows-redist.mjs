import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

export function verifyMicrosoftRuntime(file, directory, { run = spawnSync } = {}) {
  const temporary = fs.mkdtempSync(path.join(directory, '.verify-redist-'));
  const executable = path.join(temporary, 'verify-redist.exe');
  function execute(command, args, label) {
    const result = run(command, args, {
      encoding: 'utf8',
      windowsHide: true,
      timeout: 120000,
    });
    if (result.error || result.status !== 0)
      throw new Error(
        `${label}: ${result.error?.message || [result.stderr, result.stdout].filter(Boolean).join('\n') || result.signal || `exit ${result.status}`}`,
      );
    return result.stdout.trim();
  }
  try {
    // /MT keeps this build-only verifier independent of the redistributable it
    // is checking. The MSVC developer environment selects the runner's target.
    execute(
      'cl.exe',
      [
        '/nologo',
        '/std:c++20',
        '/EHsc',
        '/W4',
        '/O2',
        '/MT',
        '/utf-8',
        fileURLToPath(new URL('./verify-redist.cpp', import.meta.url)),
        `/Fe${executable}`,
        `/Fo${path.join(temporary, 'verify-redist.obj')}`,
        '/link',
        'wintrust.lib',
        'crypt32.lib',
        'version.lib',
      ],
      'Cannot build native Microsoft runtime verifier (use an MSVC developer environment)',
    );
    const version = execute(
      executable,
      [path.resolve(file)],
      'Microsoft runtime signature validation failed',
    );
    if (!/^14\.\d+\.\d+\.\d+$/.test(version)) throw new Error('Invalid Microsoft runtime version');
    return version;
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}
