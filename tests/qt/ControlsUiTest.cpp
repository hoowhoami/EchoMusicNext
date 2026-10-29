#include "controls/ControlsUiTest.h"
#include <QApplication>
#include "controls/ControlsUiTestFixture.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    ControlsUiTest test;
    return QTest::qExec(&test, argc, argv);
}
