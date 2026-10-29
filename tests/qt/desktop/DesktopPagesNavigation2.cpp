#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::accountProfileAndGradeRejectOldAccountResult()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService service(api);
    QVERIFY(AccountServiceTestAccess::adopt(service, account(900001)));
    const auto detail = AccountServiceTestAccess::profileRequest(service, api);
    AccountServiceTestAccess::complete(
        service, detail,
        R"({"id":900001,"nickname":"Qt 测试账号","avatar_url":"","biography":"说明","grade":5,"gender":1,"province":"上海","city":"上海","visitors":42,"registered_at_seconds":1700000000,"listening_minutes":100,"friends":2,"follows":3,"fans":4})");
    QCOMPARE(service.genderLabel(), QStringLiteral("男"));
    QCOMPARE(service.location(), QStringLiteral("上海"));
    QCOMPARE(service.visitors(), quint64(42));
    QVERIFY(service.accountAge() != QStringLiteral("未知"));
    const auto grade = AccountServiceTestAccess::gradeRequest(service, api);
    AccountServiceTestAccess::complete(
        service, grade,
        R"({"user_id":900001,"grade":5,"listening_seconds":864577,"current_points":17120,"next_grade_points":20000,"next_grade":6})");
    QVERIFY(service.gradeAvailable());
    QCOMPARE(service.nextGrade(), 6);
    QCOMPARE(service.listeningDuration(), QStringLiteral("10 天 9 分钟"));
    const auto stale = AccountServiceTestAccess::profileRequest(service, api);
    QVERIFY(AccountServiceTestAccess::adopt(service, account(900002)));
    AccountServiceTestAccess::complete(service, stale,
                                       R"({"id":900001,"visitors":999,"province":"旧账号"})");
    QCOMPARE(service.visitors(), quint64(0));
    QCOMPARE(service.location(), QStringLiteral("未知"));
    QVERIFY(!service.gradeAvailable());
    QVERIFY(service.studentSchool().isEmpty());
    api.shutdown();
}

