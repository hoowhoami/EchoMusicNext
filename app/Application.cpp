#include "Application.h"
#include "AppContext_p.h"

int EchoMusic::Application::run()
{
    AppContext context(m_app, m_instance, m_pluginDirectories, m_pluginSafeMode, m_openUrl,
                       m_urlHandler, m_logging);
    return context.run();
}
