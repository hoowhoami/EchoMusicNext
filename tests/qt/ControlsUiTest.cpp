#include "controls/ControlsUiTest.h"
#include <QApplication>
#include "controls/ControlsUiTestFixture.h"

void ControlsUiTest::cleanup()
{
    // Native popup/windows can retire through more than one queued delivery.
    // Finish those turns before the next engine starts receiving pointer input.
    for (int turn = 0; turn < 2; ++turn) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    configureUiTypography();
    ControlsUiTest test;
    return QTest::qExec(&test, argc, argv);
}
