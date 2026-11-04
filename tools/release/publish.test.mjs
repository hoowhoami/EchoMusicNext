import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { root, matrix, assetName, writeManifest } from './contract.mjs';
import { prepareChangelog } from './changelog.mjs';
test(
  'publishing stages the complete release and keeps failed uploads as draft',
  { skip: process.platform === 'win32' },
  async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-publish-'));
    const version = '0.1.0-beta.1';
    try {
      const releaseTools = path.join(directory, 'tools/release'),
        assets = path.join(directory, 'build/release-assets'),
        bin = path.join(directory, 'bin');
      for (const subdir of [releaseTools, assets, bin]) fs.mkdirSync(subdir, { recursive: true });
      for (const name of ['contract.mjs', 'publish.mjs', 'changelog.mjs'])
        fs.copyFileSync(path.join(root, 'tools/release', name), path.join(releaseTools, name));
      fs.writeFileSync(
        path.join(directory, 'CHANGELOG.md'),
        '# 更新记录\n\n## 未发布\n\n下次版本草稿。\n\n## 0.1.0-beta.1\n\n实际版本说明。\n\n## 0.0.1\n\n旧版本说明。\n',
      );
      const notesDirectory = path.join(directory, 'build/release-notes');
      prepareChangelog(directory, version, notesDirectory);
      const notesFile = path.join(notesDirectory, 'CHANGELOG.md');
      // The release job must use the same snapshot as all package jobs.
      fs.writeFileSync(
        path.join(directory, 'CHANGELOG.md'),
        '## 0.1.0-beta.1\n\n工作区中的其它说明。\n',
      );
      for (const target of matrix) {
        for (const kind of target.kinds)
          fs.writeFileSync(
            path.join(assets, assetName(version, target.platform, target.arch, kind)),
            'payload',
          );
        await writeManifest(assets, version, target.platform, target.arch);
      }
      const gh = path.join(bin, 'gh'),
        state = path.join(directory, 'state.json');
      fs.writeFileSync(
        gh,
        `#!/usr/bin/env node
const fs = require('node:fs'), path = require('node:path');
const file = process.env.TEST_STATE, args = process.argv.slice(2);
let state = fs.existsSync(file) ? JSON.parse(fs.readFileSync(file)) : { calls: [], release: null };
state.calls.push(args);
function done(value) { fs.writeFileSync(file, JSON.stringify(state)); if (value) console.log(JSON.stringify(value)); }
if (args.includes('--paginate')) done([state.release ? [state.release] : []]);
else if (args[0] === 'release' && args[1] === 'upload') {
  if (!state.release.draft) throw Error('upload must be private');
  if (process.env.TEST_FAIL) { done(); process.exit(2); }
  state.release.assets = args.filter(a => fs.existsSync(a) && fs.statSync(a).isFile()).map((f, i) => ({ id: i, name: path.basename(f), size: fs.statSync(f).size }));
  done();
} else {
  const method = args[args.indexOf('--method') + 1];
  if (method === 'POST' || method === 'PATCH') {
    const value = JSON.parse(fs.readFileSync(0, 'utf8'));
    state.release = { id: 42, assets: [], ...state.release, ...value };
    done(state.release);
  } else done(state.release);
}
`,
      );
      fs.chmodSync(gh, 0o755);
      const environment = {
        ...process.env,
        PATH: bin + path.delimiter + process.env.PATH,
        GITHUB_EVENT_NAME: 'push',
        GITHUB_REF: `refs/tags/v${version}`,
        RELEASE_VERSION: version,
        ECHO_CHANGELOG_FILE: notesFile,
        GITHUB_REPOSITORY: 'hoowhoami/EchoMusicNext',
        TEST_STATE: state,
      };
      const run = (extra) =>
        spawnSync(process.execPath, [path.join(releaseTools, 'publish.mjs')], {
          env: { ...environment, ...extra },
          encoding: 'utf8',
        });
      let result = run({});
      assert.equal(result.status, 0, result.stderr);
      let published = JSON.parse(fs.readFileSync(state));
      assert.equal(published.release.draft, false);
      assert.equal(published.release.prerelease, true);
      assert.equal(published.release.assets.length, 22);
      assert.equal(published.release.body, '实际版本说明。');
      assert.ok(
        published.calls.findIndex((args) => args.includes('upload')) >
          published.calls.findIndex((args) => args.includes('POST')),
      );
      fs.unlinkSync(state);
      result = run({ TEST_FAIL: '1' });
      assert.notEqual(result.status, 0);
      published = JSON.parse(fs.readFileSync(state));
      assert.equal(published.release.draft, true);
      fs.unlinkSync(state);
      result = run({ GITHUB_EVENT_NAME: 'workflow_dispatch' });
      assert.notEqual(result.status, 0);
      assert.ok(!fs.existsSync(state));
      fs.writeFileSync(notesFile, '## 9.0.0\n\n不匹配的版本。\n');
      result = run({});
      assert.notEqual(result.status, 0);
      assert.ok(!fs.existsSync(state));
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  },
);
