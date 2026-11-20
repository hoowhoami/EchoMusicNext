#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"
#include <QQmlIncubationController>

namespace
{
class SectionSources final : public QObject
{
    Q_OBJECT
public:
    explicit SectionSources(QObject *owner) : m_owner(owner)
    {
        connect(owner, SIGNAL(itemChanged()), this, SLOT(observe()));
    }
    QStringList sources;
private slots:
    void observe()
    {
        auto *popup = m_owner->property("item").value<QObject *>();
        if (!popup)
            return;
        // The owner publishes the instance before Popup.open attaches its
        // content to the window overlay; observe that detached tree as well.
        m_loader =
            visual(popup->property("contentItem").value<QQuickItem *>(), "settingsSectionLoader");
        if (m_loader) {
            connect(m_loader, SIGNAL(sourceChanged()), this, SLOT(record()));
            // A cached component can defer sourceChanged until incubation is
            // Ready. Observe Loading too, including a subsequently cancelled load.
            connect(m_loader, SIGNAL(statusChanged()), this, SLOT(record()));
            record();
        }
    }
    void record()
    {
        const auto source = m_loader ? m_loader->property("source").toUrl().toString() : QString();
        if (!source.isEmpty() && !sources.contains(source))
            sources << source;
    }

private:
    QObject *m_owner;
    QPointer<QObject> m_loader;
};
} // namespace

void DesktopPagesTest::lyricSkinGlobalSettingsHandoff_data()
{
    QTest::addColumn<QString>("skin");
    QTest::addColumn<bool>("motion");
    for (const auto &skin : {QString("cover"), QString("lyric"), QString("portrait")})
        for (bool motion : {false, true})
            QTest::newRow(qPrintable(skin + (motion ? "-animated" : "-reduced"))) << skin << motion;
}

void DesktopPagesTest::lyricSkinGlobalSettingsHandoff()
{
    QFETCH(QString, skin);
    QFETCH(bool, motion);
    Session session;
    session.preferences.setValue("playback/volume", 0);
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setMotionEnabled(motion);
    session.shell.setMotionEnabled(motion);
    QVERIFY(session.theme.extensions()->chooseSkin(skin));
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *settingsOwner = session.root->findChild<QObject *>("settingsDialogOwner");
    QVERIFY(settingsOwner);
    SectionSources observed(settingsOwner);

    for (int cycle = 0; cycle < 4; ++cycle) {
        observed.sources.clear();
        auto page = session.openOwnedPopup("playbackPageOwner");
        QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "openSkins"));
        auto *drawerOwner = page->findChild<QObject *>("lyricSkinDrawerOwner");
        QVERIFY(drawerOwner);
        QPointer<QObject> drawer;
        QTRY_VERIFY((drawer = drawerOwner->property("item").value<QObject *>()) &&
                    drawer->property("opened").toBool());
        drawer->setProperty("showSettings", cycle >= 2);
        QSignalSpy drawerClosed(drawer, SIGNAL(closed()));
        QSignalSpy requested(drawer, SIGNAL(globalSettingsRequested()));
        const auto findAction = [&](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (!item)
                return nullptr;
            if (item->property("text").toString() == QStringLiteral("全局设置") &&
                item->property("glyph").toString() == "settings")
                return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child))
                    return found;
            return nullptr;
        };
        auto *action =
            findAction(findAction, drawer->property("contentItem").value<QQuickItem *>());
        QVERIFY(action);

        // Keep Qt's asynchronous Loader, but control its budget at this boundary.
        // A user can dismiss the new popup before its section finishes binding.
        QQmlIncubationController held;
        auto *normalController = session.engine.incubationController();
        const bool controlled = cycle == 1 || cycle == 2;
        if (controlled)
            session.engine.setIncubationController(&held);
        const auto restoreController =
            qScopeGuard([&] { session.engine.setIncubationController(normalController); });
        QVERIFY(session.clickItem(action));
        QCOMPARE(requested.size(), 1);
        QPointer<QObject> settings = settingsOwner->property("item").value<QObject *>();
        QVERIFY(settings && settings->property("visible").toBool());
        auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
        QVERIFY(catalog);
        QCOMPARE(catalog->current(), QString("lyric"));
        auto *loader = visual(session.window->contentItem(), "settingsSectionLoader");
        QVERIFY(loader);
        qInfo() << "Skin/settings handoff" << skin << "motion" << motion << "cycle" << cycle
                << "drawerClosed" << drawerClosed.size() << "loaderStatus"
                << loader->property("status") << "source" << loader->property("source");
        QVERIFY(loader->property("asynchronous").toBool());
        QVERIFY(loader->property("source").toUrl().path().endsWith("/LyricSettingsSection.qml"));
        QCOMPARE(observed.sources, QStringList{loader->property("source").toUrl().toString()});
        if (controlled) {
            QCOMPARE(loader->property("status").toInt(), 2); // Loader.Loading
            QVERIFY(held.incubatingObjectCount() > 0);
            if (cycle == 2) {
                held.incubateFor(1);
                qInfo() << "After partial incubation" << skin << "motion" << motion << "remaining"
                        << held.incubatingObjectCount() << "status" << loader->property("status");
            }
            QVERIFY(QMetaObject::invokeMethod(settings, "close"));
            QTRY_VERIFY(settings.isNull());
            QCOMPARE(held.incubatingObjectCount(), 0);
        } else {
            QTRY_VERIFY(settings->property("opened").toBool());
            QVERIFY(session.settingsSectionReady());
            QPointer<QObject> section = loader->property("item").value<QObject *>();
            QVERIFY(section);
            QCOMPARE(section->property("playback").value<PlaybackService *>(), &playback);
            QVERIFY(QMetaObject::invokeMethod(settings, "close"));
            QTRY_VERIFY(settings.isNull() && section.isNull());
        }
        QTRY_VERIFY(drawer.isNull());
        QVERIFY(!settingsOwner->property("item").value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(page.isNull());
        session.engine.collectGarbage();
    }
    QStringList errors;
    for (const auto &entry : warnings)
        for (const auto &error : entry.value(0).value<QList<QQmlError>>())
            errors << error.toString();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
}

