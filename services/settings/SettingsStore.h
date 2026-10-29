#pragma once
#include <QSettings>
#include <QVariantMap>
#include <memory>

namespace EchoMusic
{
enum class SettingsDomain
{
    Portable,
    Device,
    Unknown
};
/// Settings policy belongs to the key declaration, never to a backup whitelist.
SettingsDomain settingsDomain(QStringView key);
QString canonicalStoragePath(const QString &path);

class SettingsStore final : public QObject
{
public:
    using Status = QSettings::Status;
    static constexpr auto NoError = QSettings::NoError;
    static constexpr auto AccessError = QSettings::AccessError;
    static constexpr auto FormatError = QSettings::FormatError;
    explicit SettingsStore(QObject *parent = nullptr);
    SettingsStore(const QString &file, QObject *parent = nullptr);
    QVariant value(QAnyStringView key, const QVariant &fallback = {}) const;
    bool contains(QAnyStringView key) const;
    void setValue(QAnyStringView key, const QVariant &value);
    void remove(QAnyStringView key);
    void clear();
    bool replacePortable(const QVariantMap &values);
    void sync();
    Status status() const;
    QStringList allKeys() const;
    QVariantMap portableValues(QString *error = nullptr) const;
    QString fileName() const
    {
        return m_file;
    }
    QString deviceFileName() const;
    bool isWritable() const;
    QString files(const QString &owner, SettingsDomain domain) const;
    QString portableFilesRoot() const;

private:
    QSettings *store(QStringView key) const;
    QString m_file;
    std::unique_ptr<QSettings> m_portable, m_device;
    bool m_invalid = false;
};
} // namespace EchoMusic
