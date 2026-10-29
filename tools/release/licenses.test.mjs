import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { collectQtNotices } from './licenses.mjs';
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'qt notice materials '));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const qt = path.join(root, 'qt'),
    output = path.join(root, 'output');
  fs.mkdirSync(path.join(qt, 'sbom'), { recursive: true });
  fs.mkdirSync(path.join(output, 'Qt'), { recursive: true });
  fs.writeFileSync(path.join(qt, 'sbom/qtbase-6.12.0.spdx.json'), '{"SPDXID":"base"}');
  return { root, qt, output };
}
test('required QtWebSockets SPDX is copied alongside the actual declared modules', (t) => {
  const { root, qt, output } = fixture(t);
  const text = '{"SPDXID":"QtWebSockets","licenseConcluded":"LGPL-3.0-only"}';
  fs.writeFileSync(path.join(qt, 'sbom/qtwebsockets-6.12.0.spdx.json'), text);
  collectQtNotices(output, qt, root);
  assert.equal(
    fs.readFileSync(path.join(output, 'Qt/qtwebsockets-6.12.0.spdx.json'), 'utf8'),
    text,
  );
});
test('QtWebSockets preferred source notices are preserved byte for byte', (t) => {
  const { root, qt, output } = fixture(t);
  const file = path.join(root, 'build/qt-sources/6.12.0/qtwebsockets/third_party/LICENSE.txt');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const text = 'QtWebSockets dependency attribution fixture\n';
  fs.writeFileSync(file, text);
  collectQtNotices(output, qt, root);
  assert.equal(
    fs.readFileSync(
      path.join(output, 'Qt/source-notices/qtwebsockets/third_party/LICENSE.txt'),
      'utf8',
    ),
    text,
  );
});
test('unrelated Qt SPDX records cannot hide missing required QtWebSockets notices', (t) => {
  const { root, qt, output } = fixture(t);
  assert.throws(() => collectQtNotices(output, qt, root), /QtWebSockets.*notices|required/i);
});
test('SDK QtWebSockets source SPDX original accompanies the binary SPDX', (t) => {
  const { root, qt, output } = fixture(t);
  const name = 'qtwebsockets-6.12.0.source.spdx';
  const original = 'SPDXVersion: SPDX-2.3\nDataLicense: CC0-1.0\nSPDXID: SPDXRef-DOCUMENT\n';
  fs.writeFileSync(path.join(qt, 'sbom', name), original);
  fs.writeFileSync(
    path.join(qt, 'sbom/qtwebsockets-6.12.0.spdx.json'),
    '{"SPDXID":"QtWebSockets"}',
  );
  collectQtNotices(output, qt, root);
  assert.equal(fs.readFileSync(path.join(output, 'Qt', name), 'utf8'), original);
});
