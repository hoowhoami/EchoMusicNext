import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {
  collectQtLicenses,
  prepareQtLicenseTexts,
  verifyQtFiles,
  verifyQtDeployment,
} from './qt-licenses.mjs';

function fixture() {
  const repository = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-qt-licenses-'));
  const qt = path.join(repository, 'sdk'),
    output = path.join(repository, 'licenses/deployment'),
    stage = path.join(repository, 'stage');
  fs.mkdirSync(path.join(qt, 'sbom'), { recursive: true });
  fs.mkdirSync(stage);
  fs.writeFileSync(path.join(qt, '.echo-sdk.json'), JSON.stringify({ version: '6.12.0' }));
  const texts = path.join(repository, 'build/release-tools/qt-license-texts');
  fs.mkdirSync(texts, { recursive: true });
  for (const id of ['GPL-2.0-only', 'GPL-3.0-only', 'LGPL-3.0-only', 'Qt-GPL-exception-1.0'])
    fs.writeFileSync(path.join(texts, id + '.txt'), 'Fixture license text');
  const metadata = (module, files, license = 'LGPL-3.0-only') =>
    fs.writeFileSync(
      path.join(qt, 'sbom', module + '-6.12.0.spdx.json'),
      JSON.stringify({
        spdxVersion: 'SPDX-2.3',
        packages: [
          {
            name: module,
            copyrightText: 'Copyright Qt contributors',
            downloadLocation: 'git://code.qt.io/qt/' + module + '.git@' + 'a'.repeat(40),
          },
        ],
        files: files.map((fileName) => ({ fileName, licenseConcluded: license })),
      }),
    );
  return {
    repository,
    qt,
    output,
    stage,
    texts,
    metadata,
    close: () => fs.rmSync(repository, { recursive: true, force: true }),
  };
}
test('WebSockets notices and exact source locations cover Mac, Windows and Linux layouts', () => {
  const f = fixture();
  try {
    f.metadata('qtbase', ['./lib/QtCore.framework/Versions/A/QtCore']);
    f.metadata('qtwebsockets', ['./lib/QtWebSockets.framework/Versions/A/QtWebSockets']);
    const inventory = collectQtLicenses(f.output, f.qt, { repository: f.repository });
    assert.deepEqual(
      inventory.modules.map((module) => module.name),
      ['qtbase', 'qtwebsockets'],
    );
    assert.equal(inventory.modules[1].ref, 'a'.repeat(40));
    assert.match(inventory.modules[1].source, /github.com\/qt\/qtwebsockets\/tree\/a{40}$/);
    assert.ok(fs.existsSync(path.join(f.output, 'Qt/qtwebsockets-6.12.0.spdx.json')));
    for (const files of [
      [
        'Contents/Frameworks/QtCore.framework/Versions/A/QtCore',
        'Contents/Frameworks/QtWebSockets.framework/Versions/A/QtWebSockets',
      ],
      ['bin/Qt6Core.dll', 'bin/Qt6WebSockets.dll'],
      ['lib/libQt6Core.so.6.12.0', 'lib/libQt6WebSockets.so.6'],
    ])
      assert.deepEqual(verifyQtFiles(files, inventory), ['qtbase', 'qtwebsockets']);
    fs.writeFileSync(path.join(f.stage, 'Qt6WebSockets.dll'), 'fixture binary');
    verifyQtDeployment(f.stage, path.dirname(f.output));
    fs.rmSync(path.join(f.output, 'Qt/qtwebsockets-6.12.0.spdx.json'));
    assert.throws(() => verifyQtDeployment(f.stage, path.dirname(f.output)), /qtwebsockets/);
    assert.throws(() => verifyQtFiles(['libQt6Graphs.so.6'], inventory), /Unmapped/);
  } finally {
    f.close();
  }
});
test('empty inventories and missing per-module or referenced license texts cannot pass', () => {
  const f = fixture();
  try {
    assert.throws(
      () => collectQtLicenses(f.output, f.qt, { repository: f.repository }),
      /No Qt module/,
    );
    f.metadata('qtbase', ['./lib/libQt6Core.so.6']);
    const inventory = collectQtLicenses(f.output, f.qt, { repository: f.repository });
    assert.throws(() => verifyQtFiles(['Qt6WebSockets.dll'], inventory), /Missing qtwebsockets/);
    fs.writeFileSync(path.join(f.qt, 'sbom/qtwebsockets-6.12.0.spdx.json'), '{}');
    assert.throws(
      () => collectQtLicenses(f.output, f.qt, { repository: f.repository }),
      /Invalid Qt SPDX/,
    );
    fs.rmSync(path.join(f.qt, 'sbom/qtwebsockets-6.12.0.spdx.json'));
    f.metadata('qtbase', ['./lib/libQt6Core.so.6'], 'BSD-3-Clause');
    assert.throws(
      () => collectQtLicenses(f.output, f.qt, { repository: f.repository }),
      /BSD-3-Clause/,
    );
  } finally {
    f.close();
  }
});
test('source SDK notices cover runtime plugins without requiring SPDX or shipping source code', () => {
  const f = fixture();
  try {
    fs.rmSync(path.join(f.qt, '.echo-sdk.json'));
    fs.mkdirSync(path.join(f.qt, 'lib/cmake/Qt6'), { recursive: true });
    fs.writeFileSync(
      path.join(f.qt, 'lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake'),
      'set(PACKAGE_VERSION "6.12.0")',
    );
    const source = path.join(f.repository, 'build/qt-sources/6.12.0/qtwebsockets');
    fs.mkdirSync(source, { recursive: true });
    fs.writeFileSync(path.join(source, 'LICENSE.txt'), 'Module copyright and license');
    fs.writeFileSync(path.join(source, 'qt_attribution.json'), '{"Copyright":"Qt contributors"}');
    fs.writeFileSync(path.join(source, 'private-source.cpp'), 'must not be copied');
    const inventory = collectQtLicenses(f.output, f.qt, { repository: f.repository });
    assert.deepEqual(
      verifyQtFiles(['libQt6WebSockets.so.6', 'usr/qml/QtWebSockets/plugin.so'], inventory),
      ['qtwebsockets'],
    );
    assert.ok(
      fs.existsSync(path.join(f.output, 'Qt/source-notices/qtwebsockets/qt_attribution.json')),
    );
    assert.ok(
      !fs.existsSync(path.join(f.output, 'Qt/source-notices/qtwebsockets/private-source.cpp')),
    );
  } finally {
    f.close();
  }
});
test('standard license texts are fetched once and validated, including exceptions', async () => {
  const f = fixture();
  try {
    f.metadata('qtbase', ['./lib/libQt6Core.so.6'], 'BSD-3-Clause AND MIT WITH LLVM-exception');
    const requests = [];
    const request = async (url) => {
      const id = decodeURIComponent(
        url
          .split('/')
          .pop()
          .replace(/\.json$/, ''),
      );
      requests.push(id);
      return {
        ok: true,
        json: async () =>
          id === 'LLVM-exception'
            ? { licenseExceptionId: id, licenseExceptionText: 'Exception text' }
            : { licenseId: id, licenseText: 'License text' },
      };
    };
    await prepareQtLicenseTexts(f.texts, f.qt, request);
    await prepareQtLicenseTexts(f.texts, f.qt, request);
    assert.deepEqual(requests, ['BSD-3-Clause', 'LLVM-exception', 'MIT']);
    collectQtLicenses(f.output, f.qt, { repository: f.repository });
    fs.rmSync(path.join(f.texts, 'MIT.txt'));
    await assert.rejects(
      prepareQtLicenseTexts(f.texts, f.qt, async () => ({ ok: false, status: 404 })),
      /HTTP 404/,
    );
    await assert.rejects(
      prepareQtLicenseTexts(f.texts, f.qt, async () => ({
        ok: true,
        json: async () => ({ licenseId: 'wrong', licenseText: 'Wrong license' }),
      })),
      /Invalid SPDX/,
    );
  } finally {
    f.close();
  }
});
