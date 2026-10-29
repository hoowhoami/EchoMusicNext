#pragma once
#include <QString>
namespace EchoMusic
{
/// Native login-item state, never a EchoMusic::SettingsStore value pretending registration.
/// Test/nonpersistent windows disable platform access entirely.
class StartupIntegration final
{
public:
    explicit StartupIntegration(bool native) : m_native(native) {}
    bool enabled() const;
    QString setEnabled(bool enabled);
    static QString desktopEntry(const QString &executable);

private:
    bool m_native;
};
} // namespace EchoMusic
