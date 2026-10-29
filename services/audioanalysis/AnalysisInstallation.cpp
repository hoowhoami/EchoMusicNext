// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisInstallation.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace EchoMusic::AudioAnalysis
{
namespace
{
bool containedIn(const QString &canonicalPath, const QString &canonicalPrefix)
{
    if (canonicalPath.isEmpty() || canonicalPrefix.isEmpty())
        return false;
    const auto relative = QDir(canonicalPrefix).relativeFilePath(canonicalPath);
    return !QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../");
}
QString firstExisting(const QStringList &paths, bool directory = false,
                      const QString &canonicalPrefix = {})
{
    for (const auto &candidate : paths) {
        if (candidate.isEmpty())
            continue;
        // Check the exact spelling that will be returned; cleaning after validation could
        // change the meaning of a path containing a symbolic-link parent followed by '..'.
        const auto path = QDir::cleanPath(candidate);
        const QFileInfo info(path);
        if (!(directory ? info.isDir() : info.isFile() && info.isExecutable()))
            continue;
        if (!canonicalPrefix.isEmpty() && !containedIn(info.canonicalFilePath(), canonicalPrefix))
            continue;
        return path;
    }
    return {};
}
} // namespace
Config resolveInstallation(const QString &app, const QString &buildRoot, const Config &buildTree)
{
    const auto canonicalBuild = QFileInfo(buildRoot).canonicalFilePath();
    const auto canonicalApp = QFileInfo(app).canonicalFilePath();
    const bool insideBuild = containedIn(canonicalApp, canonicalBuild);
    const auto installationPrefix =
        canonicalApp.isEmpty() ? QString() : QFileInfo(canonicalApp).absolutePath();
    QString helperName = "echo-audio-analysis-helper";
#ifdef Q_OS_WIN
    helperName += ".exe";
#endif
    QStringList helpers{app + "/../Helpers/" + helperName, app + '/' + helperName};
    QStringList roots{app + "/../Resources/audio-analysis",
                      app + "/../share/echomusicnext/audio-analysis"};
    QString helper, models, runtime;
    if (!installationPrefix.isEmpty()) {
        helper = firstExisting(helpers, false, installationPrefix);
        const auto root = firstExisting(roots, true, installationPrefix);
        if (!root.isEmpty()) {
            models = firstExisting({root + "/models"}, true, installationPrefix);
            runtime = firstExisting({root + "/runtime"}, true, installationPrefix);
        }
    }
    // Only an executable actually in the configured build tree may use its explicit
    // development dependencies. These may intentionally live in a shared directory.
    if (insideBuild) {
        if (helper.isEmpty())
            helper = firstExisting({buildTree.helper});
        if (models.isEmpty())
            models = firstExisting({buildTree.modelDirectory}, true);
        if (runtime.isEmpty())
            runtime = firstExisting({buildTree.runtimeDirectory}, true);
    }
    return {helper, models, runtime,
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/audio-analysis-v1",
            QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                "/echomusicnext-audio-analysis"};
}
} // namespace EchoMusic::AudioAnalysis
