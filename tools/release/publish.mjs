import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { releaseVersion, root, validateRelease } from './contract.mjs';
import { releaseChangelog } from './changelog.mjs';
import { releaseRequested, ensureReleaseTag } from './tag.mjs';
if (!releaseRequested()) throw new Error('Publishing was not requested');
const version = releaseVersion(),
  tag = `v${version}`;
const repo = process.env.GITHUB_REPOSITORY;
if (repo !== 'hoowhoami/EchoMusicNext') throw new Error('Publishing repository mismatch');
const directory = path.resolve(root, 'build/release-assets');
const files = await validateRelease(directory, version);
const notesFile = process.env.ECHO_CHANGELOG_FILE || path.join(root, 'CHANGELOG.md');
const { body } = releaseChangelog(fs.readFileSync(notesFile, 'utf8'), version);
// Only create the release marker after all six targets and notes are verified.
ensureReleaseTag(root, version, process.env.GITHUB_SHA);
function gh(args, input) {
  const result = spawnSync('gh', args, {
    encoding: 'utf8',
    input,
    stdio: ['pipe', 'pipe', 'inherit'],
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`gh failed (${result.status})`);
  return result.stdout;
}
function api(method, suffix, value) {
  const result = gh(
    ['api', '--method', method, `repos/${repo}/${suffix}`, ...(value ? ['--input', '-'] : [])],
    value ? JSON.stringify(value) : undefined,
  );
  return result.trim() ? JSON.parse(result) : null;
}
// Keep incomplete uploads invisible to application update checks.
const listed = JSON.parse(
  gh(['api', '--paginate', '--slurp', `repos/${repo}/releases?per_page=100`]),
);
const existing = listed.flat().find((release) => release.tag_name === tag);
const payload = {
  tag_name: tag,
  name: `EchoMusicNext ${tag}`,
  body,
  draft: true,
  prerelease: version.split('+')[0].includes('-'),
};
const release = existing
  ? api('PATCH', `releases/${existing.id}`, payload)
  : api('POST', 'releases', payload);
for (const asset of release.assets || [])
  if (!files.includes(asset.name)) api('DELETE', `releases/assets/${asset.id}`);
gh([
  'release',
  'upload',
  tag,
  '--repo',
  repo,
  '--clobber',
  ...files.map((file) => path.join(directory, file)),
]);
const uploaded = api('GET', `releases/${release.id}`);
if (
  uploaded.assets.length !== files.length ||
  uploaded.assets.some(
    (asset) =>
      !files.includes(asset.name) ||
      asset.size !== fs.statSync(path.join(directory, asset.name)).size,
  )
)
  throw new Error('Uploaded release is incomplete; retained as draft');
api('PATCH', `releases/${release.id}`, {
  draft: false,
  make_latest: payload.prerelease ? 'false' : 'legacy',
});
console.log(`Published ${tag} with ${files.length} verified files`);
