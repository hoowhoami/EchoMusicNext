#pragma once
#include <QLatin1StringView>
namespace EchoMusic::SettingsKeys
{
#define ECHO_SETTING(domain, name, key, policy)                                                    \
    namespace domain                                                                               \
    {                                                                                              \
    inline constexpr QLatin1StringView name{key};                                                  \
    }
#define ECHO_PREFIX(domain, name, key, policy) ECHO_SETTING(domain, name, key, policy)
#define ECHO_FIELD(domain, name, key)                                                              \
    namespace domain                                                                               \
    {                                                                                              \
    inline constexpr QLatin1StringView name{key};                                                  \
    }
#include "SettingsKeys.def"
#undef ECHO_SETTING
#undef ECHO_PREFIX
#undef ECHO_FIELD
} // namespace EchoMusic::SettingsKeys
