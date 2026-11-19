import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { prepareQtDeployment } from './qt-deployment.mjs';

for (const platform of ['macOS', 'Linux'])
  test(`private ${platform} deployment view preserves SDK SQL targets and binaries`, () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-qt-view-'));
    const qt = path.join(root, 'sdk'),
      output = path.join(root, 'deploy');
    const suffix = platform === 'macOS' ? '.dylib' : '.so';
    try {
      for (const name of [
        'bin',
        'lib/cmake/Qt6Sql',
        'plugins/sqldrivers',
        'plugins/imageformats',
        'plugins/multimedia',
      ])
        fs.mkdirSync(path.join(qt, name), { recursive: true });
      for (const tool of ['qmake', 'macdeployqt'])
        fs.writeFileSync(path.join(qt, 'bin', tool), 'tool', { mode: 0o755 });
      const driver = `libqsqlmimer${suffix}`,
        sqlite = `libqsqlite${suffix}`;
      fs.writeFileSync(path.join(qt, 'plugins/sqldrivers', driver), 'external database client');
      fs.writeFileSync(path.join(qt, 'plugins/sqldrivers', sqlite), 'sqlite');
      const cmake = path.join(qt, 'lib/cmake/Qt6Sql/driver.cmake');
      fs.writeFileSync(cmake, `target plugins/sqldrivers/${driver}`);
      const tools = prepareQtDeployment(qt, output, platform);
      assert.equal(
        fs.readFileSync(path.join(qt, 'plugins/sqldrivers', driver), 'utf8'),
        'external database client',
      );
      assert.equal(fs.readFileSync(cmake, 'utf8'), `target plugins/sqldrivers/${driver}`);
      assert.deepEqual(fs.readdirSync(path.join(output, 'plugins/sqldrivers')), [sqlite]);
      for (const name of ['imageformats', 'multimedia'])
        assert.equal(
          fs.realpathSync(path.join(output, 'plugins', name)),
          fs.realpathSync(path.join(qt, 'plugins', name)),
        );
      assert.equal(fs.statSync(tools.qmake).mode & 0o777, 0o755);
      assert.match(
        fs.readFileSync(path.join(output, 'bin/qt.conf'), 'utf8'),
        /Plugins=.*deploy\/plugins/,
      );
      assert.throws(
        () => prepareQtDeployment(qt, path.join(qt, 'shadow'), platform),
        /outside the Qt SDK/,
      );
      fs.rmSync(output, { recursive: true });
      assert.equal(fs.readFileSync(path.join(qt, 'plugins/sqldrivers', sqlite), 'utf8'), 'sqlite');
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
