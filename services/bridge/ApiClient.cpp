#include "ApiClient.h"
#include "ApiDomains_p.h"
#include "echo_ffi.h"

#include <QCoreApplication>
#include <QTimer>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <limits>
#include <stdexcept>
#include <utility>

namespace EchoMusic
{
namespace
{
// This object has no parent: shutdown owns its lifetime beyond ApiClient deletion.
class Dispatcher final : public QObject
{
public:
    QPointer<ApiClient> receiver;
};
} // namespace

ApiClient::ApiClient(QObject *parent) : ApiClient(DeviceIdentity::temporary(), parent) {}

ApiClient::ApiClient(DeviceIdentity device, QObject *parent)
    : QObject(parent), m_device(std::move(device))
{
    m_auth = std::make_unique<AuthClient>(*this);
    m_catalog = std::make_unique<CatalogClient>(*this);
    m_cloud = std::make_unique<CloudClient>(*this);
    m_social = std::make_unique<SocialClient>(*this);
    m_library = std::make_unique<LibraryClient>(*this);
    m_media = std::make_unique<MediaClient>(*this);
    auto dispatcher = new Dispatcher;
    dispatcher->receiver = this;
    m_dispatcher = dispatcher;
    initializeRuntime();
}
bool ApiClient::initializeRuntime()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || echo_abi_version() != 1)
        return false;
    if (!m_runtime)
        m_runtime = echo_runtime_create();
    return m_runtime != nullptr;
}

ApiClient::~ApiClient()
{
    shutdown();
}

quint64 ApiClient::capabilities()
{
    return submit({{"operation", "capabilities"}}, false);
}
bool ApiClient::setNetworkTimeout(double seconds)
{
    Q_ASSERT(QThread::currentThread() == thread());
    return !m_closing && echo_runtime_api_timeout(m_runtime, seconds);
}
bool ApiClient::setNetworkTransport(quint16 loopbackPort)
{
    return !m_closing && echo_runtime_network_transport(m_runtime, loopbackPort);
}

quint64 ApiClient::submit(QJsonObject request, bool includeIdentity, RequestOptions options)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing)
        return 0;
    if (includeIdentity) {
        const auto &device = options.authentication == AuthenticationMode::IndependentDevice
                                 ? m_independentDevice
                                 : m_device;
        const bool account = options.authentication == AuthenticationMode::CurrentAccount;
        const QString mode = account ? QStringLiteral("current_account")
                             : options.authentication == AuthenticationMode::DeviceOnly
                                 ? QStringLiteral("device_only")
                                 : QStringLiteral("independent_device");
        request.insert(QStringLiteral("identity"),
                       QJsonObject{{"userid", account ? qint64(m_userId) : 0},
                                   {"token", account ? m_token : QString()},
                                   {"t1", account ? m_t1 : QString()},
                                   {"mid", device.mid},
                                   {"dfid", device.dfid},
                                   {"guid", device.guid},
                                   {"mac", device.mac},
                                   {"device_name", device.deviceName},
                                   {"uuid", device.uuid},
                                   {"render_fingerprint", device.renderFingerprint},
                                   {"authentication", mode}});
    }
    Pending pending;
    pending.revision = m_revision;
    pending.request = std::move(request);
    pending.automaticVerification = options.automaticVerification;
    pending.replayAfterVerification = options.replayAfterVerification;
    const auto operation = pending.request.value("operation").toString();
    // Authentication and proof commands never expose credentials to middleware.
    const bool protectedOperation = operation.startsWith("login") || operation == "register_dev" ||
                                    operation.startsWith("captcha") || operation == "user_verify";
    pending.transportIntercept = !m_transportInterceptors.isEmpty() && !m_pluginRequestDepth &&
                                 !protectedOperation && includeIdentity;
    if (!m_interceptors.isEmpty() && !m_pluginRequestDepth && !protectedOperation &&
        includeIdentity) {
        const auto id = ++m_interceptRequest;
        const auto revision = m_revision;
        pending.logicalId = id;
        pending.sessionBound = true;
        auto *chain = new ApiInterceptChain(m_interceptors.values(), this);
        m_intercepted.insert(id, chain);
        QTimer::singleShot(0, chain, [this, chain, pending, id, revision] {
            if (m_closing || revision != m_revision || !m_intercepted.contains(id))
                return;
            chain->run(
                pending.request,
                [this, pending, id](QJsonObject request, ApiInterceptDone done) mutable {
                    const auto original = pending.request;
                    pending.request = std::move(request);
                    m_interceptForward[id] = std::move(done);
                    auto native = submitPrepared(pending);
                    if (!native && pending.request != original) {
                        pending.request = original;
                        native = submitPrepared(pending);
                    }
                    if (!native)
                        finishRequest(id, EchoStatus::Closed,
                                      QByteArrayLiteral("{\"kind\":\"closed\"}"));
                },
                [this, id, revision](ApiInterceptReply reply) {
                    auto chain = m_intercepted.take(id);
                    m_interceptForward.remove(id);
                    if (chain)
                        chain->deleteLater();
                    if (m_closing || revision != m_revision)
                        return;
                    if (reply.status == EchoStatus::Success)
                        emit succeeded(id, reply.payload);
                    else
                        emit failed(id, reply.status, reply.payload);
                });
        });
        return id;
    }
    return submitPrepared(std::move(pending));
}

