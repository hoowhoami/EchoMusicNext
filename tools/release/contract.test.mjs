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
test('VERSION owns local, CI and source-archive versions; external inputs only assert it', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-version-'));
  try {
    assert.throws(() => releaseVersion(dir, '', ''), /ENOENT/);
    for (const version of ['0.2.0', '1.0.0', '12.34.56-beta.2+build.3']) {
      fs.writeFileSync(path.join(dir, 'VERSION'), `${version}\n`);
      for (const ref of ['', 'refs/heads/main', `refs/tags/v${version}`])
        assert.equal(releaseVersion(dir, ref, ''), version);
      assert.equal(releaseVersion(dir, '', version), version);
      assert.throws(() => releaseVersion(dir, 'refs/tags/v99.0.0', ''), /differs/);
      assert.throws(() => releaseVersion(dir, '', '99.0.0'), /differs/);
    }
    for (const value of [
      '',
      '01.0.0',
      '1.0.0-beta.00',
      'v1.0.0',
      '../foo',
      '1.0.0\n\n',
      ' 1.0.0',
    ]) {
      fs.writeFileSync(path.join(dir, 'VERSION'), value);
      assert.throws(() => releaseVersion(dir, '', ''), /strict SemVer/);
    }
    for (const ending of ['', '\n', '\r\n']) {
      fs.writeFileSync(path.join(dir, 'VERSION'), `1.0.0${ending}`);
      assert.equal(releaseVersion(dir, '', ''), '1.0.0');
    }
    for (const value of ['1.0.0', '1.0.0-beta.2+build', '999999999999999.0.0'])
      assert.ok(validVersion(value));
    for (const value of ['1.0', '01.0.0', '1.0.0-beta.00', 'v1.0.0', '1.0.0\n'])
      assert.ok(!validVersion(value));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
test('CMake and packaging read VERSION consistently and reconfigure when it changes', () => {
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
    const configure = (ref = '', version = '', extra = []) =>
      spawnSync('cmake', ['-S', source, '-B', build, ...extra], {
        env: { ...process.env, GITHUB_REF: ref, RELEASE_VERSION: version },
        encoding: 'utf8',
      });
    for (const version of ['2.3.4-beta.1', '2.3.4-beta.2', '2.3.4+build.1', '3.0.0']) {
      fs.writeFileSync(path.join(source, 'VERSION'), `${version}\n`);
      const result = configure(`refs/tags/v${version}`, version);
      assert.equal(result.status, 0, result.stdout + result.stderr);
      const core = version.split(/[-+]/)[0];
      assert.equal(
        fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
        `${releaseVersion(source, '', '')}|${core}`,
      );
      const resource = fs.readFileSync(path.join(build, 'native.rc'), 'utf8');
      assert.ok(resource.includes(`FILEVERSION ${core.replaceAll('.', ',')},0`));
      assert.ok(resource.includes(`VALUE "ProductVersion", "${version}\\0"`));
    }
    for (const tag of ['v', '2.0.0', 'v3.0.1', 'v3.0.0-beta.01', 'v03.0.0'])
      assert.notEqual(configure(`refs/tags/${tag}`).status, 0, tag);
    assert.notEqual(configure('', '3.0.1').status, 0);
    assert.notEqual(configure('', '', ['-DECHO_RELEASE_VERSION=1.0.0']).status, 0);
    assert.equal(configure('refs/heads/main', '', ['-U', 'ECHO_RELEASE_VERSION']).status, 0);
    for (const version of ['4.5.6-beta.1', '4.5.6']) {
      fs.writeFileSync(path.join(source, 'VERSION'), `${version}\r\n`);
      const result = spawnSync('cmake', ['--build', build], {
        env: { ...process.env, GITHUB_REF: '', RELEASE_VERSION: '' },
        encoding: 'utf8',
      });
      assert.equal(result.status, 0, result.stdout + result.stderr);
      assert.equal(
        fs.readFileSync(path.join(build, 'version.txt'), 'utf8').trimEnd(),
        `${version}|4.5.6`,
      );
    }
    for (const value of ['', '1.0.0\n\n', ' 1.0.0', '1.0.0-beta.01']) {
      fs.writeFileSync(path.join(source, 'VERSION'), value);
      assert.notEqual(configure().status, 0, value);
    }
    fs.mkdirSync(path.join(build, 'generated'));
    fs.writeFileSync(
      path.join(build, 'generated/EchoBuildInfo.h'),
      '#define ECHO_RELEASE_VERSION "1.0.0"\n',
    );
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
