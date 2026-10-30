#pragma once
#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoGpuSurface.h"
namespace
{
struct GraphicsFixture
{
    Host host;
    EchoPluginContext *scope = nullptr;
    EchoPluginGraphics *gpu = nullptr;
    QList<QJSValue> roots;
    GraphicsFixture()
    {
        const auto source = package(host.temporary.path(), "export function activate(c) {}",
                                    {"lifecycle", "operations", "graphics.rhi", "graphics.indirect",
                                     "graphics.textureArrays", "graphics.textureDimensions",
                                     "graphics.sharedResources", "graphics.vertexStreams",
                                     "graphics.textureSources"},
                                    {"graphics"});
        for (const auto &name :
             {"fixture.vert", "fixture.frag", "solid.frag", "indirect.comp", "array.frag",
              "array.comp", "cube.frag", "volume.frag", "volume.comp", "shared.comp",
              "instanced.vert", "instanced.frag", "matrix.vert", "integer.vert", "source.frag"}) {
            QFile shader(":/test-gpu/shaders/gpu/" + QString(name) + ".qsb");
            if (!shader.open(QIODevice::ReadOnly))
                return;
            write(source + '/' + name + ".qsb", shader.readAll());
        }
        host.runtime.importDirectory(source);
        if (QTest::qWaitFor(
                [&] { return host.runtime.state(host.installation()) == PluginRuntime::Active; },
                5000)) {
            scope = host.runtime.context(host.installation());
            gpu = scope->graphics();
        }
    }
    EchoPluginOperation *keep(EchoPluginOperation *op)
    {
        if (op)
            roots.append(host.engine.newQObject(op));
        return op;
    }
    bool success(EchoPluginOperation *op)
    {
        keep(op);
        return op && QTest::qWaitFor([&] { return op->finished(); }, 7000) &&
               op->state() == EchoPluginOperation::Succeeded;
    }
    EchoGpuResource *pipeline(const QString &fragment = "solid.frag", bool depth = false)
    {
        return gpu->createRenderPipeline(
            {{"vertexShader", "fixture.vert.qsb"},
             {"fragmentShader", fragment + ".qsb"},
             {"blend", "none"},
             {"topology", "triangle-strip"},
             {"depthTest", depth},
             {"vertexLayout",
              QVariantMap{
                  {"stride", 8},
                  {"attributes", QVariantList{QVariantMap{
                                     {"location", 0}, {"offset", 0}, {"format", "float2"}}}}}}});
    }
    EchoGpuResource *vertices()
    {
        return gpu->createBuffer({{"size", 32}, {"usage", QStringList{"vertex"}}},
                                 host.engine.evaluate("[-1,1,1,1,-1,-1,1,-1]"));
    }
    bool pixels(EchoGpuSurface &surface, EchoGpuResource *image, QVariantMap options, int r, int g,
                int b)
    {
        auto *op = surface.readback(image, options);
        if (!success(op))
            return false;
        const auto result = op->result().toMap();
        if (options.contains("layer") && result.value("layer") != options.value("layer"))
            return false;
        const auto data = result.value("data").toByteArray();
        if (data.isEmpty() ||
            data.size() != result.value("width").toInt() * result.value("height").toInt() * 4)
            return false;
        for (int i = 0; i < data.size(); i += 4)
            if (qAbs(int(quint8(data[i])) - r) > 1 || qAbs(int(quint8(data[i + 1])) - g) > 1 ||
                qAbs(int(quint8(data[i + 2])) - b) > 1 || quint8(data[i + 3]) != 255) {
                qWarning() << "Unexpected pixel" << i << quint8(data[i]) << quint8(data[i + 1])
                           << quint8(data[i + 2]) << quint8(data[i + 3]);
                return false;
            }
        return true;
    }
};
QVariantMap bind(int n, QString kind, QString id, QString access = "read")
{
    return {{"binding", n}, {"kind", kind}, {"resource", id}, {"access", access}};
}
} // namespace
