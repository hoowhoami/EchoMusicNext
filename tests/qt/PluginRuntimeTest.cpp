#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "plugins/PluginRuntimeTest.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include "EchoWindowPixelSnap.h"

QTEST_MAIN_WRAPPER(PluginRuntimeTest, QTEST_MAIN_SETUP() EchoMusic::installWindowPixelSnap();)