void DesktopPagesTest::fixedProfileSocialAndSidebarActionsKeepIntrinsicWidths()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileSwitchAccount"));
    auto *first = visual(session.window->contentItem(), "profileSwitchAccount");
    auto *last = visual(session.window->contentItem(), "profileLogout");
    auto *actions = first->parentItem();
    auto *heading = actions->parentItem();
    auto *order = visual(session.window->contentItem(), "sidebarPlaylistOrder");
    QVERIFY(last && order);
    auto *sidebarActions = order->parentItem();
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Profile/SocialUserRow.qml"));
    // Canonical display inputs, never a fabricated upstream response sample.
    std::unique_ptr<QObject> social(component.createWithInitialProperties(
        {{"entryKey", "user:77"},
         {"userId", 77},
         {"nickname", QStringLiteral("很长的中文昵称 Long Latin nickname")},
         {"avatarUrl", ""},
         {"avatarIdentityIcon", QUrl()},
         {"avatarIdentityLabel", ""},
         {"identityBadges", QVariantList()},
         {"description", QStringLiteral("中文 Latin description")},
         {"meta", "Fans 100"},
         {"following", false},
         {"canUnfollow", false},
         {"relationshipKnown", true}}));
    QVERIFY2(social, qPrintable(component.errorString()));
    auto *socialItem = qobject_cast<QQuickItem *>(social.get());
    QVERIFY(socialItem);
    socialItem->setParentItem(session.window->contentItem());
    socialItem->setPosition({300, 460});
    QQuickItem *socialActions = nullptr;
    for (auto *child : socialItem->childItems().first()->childItems().first()->childItems())
        if (child->childItems().size() == 2 &&
            child->childItems().first()->property("text").toString() == QStringLiteral("关注"))
            socialActions = child;
    QVERIFY(socialActions);
    const auto capture = qEnvironmentVariable("ECHO_LAYOUT_ALLOCATION_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    session.theme.setMotionEnabled(false);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1.0, 1.25}) {
            session.shell.resetZoom();
            if (scale > 1)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            socialItem->setScale(scale);
            for (const int width : {1150, 1450}) {
                session.window->resize(width, 850);
                QTRY_COMPARE(actions->width(), 212.);
                QTRY_COMPARE(
                    visualAll(session.window->contentItem(), "profileStatisticValue").size(), 5);
                const auto values =
                    visualAll(session.window->contentItem(), "profileStatisticValue");
                QCOMPARE(values.size(), 5);
                for (auto *value : values) {
                    auto *row = value->parentItem();
                    auto *stat = row->parentItem();
                    // Native layouts distribute the fractional remainder across physical
                    // columns.
                    QTRY_VERIFY2(qAbs(stat->width() - stat->parentItem()->width() / 5) <= 1.,
                                 qPrintable(QString("stat=%1 grid=%2 parent=%3")
                                                .arg(stat->width())
                                                .arg(stat->parentItem()->width())
                                                .arg(stat->metaObject()->className())));
                    QTRY_VERIFY(visual(row, "profileStatisticLabel"));
                    auto *caption = visual(row, "profileStatisticLabel");
                    QVERIFY(caption);
                    QTRY_COMPARE(value->width(), value->implicitWidth());
                    QTRY_VERIFY(qAbs((value->x() + caption->x() + caption->width()) / 2 -
                                     row->width() / 2) < .6);
                }
                QTRY_COMPARE(last->mapToItem(heading, {36, 0}).x(), heading->width());
                QTRY_COMPARE(last->mapToItem(actions, {0, 0}).x(), 176.);
                QTRY_COMPARE(sidebarActions->width(), 76.);
                QTRY_COMPARE(sidebarActions->mapToItem(sidebarActions->parentItem(), {76, 0}).x(),
                             sidebarActions->parentItem()->width());
                for (const bool known : {false, true}) {
                    social->setProperty("relationshipKnown", known);
                    for (const int rowWidth : {360, 540}) {
                        socialItem->setWidth(rowWidth);
                        QTRY_COMPARE(socialActions->width(), socialActions->implicitWidth());
                        QTRY_VERIFY2(qAbs(socialActions->x() + socialActions->width() -
                                          socialActions->parentItem()->width()) < .5,
                                     qPrintable(QString("x=%1 width=%2 implicit=%3 parent=%4 "
                                                        "row=%5 known=%6 scale=%7")
                                                    .arg(socialActions->x())
                                                    .arg(socialActions->width())
                                                    .arg(socialActions->implicitWidth())
                                                    .arg(socialActions->parentItem()->width())
                                                    .arg(socialItem->width())
                                                    .arg(known)
                                                    .arg(scale)));
                    }
                }
                if (!capture.isEmpty()) {
                    session.window->update();
                    QSignalSpy frame(session.window, &QQuickWindow::frameSwapped);
                    QVERIFY(frame.wait());
                    QVERIFY(session.window->grabWindow().save(capture +
                                                              QString("/fixed-actions-%1-%2-%3.png")
                                                                  .arg(int(mode))
                                                                  .arg(scale)
                                                                  .arg(width)));
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionProfileNameTagsStayAligned()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    // Canonical display data only: this is not an upstream response sample.
    const auto profile =
        AccountServiceTestAccess::profileRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, profile,
        R"({"id":900001,"nickname":"一天","avatar_url":"","ip_location":"四川","grade":15})");
    const auto vip = AccountServiceTestAccess::vipRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, vip,
        R"({"user_id":900001,"user_type_code":16,"vip_type_code":0,"super_begins_at":"2026-01-02T03:04:05","super_expires_at":"2030-01-02T03:04:05","products":[]})");
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileNameTags"));
    const auto capture = qEnvironmentVariable("ECHO_TEXT_TAG_PRODUCTION_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            auto *row = visual(session.window->contentItem(), "profileNameTags");
            auto *box = row->property("textBox").value<QQuickItem *>();
            auto *member = visual(row, "profileMembershipTag");
            auto *region = visual(row, "profileRegionTag");
            QVERIFY(box && member && region);
            QTRY_COMPARE(member->property("text").toString(), QStringLiteral("超级VIP"));
            QTRY_COMPARE(row->property("text").toString(), QStringLiteral("一天"));
            QTRY_VERIFY(region->isVisible());
            QCOMPARE(region->property("text").toString(), QStringLiteral("四川"));
            for (auto *tag : {member, region}) {
                QTRY_VERIFY(qAbs(box->mapToScene({0, box->height() / 2}).y() -
                                 tag->mapToScene({0, tag->height() / 2}).y()) < 0.6);
            }
            if (!capture.isEmpty()) {
                QTest::qWait(100);
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/profile-%1-%2.png")
                                  .arg(mode == ThemeStore::Light ? "light" : "dark")
                                  .arg(zoomed ? "scaled" : "normal")));
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionProfileFieldsAndMembershipDates()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    const auto vip = AccountServiceTestAccess::vipRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, vip,
        R"({"user_id":900001,"user_type_code":16,"vip_type_code":0,"super_begins_at":"2026-01-02T03:04:05","super_expires_at":"2030-01-02T03:04:05","products":[]})");
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileChangeAvatar"));
    QTRY_VERIFY(visual(session.window->contentItem(), "profileAccountGroup0"));
    QTRY_VERIFY(visual(session.window->contentItem(), "profileAccountGroup1"));
    auto *basics = visual(session.window->contentItem(), "profileAccountGroup0");
    auto *times = visual(session.window->contentItem(), "profileAccountGroup1");
    QVERIFY(basics && times);
    QTRY_VERIFY(times->y() > basics->y() + basics->height());
    QTRY_COMPARE(visualAll(basics, "profileAccountFieldLabel").size(), 3);
    const auto labels = visualAll(basics, "profileAccountFieldLabel");
    QCOMPARE(labels.size(), 3);
    QTRY_COMPARE(labels.at(0)->property("text").toString(), QStringLiteral("用户 ID"));
    QTRY_COMPARE(labels.at(1)->property("text").toString(), QStringLiteral("性别"));
    QTRY_COMPARE(labels.at(2)->property("text").toString(), QStringLiteral("所在地区"));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            auto *info = visual(session.window->contentItem(), "profileMembershipInfo-super");
            QVERIFY(info && info->isVisible());
            QVERIFY(!info->findChild<QObject *>("iconButtonToolTip"));
            auto *view = visual(session.window->contentItem(), "profileScrollView");
            QVERIFY(view);
            view->setProperty("contentY", std::max(0., view->property("contentHeight").toReal() -
                                                           view->height()));
            QTest::qWait(50);
            auto *page = visual(session.window->contentItem(), "profilePage");
            QVERIFY(!page->findChild<QObject *>("profileMembershipDates"));
            QPointer<QObject> popup;
            const auto point = info->mapToScene({info->width() / 2, info->height() / 2}).toPoint();
            Test::moveNativePointer(session.window, point);
            QTest::mouseClick(session.window, Qt::LeftButton, {}, point);
            QTRY_VERIFY((popup = page->findChild<QObject *>("profileMembershipDates")) &&
                        popup->property("opened").toBool());
            QCOMPARE(session.accounts.account.memberships().first().toMap()["beginsAt"].toString(),
                     QString("2026-01-02 03:04"));
            const auto captures = qEnvironmentVariable("ECHO_PROFILE_PARITY_CAPTURE_DIR");
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                QTest::qWait(250);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/profile-dates-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "scaled" : "normal")));
            }
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QTRY_VERIFY(popup.isNull());
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionAccountSurfacesAndAnchors()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    // Typed local catalog display data; no upstream sample or real credentials.
    StoredAccounts rows;
    for (int i = 0; i < 1000; ++i)
        rows.append({quint64(900001 + i), QStringLiteral("账号 %1").arg(i), {}, {}, i});
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    SavedAccountModelTestAccess::present(
        session.accounts.saved, rows, AccountServiceTestAccess::snapshot(session.accounts.account));
    const auto profile =
        AccountServiceTestAccess::profileRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, profile,
        R"({"id":900001,"nickname":"Qt 测试账号","avatar_url":"","biography":"正式账号浮层组件的领域呈现测试","grade":5,"gender":1,"province":"上海","city":"上海","visitors":9,"registered_at_seconds":1700000000,"listening_minutes":14409,"friends":0,"follows":5,"fans":1})");
    const auto grade =
        AccountServiceTestAccess::gradeRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, grade,
        R"({"user_id":900001,"grade":5,"listening_seconds":864577,"current_points":17120,"next_grade_points":20000,"next_grade":6})");
    QCOMPARE(session.accounts.saved
                 .data(session.accounts.saved.index(0, 0), SavedAccountModel::DurationRole)
                 .toString(),
             QStringLiteral("14,409 分钟"));
    const auto vip = AccountServiceTestAccess::vipRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, vip,
        R"({"user_id":900001,"user_type_code":0,"vip_type_code":0,"products":[{"active":true,"business":"concept","product":"svip","expires_at":"2030-01-02T03:04:05"}]})");
    QTRY_COMPARE(session.accounts.saved.count(), 1000);
    QCOMPARE(session.accounts.saved
                 .data(session.accounts.saved.index(0, 0), SavedAccountModel::NicknameRole)
                 .toString(),
             QStringLiteral("Qt 测试账号"));
    QPointer<QObject> panel, switcher;
    QVERIFY(!session.root->findChild<QObject *>("sidebarAccountPopover"));
    QVERIFY(!session.root->findChild<QObject *>("accountSwitcherDialog"));
    const auto capture = qEnvironmentVariable("ECHO_ACCOUNT_SURFACES_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool collapsed : {false, true}) {
            if (session.shell.sidebarCollapsed() != collapsed)
                session.shell.toggleSidebar();
            for (const bool zoomed : {false, true}) {
                session.shell.resetZoom();
                if (zoomed)
                    session.shell.zoomIn();
                if (!collapsed) {
                    auto *meta = visual(session.window->contentItem(), "sidebarAccountMeta");
                    auto *tag = visual(session.window->contentItem(), "sidebarMembershipTag");
                    QVERIFY(meta && tag);
                    auto *label = meta->property("textBox").value<QQuickItem *>();
                    QVERIFY(label);
                    QTRY_VERIFY2(qAbs(label->mapToScene({0, label->height() / 2}).y() -
                                      tag->mapToScene({0, tag->height() / 2}).y()) < 0.5,
                                 qPrintable(QString("label height=%1 y=%2 tag height=%3 y=%4")
                                                .arg(label->height())
                                                .arg(label->y())
                                                .arg(tag->height())
                                                .arg(tag->y())));
                }
                auto *trigger = visual(session.window->contentItem(), "sidebarAccountTrigger");
                QVERIFY(trigger);
                if (collapsed) {
                    // The collapsed rail exposes only the avatar profile link.
                    QVERIFY(!trigger->isVisible());
                    auto *avatar = visual(session.window->contentItem(), "sidebarUserLink");
                    QVERIFY(avatar);
                    QTRY_COMPARE(avatar->width(), 42.0);
                    QCOMPARE(avatar->height(), 42.0);
                    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                                      avatar->mapToScene(QPointF(21, 21)).toPoint());
                    QTRY_COMPARE(session.router.current()->name(), QString("profile"));
                    QVERIFY(panel.isNull());
                    continue;
                }
                QTRY_COMPARE(trigger->height(), 44.);
                panel = session.openOwnedPopup("sidebarAccountPopoverOwner");
                QVERIFY(panel);
                QTRY_VERIFY(panel->property("opened").toBool());
                auto *content = panel->property("contentItem").value<QQuickItem *>();
                QVERIFY(content);
                const auto scale = session.shell.uiScale();
                // Check rendered overlay geometry against its bottom/end anchor and 14px
                // offset. This case clamps only against the left bound.
                const auto bottom =
                    trigger->mapToScene(QPointF(trigger->width(), trigger->height()));
                const auto actual = content->mapToScene(QPointF(0, 0));
                QVERIFY2(qAbs(actual.y() - (bottom.y() + (14 + 18) * scale)) < 1,
                         qPrintable(QString("panel y=%1 trigger bottom=%2 scale=%3")
                                        .arg(actual.y())
                                        .arg(bottom.y())
                                        .arg(scale)));
                QVERIFY(qAbs(actual.x() - (12 + 18) * scale) < 1);
                QVERIFY(!panel->property("modal").toBool());
                auto *profileAction = visual(content, "accountProfileAction");
                auto *switchAction = visual(content, "accountSwitchAction");
                auto *logoutAction = visual(content, "accountLogoutAction");
                QVERIFY(profileAction && switchAction && logoutAction);
                QTRY_VERIFY(qAbs(profileAction->width() - switchAction->width()) < 1);
                QVERIFY(qAbs(logoutAction->width() - switchAction->width()) < 1);
                auto *background = panel->property("background").value<QQuickItem *>();
                QVERIFY(background);
                if (!capture.isEmpty() && !zoomed) {
                    session.window->update();
                    QTest::qWait(100);
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/account-popover-%1-%2.png")
                                      .arg(mode == ThemeStore::Light ? "light" : "dark")
                                      .arg(collapsed ? "rail" : "full")));
                }
                QVERIFY(QMetaObject::invokeMethod(panel, "close"));
                QTRY_VERIFY(panel.isNull());
            }
        }
        session.shell.resetZoom();
        switcher = session.openOwnedPopup("accountSwitcherDialogOwner");
        QVERIFY(switcher);
        QTRY_VERIFY(switcher->property("opened").toBool());
        auto *add = visual(session.window->contentItem(), "accountAddButton");
        auto *manage = visual(session.window->contentItem(), "accountManageButton");
        auto *badge = visual(session.window->contentItem(), "dialogHeadingBadge");
        auto *close = visual(session.window->contentItem(), "dialogCloseButton");
        auto *list = visual(session.window->contentItem(), "savedAccountList");
        QVERIFY(add && manage && badge && close && list);
        QTRY_COMPARE(add->height(), 32.);
        QCOMPARE(manage->height(), add->height());
        QCOMPARE(badge->property("count").toInt(), 1000);
        auto *currentTag = visual(list, "currentAccountTag");
        QVERIFY(currentTag);
        QCOMPARE(currentTag->property("text").toString(), QStringLiteral("当前登录"));
        QCOMPARE(badge->height(), 18.);
        QVERIFY(badge->width() >= 18.);
        const auto left = switcher->property("x").toReal();
        const auto top = switcher->property("y").toReal();
        const auto closePosition = close->mapToScene(QPointF(0, 0));
        QVERIFY(qAbs(closePosition.x() + close->width() -
                     (left + switcher->property("width").toReal() - 24)) < 1);
        QVERIFY(qAbs(closePosition.y() - (top + switcher->property("topPadding").toReal())) < 1);
        QTRY_VERIFY(list->height() > 100);
        QTRY_VERIFY(visualCount(list, "savedAccountRow") > 0);
        QVERIFY(visualCount(list, "savedAccountRow") < 30);
        list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
        QTest::qWait(60);
        QVERIFY(visualCount(list, "savedAccountRow") < 30);
        list->setProperty("contentY", 0);
        if (!capture.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(session.window->grabWindow().save(
                capture + QString("/account-switcher-%1.png")
                              .arg(mode == ThemeStore::Light ? "light" : "dark")));
        }
        QVERIFY(QMetaObject::invokeMethod(switcher, "close"));
        QTRY_VERIFY(switcher.isNull());
    }
    QPointer<QObject> picker;
    QVERIFY(!session.root->findChild<QObject *>("sidebarShortcutPopup"));
    const auto captures = qEnvironmentVariable("ECHO_ACCOUNT_SURFACES_CAPTURE_DIR");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool collapsed : {false, true}) {
            if (session.shell.sidebarCollapsed() != collapsed)
                session.shell.toggleSidebar();
            QTest::qWait(300); // Source sidebar width animation stays enabled.
            picker = session.openOwnedPopup("sidebarShortcutPopupOwner");
            QVERIFY(picker);
            QTRY_VERIFY(picker->property("opened").toBool());
            const auto edge = picker->property("anchorRect").toRectF().right();
            const auto expected = session.window->width() - edge - 20 >= 340 ? edge + 8 : 12;
            QTRY_VERIFY(qAbs(picker->property("x").toReal() - expected) < 1);
            auto *content = picker->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QVERIFY(visual(content, "shortcutArtistsHeading"));
            QVERIFY(visual(content, "shortcutPlaylistsHeading"));
            if (!captures.isEmpty()) {
                QTest::qWait(100);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/shortcut-picker-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(collapsed ? "rail" : "full")));
            }
            QVERIFY(QMetaObject::invokeMethod(picker, "close"));
            QTRY_VERIFY(picker.isNull());
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::profileEditorKeepsEqualColumnsAndDateActionsClear()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 900);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileEdit"));
    auto *page = visual(session.window->contentItem(), "profilePage");
    QVERIFY(!page->findChild<QObject *>("profileEditorDialog"));
    auto editor = session.openOwnedPopup("profileEditorDialogOwner");
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("opened").toBool());
    QTRY_VERIFY(visual(session.window->contentItem(), "profileEditBirthday"));
    QTRY_VERIFY(visual(session.window->contentItem(), "profileEditGender"));
    auto *birthday = visual(session.window->contentItem(), "profileEditBirthday");
    auto *gender = visual(session.window->contentItem(), "profileEditGender");
    auto *day = visual(birthday, "dateDay");
    auto *year = visual(birthday, "dateYear");
    auto *clear = visual(birthday, "dateClear");
    auto *calendar = visual(birthday, "dateCalendar");
    QVERIFY(day && year && clear && calendar);
    QSignalSpy picked(birthday, SIGNAL(picked(QString)));
    const auto captures = qEnvironmentVariable("ECHO_PROFILE_EDITOR_CAPTURE_DIR");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            for (const int width : {520, 440}) {
                editor->setProperty("preferredWidth", width);
                for (const bool populated : {false, true}) {
                    editor->setProperty("draftBirthday", populated ? "2000-09-28" : "");
                    year->forceActiveFocus();
                    QTRY_VERIFY(qAbs(gender->width() - birthday->width()) < 1);
                    QTRY_COMPARE(birthday->property("hasDate").toBool(), populated);
                    if (populated) {
                        QTRY_COMPARE(year->property("text").toString(), QString("2000"));
                        QTRY_COMPARE(day->property("text").toString(), QString("28"));
                    }
                    const auto *firstAction = populated ? clear : calendar;
                    QTRY_VERIFY(day->mapToItem(birthday, {day->width(), 0}).x() <=
                                firstAction->mapToItem(birthday, QPointF()).x());
                    QTRY_VERIFY(calendar->mapToItem(birthday, {calendar->width(), 0}).x() <=
                                birthday->width());
                    QVERIFY(QMetaObject::invokeMethod(year, "editingFinished"));
                    QCOMPARE(picked.size(), 0); // Merely focusing never changes the draft date.
                    if (!captures.isEmpty()) {
                        QTest::qWait(100);
                        QVERIFY(session.window->grabWindow().save(
                            captures + QString("/editor-%1-%2-%3-%4.png")
                                           .arg(mode == ThemeStore::Light ? "light" : "dark")
                                           .arg(zoomed ? "125" : "100")
                                           .arg(width)
                                           .arg(populated ? "date" : "empty-focused")));
                    }
                }
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTRY_VERIFY(editor.isNull());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::profileEditorValidationFeedbackStaysVisibleWithoutScrolling()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 640);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileEdit"));
    auto editor = session.openOwnedPopup("profileEditorDialogOwner");
    QVERIFY(editor);
    auto *scroll = visual(session.window->contentItem(), "profileEditorScroll");
    QVERIFY(scroll);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->property("height").toReal());
    QCOMPARE(scroll->property("contentY").toReal(), 0.0);
    editor->setProperty("draftName", "");
    QVERIFY(session.clickItem("profileSave"));
    QTRY_VERIFY(session.commands.toasts()->visible());
    QCOMPARE(session.commands.toasts()->message(), QStringLiteral("昵称不能为空"));
    QCOMPARE(session.commands.toasts()->tone(), ToastService::Warning);
    QVERIFY(editor->property("opened").toBool());
    QCOMPARE(scroll->property("contentY").toReal(), 0.0);
    auto *message = visual(session.window->contentItem(), "toastMessage");
    QVERIFY(message);
    QTRY_VERIFY(message->isVisible());
    const auto captures = qEnvironmentVariable("ECHO_PROFILE_EDITOR_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QVERIFY(QDir().mkpath(captures));
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(captures +
                                                  QString("/profile-editor-validation-toast.png")));
    }
    QCOMPARE(warnings.size(), 0);
    session.commands.toasts()->dismiss(session.commands.toasts()->noticeId());
    QTRY_VERIFY(!session.commands.toasts()->visible());
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTRY_VERIFY(editor.isNull());
    QVERIFY(session.settleScene());
}

