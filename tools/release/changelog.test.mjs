import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { root } from './contract.mjs';
import { releaseChangelog, prepareChangelog } from './changelog.mjs';

const source = `# 更新记录

## 未发布

下次版本草稿。

## 2.0.0-beta.1

### 修复

- 当前版本说明。

## 2.0.0

正式版说明。

## 1.0.0

旧版本说明。
`;
test('only the requested manually maintained version is shipped', () => {
  const notes = releaseChangelog(source, '2.0.0-beta.1');
  assert.equal(notes.body, '### 修复\n\n- 当前版本说明。');
  assert.equal(notes.current, '# 更新记录\n\n## 2.0.0-beta.1\n\n' + notes.body + '\n');
  for (const unrelated of ['下次版本草稿', '正式版说明', '旧版本说明', '## 未发布'])
    assert.ok(!notes.current.includes(unrelated));
  assert.equal(releaseChangelog(source, '2.0.0-beta.2').body, '正式版说明。');
  assert.equal(releaseChangelog(source, '2.0.0').body, '正式版说明。');
  assert.throws(() => releaseChangelog(source, '3.0.0'), /Missing/);
  assert.throws(() => releaseChangelog('# 更新记录\n\n## 未发布\n\n草稿', '3.0.0'), /Missing/);
});
test('manual titles, fenced examples and empty or ambiguous notes are handled explicitly', () => {
  const body = '### 修复\n\n```md\n## 1.0.0\n示例，不是旧日志章节。\n```\n\n- 说明。';
  const text = '# 更新记录\r\n\r\n## [v3.2.1]（2026-11-04）\r\n\r\n' + body;
  const notes = releaseChangelog(text, '3.2.1');
  assert.equal(notes.body, body);
  assert.ok(notes.current.includes('## [v3.2.1]（2026-11-04）'));
  for (const empty of ['', '### 修复\n\n<!-- 待补 -->\n\n- '])
    assert.throws(() => releaseChangelog('## 1.0.0\n\n' + empty, '1.0.0'), /Missing/);
  assert.throws(
    () => releaseChangelog(source + '\n## 2.0.0-beta.1\n重复', '2.0.0-beta.1'),
    /Duplicate/,
  );
  assert.throws(() => releaseChangelog(source, '../invalid'), /Invalid/);
});
test('prepared notes are stable, preserve the source and feed distribution CMake', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-notes-'));
  try {
    const project = path.join(dir, 'source'),
      build = path.join(dir, 'build'),
      output = path.join(dir, 'notes');
    fs.mkdirSync(project);
    const input = path.join(project, 'CHANGELOG.md');
    fs.writeFileSync(input, source);
    const expected = prepareChangelog(project, '2.0.0-beta.1', output);
    assert.equal(fs.readFileSync(input, 'utf8'), source);
    assert.equal(fs.readFileSync(path.join(output, 'CHANGELOG.md'), 'utf8'), expected.current);
    assert.throws(() => prepareChangelog(project, '2.0.0-beta.1', project), /overwrite/);
    for (const name of ['contract.mjs', 'changelog.mjs']) {
      fs.mkdirSync(path.join(project, 'tools/release'), { recursive: true });
      fs.copyFileSync(
        path.join(root, 'tools/release', name),
        path.join(project, 'tools/release', name),
      );
    }
    fs.copyFileSync(
      path.join(root, 'cmake/ReleaseNotes.cmake'),
      path.join(project, 'ReleaseNotes.cmake'),
    );
    fs.writeFileSync(
      path.join(project, 'CMakeLists.txt'),
      `cmake_minimum_required(VERSION 3.28)
project(NotesFixture LANGUAGES NONE)
set(ECHO_DISTRIBUTION_BUILD ON)
set(ECHO_RELEASE_VERSION "2.0.0-beta.1")
include(ReleaseNotes.cmake)
`,
    );
    const configure = () =>
      spawnSync('cmake', ['-S', project, '-B', build], {
        env: {
          ...process.env,
          GITHUB_REF: '',
          RELEASE_VERSION: '',
          ECHO_CHANGELOG_FILE: path.join(output, 'CHANGELOG.md'),
        },
        encoding: 'utf8',
      });
    const result = configure();
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(
      fs.readFileSync(path.join(build, 'generated/CHANGELOG.md'), 'utf8'),
      expected.current,
    );
    assert.equal(fs.readFileSync(input, 'utf8'), source);
    fs.writeFileSync(path.join(output, 'CHANGELOG.md'), '## 9.0.0\n\n错误版本说明。\n');
    assert.notEqual(configure().status, 0);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
