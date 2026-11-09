#pragma once
#include <QFont>
#include <QObject>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Thin adapter for native tight/outline bounds and QTextLayout line wrapping.
/// No custom glyph engine; ascent/line-box centers are not ink centers.
class EchoTextInkMetrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
    Q_PROPERTY(
        qreal layoutWidth READ layoutWidth WRITE setLayoutWidth NOTIFY layoutWidthChanged FINAL)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY wrapChanged FINAL)
    Q_PROPERTY(bool elide READ elide WRITE setElide NOTIFY elideChanged FINAL)
    Q_PROPERTY(QString displayText READ displayText NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal naturalWidth READ naturalWidth NOTIFY naturalWidthChanged FINAL)
    Q_PROPERTY(qreal inkCenterFromBaseline READ inkCenterFromBaseline NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal firstLineInkCenterFromBaseline READ firstLineInkCenterFromBaseline NOTIFY
                   metricsChanged FINAL)
    Q_PROPERTY(
        qreal fontCapCenterFromBaseline READ fontCapCenterFromBaseline NOTIFY fontChanged FINAL)
    Q_PROPERTY(bool hasInk READ hasInk NOTIFY metricsChanged FINAL)
public:
    explicit EchoTextInkMetrics(QObject *parent = nullptr) : QObject(parent) {}
    QFont font() const
    {
        return font_;
    }
    QString text() const
    {
        return text_;
    }
    qreal layoutWidth() const
    {
        return width_;
    }
    qreal lineHeight() const
    {
        return lineHeight_;
    }
    bool wrap() const
    {
        return wrap_;
    }
    bool elide() const
    {
        return elide_;
    }
    QString displayText() const
    {
        return displayText_;
    }
    qreal naturalWidth() const
    {
        return naturalWidth_;
    }
    qreal inkCenterFromBaseline() const
    {
        return inkCenter_;
    }
    qreal firstLineInkCenterFromBaseline() const
    {
        return firstLineInkCenter_;
    }
    bool hasInk() const
    {
        return hasInk_;
    }
    qreal fontCapCenterFromBaseline() const;
    void setFont(const QFont &font);
    void setText(const QString &text);
    void setLayoutWidth(qreal width);
    void setLineHeight(qreal height);
    void setWrap(bool wrap);
    void setElide(bool elide);
signals:
    void fontChanged();
    void textChanged();
    void layoutWidthChanged();
    void lineHeightChanged();
    void wrapChanged();
    void elideChanged();
    void naturalWidthChanged();
    void metricsChanged();

private:
    void measure();
    QFont font_;
    QString text_, displayText_;
    QString outlineText_;
    QRectF outlineBounds_;
    qreal width_ = 0, lineHeight_ = 0, naturalWidth_ = 0, inkCenter_ = 0, firstLineInkCenter_ = 0;
    bool wrap_ = false, elide_ = false, hasInk_ = false;
};
} // namespace EchoMusic