quint64 ApiClient::addInterceptor(double priority, decltype(ApiInterceptor::invoke) invoke,
                                  bool transport)
{
    auto entry = std::make_shared<ApiInterceptor>();
    entry->sequence = ++m_interceptSequence;
    entry->priority = priority;
    entry->invoke = std::move(invoke);
    (transport ? m_transportInterceptors : m_interceptors).insert(entry->sequence, entry);
    return entry->sequence;
}
void ApiClient::removeInterceptor(quint64 id)
{
    if (auto entry = m_interceptors.take(id))
        entry->active = false;
    if (auto entry = m_transportInterceptors.take(id))
        entry->active = false;
}
quint64 ApiClient::pluginRequest(const std::function<quint64()> &start)
{
    ++m_pluginRequestDepth;
    const auto id = start();
    --m_pluginRequestDepth;
    return id;
}
void ApiClient::finishRequest(quint64 id, int status, const QByteArray &payload)
{
    if (status == EchoStatus::Cancelled && cancelInterception(id))
        return;
    if (auto done = m_interceptForward.take(id))
        done({status, payload});
    else if ((id & (quint64(1) << 62)) && !(id & (quint64(1) << 63)))
        return; // A middleware response already ended this logical request.
    else if (status == EchoStatus::Success)
        emit succeeded(id, payload);
    else
        emit failed(id, status, payload);
}
bool ApiClient::cancelInterception(quint64 id, bool sharedVerification)
{
    if (!m_intercepted.contains(id))
        return false;
    auto chain = m_intercepted.take(id);
    m_interceptForward.remove(id);
    if (chain) {
        chain->cancel();
        chain->deleteLater();
    }
    // Reuse the native/challenge cancellation path after detaching middleware.
    if (sharedVerification)
        (void)cancel(id);
    else
        (void)cancelRequest(id);
    {
        const auto revision = m_revision;
        QTimer::singleShot(0, this, [this, id, revision] {
            if (!m_closing && revision == m_revision)
                emit failed(id, EchoStatus::Cancelled,
                            QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
        });
    }
    return true;
}
void ApiClient::clearInterceptions()
{
    const auto chains = std::exchange(m_intercepted, {});
    m_interceptForward.clear();
    clearTransport();
    for (auto chain : chains)
        if (chain) {
            chain->cancel();
            chain->deleteLater();
        }
}

quint64 ApiClient::submitPrepared(Pending pending)
{
    if (m_closing || !m_runtime || pending.revision != m_revision)
        return 0;
    const QByteArray bytes = QJsonDocument(pending.request).toJson(QJsonDocument::Compact);
    // Reset on each native submission so verification wait time is excluded.
    pending.attemptTimer.start();
    const auto id =
        pending.transportIntercept
            ? echo_runtime_submit_intercepted(
                  m_runtime, reinterpret_cast<const uint8_t *>(bytes.constData()),
                  size_t(bytes.size()), pending.revision, callback, interceptCallback, m_dispatcher)
            : echo_runtime_submit(m_runtime, reinterpret_cast<const uint8_t *>(bytes.constData()),
                                  size_t(bytes.size()), pending.revision, callback, m_dispatcher);
    if (id) {
        if (!pending.logicalId)
            pending.logicalId = id;
        m_pending.insert(id, std::move(pending));
    }
    return id;
}

quint64 ApiClient::refreshLogin()
{
    return m_auth->refreshLogin();
}
bool ApiClient::setDeviceIdentity(DeviceIdentity device)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || device.guid.isEmpty() ||
        device.mid != DeviceIdentity::midForGuid(device.guid) || device.dfid.isEmpty() ||
        device.mac.isEmpty() || device.deviceName.isEmpty())
        return false;
    m_device = std::move(device);
    advanceSession(); // Device-bound old requests/challenges cannot reuse this revision.
    return true;
}
quint64 ApiClient::recognizeAudio(const QByteArray &pcm)
{
    return m_media->recognizeAudio(pcm);
}
quint64 ApiClient::registerDevice()
{
    return m_auth->registerDevice();
}
QString ApiClient::diagnosticAuthorization() const
{
    QStringList parts;
    const auto append = [&](const QString &key, const QString &value) {
        if (!value.isEmpty())
            parts.append(key + '=' + value);
    };
    append(QStringLiteral("token"), m_token);
    if (m_userId)
        append(QStringLiteral("userid"), QString::number(m_userId));
    append(QStringLiteral("t1"), m_t1);
    append(QStringLiteral("dfid"), m_device.dfid);
    append(QStringLiteral("KUGOU_API_MID"), m_device.mid);
    append(QStringLiteral("uuid"), m_device.uuid);
    append(QStringLiteral("KUGOU_API_GUID"), m_device.guid);
    append(QStringLiteral("KUGOU_API_DEV"), m_device.deviceName);
    append(QStringLiteral("KUGOU_API_MAC"), m_device.mac);
    return parts.join(';');
}
QVariantList ApiClient::diagnosticIdentityEntries() const
{
    QVariantList entries;
    const auto append = [&](const QString &label, const QString &value) {
        if (!value.isEmpty())
            entries.append(QVariantMap{{"label", label}, {"value", value}});
    };
    // Explicit laboratory diagnostics only; ordinary account properties stay credential-free.
    if (!m_token.isEmpty())
        append(QStringLiteral("token"), m_token);
    if (!m_t1.isEmpty())
        append(QStringLiteral("t1"), m_t1);
    append(QStringLiteral("dfid"), m_device.dfid);
    append(QStringLiteral("mid"), m_device.mid);
    append(QStringLiteral("uuid"), m_device.uuid);
    append(QStringLiteral("guid"), m_device.guid);
    append(QStringLiteral("dev"), m_device.deviceName);
    append(QStringLiteral("mac"), m_device.mac);
    return entries;
}
quint64 ApiClient::createQrLogin()
{
    return m_auth->createQrLogin();
}
quint64 ApiClient::checkQrLogin(const QString &key)
{
    return m_auth->checkQrLogin(key);
}

quint64 ApiClient::createProviderQr(QrProvider provider)
{
    return m_auth->createProviderQr(provider);
}
quint64 ApiClient::checkProviderQr(QrProvider provider, const QJsonObject &session)
{
    return m_auth->checkProviderQr(provider, session);
}
quint64 ApiClient::exchangeWechatCode(const QString &code)
{
    return m_auth->exchangeWechatCode(code);
}
quint64 ApiClient::finishProviderLogin(QrProvider provider, const QJsonObject &authorization)
{
    return m_auth->finishProviderLogin(provider, authorization);
}
quint64 ApiClient::sendSmsCode(const QString &mobile)
{
    return m_auth->sendSmsCode(mobile);
}
quint64 ApiClient::smsLogin(const QString &mobile, const QString &code,
                            std::optional<quint64> selectedUser)
{
    return m_auth->smsLogin(mobile, code, selectedUser);
}

quint64 ApiClient::passwordLogin(const QString &username, const QString &password)
{
    return m_auth->passwordLogin(username, password);
}

