import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { root } from './contract.mjs';
import { prepareQtLicenseTexts } from './qt-licenses.mjs';
import { verifyMicrosoftRuntime } from './windows-redist.mjs';
const directory = path.join(root, 'build/release-tools');
fs.mkdirSync(directory, { recursive: true });
async function download(url, file, expected) {
  if (
    expected &&
    fs.existsSync(file) &&
    crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') === expected
  )
    return;
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Download failed ${response.status}: ${url}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  if (expected && crypto.createHash('sha256').update(bytes).digest('hex') !== expected)
    throw new Error(`Tool checksum changed: ${url}`);
  fs.writeFileSync(file, bytes);
}
if (process.platform === 'linux') {
  const arch = process.arch === 'arm64' ? 'aarch64' : 'x86_64';
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'tools/release/linux-tools.lock.json')));
  for (const tool of lock.tools.filter((tool) => tool.name.endsWith(`-${arch}.AppImage`))) {
    const file = path.join(directory, tool.name);
    await download(tool.url, file, tool.sha256);
    fs.chmodSync(file, 0o755);
  }
}
if (process.platform === 'win32') {
  const arch = process.arch === 'arm64' ? 'arm64' : 'x64';
  const file = path.join(directory, `vc_redist.${arch}.exe`);
  await download(`https://aka.ms/vc14/vc_redist.${arch}.exe`, file);
  const version = verifyMicrosoftRuntime(file, directory);
  fs.writeFileSync(
    path.join(directory, `vc_redist.${arch}.json`),
    JSON.stringify({ version }) + '\n',
  );
}
// The SDK's SPDX records carry component notices; preserve Qt's license texts too.
const licenses = path.join(directory, 'qt-license-texts');
fs.mkdirSync(licenses, { recursive: true });
for (const name of ['GPL-2.0-only', 'GPL-3.0-only', 'LGPL-3.0-only', 'Qt-GPL-exception-1.0'])
  await download(
    `https://raw.githubusercontent.com/qt/qtbase/v6.12.0/LICENSES/${name}.txt`,
    path.join(licenses, `${name}.txt`),
  );
const configuredQt = /(?:^|\n)Qt6_DIR:[^=]+=([^\r\n]+)/.exec(
  fs.existsSync(path.join(root, 'build/release/CMakeCache.txt'))
    ? fs.readFileSync(path.join(root, 'build/release/CMakeCache.txt'), 'utf8')
    : '',
)?.[1];
const qt =
  process.argv.find((value) => value.startsWith('--qt='))?.slice(5) ||
  process.env.QT_PREFIX ||
  (configuredQt ? path.resolve(configuredQt, '../../..') : null);
if (!qt) throw new Error('Qt SDK is required to prepare license texts; pass --qt=/path/to/sdk');
await prepareQtLicenseTexts(licenses, qt);

// Packaging inventories cover the locked graph across platforms. Host-only
// builds do not necessarily populate every license source used by metadata.
const cargoFetch = spawnSync('cargo', ['fetch', '--locked'], { cwd: root, stdio: 'inherit' });
if (cargoFetch.error) throw cargoFetch.error;
if (cargoFetch.status !== 0) throw new Error('Cannot prepare locked Cargo license sources');
