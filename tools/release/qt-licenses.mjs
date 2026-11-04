import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { root } from './contract.mjs';
import { copyNotices } from './license-files.mjs';

const modules = [
  'qtbase',
  'qtdeclarative',
  'qtimageformats',
  'qtmultimedia',
  'qtsvg',
  'qtshadertools',
  'qtwayland',
  'qtwebsockets',
];
const texts = ['GPL-2.0-only', 'GPL-3.0-only', 'LGPL-3.0-only', 'Qt-GPL-exception-1.0'];
const fileKey = (file) =>
  path.posix
    .basename(file.replaceAll('\\', '/'))
    .toLowerCase()
    .replace(/\.so(?:\.\d+)*$/, '.so');
function licenseIds(document) {
  const expressions = [...(document.packages || []), ...(document.files || [])].flatMap((item) => [
    item.licenseConcluded,
    item.licenseDeclared,
  ]);
  return [
    ...new Set(
      expressions
        .filter((value) => typeof value === 'string')
        .flatMap((value) => value.match(/[A-Za-z][A-Za-z0-9.+-]*/g) || [])
        .filter((value) => !['AND', 'OR', 'WITH', 'NONE', 'NOASSERTION'].includes(value)),
    ),
  ].sort();
}
function sdkDocuments(qt) {
  const directory = path.join(qt, 'sbom');
  return fs.existsSync(directory)
    ? fs
        .readdirSync(directory)
        .filter(
          (name) =>
            modules.some((module) => name.startsWith(module + '-')) && name.endsWith('.spdx.json'),
        )
        .map((name) => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8')))
    : [];
}
export async function prepareQtLicenseTexts(directory, qt, request = fetch) {
  const ids = new Set(
    sdkDocuments(qt)
      .flatMap(licenseIds)
      .filter((id) => !id.startsWith('LicenseRef-')),
  );
  for (const id of [...ids].sort()) {
    const file = path.join(directory, id + '.txt');
    if (fs.existsSync(file) && fs.readFileSync(file, 'utf8').trim()) continue;
    const response = await request(`https://spdx.org/licenses/${encodeURIComponent(id)}.json`, {
      signal: AbortSignal.timeout(30000),
    });
    if (!response.ok)
      throw new Error(`Cannot obtain Qt license text ${id}: HTTP ${response.status}`);
    const record = await response.json();
    if (
      (record.licenseId || record.licenseExceptionId) !== id ||
      !(record.licenseText || record.licenseExceptionText)?.trim()
    )
      throw new Error(`Invalid SPDX license text for ${id}`);
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(file, (record.licenseText || record.licenseExceptionText).trim() + '\n');
  }
}

export function collectQtLicenses(output, qt, { repository = root } = {}) {
  const metadata = path.join(qt, '.echo-sdk.json');
  const versionFile = path.join(qt, 'lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake');
  const version = fs.existsSync(metadata)
    ? JSON.parse(fs.readFileSync(metadata)).version
    : /set\(PACKAGE_VERSION "([^"]+)"\)/.exec(fs.readFileSync(versionFile, 'utf8'))?.[1];
  if (!/^\d+\.\d+\.\d+$/.test(version || '')) throw new Error('Cannot determine Qt SDK version');
  const licenses = path.join(repository, 'build/release-tools/qt-license-texts');
  for (const name of texts) {
    const file = path.join(licenses, name + '.txt');
    if (!fs.existsSync(file) || !fs.readFileSync(file, 'utf8').trim())
      throw new Error(`Missing Qt license text: ${name}`);
  }
  const target = path.join(output, 'Qt');
  fs.mkdirSync(target, { recursive: true });
  fs.cpSync(licenses, path.join(target, 'licenses'), { recursive: true });
  const inventory = { schema: 1, version, modules: [] };
  for (const module of modules) {
    const source = path.join(repository, 'build/qt-sources', version, module);
    const cached = path.join(qt, 'share/echomusicnext-qt-notices', module);
    copyNotices(source, cached);
    const noticeCount = copyNotices(cached, path.join(target, 'source-notices', module));
    const name = `${module}-${version}.spdx.json`,
      file = path.join(qt, 'sbom', name);
    let document;
    if (fs.existsSync(file)) {
      document = JSON.parse(fs.readFileSync(file, 'utf8'));
      const owner = document.packages?.find((pkg) => pkg.name === module);
      if (
        !document.spdxVersion?.startsWith('SPDX-') ||
        typeof owner?.copyrightText !== 'string' ||
        !owner.copyrightText.trim() ||
        owner.copyrightText === 'NOASSERTION' ||
        !Array.isArray(document.files) ||
        !document.files.length ||
        document.files.some((file) => typeof file.fileName !== 'string' || !file.fileName)
      )
        throw new Error(`Invalid Qt SPDX inventory: ${name}`);
      for (const id of licenseIds(document)) {
        if (id.startsWith('LicenseRef-')) {
          const extracted = document.hasExtractedLicensingInfos?.find(
            (record) => record.licenseId === id,
          );
          if (!extracted?.extractedText?.trim())
            throw new Error(`Missing extracted Qt license ${id} in ${name}`);
        } else {
          const text = path.join(licenses, id + '.txt');
          if (!fs.existsSync(text) || !fs.readFileSync(text, 'utf8').trim())
            throw new Error(
              `Missing Qt component license text ${id}; rerun prepare.mjs --qt=${qt}`,
            );
        }
      }
      fs.copyFileSync(file, path.join(target, name));
    }
    if (!document && !noticeCount) continue;
    const pkg = document?.packages.find((pkg) => pkg.name === module);
    const revision = /@([0-9a-f]{40})$/.exec(pkg?.downloadLocation || '')?.[1];
    let ref = revision || `v${version}`;
    if (fs.existsSync(path.join(source, '.git'))) {
      const result = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: source, encoding: 'utf8' });
      if (result.status !== 0 || !/^[0-9a-f]{40}$/.test(result.stdout.trim()))
        throw new Error(`Cannot identify Qt source revision: ${module}`);
      ref = result.stdout.trim();
    }
    inventory.modules.push({
      name: module,
      version,
      ref,
      source: `https://github.com/qt/${module}/tree/${ref}`,
      archive: `https://github.com/qt/${module}/archive/${ref}.tar.gz`,
      metadata: document ? name : null,
      sourceNotices: noticeCount ? `source-notices/${module}` : null,
      files: document?.files?.map((file) => file.fileName) || [],
    });
  }
  if (!inventory.modules.length) throw new Error('No Qt module license inventory found');
  fs.writeFileSync(path.join(target, 'modules.json'), JSON.stringify(inventory, null, 2) + '\n');
  fs.writeFileSync(
    path.join(target, 'NOTICE.txt'),
    `This application uses Qt ${version}.\nCopyright (C) The Qt Company Ltd. and other contributors.\nQt and its bundled third-party components retain their respective licenses and copyrights.\nLicense texts: licenses/\nModule copyright and license records: *.spdx.json and source-notices/\nCorresponding upstream source locations and revisions: modules.json\n\n${inventory.modules.map((module) => `${module.name}: ${module.source}`).join('\n')}\n`,
  );
  return inventory;
}

