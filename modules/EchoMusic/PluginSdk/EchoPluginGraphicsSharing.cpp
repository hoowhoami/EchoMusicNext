#include "EchoPluginGraphics.h"
#include "EchoPluginContext.h"
#include <QUuid>
namespace EchoMusic
{
QString EchoPluginGraphics::shareResource(EchoGpuResource *resource, const QString &access)
{
    if (!active() || !resource || &resource->m_owner != this || !resource->active() ||
        resource->m_imported || !resource->m_description.shared ||
        (access != "read" && access != "read-write")) {
        fail("Share a live owned non-uniform buffer or texture with read/read-write access");
        return {};
    }
    if (!resource->m_token.isEmpty()) {
        if (resource->m_exportWritable != (access == "read-write")) {
            fail("Revoke sharing before changing export access");
            return {};
        }
        return resource->m_token;
    }
    auto *root = m_scope.activationContext()->graphics();
    resource->m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    resource->m_exportWritable = access == "read-write";
    root->m_exports.insert(resource->m_token, resource);
    emit resource->changed();
    return resource->m_token;
}
EchoGpuResource *EchoPluginGraphics::importResource(const QString &token, const QString &access)
{
    auto *root = m_scope.activationContext()->graphics();
    const auto source = root->m_exports.value(token);
    if (!active() || !source || !source->active() || source->m_token != token ||
        (access != "read" && access != "read-write") ||
        (access == "read-write" && !source->m_exportWritable)) {
        fail("Unknown, retired, foreign-account or insufficient-access shared GPU token");
        return nullptr;
    }
    return create(source->kind(), source->m_description.definition, source->m_description.data,
                  source, access == "read");
}
bool EchoPluginGraphics::revokeSharing(EchoGpuResource *resource)
{
    if (!resource || &resource->m_owner != this || resource->m_imported)
        return fail("Invalid export owner");
    if (resource->m_token.isEmpty())
        return true;
    m_scope.activationContext()->graphics()->m_exports.remove(resource->m_token);
    resource->m_token.clear();
    ++resource->m_exportEpoch;
    emit resource->changed();
    return true;
}
} // namespace EchoMusic
