#pragma once
#include <QQuickItem>
#include <memory>

class QQuickWindow;
namespace EchoMusic
{
// Window-local presentation alignment. Logical item geometry and native input
// handling stay owned by Qt; no content is captured into an extra texture.
// The pinned Qt private listener is confined to the implementation.
class EchoWindowPixelSnap final : public QQuickItem
{
    Q_OBJECT
public:
    struct Statistics
    {
        qsizetype trackedItems = 0;
        qsizetype compensatedItems = 0;
        qsizetype payloadBytes = 0;
        quint64 passes = 0;
        quint64 adjustments = 0;
        qint64 totalNanoseconds = 0;
        qint64 maximumNanoseconds = 0;
    };
    explicit EchoWindowPixelSnap(QQuickWindow *window);
    ~EchoWindowPixelSnap() override;
    static EchoWindowPixelSnap *forWindow(QQuickWindow *window, bool create = true);
    // Actual first-line paint origin/baseline for native geometry diagnostics.
    static QPointF contentAnchor(QQuickItem *item);
    // Logical layout solvers must not feed presentation offsets back into
    // their own positioning transforms. Existing native input maps stay Qt-owned.
    static QPointF layoutPositionInParent(QQuickItem *item, QPointF point = {});
    Statistics statistics() const;
    void setAlignmentEnabled(bool enabled);
    bool alignmentEnabled() const;

protected:
    void updatePolish() override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    Q_SLOT void scheduleAlignment();
    struct State;
    std::unique_ptr<State> m_state;
};

// Install before constructing windows. Windows defaults on; other platforms
// opt in with ECHO_PIXEL_SNAP_ALL=1 for native regression/diagnostics.
// ECHO_PIXEL_SNAP=0 disables both this scope and the attached leaf helpers.
void installWindowPixelSnap();
} // namespace EchoMusic
