// Native status-item interaction/layout is based on echo-menu-lyrics (MIT),
// revision acfdff13c25b216857e8e1056ad8abf53ea54290. See docs/licenses/echo-menu-lyrics-MIT.txt.
#include "MenuBarLyric.h"
#include "MenuBarLyricLayout.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <cmath>
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>

using namespace EchoMusic;
namespace
{
constexpr CGFloat rubyScale = .8;
NSString *nativeText(const QString &text)
{
    return [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(text.utf16())
                                         length:text.size()];
}
NSColor *nativeColor(const QColor &color)
{
    return [NSColor colorWithSRGBRed:color.redF()
                               green:color.greenF()
                                blue:color.blueF()
                               alpha:color.alphaF()];
}
NSFont *nativeFont(const MenuBarLyricStyle &style, CGFloat size)
{
    NSMutableArray *cascade = [NSMutableArray array];
    const auto families = style.font.families();
    for (const auto &family : families)
        [cascade addObject:[NSFontDescriptor fontDescriptorWithFontAttributes:@{
                     NSFontFamilyAttribute : nativeText(family)
                 }]];
    NSFont *font =
        families.isEmpty() ? nil : [NSFont fontWithName:nativeText(families.first()) size:size];
    if (!font)
        font = [NSFont systemFontOfSize:size];
    const CGFloat weight =
        style.fontWeight <= 500 ? (style.fontWeight - 400) / 100. * NSFontWeightMedium
        : style.fontWeight <= 600
            ? NSFontWeightMedium +
                  (style.fontWeight - 500) / 100. * (NSFontWeightSemibold - NSFontWeightMedium)
            : NSFontWeightSemibold +
                  (style.fontWeight - 600) / 100. * (NSFontWeightBold - NSFontWeightSemibold);
    const auto descriptor = [font.fontDescriptor fontDescriptorByAddingAttributes:@{
        NSFontCascadeListAttribute : cascade,
        NSFontTraitsAttribute : @{NSFontWeightTrait : @(weight)}
    }];
    auto resolved = [NSFont fontWithDescriptor:descriptor size:size];
    return resolved ? resolved : font;
}
CTLineRef makeLine(const QString &text, NSFont *font, NSFont *secondaryFont = nullptr,
                   CFIndex secondaryStart = -1, const QVariantList *ruby = nullptr,
                   NSColor *color = nil)
{
    auto foreground = color ? color : NSColor.whiteColor;
    auto attributed = [[NSMutableAttributedString alloc]
        initWithString:nativeText(text)
            attributes:@{NSFontAttributeName : font, NSForegroundColorAttributeName : foreground}];
    if (secondaryFont && secondaryStart >= 0 && secondaryStart < text.size())
        [attributed addAttribute:NSFontAttributeName
                           value:secondaryFont
                           range:NSMakeRange(secondaryStart, text.size() - secondaryStart)];
    CFIndex index = 0;
    if (ruby)
        for (const auto &value : *ruby) {
            const auto unit = value.toMap();
            const auto base = unit.value("text").toString();
            const auto reading = unit.value("ruby").toString();
            if (base.isEmpty() || text.mid(index, base.size()) != base)
                break;
            if (!reading.isEmpty()) {
                auto annotation = CTRubyAnnotationCreateWithAttributes(
                    kCTRubyAlignmentCenter, kCTRubyOverhangNone, kCTRubyPositionBefore,
                    // Readings from adjacent groups must not visually join.
                    (__bridge CFStringRef)nativeText(QChar(0x2009) + reading + QChar(0x2009)),
                    (__bridge CFDictionaryRef) @{
                        (__bridge NSString *)
                        kCTRubyAnnotationSizeFactorAttributeName : @(rubyScale),
                        // Let CoreText reserve reading width instead of shrinking
                        // a long romanization into an unreadable base-word box.
                        (__bridge NSString *)kCTRubyAnnotationScaleToFitAttributeName : @NO,
                        NSForegroundColorAttributeName : foreground
                    });
                [attributed addAttribute:(__bridge NSString *)kCTRubyAnnotationAttributeName
                                   value:(__bridge id)annotation
                                   range:NSMakeRange(index, base.size())];
                CFRelease(annotation);
            }
            index += base.size();
        }
    return CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed);
}
CGFloat textWidth(CTLineRef line)
{
    return std::ceil(CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr));
}
CGRect inkBounds(CTLineRef line)
{
    auto rect = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    return CGRectIsEmpty(rect)
               ? CTLineGetBoundsWithOptions(line, kCTLineBoundsExcludeTypographicLeading)
               : rect;
}
struct Span
{
    CGFloat from, to;
    double start, duration;
};
struct CachedLine
{
    __strong CALayer *container = nil, *base = nil, *sung = nil, *mask = nil;
    std::vector<Span> spans;
    CGFloat width = 0, fontSize = 0;
    bool timed = false;
};
} // namespace