quint64 ApiClient::searchHot(RequestOptions options)
{
    return m_catalog->searchHot(options);
}
quint64 ApiClient::searchDefault(RequestOptions options)
{
    return m_catalog->searchDefault(options);
}
quint64 ApiClient::searchSuggest(const QString &keyword, RequestOptions options)
{
    return m_catalog->searchSuggest(keyword, options);
}
quint64 ApiClient::search(SearchKind kind, const QString &keyword, quint32 page, quint32 pageSize,
                          RequestOptions options)
{
    return m_catalog->search(kind, keyword, page, pageSize, options);
}
quint64 ApiClient::recommendedPlaylists(const QString &categoryId)
{
    return m_catalog->recommendedPlaylists(categoryId);
}
quint64 ApiClient::editorPlaylists()
{
    return m_catalog->editorPlaylists();
}
quint64 ApiClient::styleRecommendations(const QStringList &tagIds)
{
    return m_catalog->styleRecommendations(tagIds);
}
quint64 ApiClient::dailyRecommendations()
{
    return m_catalog->dailyRecommendations();
}
quint64 ApiClient::freeListenSongs()
{
    return m_catalog->freeListenSongs();
}
quint64 ApiClient::discoverRecommendations(quint32 pageSize)
{
    return m_catalog->discoverRecommendations(pageSize);
}
quint64 ApiClient::personalFm(const FmRequest &request)
{
    return m_media->personalFm(request);
}
quint64 ApiClient::newSongCatalog()
{
    return m_catalog->newSongCatalog();
}
quint64 ApiClient::newAlbumCatalog()
{
    return m_catalog->newAlbumCatalog();
}
quint64 ApiClient::playlistCategories()
{
    return m_catalog->playlistCategories();
}
quint64 ApiClient::artistDirectory(quint32 sex, quint32 category, quint32 musician)
{
    return m_catalog->artistDirectory(sex, category, musician);
}
quint64 ApiClient::recommendedRanks()
{
    return m_catalog->recommendedRanks();
}
quint64 ApiClient::rankCatalog()
{
    return m_catalog->rankCatalog();
}
quint64 ApiClient::rankSongs(quint64 rankId)
{
    return m_catalog->rankSongs(rankId);
}
quint64 ApiClient::songClimax(const QString &hash)
{
    return m_media->songClimax(hash);
}
quint64 ApiClient::audioImages(const QList<AudioImageResource> &resources, quint32 count)
{
    return m_media->audioImages(resources, count);
}
quint64 ApiClient::albumDynamicCover(quint64 audio, quint64 album)
{
    return m_media->albumDynamicCover(audio, album);
}

quint64 ApiClient::comments(const CommentQuery &q)
{
    return m_social->comments(q);
}
quint64 ApiClient::sendComment(const CommentWrite &w)
{
    return m_social->sendComment(w);
}
quint64 ApiClient::replyComment(const CommentWrite &w)
{
    return m_social->replyComment(w);
}
quint64 ApiClient::deleteComment(const CommentWrite &w)
{
    return m_social->deleteComment(w);
}
quint64 ApiClient::songRanking(quint64 mix, bool details)
{
    return m_media->songRanking(mix, details);
}
quint64 ApiClient::commentCount(const QString &hash)
{
    return m_social->commentCount(hash);
}
quint64 ApiClient::favoriteCount(quint64 mix)
{
    return m_social->favoriteCount(mix);
}
quint64 ApiClient::songMv(quint64 mix)
{
    return m_media->songMv(mix);
}
quint64 ApiClient::videoDetail(const QString &id)
{
    return m_media->videoDetail(id);
}
quint64 ApiClient::videoPrivilege(const QString &hash)
{
    return m_media->videoPrivilege(hash);
}
quint64 ApiClient::videoUrl(const QString &hash)
{
    return m_media->videoUrl(hash);
}
quint64 ApiClient::collectVideo(quint64 id, bool collect)
{
    return m_social->collectVideo(id, collect);
}
quint64 ApiClient::commentAuthorProducts(const QList<quint64> &ids)
{
    return m_social->commentAuthorProducts(ids);
}
quint64 ApiClient::songBarrage(const QString &hash, quint32 size)
{
    return m_social->songBarrage(hash, size);
}
quint64 ApiClient::sendSongBarrage(const QString &hash, const QString &name, const QString &content)
{
    return m_social->sendSongBarrage(hash, name, content);
}
quint64 ApiClient::videoBarrage(const QString &hash, quint32 size)
{
    return m_social->videoBarrage(hash, size);
}
quint64 ApiClient::sendVideoBarrage(const QString &hash, const QString &name,
                                    const QString &content)
{
    return m_social->sendVideoBarrage(hash, name, content);
}
quint64 ApiClient::albumDetail(quint64 albumId, RequestOptions options)
{
    return m_catalog->albumDetail(albumId, options);
}
quint64 ApiClient::albumSongs(quint64 albumId, quint32 page, quint32 pageSize,
                              RequestOptions options)
{
    return m_catalog->albumSongs(albumId, page, pageSize, options);
}
quint64 ApiClient::artistDetail(quint64 artistId, RequestOptions options)
{
    return m_catalog->artistDetail(artistId, options);
}
quint64 ApiClient::artistSongs(quint64 artistId, quint32 page, quint32 pageSize,
                               const QString &sort, RequestOptions options)
{
    return m_catalog->artistSongs(artistId, page, pageSize, sort, options);
}
quint64 ApiClient::artistAlbums(quint64 artistId, quint32 page, quint32 pageSize,
                                const QString &sort, RequestOptions options)
{
    return m_catalog->artistAlbums(artistId, page, pageSize, sort, options);
}
quint64 ApiClient::validateAccount(quint64 userId, const QString &token, const QString &credential)
{
    if (!userId || userId > quint64(std::numeric_limits<qint64>::max()) || token.isEmpty())
        return 0;
    // This request belongs to the current operation revision, but authenticates
    // only the candidate. Never install it as the application's current account.
    const QJsonObject identity{{"userid", qint64(userId)},
                               {"token", token},
                               {"t1", credential},
                               {"mid", m_device.mid},
                               {"dfid", m_device.dfid},
                               {"guid", m_device.guid},
                               {"mac", m_device.mac},
                               {"device_name", m_device.deviceName},
                               {"uuid", m_device.uuid},
                               {"render_fingerprint", m_device.renderFingerprint},
                               {"authentication", "current_account"}};
    const auto id = submit({{"operation", "user_detail"}, {"identity", identity}}, false);
    if (id)
        m_pending[id].isolatedVerification = true;
    return id;
}

