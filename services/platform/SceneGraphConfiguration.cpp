#include "SceneGraphConfiguration.h"
#include <QByteArrayView>
#include <QtCore/qglobal.h>

namespace EchoMusic
{
void configureSceneGraph()
{
    // Qt's window-sized heuristic can reserve a mostly empty 32 MiB atlas at
    // DPR 2. Keep an 8 MiB default; larger/full-atlas images still use Qt's
    // normal independent-texture fallback without reducing image resolution.
    if (!qEnvironmentVariableIsSet("QSG_ATLAS_WIDTH"))
        qputenv("QSG_ATLAS_WIDTH", "2048");
    if (!qEnvironmentVariableIsSet("QSG_ATLAS_HEIGHT"))
        qputenv("QSG_ATLAS_HEIGHT", "1024");
}
} // namespace EchoMusic
