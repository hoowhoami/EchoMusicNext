#include "controls/ControlsUiTest.h"
#include <QApplication>
#include "controls/ControlsUiTestFixture.h"
#include "EchoWindowPixelSnap.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    EchoMusic::installWindowPixelSnap();
    configureUiTypography();
    ControlsUiTest test;
    return QTest::qExec(&test, argc, argv);
}
