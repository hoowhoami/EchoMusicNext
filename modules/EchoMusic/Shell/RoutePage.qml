pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Controls

// Shared contract for every routed page. Pause page-local work when pageActive
// becomes false; destruction releases subscriptions. Recoverable UI values live
// in the route's EchoPageState scope, independently of this visual instance.
EchoUiPartHost {
    id: page
    partRole: "page.content"
    partKey: context.key
    partSurface: "main"
    publicPart.active: context.active
    required property RouteContext context
    required property RouteController router
    required property ShellSettings settings
    // A single outgoing view may still render during the handoff. Its route
    // is already inactive, input disabled and recoverable snapshots frozen.
    readonly property bool pageActive: (context.active || (EchoPageState.presentation?.retiring ?? false)) && !publicPart.replaced
}
