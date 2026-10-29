#include "ThemeStore.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
class SystemThemeModeTest final : public QObject
{
    Q_OBJECT
private slots:
    void systemSchemePreservesUserModeAndPreferences()
    {
        QTemporaryDir temp;
        EchoMusic::SettingsStore settings(temp.filePath("theme.ini"));
        ThemeStore theme(settings);
        theme.selectEcho();
        theme.setMode(ThemeStore::System);
        const auto saved = settings.value("appearance/preferences");
        theme.adoptSystemScheme(Qt::ColorScheme::Dark);
        QVERIFY(theme.dark());
        theme.adoptSystemScheme(Qt::ColorScheme::Unknown);
        QVERIFY(theme.dark());
        theme.adoptSystemScheme(Qt::ColorScheme::Light);
        QVERIFY(!theme.dark());
        QCOMPARE(theme.mode(), ThemeStore::System);
        QCOMPARE(settings.value("appearance/preferences"), saved);
        theme.setMode(ThemeStore::Dark);
        theme.adoptSystemScheme(Qt::ColorScheme::Light);
        QVERIFY(theme.dark());
        theme.setMode(ThemeStore::Light);
        theme.adoptSystemScheme(Qt::ColorScheme::Dark);
        QVERIFY(!theme.dark());
        theme.setMode(ThemeStore::System);
        QVERIFY(theme.dark());
        theme.setMode(ThemeStore::ThemeDefault);
        theme.adoptSystemScheme(Qt::ColorScheme::Light);
        QVERIFY(!theme.dark());
    }
};
QTEST_MAIN(SystemThemeModeTest)
#include "SystemThemeModeTest.moc"
