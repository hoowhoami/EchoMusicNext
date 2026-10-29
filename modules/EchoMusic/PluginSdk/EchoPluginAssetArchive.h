#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace EchoMusic
{
class EchoPluginAssets;
struct EchoPluginArchiveSession;

/// Scope-owned, bounded private ZIP snapshot. No source or destination paths are exposed.
class EchoPluginAssetArchive final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by EchoPluginAssets.openImageArchive")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantMap jsonFiles READ jsonFiles NOTIFY changed)
public:
    ~EchoPluginAssetArchive() override;
    bool active() const
    {
        return m_active;
    }
    bool ready() const
    {
        return m_ready;
    }
    QString errorString() const
    {
        return m_error;
    }
    QVariantList entries() const
    {
        return m_entries;
    }
    QVariantMap jsonFiles() const
    {
        return m_json;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *importInto(const QString &collection,
                                                           const QVariantList &entries);
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class EchoPluginAssets;
    explicit EchoPluginAssetArchive(EchoPluginAssets &assets);
    QPointer<EchoPluginAssets> m_assets;
    std::shared_ptr<EchoPluginArchiveSession> m_session;
    QVariantList m_entries;
    QVariantMap m_json;
    QString m_error;
    bool m_active = true, m_ready = false;
};
} // namespace EchoMusic
