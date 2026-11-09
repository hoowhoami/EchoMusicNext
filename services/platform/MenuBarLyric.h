#pragma once
#include <QColor>
#include <QFont>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace EchoMusic
{
/// A bounded presentation of the shared lyric model, with source word times.
/// No player, parser, network connection or independent clock lives here.
struct MenuBarLyricContent
{
    QString primary, secondary;
    QVariantList words;
    qint64 offsetMs = 0;
    bool wordTimed = false;
    bool primaryIsTranslation = false;
    bool operator==(const MenuBarLyricContent &) const = default;
};
struct MenuBarLyricStyle
{
    // Picker order: original, translation-first, inline bilingual, two lines.
    int displayMode = 0;
    int maxWidth = 280;
    int fontSize = 13;
    int fontWeight = 500;
    int secondaryColorMode = 0; // system secondary, cover, custom
    int colorMode = 0;          // system, cover, custom
    QColor primaryColor = Qt::white, secondaryColor = QColor("#cde8ff");
    int opacity = 100;
    bool wordByWord = true;
    bool scrolling = false;
    bool motionEnabled = true;
    QFont font;
    bool operator==(const MenuBarLyricStyle &) const = default;
};
struct MenuBarLyricState
{
    MenuBarLyricContent content;
    QString title;
    QColor coverColor;
    bool hasTrack = false;
    bool playing = false;
    bool advancing = false;
    bool favorite = false;
    bool favoriteEnabled = false;
    bool visible = true;
    bool dimmed = false;
    int playMode = 0; // PlaybackService: list, single, shuffle, sequential
};
/// AppKit-only status item. All calls and callbacks run on the GUI thread.
/// The owner releases it before playback/theme shutdown. Other platforms are no-ops.
class MenuBarLyric final
{
public:
    enum class Action
    {
        PlayMode,
        Previous,
        Toggle,
        Next,
        Favorite,
        OpenMain,
        Settings,
        Disable
    };
    MenuBarLyric();
    ~MenuBarLyric();
    static bool available();
    void create(std::function<void(Action)> action, std::function<double()> position);
    void update(const MenuBarLyricState &state, const MenuBarLyricStyle &style);
    void reset();
    /// Read-only native lifecycle/render diagnostics, also used by platform tests.
    QVariantMap diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace EchoMusic
