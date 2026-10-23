#include "TrayIconTheme.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QEvent>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace EchoMusic
{
TrayIconTheme::TrayIconTheme(std::function<void()> update, QObject *parent)
    : QObject(parent), m_update(std::move(update))
{
    qApp->installNativeEventFilter(this);
    qApp->installEventFilter(this);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { m_update(); });
}
TrayIconTheme::~TrayIconTheme()
{
    qApp->removeNativeEventFilter(this);
    qApp->removeEventFilter(this);
}
QIcon TrayIconTheme::monochrome(const QColor &color)
{
    QIcon icon;
    const QImage mask(":/echo/shell/assets/mac-tray.png");
    for (const int size : {16, 20, 22, 24, 32, 40, 44, 48, 64}) {
        auto image = mask.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
        painter.end();
        icon.addPixmap(QPixmap::fromImage(image));
    }
    return icon;
}
QIcon TrayIconTheme::icon()
{
#ifdef Q_OS_MACOS
    QIcon icon(":/echo/shell/assets/mac-template.png");
    icon.setIsMask(true);
    return icon;
#else
#ifdef Q_OS_WIN
    // AppsUseLightTheme describes application windows, not the taskbar. Also
    // re-read high contrast on WM_SETTINGCHANGE instead of caching the registry.
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        const auto color = GetSysColor(COLOR_WINDOWTEXT);
        return monochrome(QColor(GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    QSettings system(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    // The supported Windows 10 1809 predates the light taskbar preference;
    // absence of that value therefore means the original dark taskbar.
    const bool light = system.value("SystemUsesLightTheme", 0).toInt() != 0;
    return monochrome(light ? Qt::black : Qt::white);
#else
    // A named symbolic icon lets SNI hosts with their own panel palette render
    // it themselves. Non-supporting/XEmbed desktops get the Qt desktop palette.
    const auto fallback = monochrome(qApp->palette().color(QPalette::WindowText));
    return QIcon::fromTheme(QStringLiteral(ECHO_APPLICATION_ID "-symbolic"), fallback);
#endif
#endif
}
bool TrayIconTheme::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event->message == WM_SETTINGCHANGE || event->message == WM_THEMECHANGED ||
        event->message == WM_SYSCOLORCHANGE)
        m_update();
#else
    Q_UNUSED(message);
#endif
    return false;
}
bool TrayIconTheme::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange)
        m_update();
    return false;
}
} // namespace EchoMusic
