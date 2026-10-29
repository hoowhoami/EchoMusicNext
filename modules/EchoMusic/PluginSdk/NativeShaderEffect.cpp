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
QSGMaterialType *materialType(const QShader &vertex, const QShader &fragment, bool additive)
{
    // Types are keyed by immutable shader bytecode, not a private Qt cache pointer that can retire
    // and be reused. The registry owns only small type tokens; no scene/texture/material survives
    // here.
    static QMutex mutex;
    static std::map<QByteArray, std::unique_ptr<QSGMaterialType>> types;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(vertex.serialized());
    hash.addData(fragment.serialized());
    hash.addData(additive ? "additive" : "source-over");
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
    bool additive = false;
    explicit BlendMaterial(QSGRhiShaderEffectMaterial *value) : source(value) {}
    void sync(bool mode)
    {
        if (!token || additive != mode || vertex != source->m_vertexShader ||
            fragment != source->m_fragmentShader) {
            additive = mode;
            vertex = source->m_vertexShader;
            fragment = source->m_fragmentShader;
            token = materialType(vertex, fragment, mode);
        }
        setFlag(flags(), false);
        setFlag(source->flags(), true);
        if (additive)
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
    if (!value->additive)
        return changed;
    pipeline->blendEnable = true;
    pipeline->separateBlendFactors = true;
    pipeline->srcColor = GraphicsPipelineState::One;
    pipeline->dstColor = GraphicsPipelineState::One;
    pipeline->srcAlpha = GraphicsPipelineState::One;
    pipeline->dstAlpha = GraphicsPipelineState::One;
    pipeline->opColor = GraphicsPipelineState::BlendOp::Add;
    pipeline->opAlpha = GraphicsPipelineState::BlendOp::Add;
    return true;
}
} // namespace
void NativeShaderEffect::setComposition(const QString &value)
{
    if (value != QStringLiteral("source-over") && value != QStringLiteral("additive")) {
        qmlWarning(this) << "Composition must be source-over or additive";
        return;
    }
    const bool additive = value == QStringLiteral("additive");
    if (additive == m_additive)
        return;
    m_additive = additive;
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
    wrapper->sync(m_additive);
    geometry->markDirty(QSGNode::DirtyMaterial);
    return node;
}
} // namespace EchoMusic
