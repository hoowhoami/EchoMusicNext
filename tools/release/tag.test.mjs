import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { checkReleaseTag, ensureReleaseTag, releaseRequested } from './tag.mjs';

test('only explicit manual publication or version-tag pushes publish; ordinary pushes do not', () => {
  const environment = {
    GITHUB_REPOSITORY: 'hoowhoami/EchoMusicNext',
    GITHUB_REF: 'refs/heads/main',
  };
  assert.equal(releaseRequested({ ...environment, GITHUB_EVENT_NAME: 'push' }), false);
  assert.equal(releaseRequested({ ...environment, GITHUB_EVENT_NAME: 'workflow_dispatch' }), false);
  const manual = {
    ...environment,
    GITHUB_EVENT_NAME: 'workflow_dispatch',
    ECHO_PUBLISH_RELEASE: 'true',
  };
  assert.equal(releaseRequested(manual), true);
  assert.throws(
    () => releaseRequested({ ...manual, GITHUB_REF: 'refs/heads/feature' }),
    /requires main/,
  );
  assert.throws(
    () => releaseRequested({ ...manual, GITHUB_REPOSITORY: 'other/fork' }),
    /repository/,
  );
  assert.equal(
    releaseRequested({ ...environment, GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/tags/v1.2.3' }),
    true,
  );
});

test('release tags are created once, compare annotated commits and never move', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-tag-'));
  try {
    const source = path.join(directory, 'source'),
      remote = path.join(directory, 'remote.git');
    fs.mkdirSync(source);
    const git = (...args) => {
      const result = spawnSync(
        'git',
        ['-c', 'user.name=Tag Test', '-c', 'user.email=tag@example.invalid', ...args],
        { cwd: source, encoding: 'utf8' },
      );
      assert.equal(result.status, 0, result.stderr);
      return result.stdout.trim();
    };
    git('init', '--bare', remote);
    git('init');
    git('remote', 'add', 'origin', remote);
    fs.writeFileSync(path.join(source, 'VERSION'), '1.2.3\n');
    git('add', 'VERSION');
    git('commit', '-m', 'release');
    const commit = git('rev-parse', 'HEAD');
    assert.deepEqual(checkReleaseTag(source, '1.2.3', commit), {
      tag: 'v1.2.3',
      commit,
      exists: false,
    });
    assert.equal(git('ls-remote', '--tags', 'origin'), '');
    assert.throws(() => ensureReleaseTag(source, '1.2.4', commit), /differs/);
    assert.throws(() => ensureReleaseTag(source, '1.2.3', '0'.repeat(40)), /Checkout/);
    assert.equal(ensureReleaseTag(source, '1.2.3', commit), 'v1.2.3');
    assert.equal(ensureReleaseTag(source, '1.2.3', commit), 'v1.2.3');
    assert.equal(checkReleaseTag(source, '1.2.3', commit).exists, true);
    fs.writeFileSync(path.join(source, 'VERSION'), '1.2.4\n');
    git('add', 'VERSION');
    git('commit', '-m', 'next release');
    git('tag', '-a', 'v1.2.4', '-m', 'annotated release');
    git('push', 'origin', 'refs/tags/v1.2.4');
    assert.equal(checkReleaseTag(source, '1.2.4').exists, true);
    git('commit', '--allow-empty', '-m', 'different commit');
    assert.throws(() => ensureReleaseTag(source, '1.2.4'), /another commit/);
    assert.equal(git('ls-remote', '--tags', 'origin', 'refs/tags/v1.2.3').split(/\s/)[0], commit);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
