#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
namespace EchoMusic
{
class EchoGpuSurface;
class EchoGpuResource;
class GlyphRun;
struct GpuGlyphAtlasState;
class EchoGpuGlyphAtlas final : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY changed)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    Q_PROPERTY(QVariantList fragments READ fragments NOTIFY changed)
    Q_PROPERTY(QVariantList metrics READ metrics NOTIFY changed)
    Q_PROPERTY(qreal pixelRatio READ pixelRatio NOTIFY changed)
    Q_PROPERTY(qulonglong textureUploads READ textureUploads NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
public:
    ~EchoGpuGlyphAtlas() override;
    bool active() const
    {
        return m_active;
    }
    bool ready() const;
    quint64 revision() const;
    quint64 generation() const
    {
        return m_generation;
    }
    QVariantList pages() const;
    QVariantList fragments() const
    {
        return m_fragments;
    }
    QVariantList metrics() const;
    qreal pixelRatio() const;
    quint64 textureUploads() const
    {
        return m_uploads;
    }
    QString errorString() const
    {
        return m_error;
    }
    Q_INVOKABLE void dispose();
    // Queued render-thread publication contains only scalar metadata.
    void publish(const std::shared_ptr<GpuGlyphAtlasState> &, quint64, const QVariantList &,
                 const QVariantList &, quint64, const QString &);
    void nativeLost(const std::shared_ptr<GpuGlyphAtlasState> &, quint64);
signals:
    void changed();

private:
    friend class EchoGpuSurface;
    EchoGpuGlyphAtlas(EchoGpuSurface &, GlyphRun &);
    void refresh();
    void activityChanged();
    void clear();
    QPointer<EchoGpuSurface> m_surface;
    QPointer<GlyphRun> m_source;
    std::shared_ptr<GpuGlyphAtlasState> m_state;
    QList<QPointer<EchoGpuResource>> m_pages;
    QVariantList m_fragments;
    QString m_error;
    bool m_active = true, m_refreshing = false;
    quint64 m_generation = 0, m_uploads = 0;
};
} // namespace EchoMusic
