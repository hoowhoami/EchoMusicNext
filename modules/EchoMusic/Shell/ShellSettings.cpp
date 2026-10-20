#include "SettingsKeys.h"
#include "ShellSettings.h"
#include <algorithm>
#include <cmath>
namespace EchoMusic
{
ShellSettings::ShellSettings(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_sidebarLayout(settings, this)
{
    m_userAgreementAccepted =
        settings.value(EchoMusic::SettingsKeys::Legal::UserAgreementAccepted, false).toBool();
    for (bool lyric : {false, true}) {
        const auto prefix = lyric ? QStringLiteral("lyrics") : QStringLiteral("video");
        const bool enabled = settings.value(prefix + "/barrageEnabled", false).toBool();
        if (lyric)
            m_lyricBarrage = enabled;
        else
            m_mvBarrage = enabled;
        const auto defaults = lyric ? m_lyricBarrageConfig : m_mvBarrageConfig;
        for (auto option :
             {BarrageOpacity, BarrageFontSize, BarrageSpeed, BarrageArea, BarrageDensity}) {
            const auto key = QStringList{
                SettingsKeys::BarrageFields::Opacity, SettingsKeys::BarrageFields::FontSize,
                SettingsKeys::BarrageFields::Speed, SettingsKeys::BarrageFields::Area,
                SettingsKeys::BarrageFields::Density}[int(option)];
            setBarrageOption(
                lyric, option,
                settings.value(prefix + "/barrage/" + key, defaults.value(key)).toDouble());
        }
    }
    m_collapsed =
        settings.value(EchoMusic::SettingsKeys::Interface::SidebarCollapsed, false).toBool();
    m_keepAlive =
        settings.value(EchoMusic::SettingsKeys::Navigation::KeepAliveEnabled, true).toBool();
    m_cacheMax = std::clamp(
        settings.value(EchoMusic::SettingsKeys::Navigation::KeepAliveMax, 20).toInt(), 3, 30);
    m_motion = settings.value(EchoMusic::SettingsKeys::Interface::MotionEnabled, true).toBool();
    m_searchDefault =
        settings.value(EchoMusic::SettingsKeys::Search::DefaultEnabled, false).toBool();
    m_fullscreenButton =
        settings.value(EchoMusic::SettingsKeys::Interface::ShowFullscreenButton, true).toBool();
    m_artistSongSort =
        settings.value(EchoMusic::SettingsKeys::Artists::SongSort).toString() == "hot" ? "hot"
                                                                                       : "new";
    m_artistAlbumSort =
        settings.value(EchoMusic::SettingsKeys::Artists::AlbumSort).toString() == "hot" ? "hot"
                                                                                        : "new";
    const auto scale = settings.value(EchoMusic::SettingsKeys::Interface::UiScale, 1.0).toDouble();
    m_scale = std::isfinite(scale) ? std::clamp(scale, std::pow(1.2, -8), std::pow(1.2, 8)) : 1.0;
}
void ShellSettings::acceptUserAgreement()
{
    if (m_userAgreementAccepted)
        return;
    m_userAgreementAccepted = true;
    m_settings.setValue(EchoMusic::SettingsKeys::Legal::UserAgreementAccepted, true);
    m_settings.sync();
    emit userAgreementAcceptedChanged();
}
void ShellSettings::save(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    emit changed();
}
void ShellSettings::updateViewport(qreal width)
{
    const bool narrow = width < 700;
    if (m_narrow == narrow)
        return;
    m_narrow = narrow;
    m_narrowExpanded = false;
    emit changed();
}
void ShellSettings::toggleSidebar()
{
    if (m_narrow && !m_narrowExpanded) {
        m_narrowExpanded = true;
        m_collapsed = false;
    } else {
        m_narrowExpanded = false;
        m_collapsed = !m_collapsed;
    }
    save(EchoMusic::SettingsKeys::Interface::SidebarCollapsed, m_collapsed);
}
void ShellSettings::setKeepAliveEnabled(bool enabled)
{
    if (m_keepAlive != enabled) {
        m_keepAlive = enabled;
        save(EchoMusic::SettingsKeys::Navigation::KeepAliveEnabled, enabled);
    }
}
void ShellSettings::setKeepAliveMax(int maximum)
{
    maximum = std::clamp(maximum, 3, 30);
    if (m_cacheMax != maximum) {
        m_cacheMax = maximum;
        save(EchoMusic::SettingsKeys::Navigation::KeepAliveMax, maximum);
    }
}
void ShellSettings::setMotionEnabled(bool enabled)
{
    if (m_motion != enabled) {
        m_motion = enabled;
        save(EchoMusic::SettingsKeys::Interface::MotionEnabled, enabled);
    }
}
void ShellSettings::setSearchDefaultEnabled(bool enabled)
{
    if (m_searchDefault != enabled) {
        m_searchDefault = enabled;
        save(EchoMusic::SettingsKeys::Search::DefaultEnabled, enabled);
    }
}
void ShellSettings::setScale(qreal scale)
{
    scale = std::clamp(scale, std::pow(1.2, -8), std::pow(1.2, 8));
    if (m_scale == scale)
        return;
    m_scale = scale;
    save(EchoMusic::SettingsKeys::Interface::UiScale, m_scale);
    // Responsive sidebar changes use changed(), but must not invalidate the
    // scale/width binding while Main is evaluating and reporting its viewport.
    emit uiScaleChanged();
}
void ShellSettings::setShowFullscreenButton(bool enabled)
{
    if (m_fullscreenButton != enabled) {
        m_fullscreenButton = enabled;
        save(EchoMusic::SettingsKeys::Interface::ShowFullscreenButton, enabled);
    }
}
void ShellSettings::setArtistSongSort(const QString &sort)
{
    if ((sort == "new" || sort == "hot") && sort != m_artistSongSort) {
        m_artistSongSort = sort;
        save(EchoMusic::SettingsKeys::Artists::SongSort, sort);
    }
}
void ShellSettings::setArtistAlbumSort(const QString &sort)
{
    if ((sort == "new" || sort == "hot") && sort != m_artistAlbumSort) {
        m_artistAlbumSort = sort;
        save(EchoMusic::SettingsKeys::Artists::AlbumSort, sort);
    }
}
void ShellSettings::zoomIn()
{
    setScale((qRound(m_scale * 100) + 5) / 100.0);
}
void ShellSettings::zoomOut()
{
    setScale((qRound(m_scale * 100) - 5) / 100.0);
}
void ShellSettings::resetZoom()
{
    setScale(1);
}
} // namespace EchoMusic

namespace EchoMusic
{
void ShellSettings::setMvBarrageEnabled(bool value)
{
    if (m_mvBarrage == value)
        return;
    m_mvBarrage = value;
    save(EchoMusic::SettingsKeys::Video::BarrageEnabled, value);
}
void ShellSettings::setLyricBarrageEnabled(bool value)
{
    if (m_lyricBarrage == value)
        return;
    m_lyricBarrage = value;
    save(EchoMusic::SettingsKeys::Lyrics::BarrageEnabled, value);
}
void ShellSettings::setLyricBarrageOption(BarrageOption option, double value)
{
    setBarrageOption(true, option, value);
}
void ShellSettings::setMvBarrageOption(BarrageOption option, double value)
{
    setBarrageOption(false, option, value);
}
void ShellSettings::setBarrageOption(bool lyric, BarrageOption option, double value)
{
    auto &config = lyric ? m_lyricBarrageConfig : m_mvBarrageConfig;
    if (!std::isfinite(value))
        return;
    QString key;
    switch (option) {
    case BarrageOpacity:
        key = SettingsKeys::BarrageFields::Opacity;
        value = std::clamp(value, 20., 100.);
        break;
    case BarrageFontSize:
        key = SettingsKeys::BarrageFields::FontSize;
        value = std::clamp(value, 12., 28.);
        break;
    case BarrageSpeed:
        key = SettingsKeys::BarrageFields::Speed;
        value = std::clamp(value, .5, 2.);
        break;
    case BarrageArea:
        key = SettingsKeys::BarrageFields::Area;
        if (value != 25 && value != 50 && value != 100)
            return;
        break;
    case BarrageDensity:
        key = SettingsKeys::BarrageFields::Density;
        value = std::clamp(value, 1., 3.);
        break;
    default:
        return;
    }
    if (config.value(key).toDouble() == value)
        return;
    config[key] = value;
    save((lyric ? EchoMusic::SettingsKeys::Lyrics::BarragePrefix
                : EchoMusic::SettingsKeys::Video::BarragePrefix) +
             key,
         value);
}
} // namespace EchoMusic
