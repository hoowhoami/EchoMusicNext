#include "ApplicationDrain.h"
#include "ApplicationShutdown.h"
#include <QTimer>
#include <QtTest>

using namespace EchoMusic;

class ApplicationLifecycleTest final : public QObject
{
    Q_OBJECT
private slots:
    void pluginDrainPrecedesBusinessAndEveryOwnerMustComplete()
    {
        QStringList events;
        ApplicationDrain drain({[&] { events << "ingress"; }, [&] { events << "business"; },
                                [&] { events << "finished"; }});
        drain.request();
        drain.request();
        QCOMPARE(events, QStringList{"ingress"});
        using S = ApplicationDrain::Service;
        drain.completed(S::Playback);
        drain.completed(S::Playback);
        drain.completed(S::Api);
        drain.completed(S::Accounts);
        drain.completed(S::DeviceRegistration);
        QCOMPARE(events, QStringList{"ingress"});
        drain.completed(S::Plugins);
        QCOMPARE(events, (QStringList{"ingress", "business"}));
        drain.completed(S::Plugins);
        drain.completed(S::Storage);
        drain.completed(S::Storage);
        QCOMPARE(events, (QStringList{"ingress", "business", "finished"}));
    }
    void synchronousCompletionIsReentrantAndFinishesOnce()
    {
        int starts = 0, business = 0, finishes = 0;
        ApplicationDrain *owner = nullptr;
        ApplicationDrain drain({[&] {
                                    ++starts;
                                    owner->completed(ApplicationDrain::Service::Plugins);
                                },
                                [&] {
                                    ++business;
                                    for (auto s : {ApplicationDrain::Service::Playback,
                                                   ApplicationDrain::Service::Api,
                                                   ApplicationDrain::Service::DeviceRegistration,
                                                   ApplicationDrain::Service::Accounts,
                                                   ApplicationDrain::Service::Storage})
                                        owner->completed(s);
                                },
                                [&] { ++finishes; }});
        owner = &drain;
        drain.request();
        drain.request();
        QCOMPARE(starts, 1);
        QCOMPARE(business, 1);
        QCOMPARE(finishes, 1);
    }
    void preclosedServicesCannotFinishBeforeRequest()
    {
        int finishes = 0;
        ApplicationDrain drain({[] {}, [] {}, [&] { ++finishes; }});
        for (int i = 0; i < static_cast<int>(ApplicationDrain::Service::Count); ++i)
            drain.completed(static_cast<ApplicationDrain::Service>(i));
        QCOMPARE(finishes, 0);
        drain.request();
        QCOMPARE(finishes, 1);
    }
    void postedQuitWaitsForAsynchronousDrain()
    {
        int requested = 0;
        bool finished = false;
        ApplicationShutdown *guard = nullptr;
        ApplicationShutdown shutdown(*qApp, [&] {
            ++requested;
            QTimer::singleShot(10, qApp, [&] {
                finished = true;
                guard->complete();
            });
        });
        guard = &shutdown;
        QTimer::singleShot(0, qApp, [] {
            QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
            QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
        });
        QCoreApplication::exec();
        QCOMPARE(requested, 1);
        QVERIFY(finished);
    }
};
QTEST_GUILESS_MAIN(ApplicationLifecycleTest)
#include "ApplicationLifecycleTest.moc"
