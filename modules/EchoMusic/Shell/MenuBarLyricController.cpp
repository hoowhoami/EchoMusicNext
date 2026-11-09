#include "MenuBarLyricController.h"
#include "PlaybackService.h"
#include "PlaybackLibraryActions.h"
#include "LyricModel.h"
#include "ThemeStore.h"
#include <QGuiApplication>
namespace
{
QVariantMap menuBarLineAt(const EchoMusic::LyricModel &model, qint64 position, bool keepPrevious)
{
    int row = model.scrollable() ? model.indexAt(position) : model.nextVisible(-1);
    auto line = model.lineAt(row);
    const bool expired = model.scrollable() &&
                         position >= model.startAt(row) + model.durationAt(row) &&
                         model.durationAt(row) > 0;
    if (keepPrevious && (line.value("filtered").toBool() || expired)) {
        row = model.visibleIndex(row);
        line = model.lineAt(row);
    } else if (line.value("filtered").toBool() || expired) {
        return {};
    }
    return line;
}
} // namespace
namespace EchoMusic
{
MenuBarLyricController::MenuBarLyricController(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings, this)
{
    connect(&m_settings, &MenuBarLyricPreferences::changed, this, &MenuBarLyricController::sync);
}
MenuBarLyricController::~MenuBarLyricController()
{
    shutdown();
}
void MenuBarLyricController::attachPlayback(PlaybackService &playback)
{
    Q_ASSERT(!m_playback);
    m_playback = &playback;
    connect(&playback, &PlaybackService::changed, this, &MenuBarLyricController::sync);
    connect(&playback, &PlaybackService::clockChanged, this, &MenuBarLyricController::sync);
    connect(playback.lyrics(), &LyricModel::changed, this, &MenuBarLyricController::sync);
    connect(&playback, &QObject::destroyed, this, [this] { m_native.reset(); });
    sync();
}
void MenuBarLyricController::attachTheme(ThemeStore &theme)
{
    Q_ASSERT(!m_theme);
    m_theme = &theme;
    connect(&theme, &ThemeStore::changed, this, &MenuBarLyricController::sync);
    connect(&theme, &ThemeStore::fontChanged, this, &MenuBarLyricController::sync);
    connect(&theme, &ThemeStore::lyricFontsChanged, this, &MenuBarLyricController::sync);
    sync();
}
void MenuBarLyricController::setPresentationReady()
{
    m_ready = true;
    sync();
}
void MenuBarLyricController::shutdown()
{
    m_closing = true;
    m_native.reset();
}

MenuBarLyricContent MenuBarLyricController::contentAt(const LyricModel &model, qint64 position,
                                                      int mode, bool keepPrevious)
{
    const auto line = menuBarLineAt(model, position, keepPrevious);
    const auto original = line.value("lineText").toString();
    const auto translation = line.value("translation").toString();
    MenuBarLyricContent result;
    result.primary = original;
    result.offsetMs = model.offsetMs();
    result.wordTimed = model.scrollable() && line.value("wordTimed").toBool();
    if (result.wordTimed)
        result.words = line.value("words").toList();
    if (mode == 1 && !translation.isEmpty()) {
        result.primary = translation;
        result.primaryIsTranslation = true;
        result.wordTimed = false;
        result.words.clear();
    } else if (mode == 2 && !translation.isEmpty()) {
        result.primary += QStringLiteral(" · ") + translation;
        result.secondary = translation;
    } else if (mode == 3) {
        result.secondary = translation;
    }
    return result;
}
void MenuBarLyricController::sync()
{
    if (m_closing || !available() || !m_ready || !m_playback || !m_settings.enabled()) {
        m_native.reset();
        return;
    }
    if (m_library != m_playback->libraryActions()) {
        if (m_library)
            disconnect(m_library, nullptr, this, nullptr);
        m_library = m_playback->libraryActions();
        if (m_library)
            connect(m_library, &PlaybackLibraryActions::changed, this,
                    &MenuBarLyricController::sync);
    }
    m_native.create([this](auto action) { invoke(action); },
                    [this] { return m_playback ? m_playback->clockPosition() : 0.; });
    MenuBarLyricState state;
    state.hasTrack = m_playback->hasTrack();
    state.playing = m_playback->playing();
    state.advancing = m_playback->clock().value("isAdvancing").toBool();
    state.title = state.hasTrack ? m_playback->title() : QStringLiteral("EchoMusicNext");
    state.coverColor = m_playback->coverColor();
    state.playMode = m_playback->playMode();
    state.favorite = m_library && m_library->favorite();
    state.favoriteEnabled = m_library && state.hasTrack && !m_library->favoriteLoading();
    const auto at = qint64(m_playback->clockPosition());
    if (state.hasTrack)
        state.content = contentAt(*m_playback->lyrics(), at, m_settings.displayMode(),
                                  m_settings.idleBehavior() == 1);
    const bool empty = state.content.primary.isEmpty();
    state.visible = !(empty && m_settings.idleBehavior() == 2) &&
                    !(!state.playing && m_settings.pauseBehavior() == 2);
    state.dimmed = !state.playing && m_settings.pauseBehavior() == 1;
    if (empty)
        state.content.primary = state.title;
    auto style = m_settings.style();
    if (m_theme) {
        style.font = m_theme->menuBarLyricBaseFont();
        style.motionEnabled = m_theme->motionEnabled();
    }
    m_native.update(state, style);
}
void MenuBarLyricController::invoke(MenuBarLyric::Action action)
{
    if (m_closing)
        return;
    using Action = MenuBarLyric::Action;
    // Native tracking may still be on the stack. Defer surface retirement and
    // window/settings creation until AppKit has completed that event dispatch.
    if (action == Action::Disable) {
        QMetaObject::invokeMethod(
            this, [this] { m_settings.setEnabled(false); }, Qt::QueuedConnection);
    } else if (action == Action::Settings || action == Action::OpenMain) {
        QMetaObject::invokeMethod(
            this,
            [this, action] {
                if (m_closing)
                    return;
                if (action == Action::Settings)
                    emit settingsRequested();
                else
                    emit openMainRequested();
            },
            Qt::QueuedConnection);
    } else if (m_playback && m_playback->hasTrack()) {
        switch (action) {
        case Action::PlayMode:
            m_playback->cycleMode();
            break;
        case Action::Previous:
            m_playback->previous();
            break;
        case Action::Toggle:
            m_playback->toggle();
            break;
        case Action::Next:
            m_playback->next();
            break;
        case Action::Favorite:
            if (m_library && !m_library->favoriteLoading())
                m_library->toggleFavorite();
            break;
        default:
            break;
        }
    }
}
} // namespace EchoMusic
