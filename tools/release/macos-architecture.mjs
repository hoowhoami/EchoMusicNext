import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

// Inspect contents rather than suffixes: QML plugins and helpers can have none.
const machOMagic = new Set([
  0xfeedface, 0xfeedfacf, 0xcefaedfe, 0xcffaedfe, 0xcafebabe, 0xbebafeca, 0xcafebabf, 0xbfbafeca,
]);
function header(file) {
  const descriptor = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(8);
    fs.readSync(descriptor, bytes, 0, bytes.length, 0);
    return bytes;
  } finally {
    fs.closeSync(descriptor);
  }
}
function target(arch) {
  if (arch === 'arm64') return { name: 'arm64', cpu: 0x100000c };
  if (arch === 'x64') return { name: 'x86_64', cpu: 0x1000007 };
  throw new Error(`Unsupported macOS architecture: ${arch}`);
}
export function isMacosBinary(file) {
  return machOMagic.has(header(file).readUInt32BE(0));
}
export function* macosBinaries(directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    // Framework aliases must remain aliases; never follow links into the SDK.
    if (entry.isSymbolicLink()) continue;
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) yield* macosBinaries(file);
    else if (entry.isFile() && isMacosBinary(file)) yield file;
  }
}
export function verifyMacosArchitecture(file, arch) {
  const expected = target(arch);
  const bytes = header(file);
  if (bytes.readUInt32LE(0) !== 0xfeedfacf || bytes.readUInt32LE(4) !== expected.cpu)
    throw new Error(`Packaged architecture mismatch (expected only ${expected.name}): ${file}`);
}
export function thinMacosBinaries(directory, arch) {
  const expected = target(arch);
  const result = { binaries: 0, thinned: 0, bytesBefore: 0, bytesAfter: 0 };
  for (const file of macosBinaries(directory)) {
    const before = fs.statSync(file);
    result.binaries += 1;
    result.bytesBefore += before.size;
    const bytes = header(file);
    if (bytes.readUInt32LE(0) !== 0xfeedfacf) {
      const temporary = fs.mkdtempSync(path.join(path.dirname(file), '.echo-thin-'));
      try {
        const output = path.join(temporary, 'binary');
        const command = spawnSync('lipo', ['-thin', expected.name, file, '-output', output], {
          encoding: 'utf8',
        });
        if (command.error || command.status !== 0)
          throw new Error(
            `Cannot retain ${expected.name} in ${file}: ${command.error?.message || command.stderr || command.status}`,
          );
        verifyMacosArchitecture(output, arch);
        fs.chmodSync(output, before.mode & 0o777);
        fs.renameSync(output, file);
        result.thinned += 1;
      } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
      }
    }
    verifyMacosArchitecture(file, arch);
    result.bytesAfter += fs.statSync(file).size;
  }
  return result;
}