quint64 ApiClient::userDetail(RequestOptions options)
{
    return m_social->userDetail(options);
}
quint64 ApiClient::userInfo()
{
    return m_social->userInfo();
}
quint64 ApiClient::userVipDetail()
{
    return m_social->userVipDetail();
}
quint64 ApiClient::userGradeInfo(std::optional<quint64> userId, bool automaticVerification)
{
    return m_social->userGradeInfo(userId, automaticVerification);
}
quint64 ApiClient::reportListeningEvent(ListeningEvent event, quint64 songId, qint64 durationMs,
                                        bool completed)
{
    return m_media->reportListeningEvent(event, songId, durationMs, completed);
}
quint64 ApiClient::reportListeningProgress(quint64 baselineSeconds, quint64 incrementSeconds)
{
    return m_media->reportListeningProgress(baselineSeconds, incrementSeconds);
}
quint64 ApiClient::socialUsers(SocialList list)
{
    return m_social->socialUsers(list);
}
quint64 ApiClient::followUser(quint64 id, bool following)
{
    return m_social->followUser(id, following);
}
quint64 ApiClient::followArtist(quint64 id, bool following)
{
    return m_social->followArtist(id, following);
}
quint64 ApiClient::socialMessages(quint64 target, const QString &tag)
{
    return m_social->socialMessages(target, tag);
}
quint64 ApiClient::sendSocialMessage(quint64 target, const QString &tag, const QString &text,
                                     const QString &nickname)
{
    return m_social->sendSocialMessage(target, tag, text, nickname);
}
quint64 ApiClient::loginDevices()
{
    return m_auth->loginDevices();
}
quint64 ApiClient::kickLoginDevice(const LoginDeviceTarget &target)
{
    return m_auth->kickLoginDevice(target);
}
quint64 ApiClient::updateUserProfile(const ProfilePatch &patch)
{
    return m_social->updateUserProfile(patch);
}
quint64 ApiClient::uploadUserAvatar(const QByteArray &image)
{
    return m_social->uploadUserAvatar(image);
}
quint64 ApiClient::updateUserAvatar(const QString &fileName)
{
    return m_social->updateUserAvatar(fileName);
}
quint64 ApiClient::contentBlacklist(BlacklistKind kind, quint32 page, quint32 pageSize)
{
    return m_social->contentBlacklist(kind, page, pageSize);
}
quint64 ApiClient::editContentBlacklist(BlacklistKind kind, const QString &key, const QString &name,
                                        const QString &mixSongId, bool remove)
{
    return m_social->editContentBlacklist(kind, key, name, mixSongId, remove);
}
quint64 ApiClient::listeningPreferences()
{
    return m_social->listeningPreferences();
}
quint64 ApiClient::updateListeningPreferences(const PreferencePatch &patch)
{
    return m_social->updateListeningPreferences(patch);
}
quint64 ApiClient::userPlaylists(quint32 page, quint32 pageSize)
{
    return m_library->userPlaylists(page, pageSize);
}
quint64 ApiClient::importPlaylistLink(const QString &text)
{
    return m_library->importPlaylistLink(text);
}
quint64 ApiClient::submitPlaylistImportImage(const QString &serial, const QByteArray &image)
{
    return m_library->submitPlaylistImportImage(serial, image);
}
quint64 ApiClient::importPlaylistScreenshot(const QString &serial, CloudPlaylistId id,
                                            const QString &title)
{
    return m_library->importPlaylistScreenshot(serial, id, title);
}
quint64 ApiClient::playlistImportStatus(quint64 task)
{
    return m_library->playlistImportStatus(task);
}
quint64 ApiClient::playlistImportResult(CloudPlaylistId id, quint32 page, quint32 size)
{
    return m_library->playlistImportResult(id, page, size);
}
quint64 ApiClient::userCloud(quint32 page, quint32 pageSize)
{
    return m_cloud->userCloud(page, pageSize);
}
quint64 ApiClient::cloudMatch(const QString &hash)
{
    return m_cloud->cloudMatch(hash);
}
quint64 ApiClient::cloudUpload(CloudUploadStage stage, QJsonObject parameters)
{
    return m_cloud->cloudUpload(stage, parameters);
}
quint64 ApiClient::userCloudDelete(const QList<CloudDeleteTarget> &targets)
{
    return m_cloud->userCloudDelete(targets);
}
quint64 ApiClient::userVideoCollect(quint32 page, quint32 pageSize)
{
    return m_social->userVideoCollect(page, pageSize);
}
quint64 ApiClient::purchasedSongs(quint32 page, quint32 pageSize)
{
    return m_library->purchasedSongs(page, pageSize);
}
quint64 ApiClient::purchasedAlbums(quint32 page, quint32 pageSize)
{
    return m_library->purchasedAlbums(page, pageSize);
}
quint64 ApiClient::playlistDetails(const QList<PublicCollectionId> &ids, RequestOptions options)
{
    return m_library->playlistDetails(ids, options);
}
quint64 ApiClient::publicPlaylistTracks(const PublicCollectionId &id, quint32 page,
                                        quint32 pageSize, RequestOptions options)
{
    return m_library->publicPlaylistTracks(id, page, pageSize, options);
}
quint64 ApiClient::cloudPlaylistTracks(CloudPlaylistId id, quint32 page, quint32 pageSize)
{
    return m_library->cloudPlaylistTracks(id, page, pageSize);
}
quint64 ApiClient::songMetadata(const QList<quint64> &ids, RequestOptions options)
{
    return m_media->songMetadata(ids, options);
}
quint64 ApiClient::audioEffects(EffectCategory category, quint32 page, quint32 pageSize,
                                quint32 sort, quint64 brand)
{
    return m_media->audioEffects(category, page, pageSize, sort, brand);
}
quint64 ApiClient::commonHeadphoneEffect()
{
    return m_media->commonHeadphoneEffect();
}
quint64 ApiClient::lyricCandidates(const QString &hash, quint64 songId, quint64 durationMs,
                                   bool allCandidates)
{
    return m_media->lyricCandidates(hash, songId, durationMs, allCandidates);
}
quint64 ApiClient::lyric(const QString &id, const QString &accessKey)
{
    return m_media->lyric(id, accessKey);
}
quint64 ApiClient::lyricCandidatesByKeywords(const QString &keywords, quint64 durationMs,
                                             bool allCandidates)
{
    return m_media->lyricCandidatesByKeywords(keywords, durationMs, allCandidates);
}
quint64 ApiClient::userVerify(RequestOptions options)
{
    return m_auth->userVerify(options);
}
quint64 ApiClient::createPlaylist(const QString &title, bool isPrivate)
{
    return m_library->createPlaylist(title, isPrivate);
}
quint64 ApiClient::collectPlaylist(const CollectionTarget &target)
{
    return m_library->collectPlaylist(target);
}
quint64 ApiClient::addPlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackInput> &tracks)
{
    return m_library->addPlaylistTracks(id, tracks);
}
quint64 ApiClient::removePlaylistTracks(CloudPlaylistId id, const QList<PlaylistTrackFileId> &files)
{
    return m_library->removePlaylistTracks(id, files);
}
quint64 ApiClient::sortPlaylistTracks(CloudPlaylistId id, quint64 revision,
                                      const QList<PlaylistTrackPosition> &positions)
{
    return m_library->sortPlaylistTracks(id, revision, positions);
}
quint64 ApiClient::sortPlaylists(quint64 totalRevision, const QList<PlaylistPosition> &positions)
{
    return m_library->sortPlaylists(totalRevision, positions);
}
quint64 ApiClient::uploadPlaylistCover(const QByteArray &png)
{
    return m_library->uploadPlaylistCover(png);
}
quint64 ApiClient::updatePlaylist(const PlaylistUpdate &update)
{
    return m_library->updatePlaylist(update);
}
quint64 ApiClient::deletePlaylist(CloudPlaylistId id)
{
    return m_library->deletePlaylist(id);
}
quint64 ApiClient::playbackSource(const QString &hash, quint64 albumId, quint64 songId,
                                  PlaybackQuality quality, const QString &authorization,
                                  RequestOptions options, SoundEffect effect,
                                  const QString &originPage)
{
    return m_media->playbackSource(hash, albumId, songId, quality, authorization, options, effect,
                                   originPage);
}
quint64 ApiClient::songPrivileges(const QList<PrivilegeResource> &resources, RequestOptions options)
{
    return m_media->songPrivileges(resources, options);
}
bool ApiClient::matchesAccountSnapshot(quint64 userId, const QString &token,
                                       const QString &t1) const
{
    return !m_closing && userId == m_userId && token == m_token && t1 == m_t1;
}

