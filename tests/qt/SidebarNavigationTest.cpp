#include "SidebarRouteCoordinator.h"
#include "LibraryController.h"
#include "RouteController.h"
#include <QtTest>

using namespace EchoMusic;
namespace EchoMusic
{
// Canonical catalog seam only; these rows are not upstream response fixtures.
class UserPlaylistModelTestAccess
{
public:
    static void append(UserPlaylistModel &model, QString kind, QString globalId,
                       QString creatorGlobalId, quint32 source = 1)
    {
        const auto position = model.m_rows.size();
        model.beginInsertRows({}, position, position);
        UserPlaylistModel::Row row;
        row.cloudId = {quint64(position + 1)};
        row.kind = kind;
        row.collected = kind == "collected";
        row.globalId = std::move(globalId);
        row.creatorGlobalId = std::move(creatorGlobalId);
        row.sourceCode = source;
        model.m_rows.append(row);
        model.endInsertRows();
    }
    static void changeCover(UserPlaylistModel &model)
    {
        model.m_rows[0].cover = "https://example.test/updated-cover.png";
        emit model.dataChanged(model.index(0), model.index(0), {UserPlaylistModel::CoverUrlRole});
        emit model.changed();
    }
    static void collect(UserPlaylistModel &model, int row)
    {
        model.m_rows[row].kind = "collected";
        model.m_rows[row].collected = true;
        model.m_rows[row].creatorGlobalId = model.m_rows[row].globalId;
        emit model.dataChanged(
            model.index(row), model.index(row),
            {UserPlaylistModel::KindRole, UserPlaylistModel::CreatorGlobalIdRole});
    }
};
} // namespace EchoMusic
namespace
{
void registerRoutes(RouteController &router)
{
    for (const auto &name : {QString("home"), QString("playlist-detail"), QString("album-detail")})
        router.registerPage({name,
                             name == "home" ? "/home" : '/' + name + "/:id",
                             QUrl("qrc:/test-only-unused-page.qml"),
                             {"tab"}});
}
struct Session
{
    ApiClient api{DeviceIdentity::temporary()};
    UserPlaylistModel source{api};
    PlaylistMutationService mutations{api};
    LibraryController library{api, source, mutations};
    RouteController router;
    SidebarRouteCoordinator coordinator{library, source, router};
    Session()
    {
        registerRoutes(router);
        UserPlaylistModelTestAccess::append(source, "owned", "owned-global", "unused-creator");
        UserPlaylistModelTestAccess::append(source, "collected", "local-collected",
                                            "creator-public");
    }
};
} // namespace
class SidebarNavigationTest final : public QObject
{
    Q_OBJECT
private slots:
    void backAndForwardRevealTheCurrentPlaylistSection()
    {
        Session session;
        auto &router = session.router;
        auto &library = session.library;
        QVERIFY(router.navigate("playlist-detail", {}, "owned-global"));
        QCOMPARE(library.tab(), 0);
        library.setTab(1);
        // Exercise the production playlist command -> navigation path too.
        connect(
            &library, &LibraryController::playlistRequested, &router,
            [&](const QString &id, quint64, bool) { router.navigate("playlist-detail", {}, id); });
        QVERIFY(library.openPlaylist(2));
        QCOMPARE(router.current()->resourceId(), QString("creator-public"));
        QCOMPARE(library.tab(), 1);
        QCOMPARE(library.playlists()->rowCount(), 1);
        QCOMPARE(library.playlists()
                     ->index(0, 0)
                     .data(UserPlaylistModel::CreatorGlobalIdRole)
                     .toString(),
                 QString("creator-public"));
        router.back();
        QCOMPARE(router.current()->resourceId(), QString("owned-global"));
        QCOMPARE(library.tab(), 0);
        QCOMPARE(library.playlists()->rowCount(), 1);
        router.forward();
        QCOMPARE(router.current()->resourceId(), QString("creator-public"));
        QCOMPARE(library.tab(), 1);
        QVERIFY(router.navigate("home"));
        library.setTab(0);
        router.back();
        QCOMPARE(library.tab(), 1);
        router.back();
        QCOMPARE(library.tab(), 0);
        router.forward();
        QCOMPARE(library.tab(), 1);
    }
    void lateCatalogAndAccountResetUseTheCurrentRoute()
    {
        Session session;
        QVERIFY(session.router.navigate("playlist-detail", {}, "late-collected"));
        QCOMPARE(session.library.tab(), 0);
        UserPlaylistModelTestAccess::append(session.source, "collected", "local-late",
                                            "late-collected");
        QCOMPARE(session.library.tab(), 1);
        QVERIFY(session.router.navigate("home"));
        session.library.setTab(0);
        UserPlaylistModelTestAccess::append(session.source, "collected", "another-local",
                                            "another-public");
        QCOMPARE(session.library.tab(), 0);
        session.router.back();
        QCOMPARE(session.library.tab(), 1);
        QVERIFY(session.api.setAccount(2, "test-only-token"));
        QCOMPARE(session.source.rowCount(), 0);
        UserPlaylistModelTestAccess::append(session.source, "owned", "late-collected", "unused");
        QCOMPARE(session.library.tab(), 0);
    }
    void manualSelectionSurvivesUnrelatedUpdatesAndUnknownRoutes()
    {
        Session session;
        QVERIFY(session.router.navigate("playlist-detail", {}, "owned-global"));
        session.library.setTab(1);
        UserPlaylistModelTestAccess::changeCover(session.source);
        QCOMPARE(session.library.tab(), 1);
        session.router.replaceQuery({{"tab", QString("comments")}});
        QCOMPARE(session.library.tab(), 1);
        session.router.refresh();
        QCOMPARE(session.library.tab(), 1);
        QVERIFY(session.router.navigate("playlist-detail", {}, "unknown-public"));
        QCOMPARE(session.library.tab(), 1);
        QVERIFY(session.router.navigate("playlist-detail", {}, "local-collected"));
        session.library.setTab(0);
        QVERIFY(session.router.navigate("album-detail", {}, "creator-public"));
        QCOMPARE(session.library.tab(), 0);
        UserPlaylistModelTestAccess::append(session.source, "collected", "album-local",
                                            "album-public", 2);
        QVERIFY(session.router.navigate("playlist-detail", {}, "album-public"));
        QCOMPARE(session.library.tab(), 0);
        QVERIFY(session.router.navigate("playlist-detail", {}, "creator-public"));
        QCOMPARE(session.library.tab(), 1);
    }
    void catalogRefreshKeepsTheManuallySelectedSection()
    {
        Session session;
        for (const bool owned : {true, false}) {
            const auto resource = owned ? QString("owned-global") : QString("creator-public");
            QVERIFY(session.router.navigate("playlist-detail", {}, resource));
            QCOMPARE(session.library.tab(), owned ? 0 : 1);
            session.library.setTab(owned ? 1 : 0);
            const auto routeKey = session.router.current()->key();
            for (int refresh = 0; refresh < 2; ++refresh) {
                // The same command as the production refresh button. With no
                // account transport, feed canonical rows after its real reset.
                session.library.reload();
                QCOMPARE(session.source.rowCount(), 0);
                UserPlaylistModelTestAccess::append(session.source, "owned", "owned-global",
                                                    "unused-creator");
                UserPlaylistModelTestAccess::append(session.source, "collected", "local-collected",
                                                    "creator-public");
                QCOMPARE(session.library.tab(), owned ? 1 : 0);
                QCOMPARE(session.library.playlists()->rowCount(), 1);
                QCOMPARE(session.router.current()->key(), routeKey);
            }
        }
    }
    void newAccountAndChangedMembershipStillRevealTheSection()
    {
        Session session;
        QVERIFY(session.router.navigate("playlist-detail", {}, "creator-public"));
        session.library.setTab(0);
        QVERIFY(session.api.setAccount(2, "test-only-token"));
        UserPlaylistModelTestAccess::append(session.source, "collected", "another-local",
                                            "creator-public");
        QCOMPARE(session.library.tab(), 1);
        QVERIFY(session.router.navigate("playlist-detail", {}, "owned-global"));
        UserPlaylistModelTestAccess::append(session.source, "owned", "owned-global", "unused");
        QCOMPARE(session.library.tab(), 0);
        UserPlaylistModelTestAccess::collect(session.source, 1);
        QCOMPARE(session.library.tab(), 1);
        session.library.setTab(0);
        UserPlaylistModelTestAccess::changeCover(session.source);
        QCOMPARE(session.library.tab(), 0);
    }
};
QTEST_GUILESS_MAIN(SidebarNavigationTest)
#include "SidebarNavigationTest.moc"
