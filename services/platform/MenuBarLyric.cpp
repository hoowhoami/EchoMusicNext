#include "MenuBarLyric.h"
#ifndef Q_OS_MACOS
namespace EchoMusic
{
struct MenuBarLyric::Impl
{
};
MenuBarLyric::MenuBarLyric() = default;
MenuBarLyric::~MenuBarLyric() = default;
bool MenuBarLyric::available()
{
    return false;
}
void MenuBarLyric::create(std::function<void(Action)>, std::function<double()>) {}
void MenuBarLyric::update(const MenuBarLyricState &, const MenuBarLyricStyle &) {}
void MenuBarLyric::reset() {}
QVariantMap MenuBarLyric::diagnostics() const
{
    return {{"created", false}};
}
} // namespace EchoMusic
#endif