bool ApiClient::setAccount(quint64 userId, QString token, QString t1)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || userId > quint64(std::numeric_limits<qint64>::max()) ||
        (userId == 0) != token.isEmpty() || (userId == 0 && !t1.isEmpty()))
        return false;
    if (userId == m_userId && token == m_token && t1 == m_t1 && m_loginConfirmation.isEmpty())
        return true;
    auto confirmations = std::exchange(m_loginConfirmation, {});
    m_userId = userId;
    m_token = std::move(token);
    m_t1 = std::move(t1);
    advanceSession(); // Direct dismissal slots now see the new account snapshot.
    const auto revision = m_revision;
    // Rebuild identity only for the explicitly confirmed login event. Posting
    // lets AccountService and all model reset listeners finish before replay.
    QTimer::singleShot(
        0, this, [this, revision, userId, confirmations = std::move(confirmations)]() mutable {
            if (m_closing || m_revision != revision || !userId)
                return;
            for (auto pending : std::as_const(confirmations)) {
                const auto previous =
                    pending.request.value("identity").toObject().value("userid").toInteger();
                if (previous > 0 && quint64(previous) != userId) {
                    finishRequest(pending.logicalId, EchoStatus::Cancelled,
                                  QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
                    continue;
                }
                pending.revision = revision;
                pending.retried = true;
                pending.verifiesRequest = 0;
                auto identity = pending.request.value("identity").toObject();
                if (identity.value("authentication") == QStringLiteral("current_account")) {
                    identity.insert("userid", qint64(m_userId));
                    identity.insert("token", m_token);
                    identity.insert("t1", m_t1);
                }
                pending.request.insert("identity", identity);
                if (!submitPrepared(pending))
                    finishRequest(pending.logicalId, EchoStatus::Closed,
                                  QByteArrayLiteral("{\"kind\":\"closed\"}"));
            }
        });
    return true;
}

void ApiClient::interceptCallback(void *context, uint64_t request, uint64_t revision,
                                  uint64_t ticket, unsigned int stage, const uint8_t *data,
                                  size_t length) noexcept
{
    auto *dispatcher = static_cast<Dispatcher *>(context);
    const QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(length));
    QMetaObject::invokeMethod(
        dispatcher,
        [dispatcher, request, revision, ticket, stage, bytes] {
            if (dispatcher->receiver)
                dispatcher->receiver->transportIntercept(request, revision, ticket, stage, bytes);
        },
        Qt::QueuedConnection);
}
void ApiClient::resumeTransport(quint64 ticket, const QJsonObject &command)
{
    if (!m_runtime || m_closing)
        return;
    const auto bytes = QJsonDocument(command).toJson(QJsonDocument::Compact);
    (void)echo_runtime_intercept_resume(m_runtime, ticket,
                                        reinterpret_cast<const uint8_t *>(bytes.constData()),
                                        size_t(bytes.size()));
}
void ApiClient::clearTransport(quint64 nativeId)
{
    for (auto ticket : m_transportCalls.keys()) {
        if (nativeId && m_transportCalls.value(ticket).nativeId != nativeId)
            continue;
        auto call = m_transportCalls.take(ticket);
        if (call.chain) {
            call.chain->cancel();
            call.chain->deleteLater();
        }
    }
}
void ApiClient::transportIntercept(quint64 nativeId, quint64 revision, quint64 ticket,
                                   unsigned int stage, const QByteArray &bytes)
{
    if (m_closing || revision != m_revision || !m_pending.contains(nativeId))
        return;
    if (stage == 1) {
        auto it = m_transportCalls.find(ticket);
        if (it != m_transportCalls.end()) {
            auto done = std::exchange(it->next, {});
            if (done)
                done({EchoStatus::Success, bytes});
        }
        return;
    }
    if (m_transportCalls.contains(ticket))
        return;
    auto *chain = new ApiInterceptChain(m_transportInterceptors.values(), this);
    m_transportCalls[ticket] = {nativeId, chain, {}};
    chain->run(
        QJsonDocument::fromJson(bytes).object(),
        [this, ticket](QJsonObject request, ApiInterceptDone done) {
            auto it = m_transportCalls.find(ticket);
            if (it == m_transportCalls.end())
                return;
            it->next = std::move(done);
            resumeTransport(ticket, {{"action", "forward"}, {"request", request}});
        },
        [this, ticket](ApiInterceptReply reply) {
            auto call = m_transportCalls.take(ticket);
            if (call.chain)
                call.chain->deleteLater();
            resumeTransport(ticket,
                            {{"action", "respond"},
                             {"response", QJsonDocument::fromJson(reply.payload).object()}});
        });
}

