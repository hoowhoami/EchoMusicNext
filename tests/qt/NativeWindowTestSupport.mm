#import <AppKit/AppKit.h>

namespace EchoMusic::Test
{
void activateTestApplication()
{
    // Command-line QtTest executables have no application bundle activation.
    // Establish the native process policy before requesting a key test window.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activate];
}
} // namespace EchoMusic::Test
