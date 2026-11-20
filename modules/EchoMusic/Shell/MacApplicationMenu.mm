#import <Cocoa/Cocoa.h>
#include "WindowController.h"
#include <QMenuBar>
#include <QMenu>
#include <QTranslator>
#include <QApplication>

namespace EchoMusic
{
namespace
{
// The product currently presents Chinese UI. Translate Qt's native application
// menu as well; leave AppKit's Services contents to the operating system.
class ApplicationMenuTranslator final : public QTranslator
{
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override
    {
        return false;
    }
    QString translate(const char *context, const char *source, const char *, int) const override
    {
        if (qstrcmp(context, "QMenuBar") != 0 && qstrcmp(context, "MAC_APPLICATION_MENU") != 0)
            return {};
        const QHash<QByteArray, QString> labels{{"About %1", QStringLiteral("关于 %1")},
                                                {"Preferences...", QStringLiteral("偏好设置…")},
                                                {"Preferences…", QStringLiteral("偏好设置…")},
                                                {"Services", QStringLiteral("服务")},
                                                {"Hide %1", QStringLiteral("隐藏 %1")},
                                                {"Hide Others", QStringLiteral("隐藏其他")},
                                                {"Show All", QStringLiteral("全部显示")},
                                                {"Quit %1", QStringLiteral("退出 %1")}};
        return labels.value(source);
    }
};
} // namespace
QMenuBar *createMacApplicationMenu(WindowController *controller)
{
    auto *bar = new QMenuBar;
    bar->setObjectName("nativeApplicationMenu");
    id tracking =
        [NSNotificationCenter.defaultCenter addObserverForName:NSMenuDidBeginTrackingNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification *) {
                                                      emit controller->nativeMenuOpening();
                                                    }];
    QObject::connect(bar, &QObject::destroyed,
                     [tracking] { [NSNotificationCenter.defaultCenter removeObserver:tracking]; });
    auto *translator = new ApplicationMenuTranslator(bar);
    qApp->installTranslator(translator);
    // A parentless QMenuBar is Qt's application-wide native menu. All three
    // actions merge into the system's first group, leaving no other visible
    // top-level menu (including when Mini or desktop lyrics has focus).
    auto *application = bar->addMenu(QStringLiteral("EchoMusicNext"));
    auto *about = application->addAction(QStringLiteral("关于 EchoMusicNext"), controller,
                                         [] { [NSApp orderFrontStandardAboutPanel:nil]; });
    about->setMenuRole(QAction::AboutRole);
    auto *preferences = application->addAction(QStringLiteral("偏好设置…"), controller,
                                               [controller] { controller->requestSettings(); });
    preferences->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    preferences->setMenuRole(QAction::PreferencesRole);
    auto *quit = application->addAction(QStringLiteral("退出 EchoMusicNext"), controller,
                                        &WindowController::quit);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);
    // Force creation of the platform menu after roles are assigned. Qt owns
    // Services, Hide/Hide Others/Show All, shortcuts and first responder rules.
    bar->toNSMenu();
    return bar;
}
void popupMacTrayMenu(QMenu *menu)
{
    NSMenu *native = menu->toNSMenu();
    // There is intentionally no NSStatusItem.menu binding. Only an actual
    // right-click reaches here; AppKit owns tracking, dismissal and submenus.
    [native popUpMenuPositioningItem:nil atLocation:NSEvent.mouseLocation inView:nil];
}
void retireMacMenu(QMenu *menu)
{
    // Cocoa queues QPlatformMenuItem::activated, then QMenu queues QAction::trigger.
    // Cross both queued hops before posting DeferredDelete. Cocoa can flush
    // deferred deletes between posted-event batches, before the second hop.
    QMetaObject::invokeMethod(
        menu,
        [menu] { QMetaObject::invokeMethod(menu, &QObject::deleteLater, Qt::QueuedConnection); },
        Qt::QueuedConnection);
}
} // namespace EchoMusic
