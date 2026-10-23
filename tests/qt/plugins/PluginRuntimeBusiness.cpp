#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginKugou.h"
namespace EchoMusic
{
// Deterministic delivery/control-flow probes. Commands still enter the actual
// Rust FFI; these payloads are not evidence of a provider's wire acceptance.
class ApiClientTestAccess
{
public:
    static quint64 pending(ApiClient &api, const QString &operation)
    {
        for (auto i = api.m_pending.begin(); i != api.m_pending.end(); ++i)
            if (i->request.value("operation") == operation)
                return i.key();
        return 0;
    }
    static QJsonObject request(ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request;
    }
    static void complete(ApiClient &api, quint64 id, const QByteArray &payload)
    {
        api.deliver(id, api.m_revision, 0, payload);
    }
    static void challenge(ApiClient &api, quint64 id)
    {
        api.deliver(id, api.m_revision, 5,
                    R"({"kind":"challenge","challenge":{"event_id":"fixture-event"}})");
    }
    static quint64 active(ApiClient &api)
    {
        return api.m_activeVerification;
    }
    static bool suspended(ApiClient &api, quint64 id)
    {
        return api.m_suspended.contains(id);
    }
    static void verified(ApiClient &api, const QString &event)
    {
        api.retryVerifiedEvent(event);
    }
};
} // namespace EchoMusic
void PluginRuntimeTest::ecosystemBusinessRequestsAndSharedVerification()
{
    Host host;
    QVERIFY(host.api.setNetworkTransport(1));
    QVERIFY(host.api.initializeRuntime());
    QVERIFY(host.api.setAccount(42, "fixture-token", "fixture-refresh"));
    auto *scope = ecosystem(host, "");
    QVERIFY(scope);
    QList<QJSValue> catalogRoots;
    for (const auto &method : EchoPluginKugou::methods()) {
        auto *invalid = scope->kugou()->request(method, {{"__unknown", true}});
        catalogRoots.append(host.engine.newQObject(invalid));
        QCOMPARE(invalid->state(), EchoPluginOperation::Failed);
        QCOMPARE(invalid->errorCode(), "InvalidArgument");
    }
    auto *search = scope->kugou()->request(
        "search", {{"kind", "song"}, {"keyword", "音乐"}, {"page", 2}, {"pageSize", 20}});
    auto keep = host.engine.newQObject(search);
    QCOMPARE(search->state(), EchoPluginOperation::Running);
    auto id = ApiClientTestAccess::pending(host.api, "search");
    QVERIFY(id);
    auto request = ApiClientTestAccess::request(host.api, id);
    QCOMPARE(request.value("query").toObject().value("keyword").toString(), "音乐");
    QCOMPARE(request.value("identity").toObject().value("userid").toInteger(), qint64(42));
    ApiClientTestAccess::complete(
        host.api, id, R"({"kind":"songs","page":{"page":2,"page_size":20,"total":0,"items":[]}})");
    QCOMPARE(search->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!search->result().toMap().contains("identity"));
    auto *lyric =
        scope->kugou()->request("lyric", {{"id", "fixture-lyric"}, {"accessKey", "fixture-key"}});
    auto lyricRoot = host.engine.newQObject(lyric);
    const auto lyricId = ApiClientTestAccess::pending(host.api, "lyric");
    QVERIFY(lyricId);
    ApiClientTestAccess::complete(
        host.api, lyricId,
        QJsonDocument(
            QJsonObject{
                {"format", "lrc"},
                {"encoding", "base64"},
                {"content", QString::fromLatin1(
                                QByteArray("[00:01][00:03]Repeated\n[00:02]Verse").toBase64())}})
            .toJson());
    QTRY_COMPARE(lyric->state(), EchoPluginOperation::Succeeded);
    const auto normalized = lyric->result().toMap();
    QVERIFY(normalized.contains("scrollable"));
    QCOMPARE(normalized.value("lines").toList().size(), 3);
    QCOMPARE(normalized.value("lines").toList()[2].toMap().value("start_ms").toLongLong(), 3000);
    auto *metadata =
        scope->kugou()->request("songMetadata", {{"ids", QVariantList{"9007199254740993"}}});
    auto metadataRoot = host.engine.newQObject(metadata);
    auto metadataId = ApiClientTestAccess::pending(host.api, "krm_audio");
    QVERIFY(metadataId);
    QCOMPARE(ApiClientTestAccess::request(host.api, metadataId)
                 .value("query")
                 .toObject()
                 .value("album_audio_ids")
                 .toArray()
                 .first()
                 .toInteger(),
             qint64(9007199254740993LL));
    QVERIFY(metadata->state() == EchoPluginOperation::Running);
    metadata->cancel();
    auto *identity = scope->kugou()->request("userInfo");
    auto identityRoot = host.engine.newQObject(identity);
    auto identityId = ApiClientTestAccess::pending(host.api, "user_info");
    QVERIFY(identityId);
    ApiClientTestAccess::complete(host.api, identityId, R"({"id":9007199254740993})");
    QCOMPARE(identity->result().toMap().value("id").toString(), "9007199254740993");
    auto *bad = scope->kugou()->request(
        "search", {{"kind", "invalid"}, {"keyword", "x"}, {"userid", 1}, {"token", "override"}});
    auto badRoot = host.engine.newQObject(bad);
    QCOMPARE(bad->state(), EchoPluginOperation::Failed);
    auto *write =
        scope->kugou()->request("createPlaylist", {{"title", "P0 fixture"}, {"isPrivate", true}});
    auto writeRoot = host.engine.newQObject(write);
    QCOMPARE(write->state(), EchoPluginOperation::Running);
    auto writeId = ApiClientTestAccess::pending(host.api, "playlist_add");
    QVERIFY(writeId);
    write->cancel();
    QCOMPARE(write->state(), EchoPluginOperation::Canceled);
    QCOMPARE(ApiClientTestAccess::pending(host.api, "playlist_add"), quint64(0));
    auto *challenge = scope->kugou()->request("searchHot");
    auto challengeRoot = host.engine.newQObject(challenge);
    auto challenged = ApiClientTestAccess::pending(host.api, "search_hot");
    QVERIFY(challenged);
    ApiClientTestAccess::challenge(host.api, challenged);
    QVERIFY(host.verification.open());
    QCOMPARE(ApiClientTestAccess::active(host.api), challenged);
    auto *explicitProof = scope->kugou()->verify("fixture-event");
    auto explicitRoot = host.engine.newQObject(explicitProof);
    QCOMPARE(explicitProof->state(), EchoPluginOperation::Running);
    explicitProof->cancel();
    QCOMPARE(explicitProof->state(), EchoPluginOperation::Canceled);
    QVERIFY(ApiClientTestAccess::suspended(host.api, challenged));
    QVERIFY(host.verification.open());
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Account
ApplicationWindow {id:win;required property VerificationService verification;width:700;height:500;visible:true;VerificationOverlay {anchors.fill:parent;service:win.verification}})",
                                  {{"verification", QVariant::fromValue(&host.verification)}});
    QVERIFY(window);
    challenge->cancel();
    QVERIFY(!host.verification.open());
    auto *standalone = scope->kugou()->verify("standalone-event");
    auto standaloneRoot = host.engine.newQObject(standalone);
    QTRY_VERIFY(host.verification.open());
    ApiClientTestAccess::verified(host.api, "standalone-event");
    QCOMPARE(standalone->state(), EchoPluginOperation::Succeeded);
    QVERIFY(standalone->result().toMap().value("verified").toBool());
    auto *login = scope->kugou()->verify("session-bound-login");
    auto loginRoot = host.engine.newQObject(login);
    QTRY_VERIFY(host.verification.open());
    QVERIFY(host.api.awaitLoginVerification(ApiClientTestAccess::active(host.api)));
    QCOMPARE(login->state(), EchoPluginOperation::Failed);
    QCOMPARE(login->errorCode(), "login_required");
    auto *old = scope->kugou()->request("userInfo");
    auto oldRoot = host.engine.newQObject(old);
    QCOMPARE(old->state(), EchoPluginOperation::Running);
    QSignalSpy accountChanged(scope->kugou(), &EchoPluginKugou::accountChanged);
    host.api.setAccount(43, "another-token");
    QTRY_VERIFY(!accountChanged.isEmpty());
    QCOMPARE(scope->kugou()->account().value("userId").toString(), "43");
    QCOMPARE(old->state(), EchoPluginOperation::Canceled);
    auto *current = scope->kugou()->request("userInfo");
    auto currentRoot = host.engine.newQObject(current);
    auto currentId = ApiClientTestAccess::pending(host.api, "user_info");
    QVERIFY(currentId);
    QCOMPARE(ApiClientTestAccess::request(host.api, currentId)
                 .value("identity")
                 .toObject()
                 .value("userid")
                 .toInteger(),
             qint64(43));
    host.runtime.setEnabled(host.runtime.installationForId("ecosystem"), false);
    QTRY_COMPARE(current->state(), EchoPluginOperation::Canceled);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("ecosystem")),
                 PluginRuntime::Disabled);
    QCOMPARE(ApiClientTestAccess::pending(host.api, "user_info"), quint64(0));
}
