import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { root } from './contract.mjs';
import { rustSbom, vendorInventory } from './sbom.mjs';
import { copyNotices } from './license-files.mjs';
import { collectQtLicenses } from './qt-licenses.mjs';
export function collectLicenses(destination, qt, version) {
  const output = path.join(destination, 'deployment');
  fs.mkdirSync(output, { recursive: true });
  collectQtLicenses(output, qt);
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
  const revision = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' });
  const sourceRef =
    revision.status === 0 && /^[0-9a-f]{40}$/.test(revision.stdout.trim())
      ? revision.stdout.trim()
      : `v${version}`;
  fs.writeFileSync(
    path.join(output, 'SOURCE.txt'),
    `EchoMusicNext ${version}\nGPL-3.0-only\nSource: https://github.com/hoowhoami/EchoMusicNext/tree/${sourceRef}\nThis distribution uses Qt under its available open-source license terms.\nQt module sources and revisions: Qt/modules.json and Qt/NOTICE.txt\nDependency versions, copyright and license records: Rust/dependencies.json and Qt/*.spdx.json\nBuild instructions: https://github.com/hoowhoami/EchoMusicNext/blob/${sourceRef}/docs/build.md\n`,
  );
}