// Physical tracking belongs to the status view. These native buttons retain
// keyboard/VoiceOver actions without letting NSStatusBarButton steal mouse-up.
@interface EchoMenuBarPlaybackButton : NSButton
@end
@implementation EchoMenuBarPlaybackButton
- (NSView *)hitTest:(NSPoint)point
{
    return nil;
}
- (void)drawRect:(NSRect)rect
{
}
@end

@interface EchoMenuBarLyricView : NSView {
@public
    MenuBarLyricState state;
    MenuBarLyricStyle style;
    std::function<void(MenuBarLyric::Action)> action;
    std::function<double()> position;
    __weak NSStatusItem *item;
    std::array<CachedLine, 2> lines;
    bool hovered;
    quint64 builds, ticks, displayRequests;
    CGFloat cutoff;
    CGFloat availableWidth;
    CGFloat layoutRightEdge;
    CGFloat layoutDecoration;
    quint64 layoutProbes;
@private
    NSTrackingArea *tracking;
    NSTimer *animation;
    NSArray<NSButton *> *buttons;
    CALayer *controlsLayer;
    CGFloat renderedScale;
    NSString *renderedAppearance;
    bool appearancePending;
    bool stopped;
    QElapsedTimer lineAge;
    bool rebuilding;
    bool layoutPending;
    bool measuringLayout;
}
- (void)applyState:(const MenuBarLyricState &)next style:(const MenuBarLyricStyle &)nextStyle;
- (void)rebuild;
- (void)scheduleLayout;
- (void)finishLayout;
- (void)refreshAnimation;
- (void)stop;
- (BOOL)animationActive;
@end

