import fs from 'node:fs';
import path from 'node:path';

// Qt deployers enumerate SQL plugins before resolving their external clients.
// Give the tools a private plugin view; the reusable SDK remains complete.
export function prepareQtDeployment(qt, directory, platform) {
  if (platform === 'Windows') return { qmake: path.join(qt, 'bin/qmake.exe') };
  qt = path.resolve(qt);
  directory = path.resolve(directory);
  if (directory === qt || directory.startsWith(qt + path.sep))
    throw new Error('Deployment view must be outside the Qt SDK');
  const sqlite = platform === 'macOS' ? 'libqsqlite.dylib' : 'libqsqlite.so';
  if (!fs.existsSync(path.join(qt, 'plugins/sqldrivers', sqlite)))
    throw new Error(`Qt SQLite driver is missing: ${sqlite}`);
  fs.mkdirSync(path.join(directory, 'bin'), { recursive: true });
  fs.mkdirSync(path.join(directory, 'plugins/sqldrivers'), { recursive: true });
  for (const entry of fs.readdirSync(path.join(qt, 'plugins'))) {
    if (entry === 'sqldrivers') continue;
    fs.symlinkSync(path.join(qt, 'plugins', entry), path.join(directory, 'plugins', entry));
  }
  fs.copyFileSync(
    path.join(qt, 'plugins/sqldrivers', sqlite),
    path.join(directory, 'plugins/sqldrivers', sqlite),
  );
  // Retain the tools' original relative runtime-library lookup.
  fs.symlinkSync(path.join(qt, 'lib'), path.join(directory, 'lib'));
  for (const tool of platform === 'macOS' ? ['qmake', 'macdeployqt'] : ['qmake'])
    fs.copyFileSync(path.join(qt, 'bin', tool), path.join(directory, 'bin', tool));
  fs.writeFileSync(
    path.join(directory, 'bin/qt.conf'),
    `[Paths]\nPrefix=${qt}\nPlugins=${path.join(directory, 'plugins')}\n`,
  );
  return {
    qmake: path.join(directory, 'bin/qmake'),
    macdeployqt: path.join(directory, 'bin/macdeployqt'),
  };
}
