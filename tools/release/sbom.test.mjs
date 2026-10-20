import test from 'node:test';
import assert from 'node:assert/strict';
import { rustSbom } from './sbom.mjs';

test('SBOM is portable, preserves locked edges and flags unknown licenses', () => {
  const metadata = {
    packages: [
      {
        id: 'local-id',
        name: 'echo-ffi',
        version: '0.1.0',
        manifest_path: '/private/work/crates/echo-ffi/Cargo.toml',
        source: null,
        license: 'GPL-3.0-only',
      },
      {
        id: 'registry-id',
        name: 'dependency',
        version: '1.2.3',
        manifest_path: '/cache/dependency/Cargo.toml',
        source: 'registry+https://github.com/rust-lang/crates.io-index',
        license: null,
      },
    ],
    workspace_members: ['local-id'],
    resolve: {
      nodes: [
        { id: 'local-id', dependencies: ['registry-id'] },
        { id: 'registry-id', dependencies: [] },
      ],
    },
  };
  const report = rustSbom(metadata, '/private/work', '0.1.0');
  const serialized = JSON.stringify(report);
  assert(!serialized.includes('/private/work'));
  assert(!serialized.includes('/cache'));
  assert.equal(report.components.length, 2);
  const local = report.components.find((c) => c.name === 'echo-ffi');
  const dependency = report.components.find((c) => c.name === 'dependency');
  assert.deepEqual(report.dependencies.find((d) => d.ref === local['bom-ref']).dependsOn, [
    dependency['bom-ref'],
  ]);
  assert(dependency.properties.some((p) => p.name === 'echomusic:license-review-required'));
  assert.deepEqual(
    report,
    rustSbom({ ...metadata, packages: [...metadata.packages].reverse() }, '/private/work', '0.1.0'),
  );
});
