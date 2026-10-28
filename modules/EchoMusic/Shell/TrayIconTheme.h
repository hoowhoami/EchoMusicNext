#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QTimer>
#include <QIcon>
#include <functional>

namespace EchoMusic
{
// System surfaces follow the desktop, independently of ThemeStore's app skin.
class TrayIconTheme final : public QObject, public QAbstractNativeEventFilter
{
public:
    explicit TrayIconTheme(std::function<void()> update, QObject *parent = nullptr);
    ~TrayIconTheme() override;
    static QIcon icon();
    static QIcon monochrome(const QColor &color);

private:
    void scheduleUpdate();
    bool eventFilter(QObject *, QEvent *) override;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
    std::function<void()> m_update;
    QTimer m_pendingUpdate;
    bool m_updating = false;
};
} // namespace EchoMusic