void DesktopPagesTest::profileDevicesKeepInlineStatusAndBoundedViewport()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 1000);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profilePage"));
    auto *page = visual(session.window->contentItem(), "profilePage");
    QVERIFY(!page->findChild<QObject *>("profileDeviceDialog"));
    QPointer<QObject> dialog;
    // Canonical service-domain presentation input, not upstream wire samples.
    const auto populate = [&](int count) {
        QJsonArray rows;
        for (int index = 0; index < count; ++index)
            rows.append(QJsonObject{
                {"id", QString("domain-device-%1").arg(index)},
                {"model", index == 2
                              ? QStringLiteral("很长的中文设备名称 Long Latin fallback device name")
                              : QString("Device %1").arg(index + 1)},
                {"platform", "Android"},
                {"location", QStringLiteral("中国,南京")},
                {"login_type", 6},
                {"app_id", 3116},
                {"client_version", 11440},
                {"transport_fingerprint", "domain-fingerprint"},
                {"active_at_seconds", 1700000000},
                {"new_at_seconds", index == 1 ? 1700000000 : 1690000000},
                {"current", index == 0}});
        const auto request = ProfileServiceTestAccess::deviceRequest(session.profile, session.api);
        ProfileServiceTestAccess::complete(session.profile, request,
                                           QJsonDocument(rows).toJson(QJsonDocument::Compact));
    };
    dialog = session.openOwnedPopup("profileDeviceDialogOwner");
    QVERIFY(dialog);
    populate(9);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTRY_VERIFY(visual(session.window->contentItem(), "profileDeviceList"));
    QPointer<QQuickItem> list = visual(session.window->contentItem(), "profileDeviceList");
    const auto captures = qEnvironmentVariable("ECHO_PROFILE_DEVICES_CAPTURE_DIR");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            QTRY_VERIFY(visualAll(list, "profileDeviceRow").size() >= 3);
            QTRY_VERIFY(list->height() > 200);
            verifyPopupScrollMapping(session.window, list);
            QVERIFY(!QTest::currentTestFailed());
            QVERIFY(dialog->property("height").toReal() <= 720);
            QVERIFY(dialog->property("height").toReal() <=
                    session.window->height() / session.shell.uiScale() - 140 + 1);
            auto *summary = visual(session.window->contentItem(), "profileDeviceSummary");
            QVERIFY(summary);
            QTRY_COMPARE(summary->property("text").toString(),
                         QStringLiteral("当前账号已登录 9 台设备"));
            for (auto *row : visualAll(list, "profileDeviceRow")) {
                auto *title = visual(row, "profileDeviceTitle");
                auto *tag = visual(row, "profileDeviceStatus");
                auto *remove = visual(row, "profileDeviceRemove");
                QVERIFY(title && tag && remove);
                auto *textBox = title->property("textBox").value<QQuickItem *>();
                QVERIFY(textBox);
                if (tag->isVisible()) {
                    // A short name must keep its tag beside the rendered text;
                    // long names reserve the tag and the separate action column.
                    QTRY_VERIFY(qAbs(tag->mapToItem(title, QPointF()).x() - textBox->width() - 8) <
                                1);
                    QVERIFY(tag->mapToItem(row, {tag->width(), 0}).x() <= row->width() - 13);
                    if (!row->property("currentDevice").toBool()) {
                        QCOMPARE(tag->property("text").toString(), QStringLiteral("新设备"));
                        QCOMPARE(tag->property("customTone").value<QColor>(), QColor("#10b981"));
                    }
                }
                if (remove->isVisible()) {
                    QVERIFY(remove->isEnabled());
                    QCOMPARE(remove->height(), 36.0);
                    QTRY_VERIFY(title->mapToItem(row, {title->width(), 0}).x() + 11 <=
                                remove->mapToItem(row, QPointF()).x());
                }
            }
            if (!captures.isEmpty()) {
                QTest::qWait(100);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/devices-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "125" : "100")));
            }
            list->setProperty("contentY", 0);
            QTest::qWait(100);
            QQuickItem *removeAction = nullptr;
            for (auto *row : visualAll(list, "profileDeviceRow")) {
                if (row->property("currentDevice").toBool() || row->y() < 0 ||
                    row->y() + row->height() > list->height())
                    continue;
                removeAction = visual(row, "profileDeviceRemove");
                break;
            }
            QVERIFY(removeAction && removeAction->isEnabled());
            const auto point =
                removeAction->mapToScene({removeAction->width() / 2, removeAction->height() / 2})
                    .toPoint();
            Test::moveNativePointer(session.window, point);
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(session.profile.pendingDeviceRemoval());
            QPointer<QObject> confirmation =
                dialog->findChild<QObject *>("profileDeviceConfirmation");
            QVERIFY(confirmation);
            QTRY_VERIFY(confirmation->property("opened").toBool());
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/device-confirm-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "125" : "100")));
            session.profile.cancelDeviceRemoval();
            QTRY_VERIFY(confirmation.isNull());
        }
    }
    // A small list shrinks to native row geometry; a large list remains virtual.
    populate(2);
    QTRY_COMPARE(list->property("count").toInt(), 2);
    if (!captures.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(captures + "/devices-two.png"));
    }
    QTRY_VERIFY2(list->height() >= 160 && list->height() < 180,
                 qPrintable(QString("height=%1 implicit=%2 measured=%3 max=%4 mounted=%5")
                                .arg(list->height())
                                .arg(list->implicitHeight())
                                .arg(list->property("measuredExtent").toDouble())
                                .arg(list->property("maximumViewportHeight").toDouble())
                                .arg(visualCount(list, "profileDeviceRow"))));
    auto *bar = popupScrollBarFor(session.window, list);
    QVERIFY(bar);
    QTRY_VERIFY(!bar->isVisible());
    populate(1000);
    QTRY_COMPARE(list->property("count").toInt(), 1000);
    QTRY_VERIFY(visualCount(list, "profileDeviceRow") < 30);
    list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
    QTest::qWait(100);
    QVERIFY(visualCount(list, "profileDeviceRow") < 30);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(dialog.isNull());
    QTRY_VERIFY(list.isNull());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::profileRelatedScrollBarsUseNativeMapping()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 900);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profilePage"));
    auto *page = visual(session.window->contentItem(), "profilePage");
    const auto social = ProfileServiceTestAccess::socialRequest(session.profile, session.api, 0);
    ProfileServiceTestAccess::complete(session.profile, social, socialRows(1000));
    auto &preferences = *session.profile.preferences();
    const auto read = ListeningPreferencesTestAccess::read(preferences, session.api);
    ListeningPreferencesTestAccess::complete(preferences, read, preferencesDomain());
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            const QList<QPair<QString, QString>> surfaces{
                {"profileEditorDialog", "profileEditorScroll"},
                {"listeningPreferencesDialog", "listeningPreferencesScroll"},
                {"profileSocialDrawer", "profileSocialList"}};
            for (const auto &[name, scrollingView] : surfaces) {
                QVERIFY(!page->findChild<QObject *>(name));
                QVariantMap properties;
                if (name != "profileSocialDrawer")
                    properties.insert("maximumHeight", 420);
                auto surface = session.openOwnedPopup(name + "Owner", properties);
                QVERIFY(surface);
                QTRY_VERIFY(surface->property("opened").toBool());
                QTRY_VERIFY(visual(session.window->contentItem(), scrollingView));
                auto *view = visual(session.window->contentItem(), scrollingView);
                verifyPopupScrollMapping(session.window, view);
                QVERIFY(!QTest::currentTestFailed());
                QVERIFY(QMetaObject::invokeMethod(surface, "close"));
                QTRY_VERIFY(surface.isNull());
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::sidebarShortcutCandidatesNavigateAndKeepResourceSnapshots_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("scaled");
    QTest::newRow("light-100") << false << false;
    QTest::newRow("dark-100") << true << false;
    QTest::newRow("light-125") << false << true;
    QTest::newRow("dark-125") << true << true;
}
void DesktopPagesTest::sidebarShortcutCandidatesNavigateAndKeepResourceSnapshots()
{
    QFETCH(bool, dark);
    QFETCH(bool, scaled);
    Session session;
    session.shell.setMotionEnabled(false);
    session.theme.setMotionEnabled(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    if (scaled)
        while (session.shell.uiScale() < 1.25 - .001)
            session.shell.zoomIn();
    PlaybackService playback(session.api, session.preferences);
    auto *layout = session.shell.sidebarLayout();
    QAbstractItemModelTester cards(layout->shortcutModel(),
                                   QAbstractItemModelTester::FailureReportingMode::Fatal);
    layout->attachResources(session.api, playback, session.accounts.playlists, false);
    session.router.setHomeLayout(layout);
    for (const auto &name : {QString("home"), QString("explore"), QString("artist-detail"),
                             QString("playlist-detail")})
        QVERIFY(
            session.router.registerPage({name,
                                         "/test/" + name + (name.endsWith("-detail") ? "/:id" : ""),
                                         QUrl("qrc:/qt/qml/EchoMusic/Shell/RoutePage.qml"),
                                         {}}));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
        playback.shutdown();
    });
    session.window->resize(1150, 950);
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto item = [&](const QString &name) {
        return visual(session.window->contentItem(), name);
    };
    QVERIFY(!session.root->findChild<QObject *>("sidebarShortcutPopup"));
    auto popup = session.openOwnedPopup("sidebarShortcutPopupOwner");
    QVERIFY(popup);
    const auto pins = layout->shortcuts();
    QVERIFY(session.clickItem("shortcutCandidate-explore"));
    QTRY_COMPARE(session.router.current()->name(), QString("explore"));
    QCOMPARE(layout->shortcuts(), pins);
    QTRY_VERIFY(popup.isNull());
    popup = session.openOwnedPopup("sidebarShortcutPopupOwner");
    QVERIFY(popup);
    QVERIFY(session.clickItem("shortcutCandidateToggle-explore"));
    QCOMPARE(layout->shortcuts().size(), 1);
    QCOMPARE(session.router.current()->name(), QString("explore"));
    // The final card remains navigable while only its remove action is disabled.
    QVERIFY(item("shortcutCandidate-home")->isEnabled());
    QVERIFY(!item("shortcutCandidateToggle-home")->isEnabled());
    QVERIFY(session.clickItem("shortcutCandidateToggle-explore"));
    QCOMPARE(layout->shortcuts().size(), 2);
    QJsonArray history;
    for (int i = 1; i <= 9; ++i) {
        const auto artist =
            QJsonObject{{"id", QString::number(i)}, {"name", QString("艺人 %1").arg(i)}};
        QJsonArray refs{artist, QJsonObject{{"id", "0"}, {"name", "无身份"}}};
        if (i == 1)
            refs.append(artist);
        const QJsonObject metadata{{"artist_refs", refs}};
        const QJsonObject track{{"metadata", metadata}};
        history.append(QJsonObject{
            {"play_count", i == 1 ? 5 : 10 - i}, {"played_at", i * 1000}, {"track", track}});
    }
    playback.historyLoaded(history);
    QCOMPARE(layout->frequentArtists().size(), 6);
    QCOMPARE(layout->frequentArtists()[0].toMap()["resourceId"].toString(), QString("2"));
    PlaybackTrack track;
    track.id = "701";
    track.songId = 701;
    track.hash = QString(32, QLatin1Char('a'));
    track.title = "队列歌曲";
    playback.queueCatalog()->restore({{"queue:playlist:901", "歌单 901", {track}, 0, {}, 9},
                                      {"queue:playlist:902", "歌单 902", {track}, 0, {}, 10},
                                      {"queue:album:903", "专辑", {track}, 0, {}, 11},
                                      {"queue:playlist:904", "空歌单", {}, -1, {}, 12}},
                                     "queue:playlist:902");
    QCOMPARE(layout->recentPlaylists().size(), 2);
    QCOMPARE(layout->recentPlaylists()[0].toMap()["resourceId"].toString(), QString("902"));
    QVERIFY(session.clickItem("shortcutCandidateToggle-artist-detail:1"));
    QCOMPARE(layout->shortcuts().size(), 3);
    QCOMPARE(session.router.current()->name(), QString("explore"));
    QVERIFY(session.clickItem("shortcutCandidate-artist-detail:1"));
    QTRY_COMPARE(session.router.current()->resourceId(), QString("1"));
    QCOMPARE(session.router.current()->name(), QString("artist-detail"));
    QTRY_VERIFY(popup.isNull());
    popup = session.openOwnedPopup("sidebarShortcutPopupOwner");
    QVERIFY(popup);
    playback.historyLoaded(history);
    QVERIFY(session.clickItem("shortcutCandidateToggle-playlist-detail:902"));
    QCOMPARE(layout->shortcuts().size(), 4);
    // Reopen and render real production content in each appearance/scale combination.
    QTRY_VERIFY(item("shortcutCandidate-playlist-detail:902"));
    QVERIFY(session.revealItem(item("shortcutCandidate-playlist-detail:902")));
    QVERIFY(session.settleScene());
    const auto output = qEnvironmentVariable("ECHO_SHORTCUT_CAPTURE_DIR");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        QVERIFY(session.window->grabWindow().save(
            output +
            QString("/shortcuts-%1-%2.png").arg(dark ? "dark" : "light", scaled ? "125" : "100")));
    }
    QVERIFY(session.clickItem("shortcutCandidate-playlist-detail:902"));
    QTRY_COMPARE(session.router.current()->resourceId(), QString("902"));
    QCOMPARE(session.router.current()->name(), QString("playlist-detail"));
    QTRY_VERIFY(popup.isNull());
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900002)));
    QCOMPARE(layout->frequentArtists().size(), 0);
    playback.historyLoaded({});
    playback.queueCatalog()->clear();
    QCOMPARE(layout->frequentArtists().size(), 0);
    QCOMPARE(layout->recentPlaylists().size(), 0);
    QCOMPARE(layout->shortcuts().size(), 4);
    QVERIFY(session.clickItem("sidebarShortcut-artist-detail:1"));
    QCOMPARE(session.router.current()->resourceId(), QString("1"));
    layout->moveShortcutBefore("playlist-detail:902", "home");
    QVERIFY(session.router.navigateHome());
    QCOMPARE(session.router.current()->resourceId(), QString("902"));
    session.router.invalidateAccount();
    QCOMPARE(session.router.current()->resourceId(), QString("902"));
    session.preferences.sync();
    EchoMusic::SettingsStore saved(session.directory.filePath("preferences.ini"));
    SidebarLayoutModel restored(saved, nullptr);
    QCOMPARE(restored.shortcuts().size(), 4);
    QCOMPARE(restored.homeRoute(), QString("playlist-detail"));
    QCOMPARE(restored.homeResourceId(), QString("902"));
    layout->toggleShortcut("artist-detail:1");
    layout->restoreShortcuts();
    QCOMPARE(layout->shortcuts().size(), 2);
    for (const auto &warning : warnings)
        for (const auto &error : qvariant_cast<QList<QQmlError>>(warning[0]))
            QVERIFY2(false, qPrintable(error.toString()));
}

