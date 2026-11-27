#pragma once
#include <QObject>

class PluginRenderLifecycleTest final : public QObject
{
    Q_OBJECT
private slots:
    void retainedSceneGraphKeepsFloatTextureProvider();
    void nativeMaterialsKeepPixelsAcrossSourceAndShaderReplacement();
    void floatTextureUploadsRecoverAndDiscardRetiredReports();
};
