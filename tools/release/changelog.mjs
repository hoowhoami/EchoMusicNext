import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { root, releaseVersion, validVersion } from './contract.mjs';

function sections(markdown) {
  const result = [];
  let current = { heading: '', lines: [] },
    fence;
  for (const line of markdown.replaceAll('\r\n', '\n').split('\n')) {
    const delimiter = /^ {0,3}(`{3,}|~{3,})(.*)$/.exec(line);
    if (delimiter) {
      if (!fence) fence = delimiter[1];
      else if (
        delimiter[1][0] === fence[0] &&
        delimiter[1].length >= fence.length &&
        !delimiter[2].trim()
      )
        fence = undefined;
    }
    const heading = !fence && !delimiter && /^##[ \t]+(.+?)[ \t]*$/.exec(line);
    if (heading) {
      result.push(current);
      current = { heading: heading[1], lines: [] };
    } else current.lines.push(line);
  }
  result.push(current);
  return result.map(({ heading, lines }) => {
    const match = /^\[?v?([0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.+-]+)?)\]?(?:$|\s|[（(])/.exec(
      heading,
    );
    const version = match && validVersion(match[1]) ? match[1] : '';
    return { heading, version, body: lines.join('\n').trim() };
  });
}
const render = (rows) =>
  rows
    .map((row) => `${row.heading ? `## ${row.heading}\n\n` : ''}${row.body}`.trim())
    .filter(Boolean)
    .join('\n\n') + '\n';

// The source keeps its history. Release consumers receive only this version.
export function releaseChangelog(markdown, version) {
  if (!validVersion(version)) throw new Error('Invalid changelog version');
  const rows = sections(markdown);
  const exact = rows.filter((row) => row.version === version);
  if (exact.length > 1) throw new Error(`Duplicate changelog section for ${version}`);
  const base = rows.filter((row) => row.version === version.split(/[-+]/)[0]);
  if (!exact.length && base.length > 1)
    throw new Error(`Duplicate changelog section for ${version}`);
  const selected = exact[0] || base[0];
  // Headings, comments and empty lists alone must not publish blank notes.
  const body = selected?.body || '';
  if (
    !body
      .replace(/<!--[\s\S]*?-->/g, '')
      .split('\n')
      .some((line) => line.trim() && !/^\s*(?:#{1,6}\s.*|[-*+]\s*)$/.test(line))
  )
    throw new Error(`Missing changelog section for ${version}`);
  const current = render([{ heading: '', body: '# 更新记录' }, selected]);
  return { body, current };
}

export function prepareChangelog(
  repository,
  version,
  output,
  sourceFile = path.join(repository, 'CHANGELOG.md'),
) {
  const source = fs.readFileSync(sourceFile, 'utf8');
  const notes = releaseChangelog(source, version);
  const destination = path.resolve(output, 'CHANGELOG.md');
  if (destination === path.resolve(sourceFile))
    throw new Error('Prepared notes must not overwrite the source changelog');
  fs.mkdirSync(output, { recursive: true });
  if (!fs.existsSync(destination) || fs.readFileSync(destination, 'utf8') !== notes.current)
    fs.writeFileSync(destination, notes.current);
  return notes;
}

if (process.argv[1] && fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [command, requested, output, sourceFile] = process.argv.slice(2);
  if (requested !== undefined && !validVersion(requested.replace(/^v/, '')))
    throw new Error('Invalid changelog version');
  const version = releaseVersion(
    root,
    process.env.GITHUB_REF || '',
    requested?.replace(/^v/, '') || process.env.RELEASE_VERSION || '',
  );
  if (command === 'prepare') {
    prepareChangelog(root, version, output || path.join(root, 'build/release-notes'), sourceFile);
    console.log(`Prepared current-version notes for ${version}`);
  } else throw new Error('Expected prepare, optionally followed by a version');
}
