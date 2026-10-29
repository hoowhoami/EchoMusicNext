import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
export function verifyStage(stage, platform, arch) {
  const binaryDirectory =
    platform === 'macOS'
      ? path.join(stage, 'echomusicnext.app/Contents/MacOS')
      : path.join(stage, 'bin');
  const executable = path.join(
    binaryDirectory,
    platform === 'Windows' ? 'echomusicnext.exe' : 'echomusicnext',
  );
  const helper =
    platform === 'macOS'
      ? path.join(stage, 'echomusicnext.app/Contents/Helpers/echo-update-helper')
      : path.join(
          binaryDirectory,
          platform === 'Windows' ? 'echo-update-helper.exe' : 'echo-update-helper',
        );
  function machine(file) {
    const bytes = fs.readFileSync(file);
    if (platform === 'Windows') {
      const offset = bytes.readUInt32LE(0x3c);
      if (bytes.toString('ascii', offset, offset + 4) !== 'PE\0\0')
        throw new Error('Invalid PE executable');
      return bytes.readUInt16LE(offset + 4) === (arch === 'arm64' ? 0xaa64 : 0x8664);
    }
    if (platform === 'macOS')
      return (
        bytes.readUInt32LE(0) === 0xfeedfacf &&
        bytes.readUInt32LE(4) === (arch === 'arm64' ? 0x100000c : 0x1000007)
      );
    return (
      bytes.toString('ascii', 1, 4) === 'ELF' &&
      bytes[4] === 2 &&
      bytes.readUInt16LE(18) === (arch === 'arm64' ? 183 : 62)
    );
  }
  const crashHandler =
    platform === 'macOS'
      ? path.join(stage, 'echomusicnext.app/Contents/Helpers/crashpad_handler')
      : path.join(
          binaryDirectory,
          platform === 'Windows' ? 'crashpad_handler.exe' : 'crashpad_handler',
        );
  const imageHelpers = ['echo-theme-image-worker', 'echo-plugin-asset-worker'].map((name) =>
    platform === 'macOS'
      ? path.join(stage, 'echomusicnext.app/Contents/Helpers', name)
      : path.join(binaryDirectory, platform === 'Windows' ? `${name}.exe` : name),
  );
  const nativeExecutables = [executable, helper, crashHandler, ...imageHelpers];
  if (platform === 'Windows')
    nativeExecutables.push(path.join(binaryDirectory, 'crashpad_wer.dll'));
  for (const file of nativeExecutables)
    if (!machine(file)) throw new Error(`Packaged architecture mismatch: ${file}`);
  function walk(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      if (
        /qt(?:6)?webengine|^electron(?: framework)?$|^(?:node|python(?:\d+(?:\.\d+)*)?)(?:\.exe)?$/i.test(
          entry.name,
        )
      )
        throw new Error(`Unexpected runtime: ${entry.name}`);
      if (entry.isSymbolicLink()) continue;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) walk(file);
      else if (
        platform === 'macOS' &&
        (file.endsWith('.dylib') ||
          file.includes('.framework/') ||
          file === executable ||
          nativeExecutables.includes(file))
      ) {
        const data = fs.readFileSync(file);
        if (
          data.length < 4 ||
          ![0xfeedfacf, 0xbebafeca, 0xcafebabe, 0xbfbafeca].includes(data.readUInt32LE(0))
        )
          continue;
        const result = spawnSync('otool', ['-L', file], { encoding: 'utf8' });
        if (result.status !== 0) throw new Error(`Cannot inspect ${file}`);
        for (const line of result.stdout.split('\n').slice(1)) {
          if (!/^\s/.test(line)) continue;
          const dependency = line.trim().split(' (')[0];
          if (
            dependency.startsWith('/') &&
            !dependency.startsWith('/usr/lib/') &&
            !dependency.startsWith('/System/Library/')
          )
            throw new Error(`Unbundled dependency: ${dependency}`);
        }
      }
    }
  }
  walk(stage);
  const environment = {
    ...process.env,
    QT_QPA_PLATFORM: platform === 'macOS' ? 'cocoa' : platform === 'Windows' ? 'windows' : 'xcb',
    QT_QUICK_BACKEND: 'software',
  };
  for (const key of [
    'QT_PLUGIN_PATH',
    'QML_IMPORT_PATH',
    'QML2_IMPORT_PATH',
    'DYLD_LIBRARY_PATH',
    'DYLD_FRAMEWORK_PATH',
    'LD_LIBRARY_PATH',
  ])
    delete environment[key];
  const program = platform === 'Linux' ? path.join(stage, '../AppRun') : executable;
  const result = spawnSync(
    platform === 'Linux' ? 'xvfb-run' : program,
    platform === 'Linux' ? ['-a', program, '--smoke-test'] : ['--smoke-test'],
    {
      env: environment,
      encoding: 'utf8',
      timeout: 30000,
    },
  );
  if (result.stdout) console.log(result.stdout);
  if (result.stderr) console.log(result.stderr);
  if ((result.stderr || '').includes('Crash diagnostics degraded:'))
    throw new Error('Packaged Crashpad initialization failed');
  if (result.error || result.status !== 0)
    throw new Error(
      `Packaged application smoke failed: ${result.error?.message || result.signal || result.status}`,
    );
}
