#pragma once
#include <QObject>
#include <QJSValue>
#include <QVariantMap>
#include <QPointer>
#include <QHash>
#include <memory>
#include <atomic>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoPluginContext;
class EchoGpuSurface;
class EchoPluginGraphics;
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
        readbacks{32ll * 1024 * 1024};
    std::atomic<int> objects = 0, surfaces = 0;
};
struct GpuResourceDescription
{
    QString kind;
    QVariantMap definition;
    QByteArray data;
    quint64 revision = 1;
    qint64 allocationBytes = 0;
};
class EchoGpuResource final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by plugin.graphics")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
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
        return m_active;
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    friend class EchoPluginGraphics;
    EchoGpuResource(EchoPluginGraphics &owner, QString id, GpuResourceDescription description);
    EchoPluginGraphics &m_owner;
    QString m_id;
    GpuResourceDescription m_description;
    bool m_active = true;
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
    bool active() const;
    bool drained() const;
    void shutdown();
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
    QHash<QString, GpuResourceDescription> descriptions() const;
    bool validateFrame(const QVariantMap &frame, QString &error) const;
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
    EchoGpuResource *create(QString kind, QVariantMap definition, QByteArray data = {});
    QByteArray shader(const QString &path, int stage);
    QByteArray bytes(const QJSValue &data, const QString &format, int maximum);
    void retire(EchoGpuResource &resource);
    EchoPluginContext &m_scope;
    QHash<QString, EchoGpuResource *> m_resources;
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
