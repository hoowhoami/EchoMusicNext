// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisInstallation.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace EchoMusic::AudioAnalysis;
namespace
{
QString executablePath(QString path)
{
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return path;
}
void executable(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("test-helper") > 0);
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner));
}
} // namespace
class AudioAnalysisInstallationTest final : public QObject
{
    Q_OBJECT
private slots:
    void relocatedMissingMaterialDoesNotUseDevelopmentCache()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        for (const auto &relative :
             {"build/app", "build/models", "build/runtime", "relocated/bin", "build-other/bin"})
            QVERIFY(root.mkpath(relative));
        const Config development{root.filePath(executablePath("build/helper")),
                                 root.filePath("build/models"),
                                 root.filePath("build/runtime"),
                                 {},
                                 {}};
        executable(development.helper);
        const auto build = root.filePath("build");
        const auto actual = resolveInstallation(root.filePath("build/app"), build, development);
        QCOMPARE(actual.helper, development.helper);
        QCOMPARE(actual.modelDirectory, development.modelDirectory);
        QCOMPARE(actual.runtimeDirectory, development.runtimeDirectory);
        for (const auto &directory : {"relocated/bin", "build-other/bin"}) {
            const auto installed =
                resolveInstallation(root.filePath(directory), build, development);
            QVERIFY2(installed.helper.isEmpty(), qPrintable(installed.helper));
            QVERIFY(installed.modelDirectory.isEmpty());
            QVERIFY(installed.runtimeDirectory.isEmpty());
        }
    }
    void packagedMaterialTakesPrecedenceAndMissingPartsStayMissing()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        for (const auto &relative : {"build/app", "build/models", "build/runtime", "relocated/bin",
                                     "relocated/share/echomusicnext/audio-analysis/models",
                                     "relocated/share/echomusicnext/audio-analysis/runtime"})
            QVERIFY(root.mkpath(relative));
        const Config development{root.filePath(executablePath("build/helper")),
                                 root.filePath("build/models"),
                                 root.filePath("build/runtime"),
                                 {},
                                 {}};
        executable(development.helper);
        const auto app = root.filePath("relocated/bin");
        auto helper = app + "/echo-audio-analysis-helper";
#ifdef Q_OS_WIN
        helper += ".exe";
#endif
        executable(helper);
        const auto installed = resolveInstallation(app, root.filePath("build"), development);
        QCOMPARE(installed.helper, helper);
        QCOMPARE(installed.modelDirectory,
                 root.filePath("relocated/share/echomusicnext/audio-analysis/models"));
        QCOMPARE(installed.runtimeDirectory,
                 root.filePath("relocated/share/echomusicnext/audio-analysis/runtime"));
        QVERIFY(QFile::remove(helper));
        QVERIFY(QDir(installed.runtimeDirectory).removeRecursively());
        const auto incomplete = resolveInstallation(app, root.filePath("build"), development);
        QVERIFY(incomplete.helper.isEmpty());
        QVERIFY(incomplete.runtimeDirectory.isEmpty());
        QCOMPARE(incomplete.modelDirectory, installed.modelDirectory);
    }
    void canonicalDirectoryRejectsSymlinkEscapeAndMissingBuildRoot()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        QVERIFY(root.mkpath("build/app"));
        QVERIFY(root.mkpath("outside"));
        const Config development{root.filePath(executablePath("build/helper")),
                                 root.filePath("build"),
                                 root.filePath("build"),
                                 {},
                                 {}};
        executable(development.helper);
        const auto withoutRoot = resolveInstallation(root.filePath("build/app"), {}, development);
        QVERIFY(withoutRoot.helper.isEmpty());
        QVERIFY(withoutRoot.modelDirectory.isEmpty());
        QVERIFY(withoutRoot.runtimeDirectory.isEmpty());
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(root.filePath("outside"), root.filePath("build/alias")));
        const auto escaped =
            resolveInstallation(root.filePath("build/alias"), root.filePath("build"), development);
        QVERIFY(escaped.helper.isEmpty());
        QVERIFY(escaped.modelDirectory.isEmpty());
        QVERIFY(escaped.runtimeDirectory.isEmpty());
#endif
    }
    void helperDirectoriesAreNotExecutables_data()
    {
        QTest::addColumn<bool>("inside");
        QTest::newRow("installed-directory") << false;
        QTest::newRow("build-directory-does-not-hide-real-helper") << true;
    }
    void helperDirectoriesAreNotExecutables()
    {
        QFETCH(bool, inside);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        QVERIFY(root.mkpath("build/app"));
        QVERIFY(root.mkpath("installed/bin"));
        const auto app = root.filePath(inside ? "build/app" : "installed/bin");
        QString name = "echo-audio-analysis-helper";
#ifdef Q_OS_WIN
        name += ".exe";
#endif
        QVERIFY(QDir().mkpath(app + '/' + name));
        const Config development{root.filePath("build/real-" + name), {}, {}, {}, {}};
        executable(development.helper);
        const auto actual = resolveInstallation(app, root.filePath("build"), development);
        QCOMPARE(actual.helper, inside ? development.helper : QString());
    }
    void installedMaterialsCannotLinkOutsidePrefix_data()
    {
        QTest::addColumn<QString>("material");
        QTest::addColumn<bool>("sibling");
        QTest::newRow("helper") << QString("helper") << false;
        QTest::newRow("models") << QString("models") << false;
        QTest::newRow("runtime") << QString("runtime") << false;
        QTest::newRow("whole-resource-root") << QString("root") << false;
        QTest::newRow("same-prefix-helper") << QString("helper") << true;
        QTest::newRow("same-prefix-models") << QString("models") << true;
        QTest::newRow("same-prefix-runtime") << QString("runtime") << true;
    }
    void installedMaterialsCannotLinkOutsidePrefix()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix symbolic-link fixture; Windows helper selection remains separately covered");