// Source-built SDKs may not have SPDX. These are the audited repositories for
// the libraries/plugins used by this host; new unknown Qt modules fail closed.
function sourceModule(file) {
  const name = path.posix.basename(file.replaceAll('\\', '/'));
  const library = /^(?:lib)?Qt6?([A-Za-z0-9]+)(?:\.dll|\.dylib|\.so(?:\.\d+)*)?$/.exec(name)?.[1];
  if (library) {
    if (
      /^(Core|Concurrent|DBus|Gui|Network|OpenGL|OpenGLWidgets|PrintSupport|Sql|Widgets|Xml|Test|XcbQpa|EglFSDeviceIntegration|EglFsKmsSupport|EglFsKmsGbmSupport)$/.test(
        library,
      )
    )
      return 'qtbase';
    if (
      /^(Qml.*|Quick|QuickControls2.*|QuickDialogs2.*|QuickEffects|QuickLayouts|QuickParticles|QuickShapes|QuickTemplates2|QuickTest|QuickVectorImage|Labs.*)$/.test(
        library,
      )
    )
      return 'qtdeclarative';
    if (/^(Multimedia.*|SpatialAudio)$/.test(library)) return 'qtmultimedia';
    if (/^Svg/.test(library)) return 'qtsvg';
    if (library === 'WebSockets') return 'qtwebsockets';
    if (library === 'ShaderTools') return 'qtshadertools';
    if (/^Wayland/.test(library)) return 'qtwayland';
    throw new Error(`Unmapped deployed Qt library: ${name}`);
  }
  if (/q(webp|tiff|icns|wbmp|tga|jp2|mng)/i.test(name)) return 'qtimageformats';
  if (/qsvg/i.test(name)) return 'qtsvg';
  if (/qwayland|wayland.*plugin/i.test(name)) return 'qtwayland';
  if (/multimedia|mediaplugin/i.test(name)) return 'qtmultimedia';
  if (/qmlwebsockets/i.test(name)) return 'qtwebsockets';
  const normalized = file.replaceAll('\\', '/').toLowerCase();
  if (/\/qml\/(qtquick|qtqml|qt\/labs|qt\.labs)(\/|$)/.test(normalized)) return 'qtdeclarative';
  if (/\/qml\/qtwebsockets\//.test(normalized)) return 'qtwebsockets';
  if (/\/qml\/qtmultimedia\//.test(normalized)) return 'qtmultimedia';
  if (/\/plugins\/wayland[^/]*\//.test(normalized)) return 'qtwayland';
  if (/\/plugins\/(scenegraph|qmltooling)\//.test(normalized)) return 'qtdeclarative';
  if (
    /\/(plugins|plugIns)\/(platforms|sqldrivers|imageformats|styles|platformthemes|platforminputcontexts|tls|networkinformation|generic|egldeviceintegrations|eglfsdeviceintegrations)\//i.test(
      normalized,
    ) ||
    /q(gif|ico|jpeg|sqlite|xcb|cocoa|windows|offscreen|minimal)/i.test(name)
  )
    return 'qtbase';
  if (/\/(plugins|qml)\//i.test(normalized))
    throw new Error(`Unmapped deployed Qt plugin: ${file}`);
  return null;
}

export function verifyQtFiles(files, inventory) {
  const owners = new Map();
  for (const module of inventory.modules)
    for (const file of module.files) {
      const key = fileKey(file);
      if (!owners.has(key)) owners.set(key, new Set());
      owners.get(key).add(module.name);
    }
  const available = new Set(inventory.modules.map((module) => module.name));
  const required = new Set();
  for (const file of files) {
    const documented = owners.get(fileKey(file));
    for (const module of documented || [sourceModule(file)]) {
      if (!module) continue;
      if (!available.has(module))
        throw new Error(`Missing ${module} license material for deployed file ${file}`);
      required.add(module);
    }
  }
  if (!required.size) throw new Error('No deployed Qt runtime found for license verification');
  return [...required].sort();
}

export function verifyQtDeployment(stage, destination) {
  const inventory = JSON.parse(
    fs.readFileSync(path.join(destination, 'deployment/Qt/modules.json')),
  );
  const files = [];
  function walk(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      if (entry.isSymbolicLink()) continue;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) walk(file);
      else if (
        /\.dll$|\.dylib$|\.so(?:\.\d+)*$/.test(entry.name) ||
        /\.framework\/Versions\/[^/]+\/[^/]+$/.test(file.replaceAll('\\', '/'))
      )
        files.push(file);
    }
  }
  walk(stage);
  const required = verifyQtFiles(files, inventory);
  const target = path.join(destination, 'deployment/Qt');
  const present = (file) =>
    fs.existsSync(file) && fs.statSync(file).isFile() && fs.readFileSync(file, 'utf8').trim();
  const hasNotices = (directory) =>
    fs.existsSync(directory) &&
    fs
      .readdirSync(directory, { withFileTypes: true })
      .some(
        (entry) =>
          !entry.isSymbolicLink() &&
          (entry.isDirectory()
            ? hasNotices(path.join(directory, entry.name))
            : present(path.join(directory, entry.name))),
      );
  for (const id of texts)
    if (!present(path.join(target, 'licenses', id + '.txt')))
      throw new Error(`Missing packaged Qt license text: ${id}`);
  for (const module of inventory.modules.filter((module) => required.includes(module.name))) {
    const metadata = module.metadata && path.join(target, module.metadata);
    if (metadata && present(metadata)) {
      for (const id of licenseIds(JSON.parse(fs.readFileSync(metadata))))
        if (!id.startsWith('LicenseRef-') && !present(path.join(target, 'licenses', id + '.txt')))
          throw new Error(`Missing packaged Qt component license text: ${id}`);
    } else if (!module.sourceNotices || !hasNotices(path.join(target, module.sourceNotices))) {
      throw new Error(`Missing packaged Qt notices: ${module.name}`);
    }
  }
  console.log('Qt license coverage verified: ' + required.join(', '));
  return required;
}
