#include <QTimer>
#include "EchoPluginAppearance.h"
#include "EchoPluginContext.h"
#include "EchoPluginViewContext.h"
#include "ThemeStore.h"
#include "RouteController.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRegularExpression>
#include <cmath>
namespace EchoMusic
{
EchoPluginAppearanceHandle::EchoPluginAppearanceHandle(EchoPluginRegistration &lease,
                                                       AppearanceEntry &e, QObject *p)
    : QObject(p), m_key(e.key), m_lease(&lease), m_configuration(e.config)
{
    connect(&lease, &EchoPluginRegistration::activeChanged, this,
            &EchoPluginAppearanceHandle::changed);
}
bool EchoPluginAppearanceHandle::active() const
{
    return m_lease && m_lease->active();
}
void EchoPluginAppearanceHandle::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
EchoPluginAppearance::EchoPluginAppearance(EchoPluginContext &s) : QObject(&s), m_scope(s)
{
    connect(&s.host().theme, &ThemeStore::changed, this, &EchoPluginAppearance::changed);
    connect(&s.host().theme, &ThemeStore::fontChanged, this, &EchoPluginAppearance::changed);
    connect(&s.host().theme, &ThemeStore::lyricFontsChanged, this, &EchoPluginAppearance::changed);
}
void EchoPluginAppearance::storageBarrier(std::function<void()> ready)
{
    if (drained()) {
        ready();
        return;
    }
    const auto completed = std::make_shared<bool>(false);
    const auto check = [this, completed, ready] {
        if (!*completed && drained()) {
            *completed = true;
            ready();
        }
    };
    for (auto configuration : m_configurations) {
        if (!configuration || !configuration->busy())
            continue;
        connect(configuration, &AppearanceConfiguration::drained, this, check,
                Qt::SingleShotConnection);
        connect(configuration, &QObject::destroyed, this, check, Qt::SingleShotConnection);
    }
}
bool EchoPluginAppearance::drained() const
{
    for (auto c : m_configurations)
        if (c && c->busy())
            return false;
    return true;
}
QString EchoPluginAppearance::key(const QString &id, const QString &kind) const
{
    return id.contains(':') ? id : m_scope.installationId() + ":" + kind + ":" + id;
}
bool EchoPluginAppearance::validateVisuals(const QVariantMap &values, QString &error)
{
    const QStringList groups{"palette", "floating",       "mini",          "background",
                             "surface", "accentGradient", "pageTransition"};
    const auto invalid = [&](const QString &message) {
        error = message;
        return false;
    };
    const auto number = [](const QVariant &value, double low, double high) {
        bool ok = false;
        const auto n = value.toDouble(&ok);
        return ok && value.metaType().id() != QMetaType::Bool && std::isfinite(n) && n >= low &&
               n <= high;
    };
    for (auto group = values.begin(); group != values.end(); ++group) {
        if (!groups.contains(group.key()) ||
            group.value().metaType().id() != QMetaType::QVariantMap)
            return invalid("Invalid visual group: " + group.key());
        const auto fields = group.value().toMap();
        for (auto field = fields.begin(); field != fields.end(); ++field) {
            const auto name = field.key();
            const auto value = field.value();
            bool valid = false;
            if (group.key() == "palette")
                valid = QStringList{"shell",    "sidebar", "main",          "player", "card",
                                    "elevated", "text",    "secondaryText", "border", "accent"}
                            .contains(name) &&
                        QColor(value.toString()).isValid();
            else if (group.key() == "floating" || group.key() == "mini")
                valid = QStringList{"background", "text",   "secondaryText", "card",
                                    "border",     "accent", "accentText"}
                            .contains(name) &&
                        QColor(value.toString()).isValid();
            else if (group.key() == "surface")
                valid =
                    QStringList{"shell", "main", "sidebar", "player", "card", "elevated"}.contains(
                        name) &&
                    number(value, 0, 1);
            else if (group.key() == "pageTransition") {
                if (name == "enabled")
                    valid = value.metaType().id() == QMetaType::Bool;
                else if (name == "mode")
                    valid = QStringList{"parallel", "out-in", "in-out"}.contains(value.toString());
                else if (name == "durationMs")
                    valid = number(value, 0, 2000);
                else if (name == "enterOpacity" || name == "leaveOpacity")
                    valid = number(value, 0, 1);
                else if (name == "enterScale" || name == "leaveScale")
                    valid = number(value, .1, 4);
                else if (QStringList{"enterX", "enterY", "leaveX", "leaveY"}.contains(name))
                    valid = number(value, -1000, 1000);
            } else {
                if (name == "kind")
                    valid = group.key() == "background" &&
                            QStringList{"color", "gradient", "image", "video"}.contains(
                                value.toString());
                else if (QStringList{"color", "shadeColor"}.contains(name))
                    valid = QColor(value.toString()).isValid();
                else if (name == "opacity" || name == "shade")
                    valid = number(value, 0, 1);
                else if (name == "angle")
                    valid = number(value, -360, 360);
                else if (name == "height")
                    valid = group.key() == "accentGradient" && number(value, 1, 100);
                else if (name == "source")
                    valid =
                        group.key() == "background" && value.metaType().id() == QMetaType::QString;
                else if (name == "fit")
                    valid = QStringList{"cover", "contain"}.contains(value.toString());
                else if (name == "stops") {
                    const auto stops = value.toList();
                    valid = stops.size() >= 2 && stops.size() <= 8;
                    double position = -1;
                    for (const auto &stop : stops) {
                        const auto map = stop.toMap();
                        const auto p = map.value("position").toDouble();
                        valid = valid && map.size() == 2 && number(map.value("position"), 0, 1) &&
                                p >= position && QColor(map.value("color").toString()).isValid();
                        position = p;
                    }
                }
            }
            if (!valid)
                return invalid("Invalid visual field: " + group.key() + "." + name);
        }
    }
    return true;
}
namespace
{
QJSValue checkedCall(EchoPluginContext &scope, const QJSValue &callback, const QVariantMap &input,
                     QString &error)
{
    auto invoke = scope.engine().evaluate(
        "(function(fn,value){try{return {ok:true,value:fn(value)}}catch(e){return "
        "{ok:false,error:String(e)}}})");
    const auto envelope = invoke.call({callback, scope.engine().toScriptValue(input)});
    if (envelope.isError() || !envelope.property("ok").toBool()) {
        error = envelope.isError() ? envelope.toString() : envelope.property("error").toString();
        if (error.isEmpty())
            error = "Appearance callback failed";
        return {};
    }
    return envelope.property("value");
}
bool resolveAsset(QVariantMap &values, const PluginPackage &package, QString &error)
{
    auto background = values.value("background").toMap();
    if (background.contains("source")) {
        const auto path = package.resolve(background.value("source").toString());
        if (path.isEmpty()) {
            error = "Background source must be a package asset";
            return false;
        }
        background.insert("source", QUrl::fromLocalFile(path).toString());
        values.insert("background", background);
    }
    return true;
}
} // namespace
EchoPluginAppearanceHandle *EchoPluginAppearance::registerTheme(const QJSValue &d)
{
    return add(d, "theme");
}
EchoPluginAppearanceHandle *EchoPluginAppearance::registerSkin(const QJSValue &d)
{
    return add(d, "skin");
}
EchoPluginAppearanceHandle *EchoPluginAppearance::add(const QJSValue &d, const QString &kind)
{
    auto reject = [this](const QString &e) -> EchoPluginAppearanceHandle * {
        m_error = e;
        emit changed();
        return nullptr;
    };
    if (m_scope.contextId() != "main")
        return reject("Appearance candidates belong to activation context");
    if (!m_scope.package().capabilities.contains(kind == "theme" ? "theme" : "lyricsPage"))
        return reject("Appearance capability is not declared");
    if (!m_scope.active() || !d.isObject())
        return reject("Closed context or invalid appearance definition");
    const auto id = d.property("id").toString();
    const auto title = d.property("title").toString();
    if (!QRegularExpression("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$").match(id).hasMatch() ||
        title.isEmpty() || title.size() > 256)
        return reject("Appearance requires a stable id and title");
    auto *catalog = m_scope.host().theme.extensions();
    // Replacement must not read an older file while the prior writer is draining.
    if (auto *existing = catalog->find(key(id, kind));
        existing && existing->configuration()->busy())
        return reject("Appearance configuration writes are still pending");
    auto *e = new AppearanceEntry(nullptr);
    e->key = key(id, kind);
    e->title = title;
    e->kind = kind;
    e->installation = m_scope.installationId();
    e->description =
        d.property("description").isString() ? d.property("description").toString() : QString();
    e->titlebar = d.property("titlebar").isString() ? d.property("titlebar").toString() : "host";
    e->tools = d.property("tools").isString() ? d.property("tools").toString() : "host";
    auto discard = [&](const QString &reason) {
        delete e;
        return reject(reason);
    };
    const auto icons = d.property("lyricsIcons");
    QStringList iconNames;
    if (!icons.isUndefined()) {
        if (kind != "theme" || !icons.isArray() || icons.property("length").toUInt() > 64)
            return discard("Theme lyricsIcons must be an array of at most 64 names");
        for (quint32 i = 0; i < icons.property("length").toUInt(); ++i) {
            const auto name = icons.property(i);
            if (!name.isString() || name.toString().trimmed().isEmpty() ||
                name.toString().size() > 128)
                return discard(
                    "Theme icon names must be nonempty strings of at most 128 characters");
            iconNames.append(name.toString());
        }
    }
    // Symbolic names are immutable theme data, not paths or renderer-specific
    // icon implementations. A lyrics skin decides how unsupported names render.
    e->definition.insert("lyricsIcons", iconNames);
    if (!QStringList{"host", "none"}.contains(e->titlebar) ||
        !QStringList{"host", "none"}.contains(e->tools))
        return discard("titlebar/tools must be host or none");
    if (!d.property("type").isUndefined())
        e->type = d.property("type").toString();
    if (!d.property("defaultMode").isUndefined())
        e->defaultMode = d.property("defaultMode").toString();
    if (!QStringList{"default", "solid", "dynamic"}.contains(e->type) ||
        !QStringList{"system", "light", "dark"}.contains(e->defaultMode))
        return discard("Invalid theme type or defaultMode");
    for (const auto &variant : QStringList{"light", "dark"}) {
        auto v = d.property(variant);
        if (!v.isUndefined() && (!v.isObject() || v.isArray() || v.isCallable()))
            return discard("Theme variants must be objects");
        auto values = v.isObject() ? v.toVariant().toMap() : QVariantMap();
        QString error;
        if (!validateVisuals(values, error) || !resolveAsset(values, m_scope.package(), error))
            return discard(error);
        e->definition.insert(variant, values);
    }
    const auto componentFields =
        QHash<QString, QString>{{"component", kind == "skin" ? "playback" : "background"},
                                {"backgroundComponent", "background"},
                                {"sidebarComponent", "sidebar"},
                                {"playerComponent", "playerbar"},
                                {"miniComponent", "mini"},
                                {"miniBackgroundComponent", "mini.background"},
                                {"settingsComponent", "settings"},
                                {"queueWallComponent", "queue-wall"}};
    for (auto it = componentFields.begin(); it != componentFields.end(); ++it) {
        auto v = d.property(it.key());
        if (v.isUndefined())
            continue;
        const auto path = m_scope.package().resolve(v.toString());
        if (path.isEmpty() || !path.endsWith(".qml"))
            return discard("Invalid package QML: " + it.key());
        e->components.insert(it.value(), QUrl::fromLocalFile(path));
    }
    if (kind == "skin" && !e->components.contains("playback"))
        return discard("Skin requires a component");
    auto preview = d.property("preview");
    if (preview.isString()) {
        auto path = m_scope.package().resolve(preview.toString());
        if (path.isEmpty())
            return discard("Invalid package preview");
        e->preview = QUrl::fromLocalFile(path);
    }
    const auto defaultsValue = d.property("defaults");
    if (!defaultsValue.isUndefined() &&
        (!defaultsValue.isObject() || defaultsValue.isArray() || defaultsValue.isCallable()))
        return discard("Defaults must be an object");
    for (const auto &callback : QStringList{"validate", "resolve"})
        if (!d.property(callback).isUndefined() && !d.property(callback).isCallable())
            return discard("Appearance callbacks must be functions");
    const auto defaults =
        defaultsValue.isObject() ? defaultsValue.toVariant().toMap() : QVariantMap();
    if (!AppearanceConfiguration::isJson(defaults))
        return discard("Defaults must be JSON");
    const auto validator = d.property("validate");
    QPointer<EchoPluginContext> scope = &m_scope;
    auto validate = [scope, validator](const QVariantMap &values) -> QString {
        if (!scope || !scope->active())
            return "Plugin context has expired";
        if (!validator.isCallable())
            return {};
        QString error;
        auto result = checkedCall(*scope, validator, values, error);
        if (!error.isEmpty())
            return error;
        if (result.isBool())
            return result.toBool() ? QString() : QString("Configuration rejected");
        if (!result.isObject() || result.isArray() ||
            (!result.property("errors").isUndefined() && !result.property("errors").isArray()))
            return "Validation must return boolean or errors";
        const auto errors = result.property("errors").toVariant().toStringList();
        return errors.isEmpty() ? QString() : errors.join("\n");
    };
    const auto validation = validate(defaults);
    if (!validation.isEmpty())
        return discard(validation);
    e->config = new AppearanceConfiguration(
        m_scope.dataPath() + "/appearance/" + kind + "/" + id + ".json", defaults, validate, e);
    e->config->setWriteGuard([scope = QPointer<EchoPluginContext>(&m_scope)] {
        return scope && scope->active() && !scope->storageFrozen();
    });
    m_configurations.removeIf([](auto item) { return !item; });
    m_configurations.append(e->config);
    connect(e->config, &AppearanceConfiguration::drained, &m_scope,
            &EchoPluginContext::resourcesDrained);
    const auto resolver = d.property("resolve");
    if (resolver.isCallable())
        e->resolver = [scope, resolver](const QVariantMap &state, QString &error) {
            if (!scope || !scope->active()) {
                error = "Plugin context expired";
                return QVariantMap();
            }
            const auto result = checkedCall(*scope, resolver, state, error);
            if (!error.isEmpty())
                return QVariantMap();
            if (result.isError() || !result.isObject()) {
                error =
                    result.isError() ? result.toString() : "Theme resolve must return an object";
                return QVariantMap();
            }
            auto values = result.toVariant().toMap();
            if (validateVisuals(values, error))
                resolveAsset(values, scope->package(), error);
            return values;
        };
    e->factory = [scope, configuration = QPointer(e->config),
                  entry = QPointer(e)](const QString &surface, QObject *owner) {
        if (!scope || !scope->active() || !configuration)
            return QVariantMap();
        return scope->appearance()->presentation(surface, owner, configuration, entry);
    };
    auto *lease = m_scope.registration(kind, id);
    if (!lease)
        return discard("Invalid appearance registration");
    e->token = lease->key() + ":" + lease->revision();
    auto entry = QPointer(e);
    // Until commit the private entry belongs to the lease; catalog adopts it only
    // when activation succeeds. Failed activation cannot publish visual resources.
    e->setParent(lease);
    auto *handle = new EchoPluginAppearanceHandle(*lease, *e, lease);
    m_scope.engine().newQObject(handle);
    QQmlEngine::setObjectOwnership(handle, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, handle, [handle, lease, entry] {
        if (!lease->active()) {
            if (entry && entry->parent() == lease) {
                entry->retired = true;
                entry->config->retire();
            }
            handle->setParent(nullptr);
            QQmlEngine::setObjectOwnership(handle, QQmlEngine::JavaScriptOwnership);
            lease->deleteLater();
        }
    });
    lease->stage([catalog, entry] { return entry && catalog->insert(entry); },
                 [catalog, token = e->token] { catalog->remove(token); });
    if (m_scope.committed() && !lease->commit()) {
        lease->dispose();
        return reject("Appearance registration failed");
    }
    m_error.clear();
    emit changed();
    return handle;
}
bool EchoPluginAppearance::useTheme(const QString &id)
{
    return m_scope.active() && m_scope.committed() &&
           m_scope.host().theme.extensions()->chooseTheme(key(id, "theme"));
}
bool EchoPluginAppearance::useSkin(const QString &id)
{
    return m_scope.active() && m_scope.committed() &&
           m_scope.host().theme.extensions()->chooseSkin(id == "cover" ? id : key(id, "skin"));
}
bool EchoPluginAppearance::previewTheme(const QString &id)
{
    return m_scope.active() && m_scope.committed() &&
           m_scope.host().theme.extensions()->previewTheme(key(id, "theme"));
}
void EchoPluginAppearance::cancelPreview()
{
    if (m_scope.active())
        m_scope.host().theme.extensions()->cancelPreview();
}
void EchoPluginAppearance::openThemes()
{
    if (m_scope.active())
        m_scope.host().theme.extensions()->request("themes");
}
void EchoPluginAppearance::openSkins()
{
    if (m_scope.active())
        m_scope.host().theme.extensions()->request("skins");
}
QVariantMap EchoPluginAppearance::presentation(const QString &surface, QObject *owner,
                                               AppearanceConfiguration *config,
                                               AppearanceEntry *entry)
{
    auto *child = m_scope.createPresentation(owner, surface);
    if (!child || !entry)
        return {};
    auto *view = new EchoPluginViewContext(*child, surface, owner);
    const auto epoch = child->accountEpoch();
    auto *configuration = new AppearanceConfiguration(
        *config,
        [scope = QPointer(child), epoch] {
            return scope && scope->active() && scope->accountEpoch() == epoch;
        },
        owner);
    connect(child, &EchoPluginContext::activeChanged, configuration,
            &AppearanceConfiguration::changed);
    if (auto *catalog = qobject_cast<AppearanceCatalog *>(entry->parent()))
        connect(child, &EchoPluginContext::callbackFailed, catalog,
                [catalog, e = QPointer(entry)](const QString &message) {
                    if (e)
                        catalog->fail(e, message);
                });
    return {{"$resourceRoot", m_scope.package().root},
            {"plugin", QVariant::fromValue(child)},
            {"page", QVariant::fromValue(view)},
            {"configuration", QVariant::fromValue(configuration)}};
}
QVariantMap EchoPluginAppearance::snapshot() const
{
    const auto &t = m_scope.host().theme;
    QVariant themeMetadata;
    if (const auto *theme = t.extensions()->theme())
        themeMetadata =
            QVariantMap{{"key", theme->key},
                        {"title", theme->title},
                        {"description", theme->description},
                        {"lyricsIcons", theme->definition.value("lyricsIcons").toStringList()}};
    return {{"dark", t.dark()},
            {"themeMetadata", themeMetadata},
            {"font", t.applicationFont()},
            {"lyricFont", t.lyricBaseFont()},
            {"miniFont", t.miniLyricBaseFont()},
            {"desktopFont", t.desktopLyricBaseFont()},
            {"accent", t.accent()},
            {"text", t.text()},
            {"secondaryText", t.secondaryText()},
            {"shell", t.shell()},
            {"sidebar", t.sidebar()},
            {"main", t.main()},
            {"player", t.player()},
            {"card", t.card()},
            {"elevated", t.elevated()},
            {"border", t.border()},
            {"floating", QVariantMap{{"background", t.elevated()},
                                     {"accent", t.floatingAccent()},
                                     {"text", t.floatingText()},
                                     {"secondaryText", t.floatingSecondaryText()},
                                     {"card", t.floatingCard()},
                                     {"border", t.floatingBorder()},
                                     {"accentText", t.floatingAccentText()}}},
            {"motionEnabled", t.motionEnabled()},
            {"transparency", t.transparency()},
            {"windowFrosted", t.windowFrosted()},
            {"floatingFrosted", t.floatingFrosted()},
            {"selectedTheme", t.extensions()->selectedTheme()},
            {"selectedSkin", t.extensions()->selectedSkin()},
            {"surfaces", t.extensionAppearance()}};
}
EchoPluginFonts::EchoPluginFonts(EchoPluginContext &s) : QObject(&s), m_scope(s)
{
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        if (m_scope.active())
            emit familiesChanged();
    });
}
QStringList EchoPluginFonts::families() const
{
    return m_scope.host().theme.fontFamilies();
}
QFont EchoPluginFonts::build(const QStringList &families, int size, int weight, qreal spacing) const
{
    auto font = m_scope.font();
    if (!families.isEmpty() && !families.contains("follow")) {
        QStringList chain;
        for (const auto &family : families)
            if (QFontDatabase::hasFamily(family))
                chain << family;
        for (const auto &family : font.families())
            if (!chain.contains(family))
                chain << family;
        if (!chain.isEmpty())
            font.setFamilies(chain);
    }
    font.setPixelSize(qBound(1, size, 256));
    font.setWeight(QFont::Weight(qBound(1, weight, 1000)));
    if (std::isfinite(spacing))
        font.setLetterSpacing(QFont::AbsoluteSpacing, qBound(-20., spacing, 100.));
    return font;
}
} // namespace EchoMusic
