#include "MenuBarLyricController.h"
#include "LyricModel.h"
#include "ThemeStore.h"
#include "ApiClient.h"
#include "PlaybackService.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace
{
QJsonObject document(bool wordTimed = true, bool scrollable = true)
{
    return {
        {"offset_ms", 200},
        {"scrollable", scrollable},
        {"lines",
         QJsonArray{QJsonObject{{"text", "作词：某人"},
                                {"start_ms", 0},
                                {"duration_ms", 1000},
                                {"words", QJsonArray{}}},
                    QJsonObject{{"text", "你好世界"},
                                {"translation", "Hello world"},
                                {"romanization", "ni hao shi jie"},
                                {"start_ms", 1000},
                                {"duration_ms", 2000},
                                {"words", wordTimed ? QJsonArray{QJsonObject{{"text", "你好"},
                                                                             {"start_ms", 1000},
                                                                             {"duration_ms", 1000}},
                                                                 QJsonObject{{"text", "世界"},
                                                                             {"start_ms", 2000},
                                                                             {"duration_ms", 1000}}}
                                                    : QJsonArray{}}},
                    QJsonObject{{"text", "纯原文"},
                                {"start_ms", 4000},
                                {"duration_ms", 1000},
                                {"words", QJsonArray{}}}}}};
}
} // namespace
class MenuBarLyricTest : public QObject
{
    Q_OBJECT
private slots:
    void projectionUsesSharedFilteringOffsetsAndSourceTimes()
    {
        LyricModel model(nullptr);
        QVERIFY(model.replace(document()));
        model.setFilter(true, "^作词");
        QVERIFY(MenuBarLyricController::contentAt(model, 500, 0, false).primary.isEmpty());
        QVERIFY(MenuBarLyricController::contentAt(model, 1100, 0, false).primary.isEmpty());
        auto line = MenuBarLyricController::contentAt(model, 1200, 0, false);
        QCOMPARE(line.primary, QString("你好世界"));
        QCOMPARE(line.offsetMs, 200);
        QVERIFY(line.wordTimed);
        QCOMPARE(line.words.first().toMap().value("start_ms").toInt(), 1000);
        QCOMPARE(MenuBarLyricController::contentAt(model, 1200, 1, false).primary,
                 QString("Hello world"));
        QVERIFY(!MenuBarLyricController::contentAt(model, 1200, 1, false).wordTimed);
        QVERIFY(MenuBarLyricController::contentAt(model, 1200, 1, false).primaryIsSecondary);
        QCOMPARE(MenuBarLyricController::contentAt(model, 1200, 2, false).secondary,
                 QString("Hello world"));
        QCOMPARE(MenuBarLyricController::contentAt(model, 1200, 2, false).primary,
                 QString("你好世界 · Hello world"));
        QCOMPARE(MenuBarLyricController::contentAt(model, 1200, 3, false).secondary,
                 QString("Hello world"));
        QVERIFY(MenuBarLyricController::contentAt(model, 3500, 0, false).primary.isEmpty());
        QCOMPARE(MenuBarLyricController::contentAt(model, 3500, 0, true).primary,
                 QString("你好世界"));
        QCOMPARE(MenuBarLyricController::contentAt(model, 4300, 1, false).primary,
                 QString("纯原文"));
        model.clear();
        QVERIFY(MenuBarLyricController::contentAt(model, 4300, 3, true).primary.isEmpty());
        QVERIFY(model.replace(document(false)));
        QVERIFY(!MenuBarLyricController::contentAt(model, 1200, 0, false).wordTimed);
        QVERIFY(model.replace(document(true, false)));
        QCOMPARE(MenuBarLyricController::contentAt(model, 0, 0, false).primary,
                 QString("你好世界"));
        QVERIFY(!MenuBarLyricController::contentAt(model, 0, 0, false).wordTimed);
    }
    void secondarySelectionAndRubyKeepSourceTiming()
    {
        LyricModel model(nullptr);
        QVERIFY(model.replace(document()));
        const auto project = [&](int mode, int content, bool ruby = false) {
            return MenuBarLyricController::contentAt(model, 1200, mode, false, content, ruby);
        };
        QCOMPARE(project(3, 0).secondary, QString("Hello world"));
        QCOMPARE(project(3, 1).secondary, QString("ni hao shi jie"));
        QCOMPARE(project(3, 2).secondary, QString("ni hao shi jie / Hello world"));
        QCOMPARE(project(2, 2).primary, QString("你好世界 · ni hao shi jie / Hello world"));
        const auto only = project(1, 2, true);
        QCOMPARE(only.primary, QString("ni hao shi jie / Hello world"));
        QVERIFY(only.primaryIsSecondary);
        QVERIFY(!only.wordTimed && only.words.isEmpty() && only.ruby.isEmpty());
        const auto ruby = project(3, 2, true);
        QCOMPARE(ruby.primary, QString("你好世界"));
        QVERIFY(ruby.secondary.isEmpty());
        QCOMPARE(ruby.ruby, model.rubyAt(1));
        QVERIFY(!ruby.ruby.isEmpty());
        QCOMPARE(ruby.ruby.first().toMap().value("start_ms").toInt(), 1000);
        QCOMPARE(ruby.offsetMs, 200);
        QCOMPARE(ruby.words, project(3, 0).words);
        const auto inlineRuby = project(2, 2, true);
        QCOMPARE(inlineRuby.primary, QString("你好世界"));
        QVERIFY(inlineRuby.secondary.isEmpty());
        QCOMPARE(inlineRuby.ruby, ruby.ruby);
        const auto phoneticRuby = project(3, 1, true);
        QVERIFY(!phoneticRuby.ruby.isEmpty());
        QVERIFY(phoneticRuby.secondary.isEmpty());
        QVERIFY(project(0, 2, true).ruby.isEmpty());
        auto missing = MenuBarLyricController::contentAt(model, 4300, 3, false, 2, true);
        QVERIFY(missing.secondary.isEmpty() && missing.ruby.isEmpty());
        QCOMPARE(MenuBarLyricController::contentAt(model, 4300, 1, false, 1).primary,
                 QString("纯原文"));
        QVERIFY(model.replace(document(false)));
        const auto untimed = project(3, 2, true);
        QVERIFY(untimed.ruby.isEmpty());
        QCOMPARE(untimed.secondary, QString("ni hao shi jie / Hello world"));
        QVERIFY(!untimed.wordTimed);
        // No dangling separator when only one auxiliary field is present.
        auto single = document();
        auto rows = single.value("lines").toArray();
        auto row = rows[1].toObject();
        row.remove("translation");
        rows[1] = row;
        single["lines"] = rows;
        QVERIFY(model.replace(single));
        QCOMPARE(project(3, 2).secondary, QString("ni hao shi jie"));
    }
    void preferencesAreBoundedPortableAndIndependent()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        settings.setValue("taskbar/enabled", true);
        {
            MenuBarLyricPreferences preferences(settings);
            QVERIFY(!preferences.enabled());
            QCOMPARE(preferences.displayMode(), 0);
            QCOMPARE(preferences.idleBehavior(), 1);
            QCOMPARE(preferences.secondaryContent(), 2);
            QCOMPARE(preferences.secondaryColorMode(), 0);
            preferences.setEnabled(true);
            preferences.setMaxWidth(9000);
            preferences.setFontSize(-5);
            preferences.setOpacity(0);
            preferences.setDisplayMode(3);
            preferences.setSecondaryContent(999);
            QCOMPARE(preferences.secondaryContent(), 2);
            preferences.setSecondaryContent(1);
            preferences.setPauseBehavior(2);
            preferences.setIdleBehavior(1);
            preferences.setColorMode(2);
            preferences.setSecondaryColorMode(1);
            preferences.setPrimaryColor(QColor("#123456"));
            preferences.setSecondaryColor(QColor("#abcdef"));
            preferences.setScrolling(true);
            preferences.setWordByWord(false);
            QCOMPARE(preferences.maxWidth(), 800);
            QCOMPARE(preferences.fontSize(), 10);
            QCOMPARE(preferences.opacity(), 20);
            QVERIFY(settings.portableValues().contains("menuBarLyric/preferences"));
        }
        settings.sync();
        SettingsStore reopened(directory.filePath("settings.ini"));
        MenuBarLyricPreferences preferences(reopened);
        QVERIFY(preferences.enabled());
        QCOMPARE(preferences.displayMode(), 3);
        QCOMPARE(preferences.secondaryContent(), 1);
        QCOMPARE(preferences.pauseBehavior(), 2);
        QCOMPARE(preferences.idleBehavior(), 1);
        QCOMPARE(preferences.primaryColor(), QColor("#123456"));
        QCOMPARE(preferences.secondaryColor(), QColor("#abcdef"));
        QCOMPARE(preferences.secondaryColorMode(), 1);
        QVERIFY(preferences.scrolling());
        QVERIFY(!preferences.wordByWord());
        QVERIFY(reopened.value("taskbar/enabled").toBool());
        ThemeStore theme(reopened);
        theme.setMenuBarLyricFont("system-ui");
        theme.setGlobalFontSizeScale(140);
        QCOMPARE(theme.menuBarLyricFont(), QString("system-ui"));
        QVERIFY(reopened.portableValues().contains("typography/menuBarLyricFont"));
    }
    void controllerWaitsForPresentationAndRetiresWithoutChangingPreference()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api;
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
        MenuBarLyricController controller(settings);
        controller.attachPlayback(playback);
        controller.settings()->setEnabled(true);
        QVERIFY(!controller.diagnostics().value("created").toBool());
        controller.setPresentationReady();
        const bool native = QGuiApplication::platformName() == "cocoa";
        QCOMPARE(controller.diagnostics().value("created").toBool(), native);
        controller.settings()->setEnabled(false);
        QVERIFY(!controller.diagnostics().value("created").toBool());
        controller.settings()->setEnabled(true);
        QCOMPARE(controller.diagnostics().value("created").toBool(), native);
        controller.shutdown();
        QVERIFY(!controller.diagnostics().value("created").toBool());
        QVERIFY(controller.settings()->enabled());
        controller.settings()->setFontSize(16);
        QVERIFY(!controller.diagnostics().value("created").toBool());
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
};
QTEST_MAIN(MenuBarLyricTest)
#include "MenuBarLyricTest.moc"
