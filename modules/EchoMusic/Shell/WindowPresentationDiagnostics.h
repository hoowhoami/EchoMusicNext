#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QTimer>
#include <memory>

namespace EchoMusic
{
/// Opt-in public-Qt timing, density and font probe. Never schedules frames or selects a backend.
/// Render callbacks own only shared atomic counters; window state is read on
/// the GUI thread so diagnostics cannot introduce scene-graph races.
class WindowPresentationDiagnostics final : public QObject
{
public:
    explicit WindowPresentationDiagnostics(QQuickWindow *window);

private:
    struct Counters;
    std::shared_ptr<Counters> m_counters;
    QTimer m_poll;
};
} // namespace EchoMusic
