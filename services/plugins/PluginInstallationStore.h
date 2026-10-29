#pragma once
#include "PluginPaths.h"
#include <QVariantList>

namespace EchoMusic
{
/// One atomic catalog, including retained-data provenance. No EchoMusic::SettingsStore mirror.
class PluginInstallationStore
{
public:
    explicit PluginInstallationStore(QString root) : m_paths(std::move(root)) {}
    QVariantList records(QString *error = nullptr) const;
    QString save(const QVariantList &records) const;
    QVariantList retained(QString *error = nullptr) const;
    QString retain(const QVariantMap &record, bool keep) const;
    QString prepareData(const QString &id, const QVariantMap &origin) const;
    QString removeRetained(const QString &key) const;
    QString detach(const QVariantMap &record, bool keepData) const;
    QString recoverDataMove() const;
    static bool validToken(const QString &token);

private:
    QVariantMap read(QString *error) const;
    QString write(const QVariantMap &catalog) const;
    PluginPaths m_paths;
};
} // namespace EchoMusic
