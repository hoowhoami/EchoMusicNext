#pragma once
#include <QObject>
#include <QJSValue>
#include <QVariantMap>
#include <QPointer>
#include <QHash>
#include <QMutex>
#include <memory>
#include <atomic>
#include <functional>
#include <QSize>
class QQuickItem;
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoPluginContext;
class EchoGpuSurface;
class EchoGpuGlyphAtlas;
struct GpuGlyphAtlasState;
class EchoPluginGraphics;
class EchoPluginOperation;
struct GpuRenderState;
struct GpuByteBudget
{
    explicit GpuByteBudget(qint64 maximum) : limit(maximum) {}
    bool reserve(qint64 amount)
    {
        auto current = used.load();
        do {
            if (amount < 0 || amount > limit - current)
                return false;
        } while (!used.compare_exchange_weak(current, current + amount));
        return true;
    }
    void release(qint64 amount)
    {
        const auto before = used.fetch_sub(amount);
        Q_ASSERT(before >= amount);
        Q_UNUSED(before);
    }
    const qint64 limit;
    std::atomic<qint64> used = 0;
};
struct GpuMemoryBudget
{
    GpuByteBudget native{128ll * 1024 * 1024}, targets{256ll * 1024 * 1024},
        readbacks{32ll * 1024 * 1024}, submissions{64ll * 1024 * 1024};
    std::atomic<int> objects = 0, surfaces = 0, pendingSubmissions = 0;
};
struct NativeGpuResource;
struct GpuSharedResourceState
{
    std::atomic<bool> active{true};
    std::atomic<qint64> generation{0};
    QMutex mutex;
    void *device = nullptr, *thread = nullptr;
    std::weak_ptr<NativeGpuResource> native;
};
struct GpuResourceDescription
{
    QString kind;
    QVariantMap definition;
    QByteArray data;
    quint64 revision = 1;
    qint64 allocationBytes = 0;
    std::shared_ptr<GpuSharedResourceState> shared;
    std::shared_ptr<std::atomic<bool>> lease;
    bool imported = false, readOnly = false;
    struct TextureSource
    {
        std::shared_ptr<GpuGlyphAtlasState> glyphAtlas;
        int glyphPage = 0;
        QPointer<QQuickItem> item;
        std::atomic<bool> active{true}, ready{false};
        std::atomic<qint64> generation{0};
        std::atomic<int> readers{0};
        // Accessed only by the source window's render thread.
        quint64 handle = 0;
        QSize extent;
    };
    std::shared_ptr<TextureSource> textureSource;
};
struct GpuSubmission
{
    ~GpuSubmission();
    std::shared_ptr<GpuMemoryBudget> budget;
    qint64 bytes = 0;
    quint64 serial = 0;
    std::function<void()> retired;
    std::atomic<bool> canceled = false;
    QPointer<EchoPluginOperation> operation;
    QHash<QString, GpuResourceDescription> resources;
    QVariantMap frame;
};
class EchoGpuResource final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by plugin.graphics")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool shared READ shared NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    Q_PROPERTY(qint64 generation READ generation NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
    QString id() const
    {
        return m_id;
    }
    QString kind() const
    {
        return m_description.kind;
    }
    bool active() const
    {
        return m_active && (!m_imported || (m_source && m_source->active() &&
                                            m_exportEpoch == m_source->m_exportEpoch));
    }
    bool shared() const
    {
        return !m_token.isEmpty() || m_imported;
    }
    bool readOnly() const
    {
        return m_description.readOnly;
    }
    qint64 generation() const
    {
        return m_description.textureSource ? m_description.textureSource->generation.load()
               : m_description.shared      ? m_description.shared->generation.load()
                                           : 0;
    }
    bool ready() const
    {
        return active() && (!m_description.textureSource || m_description.textureSource->ready);
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class EchoPluginGraphics;
    friend class EchoGpuSurface;
    friend class EchoGpuGlyphAtlas;
    EchoGpuResource(EchoPluginGraphics &owner, QString id, GpuResourceDescription description);
    EchoPluginGraphics &m_owner;
    QString m_id;
    GpuResourceDescription m_description;
    bool m_active = true, m_imported = false, m_exportWritable = false;
    QPointer<EchoGpuResource> m_source;
    QString m_token;
    quint64 m_exportEpoch = 0;
};
/// GUI logical resources. QRhi objects never cross the plugin or GUI boundary.
class EchoPluginGraphics final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the plugin runtime")
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit EchoPluginGraphics(EchoPluginContext &scope);
    QVariantMap capabilities() const;
    QString errorMessage() const
    {
        return m_error;
    }
    Q_INVOKABLE EchoMusic::EchoGpuResource *createBuffer(const QVariantMap &definition,
                                                         const QJSValue &data = QJSValue());
    Q_INVOKABLE EchoMusic::EchoGpuResource *createTexture(const QVariantMap &definition,
                                                          const QJSValue &data = QJSValue());
    Q_INVOKABLE EchoMusic::EchoGpuResource *createSampler(const QVariantMap &definition = {});
    Q_INVOKABLE EchoMusic::EchoGpuResource *createRenderPipeline(const QVariantMap &definition);
    Q_INVOKABLE EchoMusic::EchoGpuResource *createComputePipeline(const QVariantMap &definition);
    Q_INVOKABLE bool writeBuffer(EchoMusic::EchoGpuResource *resource, const QJSValue &data,
                                 int offset = 0);
    Q_INVOKABLE bool writeTexture(EchoMusic::EchoGpuResource *resource, const QJSValue &data);
    Q_INVOKABLE QString shareResource(EchoMusic::EchoGpuResource *resource,
                                      const QString &access = QStringLiteral("read"));
    Q_INVOKABLE EchoMusic::EchoGpuResource *
    importResource(const QString &token, const QString &access = QStringLiteral("read"));
    Q_INVOKABLE bool revokeSharing(EchoMusic::EchoGpuResource *resource);
    bool active() const;
    bool drained() const;
    void shutdown();
    QString atlasDomain() const;
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
    QHash<QString, GpuResourceDescription> descriptions() const;
    bool validateFrame(const QVariantMap &frame, QString &error) const;
    bool prepareSubmission(const QVariantMap &input, QVariantMap &output, QString &error);
    bool attach(EchoGpuSurface *surface);
    std::shared_ptr<GpuMemoryBudget> memoryBudget() const
    {
        return m_memory;
    }
    void detach(EchoGpuSurface *surface);
    void retainState(const std::shared_ptr<GpuRenderState> &state);
    void report(const QVariantMap &capabilities);
    bool fail(const QString &message);
signals:
    void changed();
    void drainedChanged();
    void resourcesChanged();

private:
    friend class EchoGpuResource;
    friend class EchoGpuSurface;
    friend class EchoGpuGlyphAtlas;
    EchoGpuResource *
    create(QString kind, QVariantMap definition, QByteArray data = {},
           EchoGpuResource *source = nullptr, bool readOnly = false,
           std::shared_ptr<GpuResourceDescription::TextureSource> textureSource = {});
    QByteArray shader(const QString &path, int stage);
    QByteArray bytes(const QJSValue &data, const QString &format, int maximum);
    void retire(EchoGpuResource &resource);
    QString m_atlasDomain;
    EchoPluginContext &m_scope;
    QHash<QString, EchoGpuResource *> m_resources;
    QHash<QString, QPointer<EchoGpuResource>> m_exports;
    QList<QPointer<EchoGpuSurface>> m_surfaces;
    QList<std::shared_ptr<GpuRenderState>> m_retiredStates;
    std::shared_ptr<GpuMemoryBudget> m_memory;
    QVariantMap m_capabilities;
    QString m_error;
    quint64 m_sequence = 0;
    bool m_closed = false;
    qint64 m_bytes = 0;
};
} // namespace EchoMusic
