pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Controls

// Shared contract for every routed page. Pause page-local work when pageActive
// becomes false; destruction releases subscriptions. Cached visual state stays here.
EchoUiPartHost {
    id: page
    partRole: "page.content"
    partKey: context.key
    partSurface: "main"
    publicPart.active: context.active
    required property RouteContext context
    required property RouteController router
    required property ShellSettings settings
    readonly property bool pageActive: context.active && !publicPart.replaced
}
