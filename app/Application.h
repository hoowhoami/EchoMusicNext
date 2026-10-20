#pragma once
#include <QStringList>
#include <QUrl>
#include <utility>
class QGuiApplication;

namespace EchoMusic
{
class SingleInstance;
class ApplicationUrlHandler;
class LogService;
/// Composition root. Services live for the event loop; QML
/// roots are released before injected services; the engine outlives plugin runtimes.
/// Asynchronous workers drain before the context and its callbacks are destroyed.
class Application final
{
public:
    explicit Application(QGuiApplication &app, SingleInstance *instance = nullptr,
                         QStringList pluginDirectories = {}, bool pluginSafeMode = false,
                         QUrl openUrl = {}, ApplicationUrlHandler *urlHandler = nullptr,
                         LogService *logging = nullptr)
        : m_app(app), m_instance(instance), m_pluginDirectories(std::move(pluginDirectories)),
          m_pluginSafeMode(pluginSafeMode), m_openUrl(std::move(openUrl)), m_urlHandler(urlHandler),
          m_logging(logging)
    {
    }
    int run();

private:
    QGuiApplication &m_app;
    SingleInstance *m_instance;
    QStringList m_pluginDirectories;
    bool m_pluginSafeMode;
    QUrl m_openUrl;
    ApplicationUrlHandler *m_urlHandler;
    LogService *m_logging;
};
} // namespace EchoMusic
