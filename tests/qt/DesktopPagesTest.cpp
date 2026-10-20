#include "desktop/DesktopPagesTest.h"
#include "desktop/DesktopPagesTestFixture.h"
#include <QNetworkProxyFactory>

void DesktopPagesTest::initTestCase()
{
    m_initialProxy = QNetworkProxy::applicationProxy();
    m_initialFont = QGuiApplication::font();
    m_initialSystemProxy = QNetworkProxyFactory::usesSystemConfiguration();
    qInfo() << "Fixture proxy baseline type" << m_initialProxy.type() << "uses system configuration"
            << m_initialSystemProxy;
}
void DesktopPagesTest::cleanup()
{
    // A NetworkPreferences fixture installs a process-owned proxy factory.
    // Its router is already destroyed here; never leak that dead endpoint to
    // a later engine, including across QtTest data rows.
    QNetworkProxyFactory::setApplicationProxyFactory(nullptr);
    QNetworkProxy::setApplicationProxy(m_initialProxy);
    QNetworkProxyFactory::setUseSystemConfiguration(m_initialSystemProxy);
    if (QGuiApplication::font() != m_initialFont)
        QGuiApplication::setFont(m_initialFont);
    // Retired popup windows and native commands may require queued delivery
    // before their DeferredDelete. Finish those turns before the next fixture.
    for (int turn = 0; turn < 2; ++turn) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    // Match the production entry point for translucent secondary windows.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EchoMusicTests");
    QCoreApplication::setApplicationName("DesktopPages");
    configureUiTypography();
    QQuickStyle::setStyle("Basic");
    DesktopPagesTest test;
    return QTest::qExec(&test, argc, argv);
}
