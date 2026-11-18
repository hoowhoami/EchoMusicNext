#include "NativeShaderEffect.h"
#include <QtQuick/private/qsgrhishadereffectnode_p.h>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSGGeometryNode>
#include <QSGMaterialShader>
#include <map>
#include <memory>

namespace EchoMusic
{
namespace
{
class BlendMaterial;
class BlendShader final : public QSGMaterialShader
{
    std::unique_ptr<QSGMaterialShader> delegate;

public:
    BlendShader(const QSGRhiShaderEffectMaterial *source, QSGRendererInterface::RenderMode mode)
        : delegate(source->createShader(mode))
    {
        setShader(VertexStage, source->m_vertexShader);
        setShader(FragmentStage, source->m_fragmentShader);
        setFlag(UpdatesGraphicsPipelineState, true);
    }
    bool updateUniformData(RenderState &, QSGMaterial *, QSGMaterial *) override;
    void updateSampledImage(RenderState &, int, QSGTexture **, QSGMaterial *,
                            QSGMaterial *) override;
    bool updateGraphicsPipelineState(RenderState &, GraphicsPipelineState *, QSGMaterial *,
                                     QSGMaterial *) override;
};
QSGMaterialType *materialType(const QShader &vertex, const QShader &fragment,
                              const QString &composition)
{
    // Types are keyed by immutable shader bytecode, not a private Qt cache pointer that can retire
    // and be reused. The registry owns only small type tokens; no scene/texture/material survives
    // here.
    static QMutex mutex;
    static std::map<QByteArray, std::unique_ptr<QSGMaterialType>> types;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(vertex.serialized());
    hash.addData(fragment.serialized());
    hash.addData(composition.toUtf8());
    const auto key = hash.result();
    QMutexLocker lock(&mutex);
    auto &type = types[key];
    if (!type)
        type = std::make_unique<QSGMaterialType>();
    return type.get();
}
class BlendMaterial final : public QSGMaterial
{
public:
    // Owned by QSGRhiShaderEffectNode as a member. This wrapper never deletes or copies it.
    QSGRhiShaderEffectMaterial *source;
    QSGMaterialType *token = nullptr;
    QShader vertex, fragment;
    QString composition;
    explicit BlendMaterial(QSGRhiShaderEffectMaterial *value) : source(value) {}
    void sync(const QString &mode)
    {
        if (!token || composition != mode || vertex != source->m_vertexShader ||
            fragment != source->m_fragmentShader) {
            composition = mode;
            vertex = source->m_vertexShader;
            fragment = source->m_fragmentShader;
            token = materialType(vertex, fragment, mode);
        }
        setFlag(flags(), false);
        setFlag(source->flags(), true);
        setFlag(Blending, true);
    }
    QSGMaterialType *type() const override
    {
        return token;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode mode) const override
    {
        return new BlendShader(source, mode);
    }
    int compare(const QSGMaterial *other) const override
    {
        const auto *value = static_cast<const BlendMaterial *>(other);
        if (source->type() != value->source->type())
            return std::less<QSGMaterialType *>()(source->type(), value->source->type()) ? -1 : 1;
        return source->compare(value->source);
    }
};
bool BlendShader::updateUniformData(RenderState &state, QSGMaterial *next, QSGMaterial *previous)
{
    return delegate->updateUniformData(state, static_cast<BlendMaterial *>(next)->source,
                                       previous ? static_cast<BlendMaterial *>(previous)->source
                                                : nullptr);
}
void BlendShader::updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                                     QSGMaterial *next, QSGMaterial *previous)
{
    delegate->updateSampledImage(
        state, binding, texture, static_cast<BlendMaterial *>(next)->source,
        previous ? static_cast<BlendMaterial *>(previous)->source : nullptr);
}
bool BlendShader::updateGraphicsPipelineState(RenderState &state, GraphicsPipelineState *pipeline,
                                              QSGMaterial *next, QSGMaterial *previous)
{
    const auto *value = static_cast<BlendMaterial *>(next);
    const bool changed = delegate->updateGraphicsPipelineState(
        state, pipeline, value->source,
        previous ? static_cast<BlendMaterial *>(previous)->source : nullptr);
    Q_UNUSED(changed)
    pipeline->blendEnable = true;
    pipeline->separateBlendFactors = true;
    pipeline->srcColor = value->composition == "exclusion" ? GraphicsPipelineState::OneMinusDstColor
                                                           : GraphicsPipelineState::One;
    pipeline->dstColor =
        value->composition == "additive" || value->composition == "max" ? GraphicsPipelineState::One
        : value->composition == "exclusion" ? GraphicsPipelineState::OneMinusSrcColor
        : value->composition == "screen"    ? GraphicsPipelineState::OneMinusSrcColor
                                            : GraphicsPipelineState::OneMinusSrcAlpha;
    pipeline->srcAlpha = GraphicsPipelineState::One;
    pipeline->dstAlpha = value->composition == "additive" || value->composition == "max"
                             ? GraphicsPipelineState::One
                             : GraphicsPipelineState::OneMinusSrcAlpha;
    pipeline->opColor = value->composition == "max" ? GraphicsPipelineState::BlendOp::Max
                                                    : GraphicsPipelineState::BlendOp::Add;
    pipeline->opAlpha = pipeline->opColor;
    return true;
}
} // namespace
void NativeShaderEffect::setComposition(const QString &value)
{
    if (value != "source-over" && value != "additive" && value != "screen" && value != "max" &&
        value != "exclusion") {
        qmlWarning(this) << "Composition must be source-over, additive, screen, max or exclusion";
        return;
    }
    if (value == m_composition)
        return;
    m_composition = value;
    emit compositionChanged();
    update();
}
QSGNode *NativeShaderEffect::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data)
{
    auto *node = QQuickShaderEffect::updatePaintNode(oldNode, data);
    if (!node)
        return nullptr;
    auto *geometry = static_cast<QSGGeometryNode *>(node);
    auto *wrapper = dynamic_cast<BlendMaterial *>(geometry->material());
    if (!wrapper) {
        wrapper =
            new BlendMaterial(static_cast<QSGRhiShaderEffectMaterial *>(geometry->material()));
        geometry->setMaterial(wrapper);
        // The node's original material remains its private member. Only the wrapper is heap-owned.
        geometry->setFlag(QSGNode::OwnsMaterial, true);
    }
    wrapper->sync(m_composition);
    geometry->markDirty(QSGNode::DirtyMaterial);
    return node;
}
} // namespace EchoMusic
