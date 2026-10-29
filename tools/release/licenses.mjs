import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { root } from './contract.mjs';
import { rustSbom, vendorInventory } from './sbom.mjs';
function copyNotices(directory, target, all = false) {
  if (!fs.existsSync(directory)) return;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.isSymbolicLink() || ['.git', 'target', 'node_modules'].includes(entry.name)) continue;
    const file = path.join(directory, entry.name),
      to = path.join(target, entry.name);
    if (entry.isDirectory()) copyNotices(file, to, all || /^licen[sc]es?$/i.test(entry.name));
    else if (all || /^(licen[sc]e|copying|copyright|notice|authors)([.\-_]|$)/i.test(entry.name)) {
      fs.mkdirSync(target, { recursive: true });
      fs.copyFileSync(file, to);
    }
  }
}

export function collectQtNotices(output, qt, sourceRoot = root) {
  const qtModules = new Set([
    'qtbase',
    'qtdeclarative',
    'qtmultimedia',
    'qtsvg',
    'qtshadertools',
    'qtwayland',
    'qtwebsockets',
  ]);
  fs.mkdirSync(path.join(output, 'Qt'), { recursive: true });
  let webSocketsSpdx = false;
  const sbom = path.join(qt, 'sbom');
  if (fs.existsSync(sbom))
    for (const name of fs.readdirSync(sbom)) {
      if (
        (name.endsWith('.spdx.json') || name.endsWith('.source.spdx')) &&
        qtModules.has(name.split('-6.')[0])
      ) {
        fs.copyFileSync(path.join(sbom, name), path.join(output, 'Qt', name));
        webSocketsSpdx ||= name.startsWith('qtwebsockets-6.');
      }
    }
  const cachedNotices = path.join(qt, 'share/echomusicnext-qt-notices');
  for (const module of qtModules)
    copyNotices(
      path.join(sourceRoot, 'build/qt-sources/6.12.0', module),
      path.join(cachedNotices, module),
    );
  if (fs.existsSync(cachedNotices))
    fs.cpSync(cachedNotices, path.join(output, 'Qt/source-notices'), { recursive: true });
  else if (!fs.existsSync(sbom))
    throw new Error('Qt source notices or SDK SPDX inventory required');
  const hasNotice = (directory) =>
    fs.existsSync(directory) &&
    fs
      .readdirSync(directory, { withFileTypes: true })
      .some(
        (entry) =>
          entry.isFile() || (entry.isDirectory() && hasNotice(path.join(directory, entry.name))),
      );
  if (!webSocketsSpdx && !hasNotice(path.join(output, 'Qt/source-notices/qtwebsockets')))
    throw new Error('Required QtWebSockets SPDX or source notices are missing');
}

export function collectLicenses(destination, qt, version) {
  const output = path.join(destination, 'deployment');
  fs.mkdirSync(output, { recursive: true });
  const source = path.join(root, 'build/release-tools/qt-license-texts');
  if (!fs.existsSync(source))
    throw new Error('Run node tools/release/prepare.mjs before packaging');
  fs.cpSync(source, path.join(output, 'Qt/licenses'), { recursive: true });
  collectQtNotices(output, qt);
  const result = spawnSync('cargo', ['metadata', '--locked', '--offline', '--format-version=1'], {
    cwd: root,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 ** 2,
  });
  if (result.status !== 0)
    throw new Error('Cannot collect locked Cargo dependency licenses: ' + result.stderr);
  const metadata = JSON.parse(result.stdout),
    inventory = [];
  for (const pkg of metadata.packages) {
    const directory = path.dirname(pkg.manifest_path);
    inventory.push({
      name: pkg.name,
      version: pkg.version,
      license: pkg.license,
      source: pkg.source,
      repository: pkg.repository,
    });
    copyNotices(directory, path.join(output, 'Rust', `${pkg.name}-${pkg.version}`));
    if (pkg.license_file) {
      const target = path.join(output, 'Rust', `${pkg.name}-${pkg.version}`);
      fs.mkdirSync(target, { recursive: true });
      fs.copyFileSync(
        path.resolve(directory, pkg.license_file),
        path.join(target, path.basename(pkg.license_file)),
      );
    }
  }
  fs.writeFileSync(
    path.join(output, 'Rust/dependencies.json'),
    JSON.stringify(inventory, null, 2) + '\n',
  );
  fs.writeFileSync(
    path.join(output, 'Rust/bom.cdx.json'),
    JSON.stringify(rustSbom(metadata, root, version), null, 2) + '\n',
  );
  fs.writeFileSync(
    path.join(output, 'vendor-inventory.json'),
    JSON.stringify(vendorInventory(root), null, 2) + '\n',
  );
  fs.writeFileSync(
    path.join(output, 'SOURCE.txt'),
    `EchoMusicNext ${version}\nGPL-3.0-only\nSource: https://github.com/hoowhoami/EchoMusicNext/tree/v${version}\nDependency versions and upstream source locations: Rust/dependencies.json and Qt/*.spdx.json\nBuild instructions: docs/build.md in the source repository.\n`,
  );
}
