#pragma once
#include <QQuickItem>
#include <QFont>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace EchoMusic
{
struct GlyphAtlasPreparation;
/// Shaped text fragments backed by immutable ink/halo atlas pages.
class GlyphRun : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool atlasOnly READ atlasOnly WRITE setAtlasOnly NOTIFY layoutChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY layoutChanged)
    Q_PROPERTY(
        bool measurementOnly READ measurementOnly WRITE setMeasurementOnly NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList runs READ runs WRITE setRuns NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList poses READ poses WRITE setPoses NOTIFY posesChanged)
    Q_PROPERTY(QString composition READ composition WRITE setComposition NOTIFY compositionChanged)
    Q_PROPERTY(QVariantList metrics READ metrics NOTIFY layoutChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(qulonglong maskRevision READ maskRevision NOTIFY layoutChanged)
    Q_PROPERTY(qulonglong textureUploads READ textureUploads NOTIFY renderingChanged)
    Q_PROPERTY(int renderBatches READ renderBatches NOTIFY renderingChanged)
    Q_PROPERTY(int supportedMaxHaloRadius READ supportedMaxHaloRadius CONSTANT)
    Q_PROPERTY(int supportedMaxBlur READ supportedMaxBlur CONSTANT)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY layoutChanged)
    Q_PROPERTY(QString haloSampling READ haloSampling WRITE setHaloSampling NOTIFY layoutChanged)
    Q_PROPERTY(qreal maximumRasterScale READ maximumRasterScale WRITE setMaximumRasterScale NOTIFY
                   layoutChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY preparationChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY renderingChanged)
    Q_PROPERTY(qint64 gpuCacheBytes READ gpuCacheBytes NOTIFY renderingChanged)
    Q_PROPERTY(QString sharedCacheKey READ sharedCacheKey NOTIFY renderingChanged)
    Q_PROPERTY(qint64 sharedCacheBytes READ sharedCacheBytes NOTIFY renderingChanged)
public:
    static constexpr int MaxRuns = 128;
    static constexpr int MaxTextUnits = 8192;
    static constexpr int MaxFragments = 2048;
    static constexpr int MaxPages = 8;
    static constexpr int PageSize = 1024;
    static constexpr int MaxEffectsPages = 4;
    static constexpr int MaxInkPageSize = 2048;
    static constexpr int MaxVisibleFontSize = 1024;
    static constexpr qint64 MaxGeometryBytes = 8LL * 1024 * 1024;
    static constexpr int supportedMaxHaloRadius()
    {
        return 1024;
    }
    static constexpr int supportedMaxBlur()
    {
        return 32;
    }
    explicit GlyphRun(QQuickItem *parent = nullptr);
    ~GlyphRun() override;
    bool atlasOnly() const;
    void setAtlasOnly(bool);
    // C++-only authority assigned after plugin/window ownership checks.
    void setAtlasDomain(const QString &);
    void setAtlasConsumerActive(const QObject *, bool);
    void retireAtlasPreparation();
    std::shared_ptr<const GlyphAtlasPreparation> atlasPreparation() const;
    QFont font() const;
    void setFont(const QFont &);
    bool measurementOnly() const;
    void setMeasurementOnly(bool);
    QVariantList runs() const;
    void setRuns(const QVariantList &);
    QVariantList poses() const;
    void setPoses(const QVariantList &);
    QString composition() const;
    void setComposition(const QString &);
    QVariantList metrics() const;
    QString errorString() const;
    quint64 maskRevision() const;
    quint64 textureUploads() const;
    int renderBatches() const;
    qint64 cacheBytes() const;
    QString haloSampling() const;
    void setHaloSampling(const QString &);
    qreal maximumRasterScale() const;
    void setMaximumRasterScale(qreal);
    bool preparing() const;
    bool ready() const;
    qint64 gpuCacheBytes() const;
    QString sharedCacheKey() const;
    qint64 sharedCacheBytes() const;

signals:
    void layoutChanged();
    void posesChanged();
    void errorChanged();
    void renderingChanged();
    void compositionChanged();
    void preparationChanged();
    void atlasChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange, const ItemChangeData &) override;

private:
    struct Data;
    std::unique_ptr<Data> d;
    bool rebuild(const QVariantList &, const QFont &);
    bool rebuild(const QVariantList &, const QFont &, bool measurementOnly,
                 bool clearErrorOnSuccess = true);
    void rebuildForEnvironment();
    bool prepareCanvas(const QVariantList &, const QFont &, bool);
    void cancelPreparation();
    void applyPoses(const QVariantList &, bool clearErrorOnSuccess);
    void setError(const QString &);
};
} // namespace EchoMusic
