#include "ThemeStore.h"
#include "UiTypography.h"
#include <QQuickStyle>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

class QuickControlsSetup final : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        EchoMusic::configureUiTypography();
        m_settings = std::make_unique<EchoMusic::SettingsStore>(m_directory.filePath("theme.ini"));
        m_theme = std::make_unique<EchoMusic::ThemeStore>(*m_settings);
        m_theme->setMotionEnabled(false);
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        m_theme->attachToEngine(*engine);
    }

private:
    QTemporaryDir m_directory;
    std::unique_ptr<EchoMusic::SettingsStore> m_settings;
    std::unique_ptr<EchoMusic::ThemeStore> m_theme;
};
QUICK_TEST_MAIN_WITH_SETUP(controls, QuickControlsSetup)
#include "QuickControlsTest.moc"