@implementation EchoMenuBarLyricView
- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        availableWidth = MenuBarLyricLayout::compactWidth;
        lineAge.start();
        NSMutableArray *controls = [NSMutableArray array];
        for (int i = 0; i < 5; ++i) {
            auto *button = [[EchoMenuBarPlaybackButton alloc] initWithFrame:NSZeroRect];
            button.bordered = NO;
            button.imagePosition = NSImageOnly;
            button.bezelStyle = NSBezelStyleAccessoryBarAction;
            button.tag = i;
            button.target = self;
            button.action = @selector(activateControl:);
            button.hidden = YES;
            [self addSubview:button];
            [controls addObject:button];
        }
        buttons = controls;
        for (auto &line : lines) {
            line.container = [CALayer layer];
            line.container.masksToBounds = YES;
            line.base = [CALayer layer];
            line.sung = [CALayer layer];
            line.mask = [CALayer layer];
            line.mask.backgroundColor = NSColor.blackColor.CGColor;
            line.sung.mask = line.mask;
            [line.container addSublayer:line.base];
            [line.container addSublayer:line.sung];
            [self.layer addSublayer:line.container];
        }
        controlsLayer = [CALayer layer];
        [self.layer addSublayer:controlsLayer];
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(visibilityChanged:)
                   name:NSWindowDidChangeOcclusionStateNotification
                 object:nil];
        for (NSString *name in @[
                 NSWindowDidMoveNotification, NSWindowDidChangeScreenNotification,
                 NSWindowDidUpdateNotification
             ])
            [[NSNotificationCenter defaultCenter] addObserver:self
                                                     selector:@selector(layoutChanged:)
                                                         name:name
                                                       object:nil];
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(screenParametersChanged:)
                   name:NSApplicationDidChangeScreenParametersNotification
                 object:nil];
        [[NSWorkspace sharedWorkspace].notificationCenter
            addObserver:self
               selector:@selector(motionChanged:)
                   name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                 object:nil];
    }
    return self;
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (void)updateTrackingAreas
{
    if (tracking)
        [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                            options:NSTrackingMouseEnteredAndExited |
                                                    NSTrackingActiveAlways | NSTrackingInVisibleRect
                                              owner:self
                                           userInfo:nil];
    [self addTrackingArea:tracking];
    [super updateTrackingAreas];
}
- (void)setHovered:(BOOL)value
{
    if (hovered == bool(value))
        return;
    hovered = value;
    [self rebuild];
}
- (void)mouseEntered:(NSEvent *)event
{
    [self setHovered:YES];
}
- (void)mouseExited:(NSEvent *)event
{
    [self setHovered:NO];
}
- (NSInteger)controlAt:(NSPoint)point
{
    if (!hovered || !state.hasTrack)
        return -1;
    for (NSButton *button in buttons)
        if (!button.hidden && NSPointInRect(point, button.frame))
            return button.tag;
    return -1;
}
- (void)mouseDown:(NSEvent *)event
{
    if (event.modifierFlags & NSEventModifierFlagControl) {
        [self rightMouseDown:event];
        return;
    }
    // AppKit can terminate a status item's tracking before mouseUp. Resolve
    // and dispatch once on press, as native status-bar controls do. Remote
    // status hosts can also forward a different local event coordinate space.
    auto point = [self convertPoint:[self.window convertPointFromScreen:NSEvent.mouseLocation]
                           fromView:nil];
    if (!NSPointInRect(point, self.bounds))
        point = [self convertPoint:event.locationInWindow fromView:nil];
    const auto chosen = [self controlAt:point];
    if (chosen >= 0)
        [self activateControl:buttons[chosen]];
}
- (void)mouseUp:(NSEvent *)event
{
}
- (void)activateControl:(NSButton *)sender
{
    if (!sender.enabled || !state.hasTrack)
        return;
    // A callback can change preferences and retire this view. Copy it first.
    const auto callback = action;
    if (callback)
        callback(static_cast<MenuBarLyric::Action>(sender.tag));
}
- (void)menuCommand:(NSMenuItem *)sender
{
    const auto callback = action;
    if (callback)
        callback(static_cast<MenuBarLyric::Action>(sender.tag));
}
- (NSMenu *)menuForEvent:(NSEvent *)event
{
    auto *menu = [[NSMenu alloc] initWithTitle:@""];
    if (self.bounds.size.width < 128 && state.hasTrack) {
        // A crowded menu bar can fit a lyric fragment but not five buttons.
        // Keep play/pause on hover and the remaining actions in this native menu.
        for (NSButton *button in buttons) {
            auto *entry = [[NSMenuItem alloc] initWithTitle:button.accessibilityLabel
                                                     action:@selector(menuCommand:)
                                              keyEquivalent:@""];
            entry.target = self;
            entry.tag = button.tag;
            entry.enabled = button.enabled;
            [menu addItem:entry];
        }
        menu.autoenablesItems = NO;
        [menu addItem:NSMenuItem.separatorItem];
    }
    const std::array<std::pair<QString, MenuBarLyric::Action>, 3> entries{
        {{QObject::tr("打开主窗口"), MenuBarLyric::Action::OpenMain},
         {QObject::tr("菜单栏歌词设置"), MenuBarLyric::Action::Settings},
         {QObject::tr("关闭菜单栏歌词"), MenuBarLyric::Action::Disable}}};
    for (const auto &[title, command] : entries) {
        auto *entry = [[NSMenuItem alloc] initWithTitle:nativeText(title)
                                                 action:@selector(menuCommand:)
                                          keyEquivalent:@""];
        entry.target = self;
        entry.tag = static_cast<NSInteger>(command);
        [menu addItem:entry];
    }
    return menu;
}
- (void)rightMouseDown:(NSEvent *)event
{
    auto *menu = [self menuForEvent:event];
    [self setHovered:NO];
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}
- (void)keyDown:(NSEvent *)event
{
    if ([event.charactersIgnoringModifiers isEqualToString:@" "] && state.hasTrack) {
        const auto callback = action;
        if (callback)
            callback(MenuBarLyric::Action::Toggle);
    } else if (event.keyCode == 53)
        [self setHovered:NO];
    else
        [super keyDown:event];
}
- (BOOL)isAccessibilityElement
{
    return YES;
}
- (NSAccessibilityRole)accessibilityRole
{
    return NSAccessibilityGroupRole;
}
- (NSString *)accessibilityLabel
{
    return nativeText(QObject::tr("菜单栏歌词"));
}
- (NSString *)accessibilityValue
{
    QStringList readings;
    for (const auto &unit : state.content.ruby)
        readings.append(unit.toMap().value("ruby").toString());
    return nativeText(state.content.primary +
                      (readings.isEmpty() ? QString() : " " + readings.join(' ')) +
                      (style.displayMode == 3 ? " " + state.content.secondary : QString()));
}
- (BOOL)accessibilityPerformPress
{
    [self setHovered:!hovered];
    return YES;
}
- (void)applyState:(const MenuBarLyricState &)next style:(const MenuBarLyricStyle &)nextStyle
{
    const bool contentChanged = state.content != next.content;
    const bool rebuild = contentChanged || style != nextStyle ||
                         state.coverColor != next.coverColor || state.playing != next.playing ||
                         state.playMode != next.playMode || state.favorite != next.favorite ||
                         state.favoriteEnabled != next.favoriteEnabled ||
                         state.hasTrack != next.hasTrack || state.visible != next.visible;
    state = next;
    style = nextStyle;
    const bool showing = state.visible && !item.visible;
    item.visible = state.visible;
    if (showing)
        [self scheduleLayout];
    if (contentChanged)
        lineAge.restart();
    if (!state.visible) {
        hovered = false;
    }
    if (rebuild && state.visible)
        [self rebuild];
    if (!state.visible) {
        for (auto &line : lines) {
            line.base.contents = nil;
            line.sung.contents = nil;
            line.spans.clear();
            line.width = 0;
        }
        for (NSButton *button in buttons)
            button.hidden = YES;
        controlsLayer.contents = nil;
    }
    self.layer.opacity = style.opacity / 100. * (state.dimmed ? .45 : 1.);
    [self tick:nil];
    [self refreshAnimation];
}
- (BOOL)canScroll
{
    return style.scrolling && style.motionEnabled &&
           !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion && state.advancing &&
           (lines[0].width > self.bounds.size.width - 12 ||
            lines[1].width > self.bounds.size.width - 12);
}
- (BOOL)presented
{
    // A system-hosted status item may expose a proxy NSWindow (including a
    // non-compositor windowNumber). Its occlusionState has no Visible bit even
    // while the menu bar is displaying it; only its public visibility is usable.
    return item.visible && self.window && self.window.visible && !self.hiddenOrHasHiddenAncestor;
}
- (void)refreshAnimation
{
    const auto &spans = lines[0].spans;
    const double now = position ? position() - state.content.offsetMs : 0;
    const bool progressing = style.wordByWord && state.content.wordTimed && !spans.empty() &&
                             now < spans.back().start + spans.back().duration;
    const bool needs =
        [self presented] && !hovered && state.advancing && (progressing || [self canScroll]);
    if (needs && !animation) {
        animation = [NSTimer timerWithTimeInterval:1. / 30.
                                            target:self
                                          selector:@selector(tick:)
                                          userInfo:nil
                                           repeats:YES];
        animation.tolerance = .005;
        [NSRunLoop.mainRunLoop addTimer:animation forMode:NSRunLoopCommonModes];
    } else if (!needs) {
        [animation invalidate];
        animation = nil;
    }
}
- (void)tick:(NSTimer *)timer
{
    if (timer)
        ++ticks;
    const double now = position ? position() - state.content.offsetMs : 0;
    const auto scale = renderedScale > 0 ? renderedScale : 2.;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    bool changed = false;
    for (auto &line : lines) {
        CGFloat painted = line.width;
        if (line.timed && style.wordByWord) {
            painted = 0;
            for (const auto &span : line.spans) {
                if (now < span.start)
                    break;
                const double fraction =
                    std::clamp((now - span.start) / std::max(1., span.duration), 0., 1.);
                painted = span.from + (span.to - span.from) * fraction;
                if (fraction < 1)
                    break;
            }
            if (!line.spans.empty() && now >= line.spans.back().start + line.spans.back().duration)
                painted = line.width;
        }
        if (&line == &lines[0])
            cutoff = painted;
        const auto mask = CGRectMake(0, 0, std::round(painted * scale) / scale,
                                     line.container.bounds.size.height);
        if (!CGRectEqualToRect(line.mask.frame, mask)) {
            line.mask.frame = mask;
            changed = true;
        }
        const auto width = line.container.bounds.size.width;
        const auto overflow = std::max(0., double(line.width - width));
        // Bounce between both ends, with a short dwell; no blank wraparound.
        double shift = 0;
        if ([self canScroll] && overflow > 0) {
            const auto leg = overflow / 28.;
            const auto phase =
                std::fmod(std::max(0., lineAge.elapsed() / 1000. - 1.5), 2 * leg + 3.);
            shift = phase < leg             ? phase * 28.
                    : phase < leg + 1.5     ? overflow
                    : phase < 2 * leg + 1.5 ? overflow - (phase - leg - 1.5) * 28.
                                            : 0.;
        }
        const auto x = overflow > 0 ? -shift : (width - line.width) / 2.;
        const auto textFrame = CGRectMake(std::round(x * scale) / scale, 0, line.width,
                                          line.container.bounds.size.height);
        if (!CGRectEqualToRect(line.base.frame, textFrame)) {
            line.base.frame = textFrame;
            line.sung.frame = textFrame;
            changed = true;
        }
    }
    [CATransaction commit];
    // A system-hosted status view does not publish mask-only CALayer changes
    // by itself. Invalidate the native surface, reusing the cached glyph images.
    if (changed && state.visible && !hovered) {
        ++displayRequests;
        self.needsDisplay = YES;
        item.button.needsDisplay = YES;
    }
    if (timer)
        [self refreshAnimation];
}
- (void)buildLine:(CachedLine &)cached
             text:(const QString &)text
             font:(NSFont *)font
    secondaryFont:(NSFont *)secondaryFont
            color:(NSColor *)color
           height:(CGFloat)height
        clipStart:(CGFloat)clipStart
          clipEnd:(CGFloat)clipEnd
{
    cached.spans.clear();
    cached.timed = false;
    cached.width = 0;
    cached.fontSize = 0;
    cached.base.contents = nil;
    cached.sung.contents = nil;
    if (text.isEmpty())
        return;
    const auto secondaryStart = secondaryFont ? text.size() - state.content.secondary.size() : -1;
    cached.fontSize =
        secondaryFont && &cached == &lines[1] ? secondaryFont.pointSize : font.pointSize;
    const auto *ruby = &cached == &lines[0] || secondaryFont ? &state.content.ruby : nullptr;
    CTLineRef full = makeLine(text, font, secondaryFont, secondaryStart, ruby, color);
    CTLineRef line = full;
    const auto available = self.bounds.size.width - 12;
    // Bound individual textures even for maliciously long plugin/source rows.
    const CGFloat limit = style.scrolling ? 4096. : available;
    if (textWidth(full) > limit) {
        const auto token = makeLine(QStringLiteral("…"), secondaryFont ? secondaryFont : font,
                                    nullptr, -1, nullptr, color);
        line = CTLineCreateTruncatedLine(full, limit, kCTLineTruncationEnd, token);
        CFRelease(token);
        if (!line)
            line = (CTLineRef)CFRetain(full);
    }
    cached.width = std::min(limit + 2, textWidth(line) + 2);
    cached.timed = &cached == &lines[0] && state.content.wordTimed;
    if (cached.timed) {
        CFIndex index = 0;
        for (const auto &entry : state.content.words) {
            const auto word = entry.toMap();
            const auto end = index + word.value("text").toString().size();
            if (end > text.size())
                break;
            const auto from = CTLineGetOffsetForStringIndex(full, index, nullptr);
            const auto to = CTLineGetOffsetForStringIndex(full, end, nullptr);
            cached.spans.push_back({from + 1, to + 1, word.value("start_ms").toDouble(),
                                    word.value("duration_ms").toDouble()});
            index = end;
        }
    }
    const auto backingScale =
        self.window ? self.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
    const CGFloat scale = backingScale > 0 ? backingScale : 2.;
    const size_t w = std::max<size_t>(1, std::ceil(cached.width * scale));
    const size_t h = std::max<size_t>(1, std::ceil(height * scale));
    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto context =
        CGBitmapContextCreate(nullptr, w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (context) {
        CGContextScaleCTM(context, scale, scale);
        CGContextClipToRect(
            context, CGRectMake(clipStart, 0,
                                std::max(0., std::min(cached.width, clipEnd) - clipStart), height));
        CGContextSetFillColorWithColor(context, color.CGColor);
        const auto ink = inkBounds(line);
        const CGFloat baseline = std::round((height / 2 - CGRectGetMidY(ink)) * scale) / scale;
        CGContextSetTextPosition(context, 1, baseline);
        CTLineDraw(line, context);
        auto bitmap = CGBitmapContextCreateImage(context);
        cached.base.contents = (__bridge id)bitmap;
        cached.sung.contents = (__bridge id)bitmap;
        cached.base.contentsScale = scale;
        cached.sung.contentsScale = scale;
        CGImageRelease(bitmap);
        CGContextRelease(context);
    }
    cached.base.opacity = cached.timed && style.wordByWord ? .35 : 1.;
    cached.sung.hidden = !(cached.timed && style.wordByWord);
    if (line != full)
        CFRelease(line);
    CFRelease(full);
}
- (void)rebuild
{
    if (rebuilding)
        return;
    if (!state.visible) {
        [self refreshAnimation];
        return;
    }
    rebuilding = true;
    renderedScale = self.window.backingScaleFactor;
    renderedAppearance = [self.effectiveAppearance bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua,
        NSAppearanceNameAccessibilityHighContrastAqua,
        NSAppearanceNameAccessibilityHighContrastDarkAqua
    ]];
    ++builds;
    const auto height = self.bounds.size.height > 0 ? self.bounds.size.height
                                                    : NSStatusBar.systemStatusBar.thickness;
    CGFloat size = std::min(double(style.fontSize), height - 4.);
    constexpr CGFloat bilingualGap = 2.;
    const bool inlineBilingual = style.displayMode == 2 && !state.content.secondary.isEmpty();
    const bool bilingual = style.displayMode == 3 && !state.content.secondary.isEmpty();
    constexpr CGFloat secondaryScale = .8;
    if (bilingual || !state.content.ruby.isEmpty()) {
        // Both font sizes fit the native bar together, including fallback
        // ascenders/descenders; the secondary line always remains smaller.
        while (size > 8) {
            auto primaryFont = nativeFont(style, size);
            auto secondaryFont = nativeFont(style, size * secondaryScale);
            const auto sample = makeLine(QStringLiteral("国한Agjp"), primaryFont);
            const auto subSample = makeLine(QStringLiteral("国한Agjp"), secondaryFont);
            const auto primary =
                makeLine(state.content.primary, primaryFont, nullptr, -1, &state.content.ruby);
            const auto secondary = makeLine(state.content.secondary, secondaryFont);
            const auto required =
                std::max(inkBounds(sample).size.height, inkBounds(primary).size.height) +
                (bilingual ? std::max(inkBounds(subSample).size.height,
                                      inkBounds(secondary).size.height) +
                                 bilingualGap
                           : 0);
            CFRelease(sample);
            CFRelease(subSample);
            CFRelease(primary);
            CFRelease(secondary);
            if (required <= height - 2)
                break;
            size -= .25;
        }
    }
    auto font = nativeFont(style, size);
    auto secondaryFont = nativeFont(style, size * secondaryScale);
    const auto secondaryStart =
        inlineBilingual ? state.content.primary.size() - state.content.secondary.size() : -1;
    // Inline layers share one attributed CTLine and baseline, so word offsets,
    // truncation, color clips and scrolling all use the same mixed-size shaping.
    const auto first =
        makeLine(state.content.primary, font, inlineBilingual ? secondaryFont : nullptr,
                 secondaryStart, &state.content.ruby);
    const auto second = makeLine(bilingual ? state.content.secondary : QString(), secondaryFont);
    const auto inlineSplit =
        inlineBilingual ? CTLineGetOffsetForStringIndex(first, secondaryStart, nullptr) + 1 : 0;
    const auto measured = std::max(textWidth(first), textWidth(second));
    const auto firstInk = inkBounds(first), secondInk = inkBounds(second);
    CFRelease(first);
    CFRelease(second);
    const auto maximum = measuringLayout
                             ? MenuBarLyricLayout::compactWidth
                             : std::max(MenuBarLyricLayout::compactWidth,
                                        std::min(double(style.maxWidth), double(availableWidth)));
    const auto width = std::min(std::max(measured + 16., 128.), maximum);
    if (std::abs(item.length - width) > .25)
        item.length = width;
    self.frame = NSMakeRect(0, 0, width, height);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    const auto scale = renderedScale > 0 ? renderedScale : 2.;
    const CGFloat primaryHeight =
        bilingual ? std::ceil(firstInk.size.height * scale) / scale + 2 : height;
    const CGFloat secondaryHeight =
        bilingual ? std::ceil(secondInk.size.height * scale) / scale + 2 : height;
    // Center the actual union of differently sized ink, with pixel-aligned
    // row origins; neither line is clipped into an equal half-height box.
    const auto groupHeight = firstInk.size.height + bilingualGap + secondInk.size.height;
    const auto bottom = (height - groupHeight) / 2;
    const auto primaryY = bilingual ? bottom + secondInk.size.height + bilingualGap +
                                          firstInk.size.height / 2 - primaryHeight / 2
                                    : 0;
    const auto secondaryY =
        bilingual ? bottom + secondInk.size.height / 2 - secondaryHeight / 2 : 0;
    lines[0].container.frame =
        CGRectMake(6, std::round(primaryY * scale) / scale, width - 12, primaryHeight);
    lines[1].container.frame =
        CGRectMake(6, std::round(secondaryY * scale) / scale, width - 12, secondaryHeight);
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
      NSColor *primary = NSColor.labelColor, *secondary = NSColor.secondaryLabelColor;
      if (style.colorMode == 1 && state.coverColor.isValid())
          primary = nativeColor(state.coverColor);
      else if (style.colorMode == 2)
          primary = nativeColor(style.primaryColor);
      if (style.secondaryColorMode == 1 && state.coverColor.isValid())
          secondary = nativeColor(state.coverColor);
      else if (style.secondaryColorMode == 2)
          secondary = nativeColor(style.secondaryColor);
      if (state.content.primaryIsSecondary)
          primary = secondary;
      [self buildLine:lines[0]
                   text:state.content.primary
                   font:font
          secondaryFont:inlineBilingual ? secondaryFont : nullptr
                  color:primary
                 height:primaryHeight
              clipStart:0
                clipEnd:inlineBilingual ? inlineSplit : 4098];
      [self buildLine:lines[1]
                   text:inlineBilingual ? state.content.primary
                        : bilingual     ? state.content.secondary
                                        : QString()
                   font:inlineBilingual ? font : secondaryFont
          secondaryFont:inlineBilingual ? secondaryFont : nullptr
                  color:secondary
                 height:secondaryHeight
              clipStart:inlineBilingual ? inlineSplit : 0
                clipEnd:4098];
    }];
    const bool controls = hovered && state.hasTrack && state.visible;
    for (auto &line : lines)
        line.container.hidden = controls;
    const std::array<QString, 4> modes{QObject::tr("列表循环"), QObject::tr("单曲循环"),
                                       QObject::tr("随机播放"), QObject::tr("顺序播放")};
    const std::array<NSString *, 4> modeSymbols{@"repeat", @"repeat.1", @"shuffle", @"arrow.right"};
    const std::array<QString, 5> titles{
        modes[std::clamp(state.playMode, 0, 3)], QObject::tr("上一首"),
        state.playing ? QObject::tr("暂停") : QObject::tr("播放"), QObject::tr("下一首"),
        state.favorite ? QObject::tr("取消收藏") : QObject::tr("收藏")};
    const std::array<NSString *, 5> symbols{
        modeSymbols[std::clamp(state.playMode, 0, 3)], @"backward.end.fill",
        state.playing ? @"pause.fill" : @"play.fill", @"forward.end.fill",
        state.favorite ? @"heart.fill" : @"heart"};
    const CGFloat controlWidth = std::min(180., width - 8.);
    for (int i = 0; i < 5; ++i) {
        auto *button = buttons[i];
        const bool compact = width < 128;
        button.hidden = !controls || (compact && i != 2);
        button.frame = compact ? NSMakeRect(4, 0, width - 8., height)
                               : NSMakeRect((width - controlWidth) / 2 + i * controlWidth / 5., 0,
                                            controlWidth / 5., height);
        button.image = [[NSImage imageWithSystemSymbolName:symbols[i]
                                  accessibilityDescription:nativeText(titles[i])]
            imageWithSymbolConfiguration:[NSImageSymbolConfiguration
                                             configurationWithPointSize:12
                                                                 weight:NSFontWeightMedium]];
        button.accessibilityLabel = nativeText(titles[i]);
        button.toolTip = nativeText(titles[i]);
        button.enabled = state.hasTrack && (i != 4 || state.favoriteEnabled);
        button.contentTintColor =
            i == 4 && state.favorite ? NSColor.systemRedColor : NSColor.labelColor;
    }
    [self buildControls];
    [CATransaction commit];
    rebuilding = false;
    [self tick:nil];
    ++displayRequests;
    self.needsDisplay = YES;
    item.button.needsDisplay = YES;
    [self refreshAnimation];
}
- (void)buildControls
{
    const bool controls = hovered && state.hasTrack && state.visible;
    controlsLayer.hidden = !controls;
    controlsLayer.frame = self.bounds;
    controlsLayer.contents = nil;
    if (!controls)
        return;
    // System-hosted status views do not consistently redraw child NSButton
    // cells on appearance changes. Cache all symbols together; native buttons
    // remain the accessible action targets with the exact same hit rectangles.
    const auto scale = self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 2.;
    const size_t width = std::ceil(self.bounds.size.width * scale);
    const size_t height = std::ceil(self.bounds.size.height * scale);
    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto context = CGBitmapContextCreate(nullptr, width, height, 8, width * 4, space,
                                         kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!context)
        return;
    CGContextScaleCTM(context, scale, scale);
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context
                                                                               flipped:NO];
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
      [[NSColor.labelColor colorWithAlphaComponent:.08] setFill];
      [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 4, 2) xRadius:6
                                       yRadius:6] fill];
      for (int i = 0; i < 5; ++i) {
          if (buttons[i].hidden)
              continue;
          NSImage *symbol = buttons[i].image;
          const auto color = i == 4 && state.favorite ? NSColor.systemRedColor : NSColor.labelColor;
          NSImage *tinted =
              [symbol imageWithSymbolConfiguration:[NSImageSymbolConfiguration
                                                       configurationWithPaletteColors:@[ color ]]];
          const auto bounds =
              NSInsetRect(buttons[i].frame, 4, std::max(2., (self.bounds.size.height - 14.) / 2));
          const auto factor = std::min(
              {1., bounds.size.width / symbol.size.width, bounds.size.height / symbol.size.height});
          const auto size = NSMakeSize(symbol.size.width * factor, symbol.size.height * factor);
          const auto target = NSMakeRect(NSMidX(bounds) - size.width / 2,
                                         NSMidY(bounds) - size.height / 2, size.width, size.height);
          [tinted drawInRect:target
                    fromRect:NSZeroRect
                   operation:NSCompositingOperationSourceOver
                    fraction:buttons[i].enabled ? 1. : .35
              respectFlipped:NO
                       hints:nil];
      }
    }];
    [NSGraphicsContext restoreGraphicsState];
    auto bitmap = CGBitmapContextCreateImage(context);
    controlsLayer.contents = (__bridge id)bitmap;
    controlsLayer.contentsScale = scale;
    CGImageRelease(bitmap);
    CGContextRelease(context);
}
- (void)setFrameSize:(NSSize)size
{
    const bool changed = !NSEqualSizes(size, self.frame.size);
    [super setFrameSize:size];
    if (changed && !rebuilding && item) {
        [self rebuild];
    }
}
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (item && self.window && renderedScale != self.window.backingScaleFactor) {
        [self rebuild];
    }
    [self refreshAnimation];
    if (item && self.window)
        [self scheduleLayout];
}
- (void)scheduleLayout
{
    if (stopped || !item || !state.visible || layoutPending)
        return;
    layoutPending = true;
    // Overflowing status windows keep stale origins. Measure once at a compact
    // width after AppKit/WindowServer have had time to place the native item.
    measuringLayout = true;
    [self rebuild];
    __weak EchoMenuBarLyricView *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC), dispatch_get_main_queue(),
                   ^{
                     [weakSelf finishLayout];
                   });
}
- (void)finishLayout
{
    measuringLayout = false;
    layoutPending = false;
    if (stopped || !item || !state.visible)
        return;
    auto *screen = self.window.screen;
    if (!screen)
        return;
    const auto frame = self.window.frame;
    const auto rect = [](NSRect r) {
        return QRectF(r.origin.x, r.origin.y, r.size.width, r.size.height);
    };
    availableWidth = MenuBarLyricLayout::availableWidth(
        rect(screen.frame), rect(screen.auxiliaryTopRightArea), rect(frame), item.length);
    layoutRightEdge = NSMaxX(frame);
    layoutDecoration = frame.size.width - item.length;
    ++layoutProbes;
    [self rebuild];
}
- (void)layoutChanged:(NSNotification *)note
{
    if (note.object == self.window && !rebuilding && !measuringLayout &&
        std::abs(self.window.frame.size.width - item.length - layoutDecoration) < .5 &&
        (note.name == NSWindowDidChangeScreenNotification ||
         std::abs(NSMaxX(self.window.frame) - layoutRightEdge) > .5))
        [self scheduleLayout];
}
- (void)screenParametersChanged:(NSNotification *)note
{
    [self scheduleLayout];
}
- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    if (item && renderedScale != self.window.backingScaleFactor) {
        [self rebuild];
    }
}
- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    // Drawing under performAsCurrentDrawingAppearance can notify views while
    // AppKit temporarily changes the drawing appearance. Resolve once after
    // that context has unwound, rather than creating a repaint/notify loop.
    if (stopped || !item || rebuilding || appearancePending)
        return;
    appearancePending = true;
    dispatch_async(dispatch_get_main_queue(), ^{
      appearancePending = false;
      if (stopped || !item)
          return;
      [self applyNativeAppearance];
    });
}
- (void)applyNativeAppearance
{
    NSString *appearance = [self.effectiveAppearance bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua,
        NSAppearanceNameAccessibilityHighContrastAqua,
        NSAppearanceNameAccessibilityHighContrastDarkAqua
    ]];
    if (item && renderedAppearance != appearance &&
        ![renderedAppearance isEqualToString:appearance]) {
        [self rebuild];
    }
}
- (void)visibilityChanged:(NSNotification *)note
{
    if (note.object == self.window)
        [self refreshAnimation];
}
- (void)motionChanged:(NSNotification *)note
{
    [self rebuild];
}
- (BOOL)animationActive
{
    return animation != nil;
}
- (void)stop
{
    stopped = true;
    [animation invalidate];
    animation = nil;
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [[NSWorkspace sharedWorkspace].notificationCenter removeObserver:self];
    action = {};
    position = {};
}
- (void)dealloc
{
    [self stop];
}
@end

