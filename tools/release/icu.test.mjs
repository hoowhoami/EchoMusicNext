import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { deployIcu, verifyIcuDeployment } from './icu.mjs';

function fixture(platform = 'Windows', arch = 'x64') {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'echo-icu-'));
  const prefix = path.join(directory, 'prefix'),
    build = path.join(directory, 'build'),
    stage = path.join(directory, 'stage'),
    licenses = path.join(stage, 'licenses');
  const files = {};
  function write(name, bytes) {
    const file = path.join(prefix, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, bytes);
    files[name] = createHash('sha256').update(bytes).digest('hex');
    return file;
  }
  const selected = { version: '78.3', root: prefix };
  for (const [key, name] of Object.entries(
    platform === 'Windows'
      ? { uc: 'icuuc.lib', i18n: 'icuin.lib', data: 'icudt.lib' }
      : { uc: 'libicuuc.a', i18n: 'libicui18n.a', data: 'libicudata.a' },
  ))
    selected[key] = write(`lib/${name}`, Buffer.from(`native ${key}`));
  write('LICENSE', Buffer.from('ICU license fixture'));
  write(
    'word-data.json',
    Buffer.from(
      JSON.stringify({
        purpose: 'UBRK_WORD',
        items: [
          'brkitr/word.brk',
          'brkitr/word_POSIX.brk',
          'brkitr/cjdict.dict',
          'brkitr/root.res',
          'nfkc.nrm',
          'cnvalias.icu',
        ],
      }),
    ),
  );
  const dll = Buffer.alloc(128);
  dll.write('MZ');
  dll.writeUInt32LE(64, 0x3c);
  dll.write('PE\0\0', 64);
  dll.writeUInt16LE(arch === 'x64' ? 0x8664 : 0xaa64, 68);
  for (const name of ['icuuc78.dll', 'icuin78.dll', 'icudt78.dll']) write(`bin/${name}`, dll);
  fs.writeFileSync(
    path.join(prefix, 'echo-source.json'),
    JSON.stringify({
      identity: {
        version: '78.3',
        architecture: arch,
        sha256: '3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0',
      },
      files,
    }),
  );
  fs.mkdirSync(path.join(build, 'generated'), { recursive: true });
  const metadata = path.join(build, 'generated/echo-icu-Release.json');
  fs.writeFileSync(metadata, JSON.stringify(selected));
  return {
    prefix,
    build,
    stage,
    licenses,
    selected,
    metadata,
    deploy: () => deployIcu(build, stage, platform, arch, licenses),
    cleanup: () => fs.rmSync(directory, { recursive: true, force: true }),
  };
}

for (const platform of ['Windows', 'macOS', 'Linux'])
  for (const arch of ['x64', 'arm64'])
    test(`deploy ${platform}/${arch} ICU libraries and notices`, () => {
      const f = fixture(platform, arch);
      try {
        const files = f.deploy();
        verifyIcuDeployment(files);
        assert.equal(files.length, platform === 'Windows' ? 5 : 2);
        assert.equal(fs.existsSync(path.join(f.stage, 'bin/icudt78.dll')), platform === 'Windows');
        assert.match(
          fs.readFileSync(path.join(f.licenses, 'deployment/ICU/NOTICE.txt'), 'utf8'),
          /ICU 78.3/,
        );
      } finally {
        f.cleanup();
      }
    });

test('reject absent capability, incompatible ICU version and mixed library prefix', () => {
  const f = fixture();
  try {
    fs.rmSync(f.metadata);
    assert.throws(f.deploy, /requires text.segmentation/);
    fs.writeFileSync(f.metadata, JSON.stringify({ ...f.selected, version: '72.1' }));
    assert.throws(f.deploy, /requires ICU 78.3/);
    const other = path.join(f.prefix, 'other.lib');
    fs.writeFileSync(other, 'other');
    fs.writeFileSync(f.metadata, JSON.stringify({ ...f.selected, uc: other }));
    assert.throws(f.deploy, /different ICU library/);
  } finally {
    f.cleanup();
  }
});

test('reject missing, changed or wrong-architecture DLL and missing notices', () => {
  for (const mutation of ['missing', 'changed', 'architecture', 'license']) {
    const f = fixture();
    try {
      const file = path.join(f.prefix, mutation === 'license' ? 'LICENSE' : 'bin/icuuc78.dll');
      if (mutation === 'missing' || mutation === 'license') fs.rmSync(file);
      else if (mutation === 'changed') fs.appendFileSync(file, 'changed');
      else {
        const metadata = JSON.parse(fs.readFileSync(path.join(f.prefix, 'echo-source.json')));
        const bytes = fs.readFileSync(file);
        bytes.writeUInt16LE(0xaa64, 68);
        fs.writeFileSync(file, bytes);
        metadata.files['bin/icuuc78.dll'] = createHash('sha256').update(bytes).digest('hex');
        fs.writeFileSync(path.join(f.prefix, 'echo-source.json'), JSON.stringify(metadata));
      }
      assert.throws(f.deploy, /ENOENT|changed|architecture/);
    } finally {
      f.cleanup();
    }
  }
});

test('reject full ICU data and notice or runtime replacement after Qt deployment', () => {
  const f = fixture();
  try {
    const files = f.deploy();
    fs.appendFileSync(path.join(f.stage, 'bin/icudt78.dll'), 'replaced');
    assert.throws(() => verifyIcuDeployment(files), /missing or changed/);
    const dataFile = path.join(f.prefix, 'word-data.json');
    const data = JSON.parse(fs.readFileSync(dataFile));
    data.items.push('coll/root.res');
    fs.writeFileSync(dataFile, JSON.stringify(data));
    const receiptFile = path.join(f.prefix, 'echo-source.json');
    const receipt = JSON.parse(fs.readFileSync(receiptFile));
    receipt.files['word-data.json'] = createHash('sha256')
      .update(fs.readFileSync(dataFile))
      .digest('hex');
    fs.writeFileSync(receiptFile, JSON.stringify(receipt));
    assert.throws(f.deploy, /word-only data/);
  } finally {
    f.cleanup();
  }
});
