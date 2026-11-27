#pragma once
#include <QObject>

class FloatingSurfacePixelsTest : public QObject
{
    Q_OBJECT
private slots:
    void frostReplacesSharpBackdrop_data();
    void frostReplacesSharpBackdrop();
};
