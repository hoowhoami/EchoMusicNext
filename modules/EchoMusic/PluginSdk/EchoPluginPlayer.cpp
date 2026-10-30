#include "EchoPluginPlayer.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "EchoPluginViewContext.h"
#include <cmath>
#include <QTimer>
#include <QMetaMethod>
namespace EchoMusic
{
EchoPluginReadModel::EchoPluginReadModel(QAbstractItemModel &source, QObject *parent)
    : QAbstractListModel(parent), m_source(source)
{
    connect(&source, &QAbstractItemModel::modelAboutToBeReset, this, [this] { beginResetModel(); });
    connect(&source, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
    connect(&source, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex &, int first, int last) { beginInsertRows({}, first, last); });
    connect(&source, &QAbstractItemModel::rowsInserted, this, [this] { endInsertRows(); });
    connect(&source, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &, int first, int last) { beginRemoveRows({}, first, last); });
    connect(&source, &QAbstractItemModel::rowsRemoved, this, [this] { endRemoveRows(); });
    connect(&source, &QAbstractItemModel::rowsAboutToBeMoved, this,
            [this](const QModelIndex &, int first, int last, const QModelIndex &, int target) {
                beginMoveRows({}, first, last, {}, target);
            });
    connect(&source, &QAbstractItemModel::rowsMoved, this, [this] { endMoveRows(); });
    connect(&source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                emit dataChanged(index(first.row()), index(last.row()), roles);
            });
}
int EchoPluginReadModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_source.rowCount();
}
QVariant EchoPluginReadModel::data(const QModelIndex &item, int role) const
{
    return item.isValid() ? m_source.data(m_source.index(item.row(), 0), role) : QVariant();
}
QHash<int, QByteArray> EchoPluginReadModel::roleNames() const
{
    return m_source.roleNames();
}
EchoPluginPlayer::EchoPluginPlayer(EchoPluginContext &scope, PlaybackService &host)
    : QObject(&scope), m_scope(scope), m_host(host), m_audioSource(scope),
      m_queue(*host.queue(), this), m_lyrics(*host.lyrics(), this)
{
    connect(&host, &PlaybackService::clockChanged, this, [this] {
        if (!m_scope.active())
            return;
        emit clockChanged();
        if (clock().value("displayState") != m_previous.value("displayState"))
            publishStateEvents();
        emit snapshotChanged();
        if (isSignalConnected(QMetaMethod::fromSignal(&EchoPluginPlayer::eventOccurred)))
            emit eventOccurred("clock", snapshot());
    });
    connect(this, &EchoPluginPlayer::changed, this, &EchoPluginPlayer::snapshotChanged);
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoPluginPlayer::clockChanged);
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoPluginPlayer::snapshotChanged);
    m_previous = snapshot();
    publishStateEvents();
    connect(host.personalFm(), &PersonalFmController::changed, this, &EchoPluginPlayer::changed);
    connect(&host, &PlaybackService::changed, this, [this] {
        publishStateEvents();
        emit changed();
    });
    connect(host.lyrics(), &LyricModel::changed, this, &EchoPluginPlayer::lyricChanged);
    connect(host.lyrics(), &LyricModel::currentChanged, this, &EchoPluginPlayer::lyricChanged);
    connect(host.lyricText(), &LyricTextController::changed, this, &EchoPluginPlayer::lyricChanged);
    connect(&host, &PlaybackService::positionChanged, this, [this] {
        if (isSignalConnected(QMetaMethod::fromSignal(&EchoPluginPlayer::eventOccurred)))
            emit eventOccurred("timeupdate", snapshot());
    });
    connect(&host, &PlaybackService::playbackEnded, this,
            [this] { emit eventOccurred("ended", snapshot()); });
    connect(&host, &PlaybackService::seekCompleted, this,
            [this] { emit eventOccurred("seek", snapshot()); });
    connect(&host, &PlaybackService::positionChanged, this, &EchoPluginPlayer::positionChanged);
}
QString EchoPluginPlayer::title() const
{
    return m_host.title();
}
QString EchoPluginPlayer::artist() const
{
    return m_host.artist();
}
QString EchoPluginPlayer::album() const
{
    return m_host.album();
}
QUrl EchoPluginPlayer::coverUrl() const
{
    return QUrl(m_host.cover());
}
bool EchoPluginPlayer::loading() const
{
    return m_host.loading();
}
bool EchoPluginPlayer::hasTrack() const
{
    return m_host.hasTrack();
}
QString EchoPluginPlayer::errorMessage() const
{
    return m_host.errorMessage();
}
bool EchoPluginPlayer::playing() const
{
    return m_host.playing();
}
qint64 EchoPluginPlayer::positionMs() const
{
    return m_host.positionMs();
}
qint64 EchoPluginPlayer::durationMs() const
{
    return m_host.durationMs();
}
double EchoPluginPlayer::volume() const
{
    return m_host.volume();
}
double EchoPluginPlayer::speed() const
{
    return m_host.speed();
}
EchoPluginOperation *EchoPluginPlayer::command(std::function<void()> invoke)
{
    return m_scope.execute(std::move(invoke));
}
EchoPluginOperation *EchoPluginPlayer::play()
{
    return command([this] { m_host.play(); });
}
EchoPluginOperation *EchoPluginPlayer::pause()
{
    return command([this] { m_host.pause(); });
}
EchoPluginOperation *EchoPluginPlayer::toggle()
{
    return command([this] { m_host.toggle(); });
}
EchoPluginOperation *EchoPluginPlayer::stop()
{
    return command([this] { m_host.stop(); });
}
EchoPluginOperation *EchoPluginPlayer::previous()
{
    return command([this] { m_host.previous(); });
}
EchoPluginOperation *EchoPluginPlayer::next()
{
    return command([this] { m_host.next(); });
}
EchoPluginOperation *EchoPluginPlayer::seek(qint64 position)
{
    return m_scope.execute([this, position] { m_host.seek(position); }, true);
}
EchoPluginOperation *EchoPluginPlayer::setVolume(double value)
{
    if (!std::isfinite(value) || value < 0 || value > 150)
        return m_scope.rejected("InvalidArgument", "Volume must be 0..150 percent");
    return command([this, value] { m_host.setVolume(value); });
}
EchoPluginOperation *EchoPluginPlayer::setSpeed(double value)
{
    if (!std::isfinite(value) || value < 0.1 || value > 5)
        return m_scope.rejected("InvalidArgument", "Speed must be 0.1..5");
    return command([this, value] { m_host.setSpeed(value); });
}
EchoPluginOperation *EchoPluginPlayer::playIndex(int row)
{
    return command([this, row] { m_host.playIndex(row); });
}
EchoPluginOperation *EchoPluginPlayer::remove(int row)
{
    return command([this, row] { m_host.queue()->remove(row); });
}
EchoPluginOperation *EchoPluginPlayer::move(int from, int to)
{
    return command([this, from, to] { m_host.queue()->move(from, to); });
}
EchoPluginOperation *EchoPluginPlayer::clear()
{
    return command([this] { m_host.queue()->clear(); });
}
QVariantMap EchoPluginReadModel::row(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    QVariantMap values;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        values.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return values;
}
QString EchoPluginPlayer::revision() const
{
    return QString::number(m_host.sourceRevision());
}
QVariantMap EchoPluginPlayer::lyricsInfo() const
{
    auto *lyrics = m_host.lyrics();
    auto *text = m_host.lyricText();
    return {{"count", lyrics->rowCount()},
            {"currentIndex", lyrics->currentIndex()},
            {"offsetMs", lyrics->offsetMs()},
            {"scrollable", lyrics->scrollable()},
            {"hasTranslation", lyrics->hasTranslation()},
            {"hasRomanization", lyrics->hasRomanization()},
            {"translation", text->wantTranslation()},
            {"romanization", text->wantRomanization()},
            {"conversion", text->conversion()},
            {"globalOffsetMs", text->globalOffsetMs()},
            {"trackOffsetMs", text->trackOffsetMs()},
            {"offsetStep", text->offsetStep()},
            {"state", m_host.lyricState()},
            {"currentLine", lyrics->lineAt(lyrics->currentIndex())}};
}
QVariantMap EchoPluginPlayer::lyricSnapshot() const
{
    auto result = lyricsInfo();
    QVariantList lines;
    auto *model = m_host.lyrics();
    for (int i = 0; i < model->rowCount(); ++i)
        lines << model->lineAt(i);
    result.insert("lines", lines);
    result.insert("revision", revision());
    return result;
}
QVariantMap EchoPluginPlayer::clock() const
{
    return m_scope.active() ? m_host.clock() : QVariantMap();
}
double EchoPluginPlayer::samplePosition() const
{
    return m_scope.active() ? m_host.clockPosition() : 0;
}
QVariantMap EchoPluginPlayer::snapshot() const
{
    const auto &track = m_host.currentTrack();
    return {
        {"clock", clock()},
        {"displayState", clock().value("displayState")},
        {"busyReason", clock().value("busyReason")},
        {"revision", revision()},
        {"track",
         QVariantMap{{"id", track.id},
                     {"hash", track.hash},
                     {"sourceId", track.catalogMetadata.value("plugin_source").toString()},
                     {"data", track.catalogMetadata.value("plugin_data").toObject().toVariantMap()},
                     {"title", track.title},
                     {"artist", track.artist},
                     {"artistLinks", track.artistLinks()},
                     {"album", track.album},
                     {"albumId", QString::number(track.albumId)},
                     {"songId", QString::number(track.songId)},
                     {"cover", track.cover},
                     {"durationMs", QVariant::fromValue(track.durationMs)}}},
        {"title", title()},
        {"artist", artist()},
        {"album", album()},
        {"cover", coverUrl()},
        {"coverColor", m_host.coverColor()},
        {"playing", playing()},
        {"loading", loading()},
        {"hasTrack", hasTrack()},
        {"positionMs", positionMs()},
        {"durationMs", durationMs()},
        {"volume", volume()},
        {"speed", speed()},
        {"playMode", int(m_host.playMode())},
        {"quality", m_host.quality()},
        {"effectiveQuality", m_host.effectiveQuality()},
        {"resolvedQuality", m_host.resolvedQuality()},
        {"effect", m_host.songEffect()},
        {"appliedEffect", m_host.appliedSongEffect()},
        {"sourceKind", m_host.sourceKind()},
        {"sourceNotice", m_host.sourceNotice()},
        {"sourceNoticeCode", m_host.sourceNoticeCode()},
        {"favorite", m_host.libraryActions() && m_host.libraryActions()->favorite()},
        {"favoriteBusy", m_host.libraryActions() && m_host.libraryActions()->favoriteLoading()},
        {"personalFm", m_host.personalFm()->active()},
        {"fm", QVariantMap{{"signedIn", m_host.personalFm()->signedIn()},
                           {"busy", m_host.personalFm()->busy()},
                           {"mode", m_host.personalFm()->mode()},
                           {"songPool", m_host.personalFm()->songPool()},
                           {"current", m_host.personalFm()->current()},
                           {"sideTracks", m_host.personalFm()->sideTracks()},
                           {"currentPlaying", m_host.personalFm()->currentPlaying()},
                           {"errorMessage", m_host.personalFm()->errorMessage()}}},
        {"errorMessage", errorMessage()},
        {"lyrics", lyricsInfo()}};
}
void EchoPluginPlayer::publishStateEvents()
{
    const auto state = snapshot();
    if (m_library != m_host.libraryActions()) {
        disconnect(m_libraryConnection);
        m_library = m_host.libraryActions();
        if (m_library)
            m_libraryConnection = connect(m_host.libraryActions(), &PlaybackLibraryActions::changed,
                                          this, &EchoPluginPlayer::changed);
    }
    if (state.value("revision") != m_previous.value("revision"))
        emit eventOccurred("trackchange", state);
    if (state.value("playing") != m_previous.value("playing"))
        emit eventOccurred(playing() ? "play" : "pause", state);
    if (state.value("loading") != m_previous.value("loading") ||
        state.value("playing") != m_previous.value("playing") ||
        state.value("displayState") != m_previous.value("displayState"))
        emit eventOccurred("statechange", state);
    if (!errorMessage().isEmpty() &&
        state.value("errorMessage") != m_previous.value("errorMessage"))
        emit eventOccurred("error", state);
    m_previous = state;
}
EchoPluginOperation *EchoPluginPlayer::control(const QString &action, const QVariantMap &args)
{
    auto *view = new EchoPluginViewContext(m_scope, "main", nullptr);
    auto *op = view->command(action, args);
    if (op) {
        connect(op, &EchoPluginOperation::completed, view, [this, view, op] {
            op->setParent(&m_scope);
            view->deleteLater();
        });
        if (op->finished()) {
            op->setParent(&m_scope);
            view->deleteLater();
        }
    } else
        view->deleteLater();
    return op;
}
EchoPluginRegistration *EchoPluginPlayer::on(const QString &id, const QString &event,
                                             const QJSValue &callback, bool immediate)
{
    if (!callback.isCallable() || !QStringList{"trackchange", "statechange", "play", "pause",
                                               "ended", "seek", "error", "timeupdate", "clock"}
                                       .contains(event))
        return nullptr;
    auto *lease = m_scope.registration("playbackEvent", id);
    if (!lease)
        return nullptr;
    auto connection = std::make_shared<QMetaObject::Connection>();
    const auto deliver = [this, lease, callback](const QVariantMap &value) {
        if (lease->active() && m_scope.active())
            m_scope.call(callback, {m_scope.engine().toScriptValue(value)});
    };
    lease->stage(
        [this, lease, event, deliver, immediate, connection] {
            *connection = connect(this, &EchoPluginPlayer::eventOccurred, lease,
                                  [event, deliver](const QString &name, const QVariantMap &value) {
                                      if (event == name)
                                          deliver(value);
                                  });
            if (immediate && (event != "play" || playing()))
                QTimer::singleShot(0, lease, [this, deliver] { deliver(snapshot()); });
            return true;
        },
        [connection] { QObject::disconnect(*connection); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
} // namespace EchoMusic
