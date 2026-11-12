import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { peDebugIdentity, pdbDebugIdentity, collectSymbols } from './symbols.mjs';
function pe(guid, age) {
  const bytes = Buffer.alloc(1024);
  bytes.writeUInt32LE(64, 0x3c);
  bytes.write('PE\0\0', 64, 'ascii');
  bytes.writeUInt16LE(1, 70);
  bytes.writeUInt16LE(240, 84);
  bytes.writeUInt16LE(0x20b, 88);
  bytes.writeUInt32LE(4096, 88 + 112 + 48);
  bytes.writeUInt32LE(28, 88 + 112 + 52);
  bytes.writeUInt32LE(512, 336);
  bytes.writeUInt32LE(4096, 340);
  bytes.writeUInt32LE(512, 344);
  bytes.writeUInt32LE(512, 348);
  bytes.writeUInt32LE(2, 524);
  bytes.writeUInt32LE(32, 528);
  bytes.writeUInt32LE(560, 536);
  bytes.write('RSDS', 560, 'ascii');
  Buffer.from(guid, 'hex').copy(bytes, 564);
  bytes.writeUInt32LE(age, 580);
  return bytes;
}
function pdb(guid, age) {
  const bytes = Buffer.alloc(2048);
  bytes.write('Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0', 0, 'ascii');
  bytes.writeUInt32LE(512, 32);
  bytes.writeUInt32LE(16, 44);
  bytes.writeUInt32LE(1, 52);
  bytes.writeUInt32LE(2, 512);
  bytes.writeUInt32LE(2, 1024);
  bytes.writeUInt32LE(0, 1028);
  bytes.writeUInt32LE(28, 1032);
  bytes.writeUInt32LE(3, 1036);
  bytes.writeUInt32LE(age, 1544);
  Buffer.from(guid, 'hex').copy(bytes, 1548);
  return bytes;
}
test('archive rejects a same-name PDB with a different native identity', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-symbols-'));
  try {
    const build = path.join(directory, 'build'),
      stage = path.join(directory, 'stage'),
      output = path.join(directory, 'out');
    fs.mkdirSync(path.join(build, 'generated'), { recursive: true });
    fs.mkdirSync(stage);
    fs.writeFileSync(
      path.join(build, 'generated/EchoBuildInfo.h'),
      '#define ECHO_BUILD_ID "test"\n',
    );
    const guid = '00112233445566778899aabbccddeeff';
    fs.writeFileSync(path.join(stage, 'EchoMusicNext.exe'), pe(guid, 4));
    fs.writeFileSync(path.join(build, 'EchoMusicNext.pdb'), pdb(guid, 3));
    assert.deepEqual(peDebugIdentity(pe(guid, 4)), { guid, age: 4 });
    assert.deepEqual(pdbDebugIdentity(pdb(guid, 3)), { guid, age: 3 });
    assert.throws(
      () =>
        collectSymbols({
          build,
          stage,
          output,
          platform: 'Windows',
          arch: 'x64',
          version: '0.1.0',
        }),
      /Missing matching PDB/,
    );
    fs.writeFileSync(path.join(build, 'EchoMusicNext.pdb'), pdb(guid, 4));
    const result = collectSymbols({
      build,
      stage,
      output,
      platform: 'Windows',
      arch: 'x64',
      version: '0.1.0',
    });
    assert.equal(result.files.length, 1);
    assert.deepEqual(result.files[0].debugIdentity, { guid, age: 4 });
    assert.equal(fs.readFileSync(path.join(output, 'EchoMusicNext.pdb')).length, 2048);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
test('unsupported symbol containers fail explicitly', () => {
  assert.throws(() => pdbDebugIdentity(Buffer.alloc(64)), /Unsupported PDB/);
  assert.throws(() => peDebugIdentity(Buffer.alloc(256)), /Invalid PE/);
});

test('Rust PDB lookup follows the configured external Cargo directory and keeps identity matching', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-cargo-symbols-'));
  try {
    const build = path.join(directory, 'build'),
      cargo = path.join(directory, 'shared Cargo cache'),
      stage = path.join(directory, 'stage'),
      output = path.join(directory, 'out');
    fs.mkdirSync(path.join(build, 'generated'), { recursive: true });
    fs.mkdirSync(path.join(cargo, 'release/deps'), { recursive: true });
    fs.mkdirSync(stage);
    fs.writeFileSync(path.join(build, 'generated/EchoBuildInfo.h'), 'test');
    fs.writeFileSync(path.join(build, 'CMakeCache.txt'), `ECHO_CARGO_DIR:PATH=${cargo}\r\n`);
    const appGuid = '00112233445566778899aabbccddeeff',
      rustGuid = '11223344556677889900aabbccddeeff';
    fs.writeFileSync(path.join(stage, 'EchoMusicNext.exe'), pe(appGuid, 4));
    fs.writeFileSync(path.join(build, 'EchoMusicNext.pdb'), pdb(appGuid, 4));
    fs.writeFileSync(path.join(stage, 'echo_ffi.dll'), pe(rustGuid, 7));
    fs.writeFileSync(path.join(build, 'echo_ffi.pdb'), pdb(rustGuid, 6));
    const rustPdb = path.join(cargo, 'release/deps/echo_ffi.pdb');
    fs.writeFileSync(rustPdb, pdb(rustGuid, 7));
    const options = { build, stage, output, platform: 'Windows', arch: 'x64', version: '0.1.0' };
    const result = collectSymbols(options);
    const rust = result.files.find((file) => file.file === 'echo_ffi.dll');
    assert.deepEqual(rust.debugIdentity, { guid: rustGuid, age: 7 });
    assert.deepEqual(pdbDebugIdentity(fs.readFileSync(path.join(output, 'echo_ffi.pdb'))), {
      guid: rustGuid,
      age: 7,
    });
    fs.writeFileSync(rustPdb, pdb(appGuid, 7));
    assert.throws(() => collectSymbols(options), /Missing matching PDB for echo_ffi.dll/);
    fs.writeFileSync(rustPdb, pdb(rustGuid, 8));
    assert.throws(() => collectSymbols(options), /Missing matching PDB for echo_ffi.dll/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('Rust ELF symbols are found in the configured Cargo output with their original build ID', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-cargo-elf-'));
  try {
    const build = path.join(directory, 'build'),
      cargo = path.join(directory, 'cargo'),
      stage = path.join(directory, 'stage'),
      output = path.join(directory, 'out');
    fs.mkdirSync(path.join(build, 'generated'), { recursive: true });
    fs.mkdirSync(path.join(cargo, 'release'), { recursive: true });
    fs.mkdirSync(stage);
    fs.writeFileSync(path.join(build, 'generated/EchoBuildInfo.h'), 'test');
    fs.writeFileSync(path.join(build, 'CMakeCache.txt'), `ECHO_CARGO_DIR:PATH=${cargo}\n`);
    fs.writeFileSync(path.join(stage, 'EchoMusicNext'), 'app');
    fs.writeFileSync(path.join(build, 'EchoMusicNext'), 'app-dwarf');
    fs.writeFileSync(path.join(stage, 'libecho_ffi.so'), 'rust-stripped');
    fs.writeFileSync(path.join(cargo, 'release/libecho_ffi.so'), 'rust-dwarf');
    let rustId = 'cc33';
    const run = (_, args) => ({
      status: 0,
      stdout:
        args[0] === '--notes'
          ? `Build ID: ${args[1].startsWith(cargo) ? rustId : args[1].endsWith('libecho_ffi.so') ? 'cc33' : 'aa22'}`
          : '.debug_info',
    });
    const options = {
      build,
      stage,
      output,
      platform: 'Linux',
      arch: 'arm64',
      version: '0.1.0',
      run,
    };
    const result = collectSymbols(options);
    assert.equal(result.files.find((file) => file.file === 'libecho_ffi.so').debugIdentity, 'cc33');
    assert.equal(fs.readFileSync(path.join(output, 'libecho_ffi.so'), 'utf8'), 'rust-dwarf');
    rustId = 'dd44';
    assert.throws(() => collectSymbols(options), /Missing matching DWARF ELF for libecho_ffi.so/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('stripped Linux deployment retains only DWARF with the same ELF build ID', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-elf-symbols-'));
  try {
    const build = path.join(directory, 'build'),
      stage = path.join(directory, 'stage'),
      output = path.join(directory, 'out');
    fs.mkdirSync(path.join(build, 'generated'), { recursive: true });
    fs.mkdirSync(stage);
    fs.writeFileSync(path.join(build, 'generated/EchoBuildInfo.h'), 'test');
    fs.writeFileSync(path.join(stage, 'EchoMusicNext'), 'stripped');
    fs.writeFileSync(path.join(build, 'EchoMusicNext'), 'original-dwarf');
    let originalId = 'aa22';
    const run = (_, args) => ({
      status: 0,
      stdout:
        args[0] === '--notes'
          ? `Build ID: ${args[1].startsWith(build) ? originalId : 'aa22'}`
          : '.debug_info',
    });
    const options = {
      build,
      stage,
      output,
      platform: 'Linux',
      arch: 'arm64',
      version: '0.1.0',
      run,
    };
    const result = collectSymbols(options);
    assert.equal(result.files[0].debugIdentity, 'aa22');
    assert.notEqual(result.files[0].sha256, result.files[0].packagedSha256);
    assert.equal(fs.readFileSync(path.join(output, 'EchoMusicNext'), 'utf8'), 'original-dwarf');
    originalId = 'bb33';
    assert.throws(() => collectSymbols(options), /Missing matching DWARF ELF/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
