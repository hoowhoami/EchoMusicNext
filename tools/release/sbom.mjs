import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';

const digest = (bytes) => createHash('sha256').update(bytes).digest('hex');
const portable = (file) => file.split(path.sep).join('/');

export function rustSbom(metadata, root, version) {
  const references = new Map();
  const packages = [...metadata.packages].sort((a, b) => a.id.localeCompare(b.id));
  const components = packages.map((pkg) => {
    const relative = portable(path.relative(root, pkg.manifest_path));
    const source = pkg.source ?? `path:${relative}`;
    // Cargo package IDs contain absolute paths for local crates. They are only
    // lookup keys here, never published as component IDs or source locations.
    const ref = `cargo:${pkg.name}@${pkg.version}:${digest(source).slice(0, 16)}`;
    references.set(pkg.id, ref);
    const component = {
      type: 'library',
      'bom-ref': ref,
      name: pkg.name,
      version: pkg.version,
      purl: `pkg:cargo/${encodeURIComponent(pkg.name)}@${encodeURIComponent(pkg.version)}`,
      properties: [{ name: 'echomusic:source', value: source }],
    };
    if (pkg.license) component.licenses = [{ expression: pkg.license }];
    else if (pkg.license_file)
      component.licenses = [{ license: { name: 'See bundled package license file' } }];
    else component.properties.push({ name: 'echomusic:license-review-required', value: 'true' });
    return component;
  });
  const dependencies = (metadata.resolve?.nodes ?? [])
    .filter((node) => references.has(node.id))
    .map((node) => ({
      ref: references.get(node.id),
      dependsOn: (node.dependencies ?? [])
        .map((id) => references.get(id))
        .filter(Boolean)
        .sort(),
    }))
    .sort((a, b) => a.ref.localeCompare(b.ref));
  const application = 'application:EchoMusicNext';
  dependencies.unshift({
    ref: application,
    dependsOn: (metadata.workspace_members ?? [])
      .filter((id) => references.has(id))
      .map((id) => references.get(id))
      .sort(),
  });
  return {
    bomFormat: 'CycloneDX',
    specVersion: '1.6',
    version: 1,
    metadata: {
      component: { type: 'application', 'bom-ref': application, name: 'EchoMusicNext', version },
      properties: [
        {
          name: 'echomusic:inventory-scope',
          value:
            'Locked Cargo graph including build/dev dependencies and platform conditions; Qt inventories are bundled separately.',
        },
      ],
    },
    components,
    dependencies,
  };
}

export function vendorInventory(root) {
  const result = spawnSync('git', ['ls-files', '-z', '--', '**/vendor/**', '**/SOURCE.json'], {
    cwd: root,
    encoding: 'utf8',
  });
  if (result.status !== 0) throw new Error('Cannot inspect tracked vendor provenance');
  const files = result.stdout.split('\0').filter(Boolean).sort();
  const material = files.filter((file) =>
    /(?:SOURCE\.json|\.(?:zip|tar|tgz|xz|gz|zst))$/.test(file),
  );
  return {
    schemaVersion: 1,
    trackedVendorFiles: files.filter((file) => file.includes('/vendor/')).length,
    // SOURCE.json hashes describe captured upstream snapshots, not today's
    // intentionally modified native code. This records provenance, not a false
    // equality check against the historical source_sha256 baseline.
    provenanceAndArchives: material.map((file) => {
      const location = path.join(root, file);
      if (fs.lstatSync(location).isSymbolicLink())
        throw new Error(`Vendor archive/provenance must be a regular file: ${file}`);
      const data = fs.readFileSync(location);
      return { path: file, bytes: data.length, sha256: digest(data) };
    }),
  };
}