#else
        QFETCH(QString, material);
        QFETCH(bool, sibling);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        for (const auto &relative : {"build/models", "build/runtime", "installed/bin",
                                     "installed/share/echomusicnext/audio-analysis"})
            QVERIFY(root.mkpath(relative));
        const auto app = root.filePath("installed/bin");
        const Config development{root.filePath("build/echo-audio-analysis-helper"),
                                 root.filePath("build/models"),
                                 root.filePath("build/runtime"),
                                 {},
                                 {}};
        executable(development.helper);
        const auto resources = root.filePath("installed/share/echomusicnext/audio-analysis");
        const auto link = material == "helper" ? app + "/echo-audio-analysis-helper"
                          : material == "root" ? resources
                                               : resources + '/' + material;
        auto target = material == "helper"   ? development.helper
                      : material == "root"   ? root.filePath("build")
                      : material == "models" ? development.modelDirectory
                                             : development.runtimeDirectory;
        if (sibling) {
            QVERIFY(root.mkpath("installed-other/models"));
            QVERIFY(root.mkpath("installed-other/runtime"));
            target = root.filePath("installed-other/" + (material == "helper"
                                                             ? QString("echo-audio-analysis-helper")
                                                             : material));
            if (material == "helper")
                executable(target);
        }
        if (material == "root")
            QVERIFY(QDir(resources).removeRecursively());
        QVERIFY(QFile::link(target, link));
        const auto actual = resolveInstallation(app, root.filePath("build"), development);
        QVERIFY(actual.helper.isEmpty());
        QVERIFY(actual.modelDirectory.isEmpty());
        QVERIFY(actual.runtimeDirectory.isEmpty());
#endif
    }
    void packagedRelativeLinksRemainUsable()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix relative symbolic-link fixture");
#else
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        for (const auto &relative :
             {"build/app", "installed/bin", "installed/shared/models", "installed/shared/runtime",
              "installed/share/echomusicnext/audio-analysis"})
            QVERIFY(root.mkpath(relative));
        const auto app = root.filePath("installed/bin");
        const auto resources = root.filePath("installed/share/echomusicnext/audio-analysis");
        const auto helper = root.filePath("installed/shared/echo-audio-analysis-helper");
        executable(helper);
        const auto makeRelativeLink = [](const QString &target, const QString &link) {
            const auto relative = QDir(QFileInfo(link).absolutePath()).relativeFilePath(target);
            QVERIFY(!QDir::isAbsolutePath(relative));
            QCOMPARE(::symlink(QFile::encodeName(relative).constData(),
                               QFile::encodeName(link).constData()),
                     0);
            QCOMPARE(QFileInfo(link).canonicalFilePath(), QFileInfo(target).canonicalFilePath());
        };
        makeRelativeLink(helper, app + "/echo-audio-analysis-helper");
        makeRelativeLink(root.filePath("installed/shared/models"), resources + "/models");
        makeRelativeLink(root.filePath("installed/shared/runtime"), resources + "/runtime");
        const auto actual = resolveInstallation(app, root.filePath("build"), {});
        QCOMPARE(actual.helper, app + "/echo-audio-analysis-helper");
        QCOMPARE(actual.modelDirectory, resources + "/models");
        QCOMPARE(actual.runtimeDirectory, resources + "/runtime");
#endif
    }
    void emptyDevelopmentPathsDoNotResolveTheWorkingDirectory()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        QVERIFY(root.mkpath("build/app"));
        const auto actual =
            resolveInstallation(root.filePath("build/app"), root.filePath("build"), {});
        QVERIFY(actual.helper.isEmpty());
        QVERIFY(actual.modelDirectory.isEmpty());
        QVERIFY(actual.runtimeDirectory.isEmpty());
    }
    void developmentMayUseConfiguredSharedDependenciesOutsideTheBuildTree()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QDir root(temporary.path());
        for (const auto &relative : {"build/app", "shared/models", "shared/runtime"})
            QVERIFY(root.mkpath(relative));
        auto helper = root.filePath("shared/echo-audio-analysis-helper");
#ifdef Q_OS_WIN
        helper += ".exe";
#endif
        executable(helper);
        const Config shared{
            helper, root.filePath("shared/models"), root.filePath("shared/runtime"), {}, {}};
        const auto actual =
            resolveInstallation(root.filePath("build/app"), root.filePath("build"), shared);
        QCOMPARE(actual.helper, shared.helper);
        QCOMPARE(actual.modelDirectory, shared.modelDirectory);
        QCOMPARE(actual.runtimeDirectory, shared.runtimeDirectory);
    }
};
QTEST_GUILESS_MAIN(AudioAnalysisInstallationTest)
#include "AudioAnalysisInstallationTest.moc"
