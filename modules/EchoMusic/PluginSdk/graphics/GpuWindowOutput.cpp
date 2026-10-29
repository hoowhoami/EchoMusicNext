#include "EchoGpuSurface.h"
#include <QQuickWindow>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QColorSpace>
#include <QColorTransform>
#include <QtGui/qrgbafloat.h>
#include <QMatrix4x4>
#include <rhi/qrhi.h>
namespace EchoMusic
{
namespace
{
QMatrix4x4 gamut(int mode)
{
    QMatrix4x4 matrix;
    if (mode != 2 && mode != 3)
        return matrix;
    const QColorSpace target(mode == 2 ? QColorSpace::Primaries::Bt2020
                                       : QColorSpace::Primaries::DciP3D65,
                             QColorSpace::TransferFunction::Linear);
    const auto transform = QColorSpace(QColorSpace::SRgbLinear).transformationToColorSpace(target);
    for (int column = 0; column < 3; ++column) {
        QRgbaFloat32 basis{column == 0 ? 1.f : 0.f, column == 1 ? 1.f : 0.f,
                           column == 2 ? 1.f : 0.f, 1};
        const auto mapped = transform.map(basis);
        matrix.setColumn(column, QVector4D(mapped.r, mapped.g, mapped.b, 0));
    }
    return matrix;
}
class WindowOutput final : public QObject
{
public:
    WindowOutput(QQuickWindow &window, QQmlEngine &engine) : QObject(&window), m_window(window)
    {
        QQmlComponent definition(&engine,
                                 QUrl("qrc:/echo/graphics/graphics/WindowOutputEffect.qml"));
        m_owner = definition.create();
        if (m_owner) {
            m_owner->setParent(this);
            m_effect = m_owner->property("effect").value<QQmlComponent *>();
        }
        QQmlProperty format(window.contentItem(), "layer.format", &engine);
        m_floatFormat = format.property().enumerator().keyToValue("RGBA16F");
        connect(
            &window, &QQuickWindow::beforeSynchronizing, this,
            [this] {
                auto *sc = m_window.swapChain();
                int mode = 0;
                float white = 100;
                if (sc) {
                    if (sc->format() == QRhiSwapChain::HDRExtendedSrgbLinear)
                        mode = 1;
                    else if (sc->format() == QRhiSwapChain::HDR10)
                        mode = 2;
                    else if (sc->format() == QRhiSwapChain::HDRExtendedDisplayP3Linear)
                        mode = 3;
                    const auto info = sc->hdrInfo();
                    if (info.sdrWhiteLevel > 0)
                        white = info.sdrWhiteLevel;
                }
                if (m_reported == mode && qFuzzyCompare(m_white, white))
                    return;
                m_reported = mode;
                m_white = white;
                QMetaObject::invokeMethod(
                    this, [this, mode, white] { apply(mode, white); }, Qt::QueuedConnection);
            },
            Qt::DirectConnection);
    }
    bool valid() const
    {
        return m_effect && m_floatFormat >= 0;
    }

private:
    void collectContent()
    {
        if (m_mutating || !m_content)
            return;
        m_mutating = true;
        const auto children = m_window.contentItem()->childItems();
        for (auto *item : children)
            if (item != m_content && !m_auxiliary.contains(item))
                item->setParentItem(m_content);
        m_mutating = false;
    }
    void apply(int mode, float white)
    {
        auto *root = m_window.contentItem();
        auto *engine = qmlEngine(&m_window);
        if (!root || !engine)
            return;
        if (mode && !m_content) {
            m_mutating = true;
            m_content = new QQuickItem(root);
            m_content->setObjectName("echoHdrContent");
            m_content->setSize(root->size());
            connect(root, &QQuickItem::widthChanged, m_content,
                    [this, root] { m_content->setWidth(root->width()); });
            connect(root, &QQuickItem::heightChanged, m_content,
                    [this, root] { m_content->setHeight(root->height()); });
            m_mutating = false;
            collectContent();
            connect(root, &QQuickItem::childrenChanged, this, &WindowOutput::collectContent,
                    Qt::UniqueConnection);
        }
        m_mutating = true;
        const auto before = root->childItems();
        if (mode) {
            m_owner->setProperty("transfer", mode);
            m_owner->setProperty("whiteLevel", white);
            m_owner->setProperty("gamutTransform", QVariant::fromValue(gamut(mode)));
            // Preserve encoded UI values in FP16 and convert to the actual
            // linear/PQ output once. SDR windows have no offscreen root layer.
            QQmlProperty(m_content, "layer.format", engine).write(m_floatFormat);
            QQmlProperty(m_content, "layer.effect", engine).write(QVariant::fromValue(m_effect));
            QQmlProperty(m_content, "layer.enabled", engine).write(true);
        } else if (m_content) {
            QQmlProperty(m_content, "layer.enabled", engine).write(false);
            QQmlProperty(m_content, "layer.effect", engine)
                .write(QVariant::fromValue(static_cast<QQmlComponent *>(nullptr)));
        }
        for (auto *item : root->childItems())
            if (item != m_content && !before.contains(item) && !m_auxiliary.contains(item))
                m_auxiliary.append(item);
        m_mutating = false;
        m_window.setProperty("echoHdrOutputActive", bool(mode));
        m_window.update();
    }
    QQuickWindow &m_window;
    QQuickItem *m_content = nullptr;
    QList<QPointer<QQuickItem>> m_auxiliary;
    bool m_mutating = false;
    QObject *m_owner = nullptr;
    QQmlComponent *m_effect = nullptr;
    int m_floatFormat = -1, m_reported = -1;
    float m_white = 0; // last two fields belong only to the render thread
};
} // namespace
void EchoGpuSurface::prepareWindow(QQuickWindow *window)
{
    if (!window || window->isSceneGraphInitialized() ||
        window->property("echoGraphicsOutputPrepared").toBool())
        return;
    auto *engine = qmlEngine(window);
    if (!engine)
        return;
    if (QQmlProperty(window->contentItem(), "layer.enabled", engine).read().toBool())
        return;
    auto *controller = new WindowOutput(*window, *engine);
    if (!controller->valid()) {
        delete controller;
        return;
    }
    window->setProperty("echoGraphicsOutputPrepared", true);
    // Isolated, pinned Qt6.12 hook. Qt validates support and falls back to SDR;
    // only the window owner negotiates swapchain format before initialization.
#ifdef Q_OS_LINUX
    const auto format = QByteArray("hdr10");
#else
    const auto format = QByteArray("scrgb");
#endif
    if (!window->property("_qt_sg_hdr_format").isValid())
        window->setProperty("_qt_sg_hdr_format", format);
}
} // namespace EchoMusic