void DesktopPagesTest::lyricSkinSwitchKeepsCatalogAndPageStable_data()
{
    QTest::addColumn<bool>("motion");
    QTest::addColumn<bool>("dark");
    for (bool motion : {false, true})
        for (bool dark : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dark ? "dark-125" : "light-100",
                                                          motion ? "animated" : "reduced")))
                << motion << dark;
}

void DesktopPagesTest::lyricSkinSwitchKeepsCatalogAndPageStable()
{
    QFETCH(bool, motion);
    QFETCH(bool, dark);
    Session session;
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(motion);
    session.theme.setMotionEnabled(motion);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    if (dark)
        for (int step = 0; step < 5; ++step)
            session.shell.zoomIn();
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.auxiliary.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    auto page = session.openOwnedPopup("playbackPageOwner");
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "openSkins"));
    auto *owner = page->findChild<QObject *>("lyricSkinDrawerOwner");
    QVERIFY(owner);
    QPointer<QObject> drawer;
    QTRY_VERIFY((drawer = owner->property("item").value<QObject *>()) &&
                drawer->property("opened").toBool());
    auto *catalog = visual(session.window->contentItem(), "lyricSkinCatalog");
    QVERIFY(catalog);
    QVERIFY(session.settleScene());
    QList<QPointer<QQuickItem>> cards;
    QList<QRectF> rectangles;
    for (const auto *name :
         {"coverLyricSkinCard", "portraitLyricSkinCard", "simpleLyricSkinCard"}) {
        auto *card = visual(catalog, name);
        QVERIFY(card);
        cards.append(card);
        rectangles.append(card->mapRectToScene(card->boundingRect()));
    }
    const auto windowGeometry = session.window->geometry();
    const auto catalogRect = catalog->mapRectToScene(catalog->boundingRect());
    QSignalSpy resets(session.theme.extensions()->skins(), &QAbstractItemModel::modelReset);
    QSignalSpy themeResets(session.theme.extensions()->themes(), &QAbstractItemModel::modelReset);
    QSignalSpy pageClosed(page, SIGNAL(closed()));
    QSignalSpy drawerClosed(drawer, SIGNAL(closed()));
    const auto capture = qEnvironmentVariable("ECHO_SKIN_SWITCH_CAPTURES");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int index : {2, 1, 0}) {
            auto *card = visual(catalog, index == 0   ? "coverLyricSkinCard"
                                         : index == 1 ? "portraitLyricSkinCard"
                                                      : "simpleLyricSkinCard");
            QVERIFY(card);
            QVERIFY(session.clickItem(card));
            for (int frame = 0; frame < 4; ++frame) {
                session.window->update();
                QTest::qWait(16);
                if (!capture.isEmpty() && cycle == 0) {
                    QVERIFY(session.window->grabWindow().save(capture +
                                                              QString("/%1-%2-%3.png")
                                                                  .arg(QTest::currentDataTag())
                                                                  .arg(index)
                                                                  .arg(frame)));
                }
            }
            qInfo() << "Skin switch scope" << index << "catalog resets" << resets.size()
                    << "page closed" << pageClosed.size() << "drawer closed" << drawerClosed.size();
            QCOMPARE(session.window->geometry(), windowGeometry);
            QVERIFY(page && drawer);
            QCOMPARE(pageClosed.size(), 0);
            QCOMPARE(drawerClosed.size(), 0);
            QCOMPARE(catalog->mapRectToScene(catalog->boundingRect()), catalogRect);
            for (int row = 0; row < cards.size(); ++row) {
                QVERIFY2(cards[row], "Selecting a skin destroyed an unchanged thumbnail card");
                QCOMPARE(cards[row]->mapRectToScene(cards[row]->boundingRect()), rectangles[row]);
                QCOMPARE(cards[row]->property("selected").toBool(), row == index);
            }
            QCOMPARE(resets.size(), 0);
            QCOMPARE(themeResets.size(), 0);
        }
    }
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull() && drawer.isNull());
    for (const auto &card : cards)
        QVERIFY(card.isNull());
    QCOMPARE(warnings.size(), 0);
}

#include "DesktopPagesSettingsHandoff.moc"
