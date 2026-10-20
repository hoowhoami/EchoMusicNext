#include "SettingsKeys.h"
#include "LyricTextController.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <cmath>
#include <QGuiApplication>
#include <QClipboard>
namespace EchoMusic
{
namespace
{
QString profile(const QString &mode)
{
    if (mode == "simplified")
        return "t2s";
    if (mode == "traditional")
        return "s2t";
    if (mode == "traditional-variants")
        return "s2t-variants";
    if (mode == "traditional-tw")
        return "s2tw";
    if (mode == "traditional-hk")
        return "s2hk";
    return {};
}
QString offsetKey(const QString &track)
{
    return EchoMusic::SettingsKeys::Lyrics::TrackOffsetsPrefix +
           QString::fromLatin1(
               QCryptographicHash::hash(track.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace
LyricTextController::LyricTextController(LyricModel &model, NativeBridge &native,
                                         QSettings &settings, QObject *parent)
    : QObject(parent), m_model(model), m_native(native), m_settings(settings)
{
    m_ruby = settings.value(EchoMusic::SettingsKeys::Lyrics::RomanizationAsRuby, false).toBool();
    m_filterEnabled =
        settings.value(EchoMusic::SettingsKeys::Lyrics::FilterEnabled, false).toBool();
    m_filterPattern = settings.value(EchoMusic::SettingsKeys::Lyrics::FilterPattern).toString();
    updateFilter();
    m_mode = settings.value(EchoMusic::SettingsKeys::Lyrics::Conversion, "none").toString();
    m_wantTranslation =
        settings.value(EchoMusic::SettingsKeys::Lyrics::WantTranslation, false).toBool();
    m_wantRomanization =
        settings.value(EchoMusic::SettingsKeys::Lyrics::WantRomanization, false).toBool();
    if (m_mode != "none" && profile(m_mode).isEmpty())
        m_mode = "none";
    m_globalOffset = qBound(
        -20000, settings.value(EchoMusic::SettingsKeys::Lyrics::GlobalOffsetMs, 0).toInt(), 20000);
    const auto step = settings.value(EchoMusic::SettingsKeys::Lyrics::OffsetStep, .1).toDouble();
    m_step = std::isfinite(step) ? qBound(.1, step, 5.) : .1;
    connect(&native, &NativeBridge::completed, this, &LyricTextController::completed);
}
namespace
{
QString effectiveFilter(const QString &pattern)
{
    return pattern.trimmed().isEmpty()
               ? QStringLiteral(
                     "^(作词|作曲|编曲|制作人|录音|混音|母带|出品|发行|企划|监制|和声|吉他|贝斯|鼓|"
                     "键盘|弦乐|词|曲|编|唱片|OP|SP|原唱|翻唱|许可|音乐人|纯音乐|宣推|协作推广|"
                     "策划|统筹|营销|推广|制作|配唱|和音|弦乐编写|人声录音|人声编辑)[：:]|^(Lyrics|"
                     "Composed|Produced|Written|Arranged|Mixed|Mastered|Recorded|Performed) "
                     "by[：:]|^[『「【].*[』」】]$|未经著作权人许可|不得翻唱|翻录或使用|听歌就在")
               : pattern;
}
} // namespace
QString LyricTextController::filterError() const
{
    const QRegularExpression regex(effectiveFilter(m_filterPattern));
    return regex.isValid() ? QString() : regex.errorString();
}
void LyricTextController::updateFilter()
{
    m_model.setFilter(m_filterEnabled, effectiveFilter(m_filterPattern));
}
void LyricTextController::setFilterEnabled(bool value)
{
    if (m_filterEnabled == value)
        return;
    m_filterEnabled = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::FilterEnabled, value);
    updateFilter();
    emit changed();
}
void LyricTextController::setFilterPattern(const QString &value)
{
    if (m_filterPattern == value)
        return;
    m_filterPattern = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::FilterPattern, value);
    updateFilter();
    emit changed();
}
void LyricTextController::setRomanizationAsRuby(bool value)
{
    if (m_ruby == value)
        return;
    m_ruby = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::RomanizationAsRuby, value);
    emit changed();
}
bool LyricTextController::replace(const QJsonObject &lyrics, const QString &track)
{
    if (!m_model.replace(lyrics))
        return false;
    m_original = lyrics;
    m_track = track;
    m_trackOffset = qBound(-20000, m_settings.value(offsetKey(track), 0).toInt(), 20000);
    apply();
    return true;
}
void LyricTextController::clear()
{
    ++m_revision;
    m_request = 0;
    m_original = {};
    m_presented = {};
    m_track.clear();
    m_trackOffset = 0;
    m_error.clear();
    m_model.clear();
    emit changed();
}
void LyricTextController::setPosition(qint64 position)
{
    m_position = position;
    m_model.setPosition(position);
}
void LyricTextController::setWantTranslation(bool value)
{
    if (m_wantTranslation == value)
        return;
    m_wantTranslation = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::WantTranslation, value);
    emit changed();
}
void LyricTextController::setWantRomanization(bool value)
{
    if (m_wantRomanization == value)
        return;
    m_wantRomanization = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::WantRomanization, value);
    emit changed();
}
void LyricTextController::setConversion(const QString &mode)
{
    if (mode == m_mode || (mode != "none" && profile(mode).isEmpty()))
        return;
    m_mode = mode;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::Conversion, mode);
    apply();
}
void LyricTextController::setGlobalOffsetMs(int offset)
{
    m_globalOffset = qBound(-20000, offset, 20000);
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::GlobalOffsetMs, m_globalOffset);
    publish();
}
void LyricTextController::setOffsetStep(double step)
{
    if (!std::isfinite(step))
        return;
    m_step = qBound(.1, step, 5.);
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::OffsetStep, m_step);
    emit changed();
}
void LyricTextController::adjustTrackOffset(int direction)
{
    if (m_track.isEmpty() || (direction != -1 && direction != 1))
        return;
    m_trackOffset = qBound(-20000, m_trackOffset + qRound(m_step * 1000) * direction, 20000);
    m_settings.setValue(offsetKey(m_track), m_trackOffset);
    publish();
}
void LyricTextController::resetTrackOffset()
{
    if (m_track.isEmpty())
        return;
    m_trackOffset = 0;
    m_settings.remove(offsetKey(m_track));
    publish();
}
void LyricTextController::copyLyrics()
{
    QStringList lines;
    for (const auto &entry : m_presented.value("lines").toArray()) {
        const auto line = entry.toObject();
        QStringList parts{line.value("text").toString().trimmed()};
        if (m_wantTranslation)
            parts.append(line.value("translation").toString().trimmed());
        if (m_wantRomanization)
            parts.append(line.value("romanization").toString().trimmed());
        parts.removeAll(QString());
        if (!parts.isEmpty())
            lines.append(parts.join('\n'));
    }
    const auto text = lines.join('\n').trimmed();
    if (!text.isEmpty() && QGuiApplication::clipboard()) {
        QGuiApplication::clipboard()->setText(text);
        emit copied();
    }
}
void LyricTextController::apply()
{
    ++m_revision;
    m_request = 0;
    m_error.clear();
    m_presented = m_original;
    publish();
    if (m_mode == "none" || m_original.isEmpty())
        return;
    QJsonArray texts;
    for (const auto &line : m_original.value("lines").toArray()) {
        const auto object = line.toObject();
        texts.append(object.value("text"));
        texts.append(object.value("translation").toString());
        texts.append(object.value("romanization").toString());
    }
    m_request =
        m_native.submit(NativeBridge::Opencc,
                        QJsonObject{{"texts", texts}, {"profile", profile(m_mode)}}, m_revision);
    if (!m_request) {
        m_error = tr("歌词转换任务繁忙，请重试");
        emit changed();
    }
}
void LyricTextController::publish()
{
    if (!m_presented.isEmpty()) {
        auto result = m_presented;
        // Product offset is positive = earlier; upstream KRC offset is later.
        result.insert("offset_ms",
                      m_presented.value("offset_ms").toInteger() - m_globalOffset - m_trackOffset);
        m_model.replace(result);
        m_model.setPosition(m_position);
    }
    emit changed();
}
void LyricTextController::completed(quint64 id, quint64 revision, bool success,
                                    const QByteArray &bytes)
{
    if (id != m_request || revision != m_revision || !m_request)
        return;
    m_request = 0;
    if (!success) {
        m_error = tr("歌词文字转换失败");
        emit changed();
        return;
    }
    const auto result = QJsonDocument::fromJson(bytes);
    auto lines = m_original.value("lines").toArray();
    if (!result.isArray() || result.array().size() != lines.size() * 3) {
        m_error = tr("歌词转换结果不完整");
        emit changed();
        return;
    }
    const auto texts = result.array();
    for (int i = 0; i < lines.size(); ++i) {
        if (!texts[i * 3].isString() || !texts[i * 3 + 1].isString() ||
            !texts[i * 3 + 2].isString()) {
            m_error = tr("歌词转换结果无效");
            emit changed();
            return;
        }
        auto line = lines[i].toObject();
        const auto converted = texts[i * 3].toString();
        line.insert("translation", texts[i * 3 + 1].toString());
        line.insert("romanization", texts[i * 3 + 2].toString());
        auto words = line.value("words").toArray();
        QString joined;
        for (const auto &word : words)
            joined += word.toObject().value("text").toString();
        const auto scalars = converted.toUcs4();
        if (!words.isEmpty() && joined == line.value("text").toString() &&
            joined.toUcs4().size() == scalars.size()) {
            int at = 0;
            for (int j = 0; j < words.size(); ++j) {
                auto word = words[j].toObject();
                const auto size = word.value("text").toString().toUcs4().size();
                word.insert("text", QString::fromUcs4(scalars.constData() + at, size));
                at += int(size);
                words[j] = word;
            }
            line.insert("words", words);
        } else if (!words.isEmpty()) {
            // Changed character count cannot preserve the original timing split.
            // Keep that complete line unchanged rather than corrupt karaoke timing.
            lines[i] = line;
            continue;
        }
        line.insert("text", converted);
        lines[i] = line;
    }
    m_presented = m_original;
    m_presented.insert("lines", lines);
    publish();
}
} // namespace EchoMusic
