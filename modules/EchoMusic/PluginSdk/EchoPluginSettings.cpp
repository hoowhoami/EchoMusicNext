#include "EchoPluginHost.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include "PlaybackPreferences.h"
#include "LyricAppearance.h"
#include "ShellSettings.h"
#include "FloatingPlayerController.h"
#include "ThemeStore.h"
#include <QMetaProperty>
#include <QFontDatabase>
#include <QMetaMethod>
#include <QTimer>
#include <QSet>
#include <cmath>
namespace EchoMusic
{
namespace
{
// Deliberately finite public preference surface. Never enumerate QSettings,
// credentials, paths or arbitrary writable QObject properties.
const QMap<QString, QStringList> fields{
    {"playback",
     {"mvAudioFallbackEnabled", "queueMode", "defaultQuality", "autoPlayOnLaunch",
      "compatibilityMode", "volumeFade", "volumeFadeMs", "viperQualityEnabled", "autoNext",
      "preventSleep", "autoNextDelaySeconds", "autoNextMaxAttempts"}},
    {"shell",
     {"keepAliveEnabled", "keepAliveMax", "motionEnabled", "searchDefaultEnabled",
      "lyricBarrageEnabled", "mvBarrageEnabled", "showFullscreenButton"}},
    {"appearance",
     {"mode", "coverAtmosphere", "atmosphereHeight", "atmosphereStrength", "transparency",
      "windowFrosted", "floatingFrosted", "panelOpacity", "globalFont", "lyricFont",
      "desktopLyricFont", "miniLyricFont", "taskbarLyricFont"}},
    {"lyrics", {"alignPosition",     "enableSpring",
                "highRefreshRate",   "enableBlur",
                "enableScale",       "hidePassedLines",
                "wordFadeWidth",     "dynamicAlbumCover",
                "textColor",         "useCoverColor",
                "fontScale",         "fontWeight",
                "backgroundBlur",    "backgroundRhythm",
                "playedColor",       "unplayedColor",
                "followCoverColor",  "unplayedFollowCoverColor",
                "unplayedTextColor", "unplayedUseCoverColor"}},
    {"mini",
     {"alwaysOnTop", "layout", "alignment", "shadowStrength", "bold", "showNextLinePreview",
      "playedColor", "unplayedColor", "followCoverColor", "unplayedFollowCoverColor", "fontSize",
      "secondaryFontSize", "lineGap", "fontWeight", "backgroundBlur"}},
    {"desktop",
     {"alwaysOnTop", "showUnlockButton", "layout", "alignment", "shadowStrength", "bold",
      "showNextLinePreview", "playedColor", "unplayedColor", "followCoverColor",
      "unplayedFollowCoverColor", "fontSize", "secondaryFontSize", "lineGap", "fontWeight",
      "backgroundBlur"}}};
QVariantMap descriptor(const QString &domain, const QString &key, const QMetaProperty &property)
{
    QVariantMap out{{"key", key},
                    {"type", property.metaType().id() == QMetaType::Bool      ? "boolean"
                             : property.metaType().id() == QMetaType::QString ? "string"
                                                                              : "number"},
                    {"writable", true}};
    const QMap<QString, QPair<double, double>> ranges{
        {"keepAliveMax", {3, 30}},          {"volumeFadeMs", {500, 3000}},
        {"autoNextDelaySeconds", {0, 600}}, {"autoNextMaxAttempts", {1, 999}},
        {"atmosphereHeight", {20, 100}},    {"atmosphereStrength", {20, 200}},
        {"transparency", {0, 100}},         {"panelOpacity", {0, 100}},
        {"alignPosition", {0, 1}},          {"wordFadeWidth", {.05, 1}},
        {"fontScale", {.7, 1.4}},           {"fontSize", {12, 22}},
        {"secondaryFontSize", {10, 18}},    {"lineGap", {0, 12}},
        {"fontWeight", {100, 900}},         {"mode", {0, 3}}};
    if (ranges.contains(key)) {
        out["minimum"] = ranges.value(key).first;
        out["maximum"] = ranges.value(key).second;
    }
    if (out.value("type") == "number")
        out["integer"] = property.metaType().id() != QMetaType::Double;
    QMap<QString, QStringList> choices{
        {"queueMode", {"single", "context"}},
        {"defaultQuality",
         {"standard", "higher", "flac", "hi_res", "viper_tape", "viper_clear", "viper_atmos"}},
        {"layout", {"horizontal", "vertical"}},
        {"alignment", {"left", "center", "right"}},
        {"shadowStrength", {"none", "soft", "normal", "strong"}}};
    if (domain == "desktop")
        choices["alignment"].append("both");
    if (choices.contains(key))
        out["choices"] = choices.value(key);
    if (key == "fontWeight")
        out["choices"] = domain == "lyrics"
                             ? QVariantList{100, 200, 300, 400, 500, 600, 700, 800, 900}
                             : QVariantList{400, 600, 780};
    return out;
}
bool validate(const QVariantMap &d, const QVariant &v, const QString &domain)
{
    const auto type = d.value("type").toString();
    if (type == "boolean")
        return v.metaType().id() == QMetaType::Bool;
    if (type == "string") {
        if (v.metaType().id() != QMetaType::QString || v.toString().size() > 256)
            return false;
        if (d.contains("choices") && !d.value("choices").toStringList().contains(v.toString()))
            return false;
        if (d.value("key").toString().contains("Color") &&
            (!v.toString().isEmpty() || domain == "desktop") && !QColor(v.toString()).isValid())
            return false;
        const auto key = d.value("key").toString();
        if (key.endsWith("Font") && v.toString() != "system-ui" &&
            !(key != "globalFont" && v.toString() == "follow") &&
            !QFontDatabase::hasFamily(v.toString()))
            return false;
        return true;
    }
    if (v.metaType().id() != QMetaType::Double && v.metaType().id() != QMetaType::Int &&
        v.metaType().id() != QMetaType::LongLong && v.metaType().id() != QMetaType::UInt)
        return false;
    const double n = v.toDouble();
    if (!std::isfinite(n) || (d.value("integer").toBool() && std::floor(n) != n) ||
        (d.contains("minimum") && n < d.value("minimum").toDouble()) ||
        (d.contains("maximum") && n > d.value("maximum").toDouble()))
        return false;
    if (d.value("key") == "fontWeight") {
        bool found = false;
        for (const auto &choice : d.value("choices").toList())
            found |= choice.toDouble() == n;
        if (!found)
            return false;
    }
    return true;
}
} // namespace
EchoPluginSettings::EchoPluginSettings(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    // Even the payload-free Qt notification must stop after revocation or
    // retirement. Authorized observers still own their per-account leases.
    const auto gate = [this] { blockSignals(!m_scope.permissions()->granted("hostSettings")); };
    gate();
    if (scope.host().permissions)
        connect(scope.host().permissions, &PluginPermissionStore::changed, this, gate);
    connect(&scope, &EchoPluginContext::activeChanged, this, gate);
    const auto signal = metaObject()->method(metaObject()->indexOfSignal("sourceChanged()"));
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto *source = owner(it.key());
        if (!source)
            continue;
        QSet<int> connected;
        for (const auto &name : it.value()) {
            const auto prop = source->metaObject()->property(
                source->metaObject()->indexOfProperty(name.toUtf8()));
            if (prop.hasNotifySignal() && !connected.contains(prop.notifySignalIndex())) {
                connected.insert(prop.notifySignalIndex());
                connect(source, prop.notifySignal(), this, signal);
            }
        }
    }
}
QObject *EchoPluginSettings::owner(const QString &domain) const
{
    if (domain == "playback")
        return m_scope.host().playback.preferences();
    if (domain == "shell")
        return m_scope.host().settings;
    if (domain == "appearance")
        return &m_scope.host().theme;
    if (domain == "lyrics")
        return m_scope.host().playback.lyricAppearance();
    if (domain == "mini" && m_scope.host().floating)
        return m_scope.host().floating->mini();
    if (domain == "desktop" && m_scope.host().floating)
        return m_scope.host().floating->desktop();
    return nullptr;
}
QVariantList EchoPluginSettings::catalog() const
{
    QVariantList out;
    if (!m_scope.active())
        return out;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto *source = owner(it.key());
        if (!source)
            continue;
        QVariantList properties;
        for (const auto &key : it.value()) {
            const int index = source->metaObject()->indexOfProperty(key.toUtf8());
            if (index >= 0)
                properties.append(descriptor(it.key(), key, source->metaObject()->property(index)));
        }
        out.append(QVariantMap{{"domain", it.key()}, {"properties", properties}});
    }
    return out;
}
QVariantMap EchoPluginSettings::snapshot(const QString &domain) const
{
    QVariantMap out;
    auto *source = owner(domain);
    if (source)
        for (const auto &name : fields.value(domain)) {
            auto v = source->property(name.toUtf8());
            if (v.isValid())
                out[name] = name == "mode" ? QVariant(v.toInt()) : v;
        }
    return out;
}
EchoPluginOperation *EchoPluginSettings::read(const QString &domain)
{
    if (!fields.contains(domain) || !owner(domain))
        return m_scope.rejected("Unavailable", "Unknown or unavailable settings domain");
    return m_scope.permissions()->authorize(
        "hostSettings", tr("读取宿主偏好设置"),
        [this, domain](auto *op) { op->succeed(snapshot(domain)); });
}
EchoPluginOperation *EchoPluginSettings::update(const QString &domain, const QVariantMap &patch)
{
    if (!fields.contains(domain) || !owner(domain) || patch.isEmpty())
        return m_scope.rejected("InvalidArgument", "Invalid settings domain or empty patch");
    auto *source = owner(domain);
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const int index = source->metaObject()->indexOfProperty(it.key().toUtf8());
        if (!fields.value(domain).contains(it.key()) || index < 0 ||
            !validate(descriptor(domain, it.key(), source->metaObject()->property(index)),
                      it.value(), domain))
            return m_scope.rejected("InvalidArgument", "Invalid preference: " + it.key());
    }
    return m_scope.permissions()->authorize(
        "hostSettings", tr("更改并保存宿主偏好设置"), [this, domain, patch](auto *op) {
            auto *source = owner(domain);
            if (!source) {
                op->fail("Unavailable", "Settings owner is unavailable");
                return;
            }
            if (domain == "playback" &&
                patch.value("defaultQuality").toString().startsWith("viper_") &&
                !patch.value("viperQualityEnabled", source->property("viperQualityEnabled"))
                     .toBool()) {
                op->fail("InvalidArgument",
                         "Enable Viper quality before selecting a Viper default");
                return;
            }
            // Apply the dependent switch first, through its original owner setter.
            if (patch.contains("viperQualityEnabled"))
                source->setProperty("viperQualityEnabled", patch.value("viperQualityEnabled"));
            for (auto it = patch.begin(); it != patch.end(); ++it) {
                if (it.key() == "viperQualityEnabled")
                    continue;
                if (domain == "appearance" && it.key() == "mode")
                    m_scope.host().theme.setMode(ThemeStore::Mode(it.value().toInt()));
                else if (domain == "appearance" && it.key() == "panelOpacity")
                    m_scope.host().theme.setPanelOpacity(it.value().toInt());
                else
                    source->setProperty(it.key().toUtf8(), it.value());
            }
            op->succeed(snapshot(domain));
        });
}
EchoPluginOperation *EchoPluginSettings::observe(const QString &id, const QString &domain,
                                                 const QJSValue &callback)
{
    if (!fields.contains(domain) || !owner(domain) || !callback.isCallable())
        return m_scope.rejected("InvalidArgument", "Invalid settings observer");
    return m_scope.permissions()->authorize(
        "hostSettings", tr("订阅宿主偏好设置变化"), [this, id, domain, callback](auto *op) {
            auto *lease = m_scope.registration("hostSettings", id);
            if (!lease) {
                op->fail("InvalidArgument", "Invalid observer ID");
                return;
            }
            connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
                if (!lease->active()) {
                    lease->setParent(nullptr);
                    QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
                }
            });
            auto connections = std::make_shared<QList<QMetaObject::Connection>>();
            auto last = std::make_shared<QVariantMap>();
            auto queued = std::make_shared<bool>(false);
            const auto notify = [this, lease, callback, last, queued, domain] {
                if (std::exchange(*queued, true))
                    return;
                QTimer::singleShot(0, lease, [this, lease, callback, last, queued, domain] {
                    *queued = false;
                    if (!lease->active() || !m_scope.active())
                        return;
                    const auto value = snapshot(domain);
                    if (*last == value)
                        return;
                    *last = value;
                    m_scope.call(callback, {m_scope.engine().toScriptValue(value)});
                });
            };
            lease->stage(
                [this, lease, connections, notify] {
                    connections->append(
                        connect(this, &EchoPluginSettings::sourceChanged, lease, notify));
                    connections->append(connect(&m_scope, &EchoPluginContext::sessionInvalidated,
                                                lease, &EchoPluginRegistration::dispose));
                    connections->append(connect(
                        m_scope.host().permissions, &PluginPermissionStore::revoked, lease,
                        [this, lease](const auto &installation, const auto &cap) {
                            if (installation == m_scope.installationId() && cap == "hostSettings")
                                lease->dispose();
                        }));
                    notify();
                    return true;
                },
                [connections] {
                    for (auto c : *connections)
                        QObject::disconnect(c);
                    connections->clear();
                });
            if (m_scope.committed() && !lease->commit()) {
                lease->dispose();
                op->fail("Unavailable", "Observer unavailable");
                return;
            }
            op->succeed(QVariant::fromValue(lease));
        });
}
} // namespace EchoMusic
