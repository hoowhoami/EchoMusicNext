#include "SettingsKeys.h"
#include "RecognitionService.h"
#include "PlaybackLibraryActions.h"
#include <QQmlEngine>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
namespace EchoMusic
{
RecognitionService::RecognitionService(ApiClient &api, PlaybackService &playback,
                                       RecognitionCapture &capture, QSettings &settings,
                                       RouteController &router, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_capture(capture), m_settings(settings),
      m_actions(m_matches, playback, this)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_source = settings.value(EchoMusic::SettingsKeys::Recognition::Source, "system").toString();
    if (m_source != "mic")
        m_source = "system";
    m_actions.setQueueContext(tr("听歌识曲"), "recognize");
    connect(&router, &RouteController::pageActivated, this, [this](RouteContext *context) {
        if (context->name() != "recognize")
            setAvailable(false);
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] { reset(); });
    connect(&api, &ApiClient::succeeded, this, &RecognitionService::finish);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        if (id != m_request || !m_request)
            return;
        m_request = 0;
        m_state = Failed;
        m_error =
            QJsonDocument::fromJson(bytes).object().value("message").toString(tr("识别过程出错"));
        emit changed();
    });
    connect(&capture, &RecognitionCapture::started, this,
            [this](quint64 generation, const QString &error) {
                if (generation != m_generation || !m_available || m_closing || m_state != Recording)
                    return;
                m_starting = false;
                if (!error.isEmpty()) {
                    failCapture(error);
                    return;
                }
                m_elapsed.start();
                m_timer.start();
                emit changed();
            });
    connect(&capture, &RecognitionCapture::captured, this,
            [this](quint64 generation, const QByteArray &pcm, const QString &error) {
                if (generation != m_generation || !m_available || m_closing ||
                    m_state != Recognizing)
                    return;
                if (!error.isEmpty()) {
                    failCapture(error);
                    return;
                }
                m_request = m_api.recognizeAudio(pcm);
                if (!m_request) {
                    m_state = Failed;
                    m_error = tr("未录制到声音，请检查音源后重试");
                    emit changed();
                }
            });
    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const int seconds = qMin(10, int(m_elapsed.elapsed() / 1000));
        if (seconds != m_seconds) {
            m_seconds = seconds;
            emit changed();
        }
        if (seconds >= 10)
            stop();
    });
}
RecognitionService::~RecognitionService()
{
    shutdown();
}
void RecognitionService::setSource(const QString &value)
{
    if (busy() || (value != "system" && value != "mic") || m_source == value)
        return;
    m_source = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Recognition::Source, value);
    emit changed();
}
void RecognitionService::setAvailable(bool value)
{
    if (m_closing)
        value = false;
    if (m_available == value)
        return;
    m_available = value;
    m_matches.setMetadataActive(value);
    if (!value && busy()) {
        cancel();
        m_state = Idle;
        m_seconds = 0;
        emit changed();
    }
}
void RecognitionService::cancel()
{
    ++m_generation;
    m_timer.stop();
    m_starting = false;
    if (m_request)
        m_api.cancel(m_request);
    m_request = 0;
    m_capture.cancel();
}
void RecognitionService::start()
{
    if (!m_available || m_closing || busy())
        return;
    cancel();
    m_matches.clear();
    m_scores.clear();
    m_error.clear();
    m_seconds = 0;
    m_starting = true;
    m_state = Recording;
    emit changed();
    m_capture.start(m_generation, m_source, audio()->inputDevice());
}
void RecognitionService::stop()
{
    if (m_state != Recording || !m_available || m_closing)
        return;
    if (m_starting) {
        reset();
        return;
    }
    m_timer.stop();
    m_state = Recognizing;
    emit changed();
    m_capture.stop(m_generation);
}
void RecognitionService::reset()
{
    cancel();
    m_matches.clear();
    m_scores.clear();
    m_state = Idle;
    m_error.clear();
    m_seconds = 0;
    emit changed();
}
void RecognitionService::addSong(const QString &id)
{
    if (!m_available || m_state != Success)
        return;
    if (const auto track = m_matches.track(id); track && m_playback.libraryActions())
        m_playback.libraryActions()->prepareTracks({*track}, false);
}
void RecognitionService::shutdown()
{
    if (m_closing)
        return;
    reset();
    m_available = false;
    m_closing = true;
}
QVariant RecognitionService::confidence(int row) const
{
    return row >= 0 && row < m_scores.size() ? m_scores[row] : QVariant();
}
void RecognitionService::finish(quint64 id, const QByteArray &bytes)
{
    if (!m_request || id != m_request || !m_available || m_closing)
        return;
    m_request = 0;
    const auto rows = QJsonDocument::fromJson(bytes).array();
    QJsonArray songs;
    m_scores.clear();
    for (const auto &row : rows) {
        const auto match = row.toObject();
        songs.append(match.value("song"));
        m_scores.append(match.value("confidence").toVariant());
    }
    m_matches.replaceSongs(songs);
    m_state = songs.isEmpty() ? Failed : Success;
    m_error = songs.isEmpty() ? tr("未识别到歌曲，请靠近音源重试") : QString();
    emit changed();
}
void RecognitionService::failCapture(const QString &error)
{
    m_timer.stop();
    m_starting = false;
    m_state = Failed;
    const auto text = error.toLower();
    const bool microphone = m_source == "mic";
    if (text.contains("permission") || text.contains("权限") || text.contains("denied") ||
        text.contains("declined") || text.contains("not authorized") ||
        text.contains("screen recording"))
        m_error = microphone ? tr("没有麦克风权限，请在系统隐私设置中授权 EchoMusicNext")
                             : tr("没有系统音频录制权限，请在系统隐私设置中授权 EchoMusicNext");
    else if (text.contains("no default input"))
        m_error = tr("没有可用的默认麦克风");
    else if (text.contains("no default output"))
        m_error = tr("没有可用的系统默认输出设备");
    else if (text.contains("no longer available") || text.contains("invalid input device"))
        m_error = tr("选择的麦克风已断开，请重新选择输入设备");
    else if (text.contains("too short"))
        m_error = tr("录制时间过短，请重试");
    else if (text.contains("no samples"))
        m_error = microphone ? tr("未录制到麦克风声音，请检查输入设备和系统音量")
                             : tr("未捕获到系统音频，请确认当前有声音正在播放");
    else
        m_error = microphone ? tr("无法访问麦克风，请检查输入设备和系统权限")
                             : tr("无法捕获系统音频，请检查默认输出设备并关闭独占模式后重试");
    emit changed();
}
} // namespace EchoMusic

namespace EchoMusic
{
void RecognitionService::registerRoutes(RouteController &router)
{
    router.registerPage({"recognize",
                         "/main/recognize",
                         QUrl("qrc:/qt/qml/EchoMusic/Discovery/RecognizePage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