void DesktopPagesTest::profileMembershipDatesUseAllAvailableWidth()
{
    Session session;
    session.theme.setMotionEnabled(false);
    session.theme.attachToEngine(session.engine);
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    const auto vip = AccountServiceTestAccess::vipRequest(session.accounts.account, session.api);
    // Canonical membership facts exercise all four production cells together.
    AccountServiceTestAccess::complete(session.accounts.account, vip, R"({
        "user_id":900001,"user_type_code":16,"vip_type_code":6,
        "super_begins_at":"2026-01-02T03:04:05","super_expires_at":"2027-05-29T03:04:05",
        "deluxe_begins_at":"2026-01-02T03:04:05","deluxe_expires_at":"2027-05-31T03:04:05",
        "products":[
            {"active":true,"business":"concept","product":"svip","begins_at":"2026-01-02T03:04:05","expires_at":"2027-12-02T03:04:05"},
            {"active":true,"business":"concept","product":"tvip","begins_at":"2026-01-02T03:04:05","expires_at":"2028-11-29T03:04:05"}
        ]})");
    QCOMPARE(session.accounts.account.memberships().size(), 4);
    QQuickWindow window;
    QQuickItem host(window.contentItem());
    host.setPosition({24, 24});
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Profile/ProfileDetails.qml"));
    QScopedPointer<QObject> root(component.createWithInitialProperties(
        {{"account", QVariant::fromValue(&session.accounts.account)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *panel = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(panel);
    panel->setParentItem(&host);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    window.resize(1060, 620);
    window.show();
    QVERIFY(Test::prepareNativeInput(&window));
    Test::moveNativePointer(&window, {10, 10});
    const auto render = [&] {
        for (int frame = 0; frame < 2; ++frame) {
            QSignalSpy rendered(&window, &QQuickWindow::frameSwapped);
            window.requestUpdate();
            if (!rendered.wait(1000))
                return false;
        }
        return true;
    };
    const QStringList kinds{"super", "deluxe", "concept", "music"};
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        window.setColor(mode == ThemeStore::Dark ? QColor("#202020") : QColor("#fafafa"));
        for (const qreal scale : {1., 1.25}) {
            host.setScale(scale);
            panel->setProperty("uiScale", scale);
            for (const int width : {800, 820, 900, 680, 500, 440, 360}) {
                panel->setWidth(width);
                window.resize(qCeil(width * scale) + 48, 620);
                QVERIFY(render());
                panel->setHeight(panel->implicitHeight());
                QVERIFY(render());
                qreal firstWidth = -1;
                for (const auto &kind : kinds) {
                    auto *content = visual(panel, "profileMembershipContent-" + kind);
                    auto *expiry = visual(panel, "profileMembershipExpiry-" + kind);
                    auto *info = visual(panel, "profileMembershipInfo-" + kind);
                    QVERIFY(content && expiry && info);
                    qInfo() << "membership expiry" << mode << scale << width << kind
                            << content->width() << expiry->width() << expiry->implicitWidth()
                            << expiry->property("truncated");
                    if (firstWidth < 0)
                        firstWidth = content->width();
                    QVERIFY2(qAbs(content->width() - firstWidth) <= 1,
                             qPrintable("Unequal content width: " + kind));
                    if (width >= 800)
                        QVERIFY2(!expiry->property("truncated").toBool(),
                                 qPrintable("Expiry unnecessarily elided: " + kind));
                    QCOMPARE(expiry->property("lineCount").toInt(), 1);
                    QVERIFY(expiry->mapToItem(info->parentItem(), {expiry->width(), 0}).x() <=
                            info->x());
                    QVERIFY(info->width() >= 20);
                    QVERIFY(info->x() + info->width() <= info->parentItem()->width() + 1);
                }
                const auto captures = qEnvironmentVariable("ECHO_MEMBERSHIP_CAPTURE_DIR");
                if (!captures.isEmpty() && (width == 820 || width == 360)) {
                    QDir().mkpath(captures);
                    QVERIFY(window.grabWindow().save(
                        captures + QString("/memberships-%1-%2-%3.png")
                                       .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                       .arg(scale)
                                       .arg(width)));
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}
