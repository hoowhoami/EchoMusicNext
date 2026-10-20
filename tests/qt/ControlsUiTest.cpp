#include "controls/ControlsUiTest.h"
#include "controls/ControlsUiTestFixture.h"

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    ControlsUiTest test;
    return QTest::qExec(&test, argc, argv);
}