namespace EchoMusic
{
struct MenuBarLyric::Impl
{
    __strong NSStatusItem *item = nil;
    __strong EchoMenuBarLyricView *view = nil;
    ~Impl()
    {
        [view stop];
        item.view = nil;
        [view removeFromSuperview];
        [NSStatusBar.systemStatusBar removeStatusItem:item];
    }
};
MenuBarLyric::MenuBarLyric() = default;
MenuBarLyric::~MenuBarLyric() = default;
bool MenuBarLyric::available()
{
    return true;
}
void MenuBarLyric::create(std::function<void(Action)> action, std::function<double()> position)
{
    if (m_impl || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return;
    m_impl = std::make_unique<Impl>();
    m_impl->item =
        [NSStatusBar.systemStatusBar statusItemWithLength:MenuBarLyricLayout::compactWidth];
    m_impl->item.visible = YES;
    m_impl->item.autosaveName = @"EchoMusicNextMenuBarLyrics";
    m_impl->view = [[EchoMenuBarLyricView alloc] initWithFrame:m_impl->item.button.bounds];
    m_impl->view->item = m_impl->item;
    m_impl->view->action = std::move(action);
    m_impl->view->position = std::move(position);
    m_impl->item.button.title = @"";
    // Use AppKit's custom-status-view contract so the remote menu-bar host
    // owns this view's placement and presentation lifecycle.
    m_impl->item.view = m_impl->view;
    m_impl->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
}
void MenuBarLyric::update(const MenuBarLyricState &state, const MenuBarLyricStyle &style)
{
    if (m_impl)
        [m_impl->view applyState:state style:style];
}
void MenuBarLyric::reset()
{
    m_impl.reset();
}
QVariantMap MenuBarLyric::diagnostics() const
{
    if (!m_impl)
        return {{"created", false}};
    auto *view = m_impl->view;
    qsizetype bytes = 0;
    for (const auto &line : view->lines) {
        auto bitmap = (__bridge CGImageRef)line.base.contents;
        if (bitmap)
            bytes += CGImageGetBytesPerRow(bitmap) * CGImageGetHeight(bitmap);
    }
    return {{"created", true},
            {"visible", bool(m_impl->item.visible)},
            {"width", m_impl->item.length},
            {"availableWidth", view->availableWidth},
            {"layoutRightEdge", view->layoutRightEdge},
            {"layoutProbes", QVariant::fromValue(view->layoutProbes)},
            {"height", view.bounds.size.height},
            {"windowNumber", qlonglong(view.window.windowNumber)},
            {"windowX", view.window.frame.origin.x},
            {"windowY", view.window.frame.origin.y},
            {"windowWidth", view.window.frame.size.width},
            {"windowHeight", view.window.frame.size.height},
            {"windowVisible", bool(view.window.visible)},
            {"occlusion", qulonglong(view.window.occlusionState)},
            {"hiddenAncestor", bool(view.hiddenOrHasHiddenAncestor)},
            {"advancing", view->state.advancing},
            {"position", view->position ? view->position() : 0.},
            {"spans", int(view->lines[0].spans.size())},
            {"rubyGroups", int(view->state.content.ruby.size())},
            {"rubyFontSize",
             view->state.content.ruby.isEmpty() ? 0. : view->lines[0].fontSize * rubyScale},
            {"primaryFontSize", view->lines[0].fontSize},
            {"secondaryFontSize", view->lines[1].fontSize},
            {"primary", view->state.content.primary},
            {"secondary", view->state.content.secondary},
            {"hovered", view->hovered},
            {"timerActive", bool([view animationActive])},
            {"textBuilds", QVariant::fromValue(view->builds)},
            {"ticks", QVariant::fromValue(view->ticks)},
            {"displayRequests", QVariant::fromValue(view->displayRequests)},
            {"cutoff", view->cutoff},
            {"bitmapBytes", bytes}};
}
} // namespace EchoMusic
