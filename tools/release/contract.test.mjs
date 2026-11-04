import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import {
  assetName,
  matrix,
  writeManifest,
  validateRelease,
  validVersion,
  releaseVersion,
  verifyBuildVersion,
  verifyReleaseBuild,
  root,
} from './contract.mjs';
test('release tags own the version independently of CMake and Cargo declarations', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-version-'));
  try {
    for (const version of ['0.2.0', '1.0.0', '12.34.56-beta.2+build.3'])
      assert.equal(releaseVersion(dir, `refs/tags/v${version}`, ''), version);
    assert.equal(releaseVersion(dir, 'refs/heads/main', ''), '0.1.0-dev');
    assert.equal(releaseVersion(dir, 'refs/heads/main', '2.0.0-beta.1'), '2.0.0-beta.1');
    assert.throws(() => releaseVersion(dir, 'refs/tags/v2.0.0', '1.0.0'), /differs/);
    for (const ref of [
      'refs/tags/v',
      'refs/tags/0.1.0',
      'refs/tags/v0.1.0-beta.01',
      'refs/tags/v1.0',
      'refs/tags/v01.0.0',
    ])
      assert.throws(() => releaseVersion(dir, ref, ''));
    for (const value of ['1.0.0', '1.0.0-beta.2+build', '999999999999999.0.0'])
      assert.ok(validVersion(value));
    for (const value of ['1.0', '01.0.0', '1.0.0-beta.00', 'v1.0.0', '../foo'])
      assert.ok(!validVersion(value));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
test('CMake and packaging resolve tags, archives and reconfiguration consistently', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-cmake-version-'));
  try {
    const source = path.join(dir, 'source'),
      build = path.join(dir, 'build');
    fs.mkdirSync(path.join(source, 'cmake'), { recursive: true });
    fs.copyFileSync(
      path.join(root, 'cmake/ApplicationVersion.cmake'),
      path.join(source, 'cmake/ApplicationVersion.cmake'),
    );
    fs.copyFileSync(path.join(root, 'app/Application.rc.in'), path.join(source, 'native.rc.in'));
    fs.writeFileSync(
      path.join(source, 'CMakeLists.txt'),
      `cmake_minimum_required(VERSION 3.28)
include(cmake/ApplicationVersion.cmake)
project(VersionFixture VERSION "\${ECHO_RELEASE_CORE}" LANGUAGES NONE)
configure_file(version.in version.txt @ONLY)
configure_file(native.rc.in native.rc @ONLY)
`,
    );
    fs.writeFileSync(path.join(source, 'version.in'), '@ECHO_RELEASE_VERSION@|@PROJECT_VERSION@');
    const configure = (ref, version = '', extra = []) =>
      spawnSync('cmake', ['-S', source, '-B', build, ...extra], {
        env: { ...process.env, GITHUB_REF: ref, RELEASE_VERSION: version },
        encoding: 'utf8',
      });
    for (const version of ['2.3.4-beta.1', '2.3.4-beta.2', '2.3.4', '3.0.0']) {
      const result = configure(`refs/tags/v${version}`, version);
      assert.equal(result.status, 0, result.stdout + result.stderr);
      assert.equal(
        fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
        `${releaseVersion(source, `refs/tags/v${version}`, version)}|${version.split('-')[0]}`,
      );
      const resource = fs.readFileSync(path.join(build, 'native.rc'), 'utf8');
      assert.ok(resource.includes(`FILEVERSION ${version.split('-')[0].replaceAll('.', ',')},0`));
      assert.ok(resource.includes(`VALUE "ProductVersion", "${version}\\0"`));
    }
    for (const tag of ['v', '2.0.0', 'v2.0.0-beta.01', 'v02.0.0', 'v2.0', 'v2.0.0+'])
      assert.notEqual(configure(`refs/tags/${tag}`).status, 0, tag);
    assert.notEqual(configure('refs/tags/v2.0.0', '3.0.0').status, 0);
    assert.notEqual(configure('refs/tags/v2.0.0', '', ['-DECHO_RELEASE_VERSION=1.0.0']).status, 0);
    let result = configure('refs/heads/main', '', ['-U', 'ECHO_RELEASE_VERSION']);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(
      fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
      '0.1.0-dev|0.1.0',
    );
    const git = (...args) => {
      const run = spawnSync('git', args, { cwd: source, encoding: 'utf8' });
      assert.equal(run.status, 0, run.stderr);
    };
    git('init');
    git('add', '.');
    git(
      '-c',
      'user.name=Version Test',
      '-c',
      'user.email=version@example.invalid',
      'commit',
      '-m',
      'fixture',
    );
    git('tag', 'v4.5.6-beta.1');
    result = configure('');
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(
      fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
      '4.5.6-beta.1|4.5.6',
    );
    assert.equal(releaseVersion(source, '', ''), '4.5.6-beta.1');
    git(
      '-c',
      'user.name=Version Test',
      '-c',
      'user.email=version@example.invalid',
      'commit',
      '--allow-empty',
      '-m',
      'after tag',
    );
    result = configure('');
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(
      fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
      '0.1.0-dev|0.1.0',
    );
    assert.equal(releaseVersion(source, '', ''), '0.1.0-dev');
    fs.mkdirSync(path.join(build, 'generated'));
    const info = path.join(build, 'generated/EchoBuildInfo.h');
    fs.writeFileSync(info, '#define ECHO_RELEASE_VERSION "1.0.0"\n');
    assert.throws(() => verifyBuildVersion(build, '2.0.0'), /reconfigure/);
    assert.doesNotThrow(() => verifyBuildVersion(build, '1.0.0'));
    const cache = path.join(build, 'CMakeCache.txt');
    fs.writeFileSync(cache, 'ECHO_DISTRIBUTION_BUILD:BOOL=OFF\n');
    assert.throws(() => verifyReleaseBuild(build, '1.0.0'), /distribution/);
    fs.writeFileSync(cache, 'ECHO_DISTRIBUTION_BUILD:BOOL=ON\n');
    assert.doesNotThrow(() => verifyReleaseBuild(build, '1.0.0'));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
test('publishing requires all six targets, exact bytes and no unexpected files', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-release-'));
  const version = '0.1.0-beta.1';
  try {
    for (const target of matrix) {
      for (const kind of target.kinds)
        fs.writeFileSync(
          path.join(dir, assetName(version, target.platform, target.arch, kind)),
          `payload-${target.platform}-${target.arch}-${kind}`,
        );
      await writeManifest(dir, version, target.platform, target.arch);
    }
    assert.equal((await validateRelease(dir, version)).length, 22);
    const file = path.join(dir, assetName(version, 'Windows', 'x64', 'nsis'));
    const bytes = fs.readFileSync(file);
    fs.writeFileSync(file, Buffer.alloc(bytes.length, 0));
    await assert.rejects(validateRelease(dir, version), /verification/);
    fs.writeFileSync(file, bytes);
    fs.writeFileSync(path.join(dir, 'extra.txt'), 'unintended');
    await assert.rejects(validateRelease(dir, version), /Unexpected/);
    fs.unlinkSync(path.join(dir, 'extra.txt'));
    fs.unlinkSync(file);
    await assert.rejects(validateRelease(dir, version));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
