#include "EchoPluginAssetArchive.h"
#include "EchoPluginAssets.h"

namespace EchoMusic
{
EchoPluginAssetArchive::EchoPluginAssetArchive(EchoPluginAssets &assets)
    : QObject(&assets), m_assets(&assets)
{
}
EchoPluginAssetArchive::~EchoPluginAssetArchive()
{
    if (m_assets && m_active)
        m_assets->releaseArchive(this);
}
EchoPluginOperation *EchoPluginAssetArchive::importInto(const QString &collection,
                                                        const QVariantList &entries)
{
    return m_assets ? m_assets->importArchive(this, collection, entries) : nullptr;
}
void EchoPluginAssetArchive::dispose()
{
    if (!m_active)
        return;
    m_active = false;
    m_ready = false;
    m_entries.clear();
    m_json.clear();
    if (m_assets)
        m_assets->releaseArchive(this);
    emit changed();
    deleteLater();
}
} // namespace EchoMusic
