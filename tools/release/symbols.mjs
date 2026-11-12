import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { root, releaseVersion } from './contract.mjs';

const digest = (file) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const owned =
  /^(?:echomusicnext|echo[-_]\w[\w-]*|libecho_ffi|crashpad_handler|crashpad_wer)(?:\.(?:exe|dll|so|dylib))?$/i;
function walk(directory, result = [], includeDeps = false) {
  if (!fs.existsSync(directory)) return result;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.isSymbolicLink()) continue;
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      if (
        !/^(?:CMakeFiles|incremental|qml|qt-official|qt-sdk|package-staging)$/.test(entry.name) &&
        (includeDeps || entry.name !== 'deps') &&
        !entry.name.endsWith('-src')
      )
        walk(file, result, includeDeps);
    } else result.push(file);
  }
  return result;
}
function nativeBuildFiles(build) {
  const files = walk(build);
  const cache = path.join(build, 'CMakeCache.txt');
  if (!fs.existsSync(cache)) return files;
  // Rust.cmake permits a shared Cargo cache outside the CMake build tree.
  // Use its configured location for both Rust PDBs and original DWARF ELFs.
  const directory = /^ECHO_CARGO_DIR:[^=]+=(.*)$/m
    .exec(fs.readFileSync(cache, 'utf8'))?.[1]
    .replace(/\r$/, '');
  if (directory && path.resolve(build, directory) !== path.resolve(build))
    walk(path.resolve(build, directory), files, true);
  return files;
}
export function peDebugIdentity(bytes) {
  const pe = bytes.readUInt32LE(0x3c);
  if (bytes.toString('ascii', pe, pe + 4) !== 'PE\0\0') throw new Error('Invalid PE signature');
  const optional = pe + 24;
  const magic = bytes.readUInt16LE(optional);
  const directory = optional + (magic === 0x20b ? 112 : 96) + 6 * 8;
  const rva = bytes.readUInt32LE(directory);
  const size = bytes.readUInt32LE(directory + 4);
  let debug;
  const sections = optional + bytes.readUInt16LE(pe + 20);
  for (let i = 0; i < bytes.readUInt16LE(pe + 6); i++) {
    const section = sections + i * 40;
    const address = bytes.readUInt32LE(section + 12);
    if (
      rva >= address &&
      rva - address < Math.max(bytes.readUInt32LE(section + 8), bytes.readUInt32LE(section + 16))
    )
      debug = bytes.readUInt32LE(section + 20) + rva - address;
  }
  if (debug === undefined) throw new Error('PE has no mapped debug directory');
  for (let offset = debug; offset + 28 <= debug + size; offset += 28) {
    if (bytes.readUInt32LE(offset + 12) !== 2) continue;
    const codeview = bytes.readUInt32LE(offset + 24);
    if (bytes.toString('ascii', codeview, codeview + 4) === 'RSDS')
      return {
        guid: bytes.subarray(codeview + 4, codeview + 20).toString('hex'),
        age: bytes.readUInt32LE(codeview + 20),
      };
  }
  throw new Error('PE has no RSDS/PDB identity');
}
export function pdbDebugIdentity(bytes) {
  if (!bytes.toString('ascii', 0, 32).startsWith('Microsoft C/C++ MSF 7.00'))
    throw new Error('Unsupported PDB format');
  const blockSize = bytes.readUInt32LE(32);
  if (blockSize < 512 || blockSize > 65536 || blockSize & (blockSize - 1))
    throw new Error('Invalid PDB block size');
  const directorySize = bytes.readUInt32LE(44);
  const map = bytes.readUInt32LE(52) * blockSize;
  const blocks = [];
  for (let i = 0; i < Math.ceil(directorySize / blockSize); i++) {
    const offset = bytes.readUInt32LE(map + i * 4) * blockSize;
    blocks.push(bytes.subarray(offset, offset + blockSize));
  }
  const directory = Buffer.concat(blocks).subarray(0, directorySize);
  const streams = directory.readUInt32LE(0);
  if (streams < 2 || streams > 100000) throw new Error('Invalid PDB stream count');
  let offset = 4 + streams * 4;
  const firstSize = directory.readUInt32LE(4);
  if (firstSize !== 0xffffffff) offset += Math.ceil(firstSize / blockSize) * 4;
  const info = directory.readUInt32LE(offset) * blockSize;
  return {
    guid: bytes.subarray(info + 12, info + 28).toString('hex'),
    age: bytes.readUInt32LE(info + 8),
  };
}
export function collectSymbols({
  build,
  stage,
  output,
  platform,
  arch,
  version = releaseVersion(),
  run = spawnSync,
}) {
  const binaries = walk(stage).filter((file) => owned.test(path.basename(file)));
  if (!binaries.some((file) => /^echomusicnext(?:\.exe)?$/i.test(path.basename(file))))
    throw new Error('No staged application for symbols');
  fs.mkdirSync(output, { recursive: true });
  const nativeFiles = platform === 'Windows' || platform === 'Linux' ? nativeBuildFiles(build) : [];
  const pdbs = platform === 'Windows' ? nativeFiles.filter((file) => /\.pdb$/i.test(file)) : [];
  const linuxBuildFiles = platform === 'Linux' ? nativeFiles : [];
  const records = [];
  for (const file of binaries) {
    const name = path.basename(file);
    const destination = path.join(output, name);
    fs.copyFileSync(file, destination);
    const record = { file: name, sha256: digest(file) };
    if (platform === 'Windows') {
      const identity = peDebugIdentity(fs.readFileSync(file));
      const expected = name.replace(/\.(exe|dll)$/i, '.pdb');
      const pdb = pdbs.find((candidate) => {
        if (path.basename(candidate).toLowerCase() !== expected.toLowerCase()) return false;
        try {
          return (
            JSON.stringify(pdbDebugIdentity(fs.readFileSync(candidate))) ===
            JSON.stringify(identity)
          );
        } catch {
          return false;
        }
      });
      if (!pdb) throw new Error(`Missing matching PDB for ${name}`);
      fs.copyFileSync(pdb, path.join(output, expected));
      Object.assign(record, {
        symbols: expected,
        symbolSha256: digest(pdb),
        debugIdentity: identity,
      });
    } else if (platform === 'macOS') {
      const dsym = path.join(output, `${name}.dSYM`);
      const result = run('dsymutil', [file, '-o', dsym], { encoding: 'utf8' });
      if (result.status !== 0)
        throw new Error(`dSYM generation failed for ${name}: ${result.stderr}`);
      const binaryId = run('dwarfdump', ['--uuid', file], { encoding: 'utf8' });
      const symbolsId = run('dwarfdump', ['--uuid', dsym], { encoding: 'utf8' });
      const ids = (text) =>
        [...text.matchAll(/UUID: ([0-9A-F-]+)/gi)].map((match) => match[1].toLowerCase()).sort();
      const binaryIds = ids(binaryId.stdout || '');
      if (
        binaryId.status !== 0 ||
        symbolsId.status !== 0 ||
        !binaryIds.length ||
        JSON.stringify(binaryIds) !== JSON.stringify(ids(symbolsId.stdout || ''))
      )
        throw new Error(`dSYM UUID mismatch for ${name}`);
      Object.assign(record, { symbols: `${name}.dSYM`, debugIdentity: binaryIds });
    } else {
      const buildId = (candidate) => {
        const notes = run('readelf', ['--notes', candidate], { encoding: 'utf8' });
        return notes.status === 0
          ? /Build ID: ([0-9a-f]+)/i.exec(notes.stdout)?.[1].toLowerCase()
          : undefined;
      };
      const identity = buildId(file);
      if (!identity) throw new Error(`Missing ELF build ID for ${name}`);
      // linuxdeploy may strip the staged executable. Match its build ID to the
      // original optimized ELF and preserve that ELF's DWARF outside the package.
      const original = linuxBuildFiles.find((candidate) => {
        if (path.basename(candidate) !== name || buildId(candidate) !== identity) return false;
        const sections = run('readelf', ['--sections', candidate], { encoding: 'utf8' });
        return sections.status === 0 && sections.stdout.includes('.debug_info');
      });
      if (!original) throw new Error(`Missing matching DWARF ELF for ${name}`);
      fs.copyFileSync(original, destination);
      Object.assign(record, {
        symbols: name,
        sha256: digest(original),
        packagedSha256: digest(file),
        debugIdentity: identity,
      });
    }
    records.push(record);
  }
  const buildInfo = path.join(build, 'generated/EchoBuildInfo.h');
  if (!fs.existsSync(buildInfo)) throw new Error('Missing configured build identity');
  fs.copyFileSync(buildInfo, path.join(output, 'EchoBuildInfo.h'));
  const manifest = {
    version,
    platform,
    arch,
    sourceRevision: process.env.GITHUB_SHA || null,
    workflowRun: process.env.GITHUB_RUN_ID || null,
    files: records,
  };
  fs.writeFileSync(path.join(output, 'symbols.json'), JSON.stringify(manifest, null, 2) + '\n');
  return manifest;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const platform = { win32: 'Windows', darwin: 'macOS', linux: 'Linux' }[process.platform];
  const arch = process.argv[2] || process.arch;
  const build = path.join(root, 'build/release');
  const staging = path.join(
    build,
    'package-staging',
    platform === 'Linux' ? 'AppDir/usr' : 'install',
  );
  collectSymbols({
    build,
    stage: staging,
    output: path.join(root, 'build/symbols', `${platform}-${arch}`),
    platform,
    arch,
  });
}
