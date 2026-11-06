import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { releaseVersion, root } from './contract.mjs';

export function releaseRequested(environment = process.env) {
  const ref = environment.GITHUB_REF || '';
  const tagged = environment.GITHUB_EVENT_NAME === 'push' && ref.startsWith('refs/tags/v');
  const manual =
    environment.GITHUB_EVENT_NAME === 'workflow_dispatch' &&
    environment.ECHO_PUBLISH_RELEASE === 'true';
  if (!tagged && !manual) return false;
  if (environment.GITHUB_REPOSITORY !== 'hoowhoami/EchoMusicNext')
    throw new Error('Publishing repository mismatch');
  if (ref !== 'refs/heads/main' && !ref.startsWith('refs/tags/v'))
    throw new Error('Publishing requires main or a matching version tag');
  return true;
}

function git(repository, args) {
  const result = spawnSync('git', args, { cwd: repository, encoding: 'utf8' });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`git ${args[0]} failed: ${result.stderr.trim()}`);
  return result.stdout.trim();
}

function remoteTag(repository, tag) {
  const rows = git(repository, [
    'ls-remote',
    '--tags',
    'origin',
    `refs/tags/${tag}`,
    `refs/tags/${tag}^{}`,
  ])
    .split('\n')
    .map((line) => line.split(/\s+/));
  // Annotated tags point at a tag object; compare their peeled commit instead.
  return (
    rows.find((row) => row[1] === `refs/tags/${tag}^{}`)?.[0] ||
    rows.find((row) => row[1] === `refs/tags/${tag}`)?.[0] ||
    ''
  );
}

export function checkReleaseTag(repository, version, expectedCommit = '') {
  if (releaseVersion(repository, '', '') !== version)
    throw new Error('Tag version differs from VERSION');
  const commit = git(repository, ['rev-parse', 'HEAD']);
  if (expectedCommit && expectedCommit !== commit)
    throw new Error('Checkout differs from release commit');
  const tag = `v${version}`,
    existing = remoteTag(repository, tag);
  if (existing && existing !== commit) throw new Error(`${tag} already points to another commit`);
  return { tag, commit, exists: Boolean(existing) };
}

export function ensureReleaseTag(repository, version, expectedCommit = '') {
  const state = checkReleaseTag(repository, version, expectedCommit);
  if (!state.exists) {
    try {
      git(repository, ['push', 'origin', `${state.commit}:refs/tags/${state.tag}`]);
    } catch (error) {
      // A retry or concurrent run can create the same immutable tag first.
      if (remoteTag(repository, state.tag) !== state.commit) throw error;
    }
  }
  return state.tag;
}

if (process.argv[1] && fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv[2] !== 'check') throw new Error('Expected check');
  if (releaseRequested()) {
    const state = checkReleaseTag(root, releaseVersion(), process.env.GITHUB_SHA);
    console.log(`Release target ${state.tag}: ${state.commit}`);
  }
}