void ApiClient::callback(void *context, const EchoResult *result) noexcept
{
    auto dispatcher = static_cast<Dispatcher *>(context);
    const QByteArray bytes(reinterpret_cast<const char *>(result->payload),
                           qsizetype(result->payload_len));
    const auto id = result->request_id;
    const auto revision = result->session_revision;
    const int status = int(result->status);
    // Access the QPointer only on its QObject thread. Rust drain keeps dispatcher
    // alive during invokeMethod; Qt removes posted events when it is later deleted.
    QMetaObject::invokeMethod(
        dispatcher,
        [dispatcher, id, revision, status, bytes] {
            if (dispatcher->receiver)
                dispatcher->receiver->deliver(id, revision, status, bytes);
        },
        Qt::QueuedConnection);
}

void ApiClient::deliver(quint64 id, quint64 revision, int status, const QByteArray &payload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.contains(id) || m_closing || revision != m_revision)
        return;
    clearTransport(id);
    auto pending = m_pending.take(id);
    const auto original = pending.logicalId;
    const auto error = QJsonDocument::fromJson(payload).object();
    // Canonical metadata only: never log query/identity, response bodies,
    // provider messages, account/device ids or CAPTCHA secrets.
    const auto category =
        QByteArray("echo.api.") + pending.request.value("operation").toString().toLatin1();
    const auto code = error.value("code");
    const bool routine = status == EchoStatus::Success || status == EchoStatus::Cancelled;
    QString summary = status == EchoStatus::Success     ? QStringLiteral("Request completed")
                      : status == EchoStatus::Cancelled ? QStringLiteral("Request cancelled")
                                                        : QStringLiteral("Request failed");
    summary += QStringLiteral(" request=%1 session=%2").arg(original).arg(revision);
    summary += QStringLiteral(" attempt_elapsed_ms=%1").arg(pending.attemptTimer.elapsed());
    if (id != original)
        summary += QStringLiteral(" native_request=%1").arg(id);
    if (!routine) {
        const auto statusName = status == EchoStatus::ApiFailure ? "api_failure"
                                : status == EchoStatus::Internal ? "internal"
                                : status == EchoStatus::Closed   ? "closed"
                                                                 : "invalid_input";
        summary += QStringLiteral(" status=%1").arg(QString::fromLatin1(statusName));
        const auto kind = error.value("kind").toString();
        if (!kind.isEmpty())
            summary += QStringLiteral(" kind=%1").arg(kind);
        const auto transport = error.value("transport").toString();
        if (!transport.isEmpty())
            summary += QStringLiteral(" transport=%1").arg(transport);
        if (code.isDouble())
            summary += QStringLiteral(" code=%1").arg(code.toInteger());
        const auto diagnostics = error.value("diagnostics").toObject();
        // Only fixed protocol labels and counters, never arbitrary error text.
        for (const auto &key : {"phase", "io_kind", "quality"}) {
            const auto value = diagnostics.value(key).toString();
            if (!value.isEmpty())
                summary += QStringLiteral(" %1=%2").arg(QString::fromLatin1(key), value);
        }
        for (const auto &key : {"elapsed_ms", "http_status", "received_bytes", "service_status"}) {
            const auto value = diagnostics.value(key);
            if (value.isDouble())
                summary +=
                    QStringLiteral(" %1=%2").arg(QString::fromLatin1(key)).arg(value.toInteger());
        }
    }
    if (pending.retried)
        summary += QStringLiteral(" retried=true");
    const QMessageLogger logger(nullptr, 0, nullptr, category.constData());
    if (routine)
        logger.debug().noquote() << summary;
    else
        logger.warning().noquote() << summary;
    if (status == EchoStatus::ApiFailure && error.value("kind") == QStringLiteral("challenge") &&
        pending.automaticVerification && !pending.verifiesRequest) {
        if (pending.retried) {
            finishRequest(original, status,
                          QByteArrayLiteral("{\"kind\":\"verification_retry_rejected\",\"message\":"
                                            "\"验证后原请求仍被拒绝，请稍后重试\"}"));
            return;
        }
        const auto eventId =
            error.value("challenge").toObject().value("event_id").toString().trimmed();
        if (!eventId.isEmpty()) {
            pending.eventId = eventId;
            m_suspended.insert(original, std::move(pending));
            m_verificationQueue.append(original);
            presentNextVerification();
            return; // Logical request has not reached a terminal state yet.
        }
    }
    if (status == EchoStatus::Success && pending.verifiesRequest) {
        const auto target = m_suspended.constFind(pending.verifiesRequest);
        if (target != m_suspended.cend() && error.value("acknowledged").toBool())
            retryVerifiedEvent(*target);
    }
    if (m_closing || revision != m_revision)
        return; // A direct signal may switch accounts.
    if (status == EchoStatus::ApiFailure &&
        error.value("kind") == QStringLiteral("authentication") &&
        pending.request.value("identity").toObject().value("authentication") ==
            QStringLiteral("current_account") &&
        m_userId &&
        matchesAccountSnapshot(
            quint64(pending.request.value("identity").toObject().value("userid").toInteger()),
            pending.request.value("identity").toObject().value("token").toString(),
            pending.request.value("identity").toObject().value("t1").toString())) {
        emit authenticationExpired();
        if (m_closing || revision != m_revision)
            return;
    }
    if (status == EchoStatus::Success)
        finishRequest(original, EchoStatus::Success, payload);
    else
        finishRequest(original, status, payload);
}

