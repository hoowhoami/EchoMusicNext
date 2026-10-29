#pragma once
#include "ArtistDetailService.h"
#include "DetailsService.h"
#include "AlbumDetailService.h"
#include <ctime>
#include <limits>
#include <QScopeGuard>
#include <utility>
#include "AccountUiFixture.h"
#include "NativeWindow.h"
#include "ProfileRouteCoordinator.h"
#include "SidebarRouteCoordinator.h"
#include "ProfileService.h"
#include "PlaylistOrderModel.h"
#include "PlaylistImportTaskCoordinator.h"
#include "PlaybackService.h"
#include "StartupController.h"
#include "StorageService.h"
#include "DeviceRegistrationService.h"
#include "SearchModel.h"
#include "SearchRouteCoordinator.h"
#include "SettingsCatalog.h"
#include "ShortcutService.h"
#include "AboutService.h"
#include "SettingsDataService.h"
#include "ExperimentalSettingsService.h"
#include "PluginRuntime.h"
#include "PluginManager.h"
#include "NetworkBootstrap.h"
#include "LogService.h"
#include "RelaunchService.h"
#include "ShellController.h"
#include "PlaylistDetailService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "UiTypography.h"
#include "WindowController.h"
#include "StartupIntegration.h"
#include <QJsonArray>
#include <QMetaEnum>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubator>
#include <QQmlProperty>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDataStream>
#include <QClipboard>
#include <QCursor>
#include <QProcess>
#include <atomic>
#include <chrono>
#include <QMimeData>
#include <iostream>
#include <memory>
#include <QUuid>
#include <QtTest>
using namespace EchoMusic;
#ifdef Q_OS_MACOS
QVariantMap nativeWindowAppearance(QWindow *window);
QVariantMap nativeSystemMediaInfo();
QVariantMap nativeFloatingAppearance(QWindow *window);
QVariantMap nativeMacComposition(QWindow *window, qint64 hostWindow);
#endif
namespace EchoMusic
{
// Domain-state seam for visual selected-state checks, not a provider response fixture.
class PlaybackLibraryActionsTestAccess
{
public:
    static void favorite(PlaybackLibraryActions &actions, const PlaybackTrack &track, bool value)
    {
        if (value)
            actions.m_favorites.insert(track.hash.toUpper(), 1);
        else
            actions.m_favorites.remove(track.hash.toUpper());
        emit actions.changed();
    }
};
class TrackTransitionControllerTestAccess
{
public:
    static bool prepared(const TrackTransitionController &controller)
    {
        return controller.m_prepared;
    }
};
class AccountServiceTestAccess
{
public:
    static QJsonObject membershipDomain(const AccountService &account)
    {
        return account.m_membership->domain();
    }
    static bool membershipKnown(const AccountService &account)
    {
        return account.m_membership->facts().known;
    }
    static QByteArray snapshot(const AccountService &account)
    {
        return account.storedSession();
    }
    static bool adopt(AccountService &account, const QByteArray &payload)
    {
        return account.adoptStoredSession(payload);
    }
    // Domain-only display/lifecycle seam, not a fabricated upstream sample.
    static quint64 profileRequest(AccountService &account, ApiClient &api)
    {
        return account.m_detail = api.capabilities();
    }
    static quint64 vipRequest(AccountService &account, ApiClient &api)
    {
        account.m_membership->m_revision = api.sessionRevision();
        return account.m_membership->m_request = api.capabilities();
    }
    static quint64 identityRequest(AccountService &account, ApiClient &api)
    {
        return account.m_identity = api.capabilities();
    }
    static quint64 gradeRequest(AccountService &account, ApiClient &api)
    {
        return account.m_gradeRequest = api.capabilities();
    }
    static void complete(AccountService &account, quint64 id, const QByteArray &payload)
    {
        if (id && id == account.m_membership->m_request)
            account.m_membership->succeed(id, payload);
        else
            account.succeed(id, payload);
    }
};
class PlaylistOrderModelTestAccess
{
public:
    static quint64 request(const PlaylistOrderModel &model)
    {
        return model.m_request;
    }
    static void complete(PlaylistOrderModel &model, quint64 id, const QByteArray &payload)
    {
        model.succeed(id, payload);
    }
    static QList<ApiClient::PlaylistPosition> positions(const PlaylistOrderModel &model)
    {
        return model.positions();
    }
};
class PlaybackServiceTestAccess
{
public:
    static AudioEngine &engine(PlaybackService &playback)
    {
        return playback.m_engine;
    }
    // Paused presentation input, independent of native playback/clock accuracy.
    static void position(PlaybackService &playback, qint64 value, bool reset = true)
    {
        playback.m_position = value;
        playback.lyricText()->setPosition(value);
        if (reset)
            emit playback.timelineReset();
        emit playback.positionChanged();
    }
    static void sourcePresentation(PlaybackService &p, const QString &kind)
    {
        quality(p, "standard");
        p.m_sourceKind = kind;
        p.m_loading = false;
        p.m_privileges = {{"variants", QJsonArray{QJsonObject{{"quality", "128"},
                                                              {"hash", p.m_track.hash},
                                                              {"privilege", 0},
                                                              {"service_status", 1}}}}};
        p.m_track.catalogMetadata = {{"cloud_source", QJsonObject{{"hash", p.m_track.hash},
                                                                  {"hash_std", p.m_track.hash},
                                                                  {"size_bytes", 23456789}}}};
        emit p.changed();
    }
    // Actual public lyric API with presentation metadata, independent of audio URL authorization.
    static void lyricMetadata(PlaybackService &playback, const PlaybackTrack &track)
    {
        playback.m_track = track;
        playback.m_duration = qint64(track.durationMs);
        playback.m_lyricSource.load(track);
        emit playback.changed();
    }
    // Presentation-only domain state; live source authorization is tested separately.
    static void quality(PlaybackService &playback, const QString &resolved)
    {
        playback.m_track = {};
        playback.m_track.id = "quality-presentation";
        playback.m_track.hash = "105A2705BEEE36A8BED029A8E1A6311D";
        playback.m_track.title = "Quality presentation";
        playback.m_quality = "viper_tape";
        playback.m_resolvedQuality = resolved;
        emit playback.changed();
    }
};
class PlaylistImportServiceTestAccess
{
public:
    // Canonical pending business state, not a fabricated upstream response.
    static void awaitDuplicate(PlaylistImportService &service, const QString &title)
    {
        service.m_phase = PlaylistImportService::Preparing;
        service.m_stage = PlaylistImportService::None;
        service.m_duplicate = title;
        emit service.changed();
        emit service.duplicateConfirmationRequested();
    }
    static void pausePoll(PlaylistImportService &service)
    {
        service.m_poll.stop();
    }
    static void poll(PlaylistImportService &service)
    {
        service.poll();
    }
    static quint64 target(const PlaylistImportService &service)
    {
        return service.m_target;
    }
    static quint64 request(const PlaylistImportService &service)
    {
        return service.m_request;
    }
    static void complete(PlaylistImportService &service, const QByteArray &payload)
    {
        const auto request = service.m_request;
        service.m_api.cancel(request);
        service.succeed(request, payload);
    }
    static void stale(PlaylistImportService &service, quint64 request, const QByteArray &payload)
    {
        service.succeed(request, payload);
    }
    static void prepareScreenshot(PlaylistImportService &service, quint64 target,
                                  const QString &title)
    {
        QVERIFY(service.beginRun());
        service.m_target = target;
        service.m_title = title;
        service.m_images = {QByteArray("domain-image")};
        service.beginCatalog(target == 0);
    }
};
class UserPlaylistModelTestAccess
{
public:
    static void complete(UserPlaylistModel &model, ApiClient &api, const QByteArray &payload)
    {
        model.m_sessionRevision = api.sessionRevision();
        model.m_request = api.capabilities();
        const auto id = model.m_request;
        api.cancel(id);
        model.succeed(id, payload);
    }
    static void present(UserPlaylistModel &model, ApiClient &api, int count)
    {
        if (model.m_request)
            api.cancel(model.m_request);
        model.m_request = 0;
        model.beginResetModel();
        model.m_rows.clear();
        for (int i = 0; i < count; ++i) {
            UserPlaylistModel::Row row;
            row.cloudId = {quint64(i + 1)};
            row.globalId = QString("display-playlist-%1").arg(i + 1);
            row.creatorGlobalId = row.globalId;
            row.creatorId = 900001;
            row.title = i == 0   ? QStringLiteral("默认收藏")
                        : i == 1 ? QStringLiteral("我喜欢")
                                 : QString("领域歌单 %1").arg(i + 1);
            row.sourceCode = 1;
            row.sortIndex = i;
            row.editable = true;
            if (i < 2)
                row.defaultKind = i + 1;
            model.m_rows.append(row);
        }
        model.m_sessionRevision = api.sessionRevision();
        model.m_total = count;
        model.m_exhausted = true;
        model.m_state = UserPlaylistModel::State::Ready;
        model.endResetModel();
        emit model.changed();
    }
    static void collectSecond(UserPlaylistModel &model)
    {
        auto &row = model.m_rows[1];
        row.kind = QStringLiteral("collected");
        row.collected = true;
        row.globalId = QStringLiteral("history-collected-local");
        row.creatorGlobalId = QStringLiteral("history-collected-public");
        row.title = QStringLiteral("收藏联动歌单");
        row.defaultKind.reset();
        row.editable = false;
        emit model.dataChanged(model.index(1), model.index(1), {});
        emit model.changed();
    }
};
class SavedAccountModelTestAccess
{
public:
    static void present(SavedAccountModel &model, const StoredAccounts &rows,
                        const QByteArray &session)
    {
        // Isolate view geometry from persistence. Do not enqueue a persistent account
        // write when the account display fixture announces its session.
        Q_ASSERT(!model.m_ready);
        model.m_queue.clear();
        model.m_lastSession = session;
        model.publish(rows);
    }
};
class PlaylistMutationServiceTestAccess
{
public:
    static quint64 request(PlaylistMutationService &s)
    {
        return s.m_request;
    }
    static void reject(PlaylistMutationService &s)
    {
        s.fail(s.m_request, R"({"kind":"business"})");
    }
};
class PlaylistDetailContextTestAccess
{
public:
    static void seed(PlaylistDetailContext &c)
    {
        c.m_detail = {
            {"global_id", "public-playlist"}, {"title", "歌单收藏测试"},   {"creator_id", 42},
            {"creator_list_id", 287},         {"creator_name", "Creator"}, {"source_code", 1}};
        c.m_complete = true;
        emit c.changed();
    }
};
class AlbumDetailContextTestAccess
{
public:
    static void seed(AlbumDetailContext &c)
    {
        c.m_detail = {{"id", 960399},
                      {"title", "专辑收藏测试"},
                      {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "周杰伦"}}}}};
        c.m_complete = true;
        emit c.changed();
    }
};
class ArtistDetailContextTestAccess
{
public:
    static void seed(ArtistDetailContext &c)
    {
        c.m_detail = {{"id", 3060},
                      {"name", "歌手关注测试"},
                      {"fans", 12450},
                      {"description", "Artist description"}};
        c.m_complete = true;
        emit c.changed();
    }
};
class ProfileServiceTestAccess
{
public:
    static void seed(ProfileService &p, int tab, QJsonArray rows)
    {
        const auto old = std::exchange(p.m_social[tab].request, 0);
        if (old)
            p.m_api.cancel(old);
        p.m_social[tab].loaded = true;
        p.m_social[tab].model->replace(rows);
        emit p.changed();
    }
    static quint64 followWrite(ProfileService &p)
    {
        return p.m_followWrites.isEmpty() ? 0 : p.m_followWrites.keys().first();
    }
    static quint64 chatRead(ProfileService &p)
    {
        return p.m_chatRead;
    }
    static quint64 chatWrite(ProfileService &p)
    {
        return p.m_chatWrite;
    }
    // Feed the already-validated canonical bridge contract, without network
    // writes or inventing an upstream response fixture.
    static quint64 socialRequest(ProfileService &service, ApiClient &api, int tab)
    {
        return service.m_social[tab].request = api.capabilities();
    }
    static quint64 deviceRequest(ProfileService &service, ApiClient &api)
    {
        return service.m_deviceRequest = api.capabilities();
    }
    static quint64 deviceKick(const ProfileService &service)
    {
        return service.m_deviceKick;
    }
    static quint64 deviceRead(const ProfileService &service)
    {
        return service.m_deviceRequest;
    }
    static quint64 saveRequest(ProfileService &service, ApiClient &api,
                               const ApiClient::ProfilePatch &patch)
    {
        service.m_patch = patch;
        return service.m_save = api.capabilities();
    }
    static quint64 avatarPending(const ProfileService &service)
    {
        return service.m_avatarRequest;
    }
    static quint64 avatarRequest(ProfileService &service, ApiClient &api)
    {
        return service.m_avatarRequest = api.capabilities();
    }
    static void complete(ProfileService &service, quint64 id, const QByteArray &payload)
    {
        service.succeeded(id, payload);
    }
    static void fail(ProfileService &service, quint64 id, const QByteArray &payload)
    {
        service.failed(id, payload);
    }
};
class ContentBlacklistTestAccess
{
public:
    static quint64 singerWrite(const ContentBlacklistService &service)
    {
        return service.m_singerWrite;
    }
    static quint64 read(ContentBlacklistService &service, ApiClient &api, int category,
                        int page = 1)
    {
        auto &b = service.m_buckets[category];
        if (b.request)
            api.cancel(std::exchange(b.request, 0));
        b.requestedPage = page;
        return b.request = api.capabilities();
    }
    static quint64 hold(ContentBlacklistService &service, ApiClient &api, int category)
    {
        auto &b = service.m_buckets[category];
        if (b.request)
            api.cancel(b.request);
        b.requestedPage = 1;
        b.error.clear();
        b.request = std::numeric_limits<quint64>::max() - category;
        emit service.changed();
        return b.request;
    }
    static quint64 request(const ContentBlacklistService &service, int category)
    {
        return service.m_buckets[category].request;
    }
    static quint64 write(const ContentBlacklistService &service)
    {
        return service.m_write;
    }
    static void complete(ContentBlacklistService &service, ApiClient &api, quint64 id,
                         const QByteArray &bytes)
    {
        api.cancel(id);
        service.succeeded(id, bytes);
    }
    static void fail(ContentBlacklistService &service, ApiClient &api, quint64 id)
    {
        api.cancel(id);
        service.failed(id, R"({"message":"呈现回归中的请求失败"})");
    }
    static quint64 songIndexRequest(const ContentBlacklistService &service)
    {
        return service.m_songIndex.request;
    }
    static quint64 menuWrite(const ContentBlacklistService &service)
    {
        return service.m_menuWrite;
    }
};
class ApiClientTestAccess
{
public:
    static QJsonObject request(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request;
    }
    static bool automaticVerification(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).automaticVerification;
    }
};
class ListeningPreferencesTestAccess
{
public:
    static quint64 read(ListeningPreferencesService &service, ApiClient &api)
    {
        return service.m_read = api.capabilities();
    }
    static void complete(ListeningPreferencesService &service, quint64 id, const QByteArray &bytes)
    {
        service.succeeded(id, bytes);
    }
    static quint64 submitted(ListeningPreferencesService &service, ApiClient &api)
    {
        service.m_submitted = service.m_draft;
        return service.m_save = api.capabilities();
    }
    static void fail(ListeningPreferencesService &service, quint64 id)
    {
        service.failed(id);
    }
    static ApiClient::PreferencePatch patch(const ListeningPreferencesService &service)
    {
        return ListeningPreferencesService::changes(service.m_baseline, service.m_draft);
    }
};
} // namespace EchoMusic
namespace
{
[[maybe_unused]] QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
[[maybe_unused]] QList<QQuickItem *> visualAll(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> result;
    if (root->objectName() == name)
        result.append(root);
    for (auto *child : root->childItems())
        result.append(visualAll(child, name));
    return result;
}
[[maybe_unused]] QQuickItem *popupScrollBarFor(QQuickWindow *window, QQuickItem *view)
{
    for (auto *bar : visualAll(window->contentItem(), "popupScrollBar"))
        if (bar->property("view").value<QQuickItem *>() == view)
            return bar;
    return nullptr;
}
[[maybe_unused]] void verifyPopupScrollMapping(QQuickWindow *window, QQuickItem *view)
{
    auto *bar = popupScrollBarFor(window, view);
    QVERIFY(bar);
    QTRY_COMPARE(bar->property("orientation").toInt(), int(Qt::Vertical));
    QTRY_VERIFY(qAbs(bar->property("size").toReal() -
                     QQmlProperty(view, "visibleArea.heightRatio").read().toReal()) < 0.001);
    QTRY_VERIFY(qAbs(bar->property("position").toReal() -
                     QQmlProperty(view, "visibleArea.yPosition").read().toReal()) < 0.001);
    QTRY_VERIFY(view->property("contentHeight").toReal() > view->height() + 10);
    view->setProperty("contentY", view->property("originY"));
    QTRY_VERIFY(bar->property("position").toReal() < 0.001);
    // Qt's attached bar must track both scrolling the content and dragging the
    // reparented visual thumb. Geometry alone does not establish this connection.
    view->setProperty("contentY", view->property("originY").toReal() + 30);
    QTRY_VERIFY(bar->property("position").toReal() > 0);
    QTRY_VERIFY(qAbs(bar->property("position").toReal() -
                     QQmlProperty(view, "visibleArea.yPosition").read().toReal()) < 0.001);
    view->setProperty("contentY", view->property("originY"));
    QTRY_VERIFY(bar->property("position").toReal() < 0.001);
    auto *thumb = visual(bar, "scrollThumb");
    QVERIFY(thumb);
    QVERIFY(Test::prepareNativeInput(window));
    QTest::qWait(40);
    const auto from = thumb->mapToScene({thumb->width() / 2, thumb->height() / 2}).toPoint();
    const auto to = bar->mapToScene({bar->width() / 2, bar->height() - 10}).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(window, to, 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    QTRY_VERIFY(view->property("contentY").toReal() > view->property("originY").toReal() + 10);
    QTRY_VERIFY(qAbs(bar->property("position").toReal() -
                     QQmlProperty(view, "visibleArea.yPosition").read().toReal()) < 0.001);
    view->setProperty("contentY", view->property("originY"));
}
[[maybe_unused]] QQuickItem *visualText(QQuickItem *root, const QString &text)
{
    if (root->isVisible() && root->property("text").toString() == text &&
        root->property("color").isValid())
        return root;
    for (auto *child : root->childItems())
        if (auto *found = visualText(child, text))
            return found;
    return nullptr;
}
[[maybe_unused]] qreal glyphCenterY(QQuickWindow *window, const QImage &image, QQuickItem *row)
{
    auto *text = row->property("textItem").value<QQuickItem *>();
    if (!text)
        text = row;
    const qreal ratio = image.width() / qreal(window->width());
    const auto bounds = text->mapRectToScene(QRectF(0, 0, text->width(), text->height()));
    const auto pixels = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                            .toAlignedRect()
                            .intersected(image.rect());
    QHash<QRgb, int> frequencies;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x)
            ++frequencies[image.pixel(x, y)];
    QRgb background = 0;
    int count = 0;
    for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it)
        if (it.value() > count) {
            background = it.key();
            count = it.value();
        }
    int top = image.height(), bottom = -1;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            const auto pixel = image.pixel(x, y);
            if (std::max({qAbs(qRed(pixel) - qRed(background)),
                          qAbs(qGreen(pixel) - qGreen(background)),
                          qAbs(qBlue(pixel) - qBlue(background))}) > 25) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    return bottom >= top ? (top + bottom + 1) / (2 * ratio) : qQNaN();
}
class FloatingFrameProbe final : public QObject
{
    struct Counters
    {
        std::atomic<qint64> begin{0}, renderBegin{0}, peak{0}, syncPeak{0};
        std::atomic<quint64> frames{0};
    };

public:
    explicit FloatingFrameProbe(QQuickWindow *window) : QObject(window)
    {
        const auto counters = m_counters;
        connect(
            window, &QQuickWindow::beforeSynchronizing, this,
            [counters] { counters->begin.store(now()); }, Qt::DirectConnection);
        connect(
            window, &QQuickWindow::afterSynchronizing, this,
            [counters] {
                const auto begin = counters->begin.exchange(0);
                if (!begin)
                    return;
                const auto span = now() - begin;
                auto peak = counters->syncPeak.load();
                while (span > peak && !counters->syncPeak.compare_exchange_weak(peak, span)) {
                }
            },
            Qt::DirectConnection);
        connect(
            window, &QQuickWindow::beforeRendering, this,
            [counters] { counters->renderBegin.store(now()); }, Qt::DirectConnection);
        connect(
            window, &QQuickWindow::afterRendering, this,
            [counters] {
                // Separate GUI synchronization waits from render recording,
                // and consume each phase once when a scene is reused.
                const auto begin = counters->renderBegin.exchange(0);
                if (!begin)
                    return;
                const auto span = now() - begin;
                ++counters->frames;
                auto peak = counters->peak.load();
                while (span > peak && !counters->peak.compare_exchange_weak(peak, span)) {
                }
            },
            Qt::DirectConnection);
    }
    quint64 frames() const
    {
        return m_counters->frames.load();
    }
    double maximumMs() const
    {
        return m_counters->peak.load() / 1000.;
    }
    double maximumSyncMs() const
    {
        return m_counters->syncPeak.load() / 1000.;
    }

private:
    static qint64 now()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    const std::shared_ptr<Counters> m_counters = std::make_shared<Counters>();
};
[[maybe_unused]] qint64 floatingRssKiB()
{
#ifdef Q_OS_UNIX
    QProcess process;
    process.start("ps", {"-o", "rss=", "-p", QString::number(QCoreApplication::applicationPid())});
    if (process.waitForFinished(1000))
        return process.readAllStandardOutput().trimmed().toLongLong();
#endif
    return -1;
}
[[maybe_unused]] bool writeFloatingTestWave(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 48000 * 16 * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    for (int i = 0; i < 48000 * 16; ++i)
        stream << qint16(std::sin(i * 2. * 3.141592653589793 * 220 / 48000) * 800);
    return stream.status() == QDataStream::Ok;
}
struct Session
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore preferences{directory.filePath("preferences.ini")};
    ThemeStore theme{preferences};
    ShellSettings shell{preferences};
    WindowController windows;
    ApiClient api{DeviceIdentity::temporary()};
    Test::AccountUiFixture accounts{api};
    ProfileService profile{api, accounts.account};
    SearchModel search{api};
    RouteController router;
    SidebarRouteCoordinator sidebarRoutes{accounts.library, accounts.playlists, router};
    ShellController commands{preferences, router};
    SearchRouteCoordinator searchRoutes{search, router, false};
    ProfileRouteCoordinator profileRoutes{accounts.controller, profile, router, false};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    QString initialRoute = QStringLiteral("profile");
    // Native presentation tests opt in with temporary settings; ordinary
    // fixtures retain their non-native window/backdrop/startup behavior.
    explicit Session(bool nativeWindows = false) : windows(preferences, nativeWindows) {}
    bool settleScene()
    {
        // Zoom and capacity changes polish layouts and rebuild projections on
        // the next frame. Never click geometry from the preceding scene.
        QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        return rendered.wait(1000);
    }
    bool settingsSectionReady()
    {
        // Object names become discoverable during asynchronous incubation,
        // before bindings, parents and the enclosing viewport are complete.
        if (!QTest::qWaitFor([&] {
                auto *loader = visual(window->contentItem(), "settingsSectionLoader");
                return loader && loader->property("active").toBool() &&
                       loader->property("status").toInt() == 1 &&
                       loader->property("item").value<QQuickItem *>();
            }))
            return false;
        return settleScene();
    }
    bool revealItem(QQuickItem *item)
    {
        QPointer<QQuickItem> retained(item);
        if (!settleScene() || !retained)
            return false;
        for (auto *parent = retained->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->inherits("QQuickFlickable") ||
                parent->property("contentHeight").toReal() <= parent->height() + 1)
                continue;
            auto *content = parent->property("contentItem").value<QQuickItem *>();
            if (!content)
                return false;
            const auto bounds = retained->mapRectToItem(content, retained->boundingRect());
            const qreal current = parent->property("contentY").toReal();
            if (bounds.top() >= current && bounds.bottom() <= current + parent->height())
                continue;
            QMetaObject::invokeMethod(parent, "cancelFlick");
            const qreal first =
                parent->property("originY").toReal() - parent->property("topMargin").toReal();
            const qreal last =
                qMax(first, parent->property("originY").toReal() +
                                parent->property("contentHeight").toReal() - parent->height() +
                                parent->property("bottomMargin").toReal());
            parent->setProperty("contentY",
                                qBound(first, bounds.center().y() - parent->height() / 2, last));
            if (!settleScene() || !retained)
                return false;
        }
        return true;
    }
    bool clickItem(QQuickItem *item)
    {
        QPointer<QQuickItem> retained(item);
        if (!Test::prepareNativeInput(window) || !revealItem(item))
            return false;
        QRectF previous;
        int stable = 0;
        for (int frame = 0; frame < 60 && stable < 2; ++frame) {
            if (!settleScene() || !retained)
                return false;
            const auto bounds = retained->mapRectToScene(retained->boundingRect());
            stable = bounds == previous ? stable + 1 : 0;
            previous = bounds;
        }
        if (stable < 2) {
            qWarning() << "Test trigger geometry did not settle" << retained->objectName()
                       << previous;
            return false;
        }
        item = retained;
        if (!item || !item->isVisible() || !item->isEnabled()) {
            qWarning() << "Test trigger unavailable" << (item ? item->objectName() : QString())
                       << (item && item->isVisible()) << (item && item->isEnabled());
            return false;
        }
        const auto point =
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        if (!window->contentItem()->boundingRect().contains(point)) {
            qWarning() << "Test trigger outside window" << item->objectName() << point
                       << window->size();
            return false;
        }
        Test::moveNativePointer(window, point);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        return true;
    }
    bool clickItem(const QString &name)
    {
        if (!settleScene())
            return false;
        QQuickItem *item = nullptr;
        if (!QTest::qWaitFor([&] {
                item = visual(window->contentItem(), name);
                return item && item->isVisible() && item->isEnabled();
            }))
            return false;
        return clickItem(item);
    }
    bool clickPlayerAction(const QString &id)
    {
        if (!settleScene())
            return false;
        QQuickItem *item = nullptr;
        (void)QTest::qWaitFor(
            [&] {
                item = visual(window->contentItem(), "playerbar-action-" + id);
                return item && item->isVisible();
            },
            500);
        if (item && item->isVisible())
            return clickItem(item);
        if (!clickItem("playerbarMoreAction"))
            return false;
        const auto *actions = commands.playerbarActions();
        for (int row = 0; row < actions->rowCount(); ++row)
            if (actions->index(row, 0).data(ShellActionModel::IdRole).toString() == id)
                return clickItem("playerbarMenuAction-" +
                                 actions->index(row, 0).data(ShellActionModel::KeyRole).toString());
        return false;
    }
    QPointer<QObject> openPlayerPopup(const QString &action, const QString &name)
    {
        QSignalSpy requested(&commands, &ShellController::playbackPopupRequested);
        if (!clickPlayerAction(action))
            return {};
        if (!QTest::qWaitFor([&] { return !requested.isEmpty(); })) {
            qWarning() << "Player trigger delivered no popup request" << action
                       << accounts.controller.loginOpen() << shell.uiScale();
            return {};
        }
        QPointer<QObject> popup;
        if (!QTest::qWaitFor([&] {
                popup = root->findChild<QObject *>(name);
                return popup && popup->property("opened").toBool();
            })) {
            auto *owner = root->findChild<QObject *>(name + "Owner");
            qWarning() << "Popup request did not open" << action << name << requested << "owner"
                       << owner << "available"
                       << (owner ? owner->property("available") : QVariant()) << "item"
                       << (owner ? owner->property("item") : QVariant());
            return {};
        }
        return popup;
    }
    QPointer<QObject> openOwnedPopup(const QString &name, const QVariantMap &properties = {})
    {
        QObject *owner = nullptr;
        QSet<QObject *> seen;
        const auto visit = [&](auto &&self, QObject *object) -> void {
            if (!object || owner || seen.contains(object))
                return;
            seen.insert(object);
            if (object->objectName() == name && object->property("available").toBool()) {
                owner = object;
                return;
            }
            for (auto *child : object->children())
                self(self, child);
            if (auto *visual = qobject_cast<QQuickItem *>(object))
                for (auto *child : visual->childItems())
                    self(self, child);
        };
        visit(visit, root.data());
        if (!owner ||
            !QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariant(properties))))
            return {};
        QPointer<QObject> popup = owner->property("item").value<QObject *>();
        if (!QTest::qWaitFor([&] { return popup && popup->property("opened").toBool(); }))
            return {};
        return popup;
    }
    bool create(PlaybackService *playback = nullptr, NetworkPreferences *network = nullptr,
                LogService *logging = nullptr, RelaunchService *relaunch = nullptr,
                StartupController *startup = nullptr,
                ExperimentalSettingsService *experimental = nullptr)
    {
        if (playback) {
            playback->attachLibrary(accounts.playlists, accounts.mutations);
            commands.attachPlayback(*playback);
            commands.attachWindows(windows);
            playback->songCommands()->attach(*profile.blacklist(), accounts.account);
        }
        // Page wiring forwards profile editor feedback to the shared toast
        // surface; the fixture creates the same owners without PageWiring.
        QObject::connect(&profile, &ProfileService::editNotice, &commands,
                         [&](const QString &message, int tone) {
                             commands.toasts()->show(message, ToastService::Tone(tone));
                         });
        router.navigate(initialRoute);
        theme.attachToEngine(engine);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        root.reset(component.createWithInitialProperties(
            {{"experimentalSettingsService", QVariant::fromValue(experimental)},
             {"startup", QVariant::fromValue(startup)},
             {"playbackService", QVariant::fromValue(playback)},
             {"networkPreferences", QVariant::fromValue(network)},
             {"logService", QVariant::fromValue(logging)},
             {"relaunchService", QVariant::fromValue(relaunch)},
             {"libraryController", QVariant::fromValue(&accounts.library)},
             {"accountController", QVariant::fromValue(&accounts.controller)},
             {"shellController", QVariant::fromValue(&commands)},
             {"searchService", QVariant::fromValue(&search)},
             {"router", QVariant::fromValue(&router)},
             {"shellSettings", QVariant::fromValue(&shell)},
             {"windowService", QVariant::fromValue(&windows)}}));
        if (!root) {
            qWarning().noquote() << component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        windows.attach(window);
        return window;
    }
    ~Session()
    {
        router.shutdown();
        windows.shutdown();
        search.shutdown();
        api.shutdown();
        if (window)
            window->close();
        root.reset();
    }
};
[[maybe_unused]] QByteArray account(quint64 id)
{
    return QString("{\"user_id\":%1,\"token\":\"offline-display-fixture\",\"nickname\":\"Qt "
                   "测试账号\",\"avatar_url\":\"\"}")
        .arg(id)
        .toUtf8();
}
[[maybe_unused]] QByteArray socialRows(int count)
{
    QJsonArray users;
    for (int i = 0; i < count; ++i)
        users.append(QJsonObject{
            {"key", QString("user:%1").arg(i + 1)},
            {"user_id", i + 1},
            {"artist_id", 0},
            {"nickname", QString("领域列表用户 %1").arg(i + 1)},
            {"avatar_url", ""},
            {"description", "领域展示压力测试"},
            {"following", true},
            {"mutual", false},
            {"identity",
             i == 0 ? QJsonObject{{"talent_bits", 3}, {"singer", false}} : QJsonObject{}},
            {"membership", i == 0 ? QJsonObject{{"deluxe_active", true}} : QJsonObject{}}});
    return QJsonDocument(QJsonObject{{"users", users}, {"total", count}}).toJson();
}
[[maybe_unused]] QByteArray preferencesDomain()
{
    const auto weight = [](QJsonObject values) {
        return QJsonObject{
            {"encoded", QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact))},
            {"weights", values}};
    };
    return QJsonDocument(QJsonObject{{"recommendation_mode", ""},
                                     {"gender", "other-version"},
                                     {"age", ""},
                                     {"recommendation", weight({{"125", 100}})},
                                     {"languages", weight({{"2", 100}})},
                                     {"styles", weight({})}})
        .toJson();
}
[[maybe_unused]] int visualCount(QQuickItem *root, const QString &name)
{
    int count = root->objectName() == name ? 1 : 0;
    for (auto *child : root->childItems())
        count += visualCount(child, name);
    return count;
}
} // namespace
