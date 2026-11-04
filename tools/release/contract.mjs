import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const appId = 'io.github.hoowhoami.echomusicnext';
export const matrix = [
  { platform: 'macOS', arch: 'arm64', runner: 'macos-15', kinds: ['dmg', 'zip'] },
  { platform: 'macOS', arch: 'x64', runner: 'macos-15-intel', kinds: ['dmg', 'zip'] },
  {
    platform: 'Linux',
    arch: 'x64',
    runner: 'ubuntu-22.04',
    kinds: ['appimage', 'deb', 'rpm', 'pacman', 'tar'],
  },
  {
    platform: 'Linux',
    arch: 'arm64',
    runner: 'ubuntu-22.04-arm',
    kinds: ['appimage', 'deb', 'rpm', 'pacman', 'tar'],
  },
  { platform: 'Windows', arch: 'x64', runner: 'windows-2022', kinds: ['nsis'] },
  { platform: 'Windows', arch: 'arm64', runner: 'windows-11-arm', kinds: ['nsis'] },
];
const extensions = {
  dmg: '.dmg',
  zip: '.zip',
  appimage: '.AppImage',
  deb: '.deb',
  rpm: '.rpm',
  pacman: '.pkg.tar.zst',
  tar: '.tar.gz',
  nsis: '-Setup.exe',
};
export function validVersion(version) {
  const match =
    /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$/.exec(
      version,
    );
  return Boolean(
    match &&
    !(match[4] || '')
      .split('.')
      .some((part) => /^\d+$/.test(part) && part.length > 1 && part.startsWith('0')),
  );
}
export function releaseVersion(
  repository = root,
  ref = process.env.GITHUB_REF || '',
  override = process.env.RELEASE_VERSION || '',
) {
  const tagged = ref.startsWith('refs/tags/');
  if (tagged && !ref.startsWith('refs/tags/v')) throw new Error('Release tags must start with v');
  let version = tagged ? ref.slice('refs/tags/v'.length) : override;
  if (!version && !ref) {
    const tag = spawnSync(
      'git',
      ['describe', '--tags', '--exact-match', '--match', 'v[0-9]*', 'HEAD'],
      {
        cwd: repository,
        encoding: 'utf8',
      },
    );
    if (tag.status === 0) version = tag.stdout.trim().slice(1);
  }
  if (!tagged && !version) version = '0.1.0-dev';
  if (!validVersion(version)) throw new Error('Application version must be strict SemVer');
  if (tagged && override && override !== version)
    throw new Error('RELEASE_VERSION differs from the release tag');
  return version;
}
export function verifyBuildVersion(build, version) {
  const info = fs.readFileSync(path.join(build, 'generated/EchoBuildInfo.h'), 'utf8');
  const actual = /^#define ECHO_RELEASE_VERSION "([^"]+)"$/m.exec(info)?.[1];
  if (actual !== version)
    throw new Error(
      `Build version ${actual || '(missing)'} differs from package version ${version}; reconfigure and rebuild first`,
    );
}
export function verifyReleaseBuild(build, version) {
  verifyBuildVersion(build, version);
  const cache = fs.readFileSync(path.join(build, 'CMakeCache.txt'), 'utf8');
  if (!/^ECHO_DISTRIBUTION_BUILD:BOOL=(?:ON|TRUE|YES|1)$/im.test(cache))
    throw new Error(
      'Packages require a distribution build; reconfigure with --distribution and rebuild first',
    );
}
export function assetName(version, platform, arch, kind) {
  if (
    !validVersion(version) ||
    !matrix.some(
      (item) => item.platform === platform && item.arch === arch && item.kinds.includes(kind),
    )
  )
    throw new Error('Invalid release target');
  return `EchoMusicNext-${version}-${platform}-${arch}${extensions[kind]}`;
}
export function manifestName(platform, arch) {
  return `update-${platform}-${arch}.json`;
}
export async function sha512(file) {
  const hash = crypto.createHash('sha512');
  for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}
export async function writeManifest(directory, version, platform, arch) {
  const target = matrix.find((item) => item.platform === platform && item.arch === arch);
  if (!target) throw new Error('Unsupported release target');
  const assets = [];
  for (const kind of target.kinds) {
    const name = assetName(version, platform, arch, kind),
      file = path.join(directory, name);
    const size = fs.statSync(file).size;
    if (size <= 0 || size > 4 * 1024 ** 3) throw new Error(`Invalid package size: ${name}`);
    assets.push({ name, kind, size, sha512: await sha512(file) });
  }
  const manifest = { schema: 1, applicationId: appId, version, platform, arch, assets };
  fs.writeFileSync(
    path.join(directory, manifestName(platform, arch)),
    JSON.stringify(manifest, null, 2) + '\n',
  );
  return manifest;
}
export async function validateRelease(directory, version) {
  if (!validVersion(version)) throw new Error('Invalid release version');
  const expected = new Set();
  for (const target of matrix) {
    const name = manifestName(target.platform, target.arch);
    expected.add(name);
    const manifest = JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
    if (
      manifest.schema !== 1 ||
      manifest.applicationId !== appId ||
      manifest.version !== version ||
      manifest.platform !== target.platform ||
      manifest.arch !== target.arch ||
      !Array.isArray(manifest.assets) ||
      manifest.assets.length !== target.kinds.length
    )
      throw new Error(`Invalid manifest: ${name}`);
    const kinds = new Set();
    for (const asset of manifest.assets) {
      if (
        kinds.has(asset.kind) ||
        !target.kinds.includes(asset.kind) ||
        asset.name !== assetName(version, target.platform, target.arch, asset.kind)
      )
        throw new Error(`Invalid asset in ${name}`);
      kinds.add(asset.kind);
      expected.add(asset.name);
      const file = path.join(directory, asset.name),
        stat = fs.lstatSync(file);
      if (
        !stat.isFile() ||
        stat.isSymbolicLink() ||
        stat.size !== asset.size ||
        (await sha512(file)) !== asset.sha512
      )
        throw new Error(`Package verification failed: ${asset.name}`);
    }
  }
  const actual = fs.readdirSync(directory);
  if (actual.some((name) => !expected.has(name)) || actual.length !== expected.size)
    throw new Error('Unexpected or missing release files');
  return [...expected];
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [command, directory, version] = process.argv.slice(2);
  if (command === 'version') console.log(releaseVersion());
  else if (command === 'validate') {
    const files = await validateRelease(directory, version);
    console.log(`Verified ${files.length} release files`);
  } else if (command === 'matrix') console.log(JSON.stringify({ include: matrix }));
  else throw new Error('Expected version, validate, or matrix');
}
