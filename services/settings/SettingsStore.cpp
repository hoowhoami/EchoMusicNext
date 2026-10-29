#include "SettingsStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>

namespace EchoMusic
{
namespace
{
struct Definition
{
    QLatin1StringView key;
    SettingsDomain domain;
    bool prefix;
};
constexpr Definition definitions[]{
#define ECHO_SETTING(owner, name, key, policy)                                                     \
    {QLatin1StringView(key), SettingsDomain::policy, false},
#define ECHO_PREFIX(owner, name, key, policy)                                                      \
    {QLatin1StringView(key), SettingsDomain::policy, true},
#define ECHO_FIELD(owner, name, key)
#include "SettingsKeys.def"
#undef ECHO_SETTING
#undef ECHO_PREFIX
#undef ECHO_FIELD
};
QString defaultFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           "/portable/settings.ini";
}
} // namespace
QString canonicalStoragePath(const QString &path)
{
    QFileInfo info(QDir::cleanPath(path));
    QStringList missing;
    while (!info.exists() && !info.fileName().isEmpty()) {
        missing.prepend(info.fileName());
        info.setFile(info.dir().absolutePath());
    }
    const auto base = info.canonicalFilePath();
    return QDir::cleanPath((base.isEmpty() ? info.absoluteFilePath() : base) +
                           (missing.isEmpty() ? QString() : "/" + missing.join('/')));
}
SettingsDomain settingsDomain(QStringView key)
{
    if (key.isEmpty() || key.size() > 1024 || key.contains(u'\0') || key.contains(u'\\') ||
        key.startsWith(u'/') || key.contains(QStringView(u"..")))
        return SettingsDomain::Unknown;
    auto domain = SettingsDomain::Unknown;
    qsizetype length = -1;
    for (const auto &definition : definitions) {
        if (key == definition.key)
            return definition.domain;
        if (definition.prefix && key.startsWith(definition.key) && definition.key.size() > length) {
            domain = definition.domain;
            length = definition.key.size();
        }
    }
    return domain;
}
SettingsStore::SettingsStore(QObject *parent) : SettingsStore(defaultFile(), parent) {}
SettingsStore::SettingsStore(const QString &file, QObject *parent)
    : QObject(parent), m_file(canonicalStoragePath(file)),
      m_portable(std::make_unique<QSettings>(m_file, QSettings::IniFormat)),
      m_device(std::make_unique<QSettings>(deviceFileName(), QSettings::IniFormat))
{
    m_portable->setFallbacksEnabled(false);
    m_device->setFallbacksEnabled(false);
}
QString SettingsStore::deviceFileName() const
{
    if (QFileInfo(m_file).isDir())
        return m_file + "/local/settings.ini";
    auto directory = QFileInfo(m_file).dir();
    if (directory.dirName() == "portable")
        directory = QDir(QFileInfo(directory.absolutePath()).absolutePath());
    return directory.filePath("local/" + QFileInfo(m_file).fileName());
}
QString SettingsStore::portableFilesRoot() const
{
    return QFileInfo(m_file).dir().filePath("files");
}
QString SettingsStore::files(const QString &owner, SettingsDomain domain) const
{
    static const QRegularExpression name("^[a-z][a-z0-9-]{0,63}$");
    if (!name.match(owner).hasMatch() || domain == SettingsDomain::Unknown)
        return {};
    const auto base = domain == SettingsDomain::Portable
                          ? portableFilesRoot()
                          : QFileInfo(deviceFileName()).dir().filePath("files");
    return base + '/' + owner;
}
QSettings *SettingsStore::store(QStringView key) const
{
    switch (settingsDomain(key)) {
    case SettingsDomain::Portable:
        return m_portable.get();
    case SettingsDomain::Device:
        return m_device.get();
    default:
        return nullptr;
    }
}
QVariant SettingsStore::value(QAnyStringView key, const QVariant &fallback) const
{
    const auto name = key.toString();
    auto *backend = store(name);
    return backend ? backend->value(name, fallback) : fallback;
}
bool SettingsStore::contains(QAnyStringView key) const
{
    const auto name = key.toString();
    auto *backend = store(name);
    return backend && backend->contains(name);
}
void SettingsStore::setValue(QAnyStringView key, const QVariant &value)
{
    const auto name = key.toString();
    auto *backend = store(name);
    if (!backend || QFileInfo(m_file).isDir()) {
        m_invalid = true;
        return;
    }
    backend->setValue(name, value);
}
void SettingsStore::remove(QAnyStringView key)
{
    const auto name = key.toString();
    // A parent prefix may own settings in both domains; remove only its descendants.
    for (auto *backend : {m_portable.get(), m_device.get()})
        backend->remove(name);
}
bool SettingsStore::replacePortable(const QVariantMap &values)
{
    for (auto it = values.begin(); it != values.end(); ++it)
        if (settingsDomain(it.key()) != SettingsDomain::Portable) {
            m_invalid = true;
            return false;
        }
    m_portable->clear();
    for (auto it = values.begin(); it != values.end(); ++it)
        m_portable->setValue(it.key(), it.value());
    return status() == NoError;
}
void SettingsStore::clear()
{
    m_portable->clear();
    m_device->clear();
    m_invalid = false;
}
void SettingsStore::sync()
{
    m_portable->sync();
    m_device->sync();
}
SettingsStore::Status SettingsStore::status() const
{
    if (m_invalid)
        return FormatError;
    if (m_portable->status() != NoError)
        return m_portable->status();
    return m_device->status();
}
bool SettingsStore::isWritable() const
{
    return m_portable->isWritable() && m_device->isWritable();
}
QStringList SettingsStore::allKeys() const
{
    auto keys = m_portable->allKeys();
    keys.append(m_device->allKeys());
    keys.removeDuplicates();
    keys.sort();
    return keys;
}
QVariantMap SettingsStore::portableValues(QString *error) const
{
    QVariantMap result;
    if (error)
        error->clear();
    if (m_invalid || m_portable->status() != NoError) {
        if (error)
            *error = "Portable settings cannot be read reliably";
        return {};
    }
    for (const auto &key : m_portable->allKeys()) {
        if (settingsDomain(key) != SettingsDomain::Portable) {
            if (error)
                *error = "Portable settings contain an unclassified or local key";
            return {};
        }
        result.insert(key, m_portable->value(key));
    }
    return result;
}
} // namespace EchoMusic