void ApiClient::presentNextVerification()
{
    if (m_activeVerification || m_closing)
        return;
    while (!m_verificationQueue.isEmpty()) {
        const auto id = m_verificationQueue.takeFirst();
        if (!m_suspended.contains(id))
            continue;
        m_activeVerification = id;
        emit verificationRequired(id, m_suspended.value(id).eventId);
        return;
    }
}

bool ApiClient::awaitLoginVerification(quint64 requestId)
{
    if (requestId != m_activeVerification || !m_suspended.contains(requestId) ||
        !m_loginConfirmation.isEmpty())
        return false;
    QList<quint64> sessionBound;
    const auto revision = m_revision;
    const auto original = m_suspended.value(requestId);
    for (const auto id : m_suspended.keys())
        if (sameVerificationEvent(m_suspended.value(id), original)) {
            auto pending = m_suspended.take(id);
            if (pending.sessionBound) {
                m_verificationQueue.removeAll(id);
                sessionBound.append(id);
                continue;
            }
            m_loginConfirmation.insert(id, std::move(pending));
            m_verificationQueue.removeAll(id);
        }
    m_activeVerification = 0;
    emit verificationDismissed();
    presentNextVerification();
    for (auto id : sessionBound) {
        if (m_revision != revision || m_closing)
            break;
        finishRequest(id, EchoStatus::Cancelled,
                      QByteArrayLiteral(
                          "{\"kind\":\"login_required\",\"message\":\"请登录后重试该插件操作\"}"));
    }
    return true;
}
void ApiClient::cancelLoginVerification()
{
    const auto confirmations = std::exchange(m_loginConfirmation, {});
    for (const auto &pending : confirmations)
        finishRequest(pending.logicalId, EchoStatus::Cancelled,
                      QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
}
void ApiClient::bindRequestToSession(quint64 id)
{
    for (auto &p : m_pending)
        if (p.logicalId == id)
            p.sessionBound = true;
    if (m_suspended.contains(id))
        m_suspended[id].sessionBound = true;
}
quint64 ApiClient::requestVerification(const QString &eventId)
{
    if (m_closing || !m_runtime || eventId.trimmed().isEmpty() || eventId.size() > 512 ||
        m_suspended.size() >= 128)
        return 0;
    Pending p;
    p.revision = m_revision;
    p.logicalId = ++m_externalVerificationSequence;
    p.externalVerification = true;
    p.eventId = eventId;
    p.request = {{"operation", "plugin_verification"},
                 {"identity", QJsonObject{{"userid", qint64(m_userId)},
                                          {"token", m_token},
                                          {"t1", m_t1},
                                          {"mid", m_device.mid},
                                          {"dfid", m_device.dfid},
                                          {"guid", m_device.guid},
                                          {"mac", m_device.mac},
                                          {"uuid", m_device.uuid},
                                          {"device_name", m_device.deviceName},
                                          {"render_fingerprint", m_device.renderFingerprint},
                                          {"authentication", "current_account"}}}};
    const auto id = p.logicalId;
    m_suspended.insert(id, std::move(p));
    m_verificationQueue.append(id);
    // Defer UI and transport until the caller has installed its operation ownership.
    QMetaObject::invokeMethod(this, [this] { presentNextVerification(); }, Qt::QueuedConnection);
    return id;
}
quint64 ApiClient::verificationInfo(quint64 requestId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto original = m_suspended.constFind(requestId);
    if (requestId != m_activeVerification || original == m_suspended.cend())
        return 0;
    Pending pending;
    pending.revision = original->revision;
    pending.automaticVerification = false; // Verification cannot recursively prompt.
    pending.verifiesRequest = requestId;
    pending.request = {{"operation", "get_verify_info"},
                       {"identity", original->request.value("identity")},
                       {"query", QJsonObject{{"event_id", original->eventId}}}};
    return submitPrepared(std::move(pending));
}

quint64 ApiClient::submitVerification(quint64 requestId, quint32 verifyType, const QString &proof)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto original = m_suspended.constFind(requestId);
    if (requestId != m_activeVerification || original == m_suspended.cend() ||
        proof.trimmed().isEmpty() || (verifyType != 23 && verifyType != 32))
        return 0;
    // One submit in flight per active challenge, avoiding duplicate proof writes.
    for (const auto &pending : std::as_const(m_pending))
        if (pending.verifiesRequest == requestId)
            return 0;
    Pending pending;
    pending.revision = original->revision;
    pending.automaticVerification = false;
    pending.verifiesRequest = requestId;
    pending.request = {{"operation", "sidedt"},
                       {"identity", original->request.value("identity")},
                       {"query", QJsonObject{{"event_id", original->eventId},
                                             {"verify_type", qint64(verifyType)},
                                             {"proof", proof}}}};
    return submitPrepared(std::move(pending));
}

bool ApiClient::sameVerificationEvent(const Pending &left, const Pending &right)
{
    return left.eventId == right.eventId &&
           ((!left.isolatedVerification && !right.isolatedVerification) ||
            left.request.value("identity") == right.request.value("identity"));
}
void ApiClient::retryVerifiedEvent(const Pending &original)
{
    // A candidate's challenge must not acknowledge/cancel current-account work
    // even when the provider uses the same event ID for both accounts.
    const auto snapshot = original;
    const QString event = snapshot.eventId;
    for (const auto id : m_suspended.keys()) {
        if (!sameVerificationEvent(m_suspended.value(id), snapshot))
            continue;
        auto pending = m_suspended.take(id);
        pending.retried = true;
        m_verificationQueue.removeAll(id);
        if (pending.externalVerification) {
            finishRequest(id, EchoStatus::Success,
                          QJsonDocument(QJsonObject{{"eventId", event}, {"verified", true}})
                              .toJson(QJsonDocument::Compact));
            continue;
        }
        if (!pending.replayAfterVerification) {
            finishRequest(id, EchoStatus::ApiFailure,
                          QByteArrayLiteral("{\"kind\":\"verification_complete\",\"message\":"
                                            "\"安全验证已完成，请确认后继续上传\"}"));
            continue;
        }
        if (!submitPrepared(std::move(pending)))
            finishRequest(id, EchoStatus::Closed, QByteArrayLiteral("{\"kind\":\"closed\"}"));
    }
    m_activeVerification = 0;
    emit verificationDismissed();
    presentNextVerification();
}

