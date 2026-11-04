import fs from 'node:fs';
import path from 'node:path';

export function copyNotices(directory, target, all = false) {
  if (!fs.existsSync(directory)) return 0;
  let count = 0;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.isSymbolicLink() || ['.git', 'target', 'node_modules'].includes(entry.name)) continue;
    const file = path.join(directory, entry.name),
      to = path.join(target, entry.name);
    if (entry.isDirectory())
      count += copyNotices(file, to, all || /^licen[sc]es?$/i.test(entry.name));
    else if (
      (all ||
        /^(licen[sc]e|copying|copyright|notice|authors)([.\-_]|$)/i.test(entry.name) ||
        entry.name === 'qt_attribution.json') &&
      fs.statSync(file).size > 0
    ) {
      fs.mkdirSync(target, { recursive: true });
      fs.copyFileSync(file, to);
      ++count;
    }
  }
  return count;
}
