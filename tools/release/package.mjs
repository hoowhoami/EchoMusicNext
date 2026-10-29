import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { verifyStage } from './verify-stage.mjs';
import { collectLicenses } from './licenses.mjs';
import { root, matrix, releaseVersion, assetName, writeManifest } from './contract.mjs';
const options = Object.fromEntries(
  process.argv.slice(2).map((value) => {
    const [key, ...rest] = value.replace(/^--/, '').split('=');
    return [key, rest.join('=')];
  }),
);
const platform = { win32: 'Windows', darwin: 'macOS', linux: 'Linux' }[process.platform];
const arch = options.arch || { x64: 'x64', arm64: 'arm64' }[process.arch];
if (!matrix.some((item) => item.platform === platform && item.arch === arch))
  throw new Error('Unsupported package platform');
const version = releaseVersion();
const build = path.resolve(root, options.build || 'build/release');
const output = path.resolve(root, options.out || 'build/packages');
const qt = path.resolve(options.qt || '');
if (
  !options.qt ||
  !build.startsWith(root + path.sep + 'build' + path.sep) ||
  !output.startsWith(root + path.sep + 'build' + path.sep)
)
  throw new Error('Build and output must be managed build directories; --qt is required');
const staging = path.join(build, 'package-staging');
fs.rmSync(staging, { recursive: true, force: true });
fs.mkdirSync(staging, { recursive: true });
fs.mkdirSync(output, { recursive: true });
function run(command, args, extras = {}) {
  console.log('+', command, ...args);
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: 'inherit',
    ...extras,
    env: { ...process.env, ...extras.env },
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed (${result.status})`);
}
function name(kind) {
  return path.join(output, assetName(version, platform, arch, kind));
}
const stage =
  platform === 'Linux' ? path.join(staging, 'AppDir/usr') : path.join(staging, 'install');
run('cmake', ['--install', build, '--prefix', stage, '--config', 'Release']);
collectLicenses(
  platform === 'macOS'
    ? path.join(stage, 'echomusicnext.app/Contents/Resources/licenses')
    : path.join(stage, 'share/echomusicnext/licenses'),
  qt,
  version,
);
if (platform === 'macOS') {
  const app = path.join(stage, 'echomusicnext.app');
  const platforms = path.join(app, 'Contents/PlugIns/platforms');
  fs.mkdirSync(platforms, { recursive: true });
  fs.copyFileSync(
    path.join(qt, 'plugins/platforms/libqoffscreen.dylib'),
    path.join(platforms, 'libqoffscreen.dylib'),
  );
  const analysisHelper = path.join(app, 'Contents/Helpers/echo-audio-analysis-helper');
  run(path.join(qt, 'bin/macdeployqt'), [
    app,
    `-qmldir=${path.join(root, 'modules')}`,
    `-qmlimport=${path.join(build, 'qml')}`,
    `-executable=${path.join(app, 'Contents/Helpers/echo-theme-image-worker')}`,
    `-executable=${path.join(app, 'Contents/Helpers/echo-plugin-asset-worker')}`,
    ...(fs.existsSync(analysisHelper) ? [`-executable=${analysisHelper}`] : []),
    '-always-overwrite',
  ]);
  const sqlDrivers = path.join(app, 'Contents/PlugIns/sqldrivers');
  if (fs.existsSync(sqlDrivers))
    for (const file of fs.readdirSync(sqlDrivers)) {
      if (file !== 'libqsqlite.dylib') fs.rmSync(path.join(sqlDrivers, file), { recursive: true });
    }
  run('codesign', [
    '--force',
    '--deep',
    '--sign',
    '-',
    '--options',
    'runtime',
    '--entitlements',
    path.join(root, 'tools/release/entitlements.mac.plist'),
    app,
  ]);
  run('codesign', ['--verify', '--deep', '--strict', app]);
  verifyStage(stage, platform, arch);
  fs.symlinkSync('/Applications', path.join(stage, 'Applications'));
  run('hdiutil', [
    'create',
    '-volname',
    'EchoMusicNext',
    '-srcfolder',
    stage,
    '-ov',
    '-format',
    'UDZO',
    name('dmg'),
  ]);
  run('hdiutil', ['verify', name('dmg')]);
  run('ditto', ['-c', '-k', '--sequesterRsrc', '--keepParent', app, name('zip')]);
} else if (platform === 'Windows') {
  run(path.join(qt, 'bin/windeployqt.exe'), [
    '--release',
    '--no-compiler-runtime',
    '--skip-plugin-types',
    'sqldrivers',
    '--qmldir',
    path.join(root, 'modules'),
    '--qmlimport',
    path.join(build, 'qml'),
    '--dir',
    path.join(stage, 'bin'),
    path.join(stage, 'bin/echomusicnext.exe'),
    ...(fs.existsSync(path.join(stage, 'bin/echo-audio-analysis-helper.exe'))
      ? [path.join(stage, 'bin/echo-audio-analysis-helper.exe')]
      : []),
  ]);
  fs.copyFileSync(
    path.join(qt, 'plugins/platforms/qoffscreen.dll'),
    path.join(stage, 'bin/platforms/qoffscreen.dll'),
  );
  const sqlite = path.join(stage, 'bin/sqldrivers');
  fs.mkdirSync(sqlite, { recursive: true });
  fs.copyFileSync(
    path.join(qt, 'plugins/sqldrivers/qsqlite.dll'),
    path.join(sqlite, 'qsqlite.dll'),
  );
  verifyStage(stage, platform, arch);
  const redist = path.join(root, `build/release-tools/vc_redist.${arch}.exe`);
  const redistVersion = JSON.parse(
    fs.readFileSync(path.join(root, `build/release-tools/vc_redist.${arch}.json`)),
  ).version;
  const nsis =
    options.nsis ||
    path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'NSIS/makensis.exe');
  const define = (key, value) => `/D${key}=${value.replaceAll('$', '$$')}`;
  const uninstall = path.join(staging, 'uninstall-files.nsh');
  const directories = [],
    deletions = [];
  function uninstallFiles(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      const relative = path.relative(stage, file).split(path.sep).join('\\').replaceAll('$', '$$');
      if (entry.isDirectory()) {
        uninstallFiles(file);
        directories.push(`RMDir "$INSTDIR\\${relative}"`);
      } else deletions.push(`Delete "$INSTDIR\\${relative}"`);
    }
  }
  uninstallFiles(stage);
  fs.writeFileSync(uninstall, [...deletions, ...directories].join('\n') + '\n');
  run(nsis, [
    define('STAGE', stage),
    define('OUTPUT', name('nsis')),
    define('VERSION', version),
    define('ARCH', arch),
    define('REDIST', redist),
    define('REDIST_VERSION', redistVersion),
    define('UNINSTALL_FILES', uninstall),
    define('ICON', path.join(root, 'app/assets/icon.ico')),
    define('LICENSE', path.join(root, 'LICENSE')),
    path.join(root, 'tools/release/installer.nsi'),
  ]);
} else {
  const appdir = path.dirname(stage),
    marker = path.join(stage, 'share/echomusicnext/package-type');
  fs.mkdirSync(path.dirname(marker), { recursive: true });
  fs.writeFileSync(marker, 'appimage\n');
  const linuxdeploy = path.resolve(options.linuxdeploy || path.join(build, 'linuxdeploy.AppImage'));
  run(
    linuxdeploy,
    [
      '--appdir',
      appdir,
      '--executable',
      path.join(stage, 'bin/echomusicnext'),
      '--executable',
      path.join(stage, 'bin/echo-theme-image-worker'),
      '--executable',
      path.join(stage, 'bin/echo-plugin-asset-worker'),
      ...(fs.existsSync(path.join(stage, 'bin/echo-audio-analysis-helper'))
        ? ['--executable', path.join(stage, 'bin/echo-audio-analysis-helper')]
        : []),
      '--executable',
      path.join(stage, 'bin/echo-update-helper'),
      '--executable',
      path.join(stage, 'bin/crashpad_handler'),
      '--library',
      path.join(stage, 'lib/libecho_ffi.so'),
      '--plugin',
      'qt',
      '--output',
      'appimage',
    ],
    {
      cwd: output,
      env: {
        ...process.env,
        QMAKE: path.join(qt, 'bin/qmake'),
        QML_SOURCES_PATHS: path.join(root, 'modules'),
        QML_MODULES_PATHS: path.join(build, 'qml'),
        EXTRA_PLATFORM_PLUGINS: 'libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so',
        EXTRA_QT_MODULES: 'waylandcompositor',
        APPIMAGE_EXTRACT_AND_RUN: '1',
        OUTPUT: name('appimage'),
        LD_LIBRARY_PATH: [
          path.join(stage, 'lib'),
          path.join(qt, 'lib'),
          process.env.LD_LIBRARY_PATH || '',
        ]
          .filter(Boolean)
          .join(':'),
      },
    },
  );
  if (!fs.existsSync(name('appimage'))) throw new Error('AppImage output missing');
  verifyStage(stage, platform, arch);
  const osArch = arch === 'x64' ? 'x86_64' : 'aarch64';
  const match = /^([0-9.]+)(?:-(.*))?$/.exec(version.split('+')[0]);
  const [, base, prerelease] = match;
  for (const kind of ['deb', 'rpm', 'pacman', 'tar']) {
    const pkgroot = path.join(staging, kind),
      privateRoot = path.join(pkgroot, 'opt/echomusicnext');
    fs.cpSync(appdir, privateRoot, { recursive: true, dereference: false });
    fs.writeFileSync(path.join(privateRoot, 'usr/share/echomusicnext/package-type'), kind + '\n');
    const shared = path.join(pkgroot, 'usr/share');
    fs.mkdirSync(shared, { recursive: true });
    for (const directory of ['applications', 'icons', 'mime']) {
      const source = path.join(stage, 'share', directory);
      if (fs.existsSync(source))
        fs.cpSync(source, path.join(shared, directory), { recursive: true });
    }
    fs.mkdirSync(path.join(pkgroot, 'usr/bin'), { recursive: true });
    fs.symlinkSync('/opt/echomusicnext/AppRun', path.join(pkgroot, 'usr/bin/echomusicnext'));
    if (kind === 'tar') {
      run('tar', ['-czf', name(kind), '-C', path.join(pkgroot, 'opt'), 'echomusicnext']);
      continue;
    }
    const packageVersion =
      kind === 'deb'
        ? base + (prerelease ? '~' + prerelease : '')
        : kind === 'pacman'
          ? base + (prerelease ? 'pre.' + prerelease.replaceAll('-', '.') : '')
          : base;
    const iteration = kind === 'rpm' && prerelease ? '0.' + prerelease.replaceAll('-', '.') : '1';
    const dependencies =
      kind === 'deb'
        ? [
            'libc6 (>= 2.35)',
            'libglib2.0-0',
            'libproxy1v5',
            'libproxy1-plugin-gsettings',
            'libproxy1-plugin-webkit',
            'libcurl4',
            'libgl1',
            'libfontconfig1',
            'libxkbcommon0',
            'libxcb1',
            'libssl3',
            'libasound2',
            'libpulse0',
            'policykit-1',
          ]
        : kind === 'rpm'
          ? [
              'glibc >= 2.35',
              'glib2',
              'libproxy',
              'libcurl',
              'mesa-libGL',
              'fontconfig',
              'libxkbcommon',
              'libxcb',
              'openssl-libs',
              'alsa-lib',
              'pulseaudio-libs',
              'polkit',
            ]
          : [
              'glibc>=2.35',
              'glib2',
              'libproxy',
              'curl',
              'libglvnd',
              'fontconfig',
              'libxkbcommon',
              'libxcb',
              'openssl',
              'alsa-lib',
              'libpulse',
              'polkit',
            ];
    run('fpm', [
      '--force',
      '-s',
      'dir',
      '-t',
      kind,
      '--name',
      'echomusicnext',
      '--version',
      packageVersion,
      ...(kind === 'deb' ? [] : ['--iteration', iteration]),
      '--architecture',
      kind === 'deb' ? (arch === 'x64' ? 'amd64' : 'arm64') : osArch,
      '--license',
      'GPL-3.0-only',
      '--url',
      'https://github.com/hoowhoami/EchoMusicNext',
      '--maintainer',
      'hoowhoami',
      '--description',
      'EchoMusicNext native desktop music player',
      '--package',
      name(kind),
      '--chdir',
      pkgroot,
      ...(kind === 'pacman' ? ['--pacman-compression', 'zstd'] : []),
      ...dependencies.flatMap((dependency) => ['--depends', dependency]),
      '.',
    ]);
  }
}
await writeManifest(output, version, platform, arch);
console.log(`Packaged ${platform}/${arch} ${version}`);