void ApiClient::advanceSession()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_revision;
    clearInterceptions();
    for (auto id : m_pending.keys())
        echo_runtime_cancel(m_runtime, id);
    m_pending.clear();
    m_suspended.clear();
    m_loginConfirmation.clear();
    m_verificationQueue.clear();
    if (std::exchange(m_activeVerification, 0))
        emit verificationDismissed();
    emit sessionChanged(m_revision);
}

bool ApiClient::cancel(quint64 requestId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing)
        return false;
    if (cancelInterception(requestId, true))
        return true;
    // Type-38 transfers ownership from the old page/QR service to the login
    // confirmation UI. Its teardown must not cancel the continuation; explicit
    // dismissal uses cancelLoginVerification(). Session changes still discard it.
    if (m_loginConfirmation.contains(requestId))
        return false;
    if (m_suspended.contains(requestId)) {
        const auto cancellationRevision = m_revision;
        const auto original = m_suspended.value(requestId);
        const bool dismissActive =
            sameVerificationEvent(m_suspended.value(m_activeVerification), original);
        QList<quint64> cancelled;
        for (const auto id : m_suspended.keys()) {
            if (!sameVerificationEvent(m_suspended.value(id), original))
                continue;
            m_suspended.remove(id);
            m_verificationQueue.removeAll(id);
            cancelled.append(id);
        }
        for (const auto id : m_pending.keys()) {
            if (cancelled.contains(m_pending.value(id).verifiesRequest)) {
                echo_runtime_cancel(m_runtime, id);
                cancelled.append(m_pending.value(id).logicalId);
                clearTransport(id);
                m_pending.remove(id); // Late success cannot resume cancelled requests.
            }
        }
        if (dismissActive) {
            m_activeVerification = 0;
            emit verificationDismissed();
            presentNextVerification();
        }
        for (const auto id : cancelled) {
            if (m_closing || m_revision != cancellationRevision)
                break;
            finishRequest(id, EchoStatus::Cancelled, QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
        }
        return true;
    }
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it->logicalId == requestId) {
            const auto nativeId = it.key();
            const auto revision = it->revision;
            // Rust may already have completed while its GUI callback is queued.
            // The Qt logical request is still cancellable: invalidate it before
            // that callback runs, independently of the worker's cancellation result.
            clearTransport(nativeId);
            m_pending.remove(nativeId);
            (void)echo_runtime_cancel(m_runtime, nativeId);
            QMetaObject::invokeMethod(
                this,
                [this, requestId, revision] {
                    if (!m_closing && revision == m_revision)
                        finishRequest(requestId, EchoStatus::Cancelled,
                                      QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
                },
                Qt::QueuedConnection);
            return true;
        }
    return false;
}

bool ApiClient::cancelRequest(quint64 requestId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing)
        return false;
    if (cancelInterception(requestId))
        return true;
    // Type-38 transfers ownership from the old page/QR service to the login
    // confirmation UI. Its teardown must not cancel the continuation; explicit
    // dismissal uses cancelLoginVerification(). Session changes still discard it.
    if (m_loginConfirmation.contains(requestId))
        return false;
    if (m_suspended.contains(requestId)) {
        const auto cancellationRevision = m_revision;
        const bool dismissActive = m_activeVerification == requestId;
        QList<quint64> cancelled{requestId};
        m_suspended.remove(requestId);
        m_verificationQueue.removeAll(requestId);
        for (const auto id : m_pending.keys()) {
            if (cancelled.contains(m_pending.value(id).verifiesRequest)) {
                echo_runtime_cancel(m_runtime, id);
                cancelled.append(m_pending.value(id).logicalId);
                clearTransport(id);
                m_pending.remove(id); // Late success cannot resume cancelled requests.
            }
        }
        if (dismissActive) {
            m_activeVerification = 0;
            emit verificationDismissed();
            presentNextVerification();
        }
        for (const auto id : cancelled) {
            if (m_closing || m_revision != cancellationRevision)
                break;
            finishRequest(id, EchoStatus::Cancelled, QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
        }
        return true;
    }
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it->logicalId == requestId) {
            const auto nativeId = it.key();
            const auto revision = it->revision;
            // Rust may already have completed while its GUI callback is queued.
            // The Qt logical request is still cancellable: invalidate it before
            // that callback runs, independently of the worker's cancellation result.
            clearTransport(nativeId);
            m_pending.remove(nativeId);
            (void)echo_runtime_cancel(m_runtime, nativeId);
            QMetaObject::invokeMethod(
                this,
                [this, requestId, revision] {
                    if (!m_closing && revision == m_revision)
                        finishRequest(requestId, EchoStatus::Cancelled,
                                      QByteArrayLiteral("{\"kind\":\"cancelled\"}"));
                },
                Qt::QueuedConnection);
            return true;
        }
    return false;
}

void ApiClient::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (std::exchange(m_closing, true))
        return;
    clearInterceptions();
    auto runtime = std::exchange(m_runtime, nullptr);
    m_pending.clear();
    m_suspended.clear();
    m_loginConfirmation.clear();
    m_verificationQueue.clear();
    if (std::exchange(m_activeVerification, 0))
        emit verificationDismissed();
    // Never wait for callbacks while the GUI event loop is blocked. The native
    // worker does not depend on GUI delivery, only on dispatcher lifetime.
    m_closer = QThread::create([runtime] { echo_runtime_destroy(runtime); });
    connect(m_closer, &QThread::finished, m_dispatcher, &QObject::deleteLater);
    connect(m_closer, &QThread::finished, this, &ApiClient::closed);
    connect(m_closer, &QThread::finished, m_closer, &QObject::deleteLater);
    m_closer->start();
}
} // namespace EchoMusic

quint64 EchoMusic::ApiClient::cloudSource(const QJsonObject &source)
{
    return submit({{"operation", "user_cloud_url"},
                   {"query", QJsonObject{{"hash", source.value("hash")},
                                         {"album_audio_id", source.value("song_id").toInteger()},
                                         {"audio_id", source.value("audio_id").toInteger()},
                                         {"name", source.value("name").toString()}}}},
                  true, {AuthenticationMode::CurrentAccount, true});
}
