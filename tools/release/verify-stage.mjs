import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { isDeepStrictEqual } from 'node:util';
// Pure distribution check: no executable, GPU or application is started.
export function verifyAudioAnalysisMaterials(analysisDirectory) {
  const required = [
    'runtime/graph-runtime.js',
    'runtime/signal-runtime.js',
    'runtime/provenance.json',
    'licenses/LICENSE',
    'licenses/THIRD_PARTY_NOTICES.md',
    'source/LICENSE',
    'source/CMakeLists.txt',
    'source/NativeGraph.cpp',
    'source/NativeGraph.h',
    'source/SourceGraph.cpp',
    'source/SignalProfile.cpp',
    'source/SignalProfile.h',
    'source/ModelSnapshot.cpp',
    'source/ModelSnapshot.h',
    'source/worker.cpp',
    'source/build-source.mjs',
    'source/graph-recorder.mjs',
    'source/signal-job.mjs',
    'source/typed-array-compat.mjs',
    'source/fetch-dependencies.py',
    'source/generated/provenance.json',
    'source/upstream/SOURCE.json',
    'source/upstream/LICENSE',
    'source/upstream/tsconfig.json',
    'source/source-format.json',
    'source/source-rebuild.md',
  ];
  const materialRoot = fs.realpathSync(analysisDirectory);
  const regular = (relative) => {
    const file = path.join(analysisDirectory, relative);
    if (!fs.existsSync(file) || !fs.lstatSync(file).isFile())
      throw new Error(`Missing audio analysis distribution material: ${relative}`);
    const actual = fs.realpathSync(file);
    if (actual !== materialRoot && !actual.startsWith(materialRoot + path.sep))
      throw new Error(`Packaged analysis material escapes distribution: ${relative}`);
    return file;
  };
  const digest = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  for (const file of required) regular(file);
  const provenance = JSON.parse(fs.readFileSync(regular('runtime/provenance.json'), 'utf8'));
  const sourceProvenance = JSON.parse(
    fs.readFileSync(regular('source/generated/provenance.json'), 'utf8'),
  );
  if (!isDeepStrictEqual(provenance, sourceProvenance))
    throw new Error('Packaged analysis provenance documents differ');
  const artifacts = [
    'graph-runtime.js',
    'signal-runtime.js',
    'source-models.mjs',
    'source-signal.mjs',
  ];
  if (
    !provenance.artifacts ||
    !isDeepStrictEqual(Object.keys(provenance.artifacts).sort(), artifacts.slice().sort())
  )
    throw new Error('Invalid analysis artifact inventory');
  for (const name of artifacts) {
    const expected = provenance.artifacts[name];
    if (!/^[a-f0-9]{64}$/.test(expected)) throw new Error('Invalid analysis artifact digest');
    for (const directory of [
      'source/generated',
      ...(name.endsWith('-runtime.js') ? ['runtime'] : []),
    ]) {
      const relative = `${directory}/${name}`;
      if (digest(regular(relative)) !== expected)
        throw new Error(`Packaged analysis artifact digest mismatch: ${relative}`);
    }
  }
  // The release repository is the authority for bundled third-party originals.
  // Compare both installed copies, including the pinned model license records and attribution.
  const thirdParty = new URL('../audio-analysis-helper/third_party/', import.meta.url);
  const licenseNames = [
    'wgpu-native-LICENSE.MIT.txt',
    'wgpu-native-LICENSE.APACHE.txt',
    'typescript-LICENSE.txt',
    'esbuild-LICENSE.md',
    'beat-this-LICENSE.txt',
    'demucs-LICENSE.txt',
    'model-manifest.json',
  ];
  for (const name of licenseNames) {
    const expected = digest(new URL(name, thirdParty));
    for (const directory of ['licenses/third_party', 'source/third_party']) {
      const relative = `${directory}/${name}`;
      if (digest(regular(relative)) !== expected)
        throw new Error(`Packaged analysis license digest mismatch: ${relative}`);
    }
  }
  const preferredRoot = path.join(analysisDirectory, 'source/upstream');
  const preferred = JSON.parse(fs.readFileSync(path.join(preferredRoot, 'SOURCE.json'), 'utf8'));
  if (preferred.commit !== provenance.commit)
    throw new Error('Packaged analysis preferred source revision differs from runtime');
  for (const [relative, expected] of Object.entries(preferred.hashes)) {
    if (
      typeof relative !== 'string' ||
      relative.includes('\\') ||
      path.win32.isAbsolute(relative) ||
      path.posix.isAbsolute(relative) ||
      relative.split('/').some((part) => !part || part === '.' || part === '..') ||
      !/^[a-f0-9]{64}$/.test(expected)
    )
      throw new Error('Invalid preferred source inventory');
    const file = regular(`source/upstream/${relative}`);
    if (digest(file) !== expected)
      throw new Error(`Packaged analysis preferred source digest mismatch: ${relative}`);
  }
  for (const [relative, expected] of Object.entries(provenance.hashes))
    if (preferred.hashes[relative] !== expected)
      throw new Error(`Runtime is missing its preferred source: ${relative}`);
  for (const [file, expected] of [
    ['beat_this-wgsl.bin', '6365ae5ba2e42922772da01fbfd0bde8dd20b7362a679018e78a5f251fe8a967'],
    ['htdemucs-wgsl.bin', '27d5331a74a4e23b18172b3d59d0378e698fd79bc0d0fed3316da77e3a37d8d1'],
  ]) {
    const model = path.join(analysisDirectory, 'models', file);
    // Deliberately omitted models remain unavailable, rather than becoming a package failure.
    if (!fs.lstatSync(model, { throwIfNoEntry: false })) continue;
    if (digest(regular(`models/${file}`)) !== expected)
      throw new Error(`Packaged audio analysis model digest mismatch: ${file}`);
  }
}

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
  const analysisHelper =
    platform === 'macOS'
      ? path.join(stage, 'echomusicnext.app/Contents/Helpers/echo-audio-analysis-helper')
      : path.join(
          binaryDirectory,
          platform === 'Windows' ? 'echo-audio-analysis-helper.exe' : 'echo-audio-analysis-helper',
        );
  if (fs.existsSync(analysisHelper)) {
    nativeExecutables.push(analysisHelper);
    const analysisDirectory =
      platform === 'macOS'
        ? path.join(stage, 'echomusicnext.app/Contents/Resources/audio-analysis')
        : path.join(stage, 'share/echomusicnext/audio-analysis');
    verifyAudioAnalysisMaterials(analysisDirectory);
  }
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
