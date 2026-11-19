import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';

const version = '78.3';
const sourceSha256 = '3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0';
const hash = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function verifyDll(file, arch) {
  const bytes = fs.readFileSync(file);
  const offset = bytes.length >= 64 ? bytes.readUInt32LE(0x3c) : bytes.length;
  if (
    bytes.toString('ascii', 0, 2) !== 'MZ' ||
    offset + 6 > bytes.length ||
    bytes.toString('ascii', offset, offset + 4) !== 'PE\0\0' ||
    bytes.readUInt16LE(offset + 4) !== { x64: 0x8664, arm64: 0xaa64 }[arch]
  )
    throw new Error(`ICU DLL architecture mismatch: ${file}`);
}

export function deployIcu(build, stage, platform, arch, licenseDirectory) {
  const metadata = path.join(build, 'generated/echo-icu-Release.json');
  if (!fs.existsSync(metadata))
    throw new Error(
      'Distribution requires text.segmentation; reconfigure with pinned ICU_ROOT and rebuild',
    );
  const selected = JSON.parse(fs.readFileSync(metadata, 'utf8'));
  if (selected.version !== version) throw new Error(`Distribution requires ICU ${version}`);
  const prefix = path.resolve(selected.root);
  const receipt = JSON.parse(fs.readFileSync(path.join(prefix, 'echo-source.json'), 'utf8'));
  if (receipt.identity?.version !== version || receipt.identity?.sha256 !== sourceSha256)
    throw new Error('ICU prefix is not the pinned official source build');
  const data = JSON.parse(fs.readFileSync(path.join(prefix, 'word-data.json'), 'utf8'));
  if (
    receipt.files?.['word-data.json'] !== hash(path.join(prefix, 'word-data.json')) ||
    data.purpose !== 'UBRK_WORD' ||
    !Array.isArray(data.items) ||
    !data.items.includes('brkitr/word.brk') ||
    !data.items.includes('brkitr/word_POSIX.brk') ||
    !data.items.every((name) =>
      /^(?:brkitr\/[^/]+\.(?:res|dict)|brkitr\/word(?:_POSIX)?\.brk|nfkc\.nrm|cnvalias\.icu)$/.test(
        name,
      ),
    )
  )
    throw new Error('ICU word-only data receipt is missing or inconsistent');
  const nativeFiles =
    platform === 'Windows'
      ? { uc: 'icuuc.lib', i18n: 'icuin.lib', data: 'icudt.lib' }
      : { uc: 'libicuuc.a', i18n: 'libicui18n.a', data: 'libicudata.a' };
  function verifyPrefixFile(relative) {
    const file = path.join(prefix, relative);
    if (!receipt.files?.[relative] || receipt.files[relative] !== hash(file))
      throw new Error(`ICU prefix file changed: ${relative}`);
    return file;
  }
  for (const [component, name] of Object.entries(nativeFiles)) {
    const file = verifyPrefixFile(`lib/${name}`);
    if (fs.realpathSync(selected[component]) !== fs.realpathSync(file))
      throw new Error(`CMake selected a different ICU library: ${component}`);
  }
  const staged = [];
  function copy(source, target) {
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(source, target);
    staged.push({ file: target, sha256: hash(source) });
  }
  const licenses = path.join(licenseDirectory, 'deployment/ICU');
  copy(verifyPrefixFile('LICENSE'), path.join(licenses, 'LICENSE'));
  copy(verifyPrefixFile('word-data.json'), path.join(licenses, 'word-data.json'));
  fs.writeFileSync(
    path.join(licenses, 'NOTICE.txt'),
    `ICU ${version}\nSource: https://github.com/unicode-org/icu/releases/tag/release-${version}\nSource archive SHA-256: ${sourceSha256}\nWord-break data only; all original language dictionaries retained.\nLinkage: ${platform === 'Windows' ? 'MSVC DLL' : 'static'}\n`,
  );
  if (platform === 'Windows') {
    if (receipt.identity.architecture !== arch) throw new Error('ICU target architecture differs');
    for (const name of ['icuuc78.dll', 'icuin78.dll', 'icudt78.dll']) {
      const file = verifyPrefixFile(`bin/${name}`);
      verifyDll(file, arch);
      copy(file, path.join(stage, 'bin', name));
    }
  }
  return staged;
}

export function verifyIcuDeployment(files) {
  for (const { file, sha256 } of files)
    if (!fs.existsSync(file) || hash(file) !== sha256)
      throw new Error(`Deployed ICU dependency is missing or changed: ${file}`);
}
