#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Native QTextLayout paragraph spacing for selectable, plain comment text.
/// Does not alter the complete Theme font or reconstruct individual glyphs.
class CommentTextStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
    Q_PROPERTY(qreal firstLineBaseline READ firstLineBaseline NOTIFY layoutChanged)
public:
    explicit CommentTextStyle(QObject *p = nullptr) : QObject(p) {}
    QQuickTextDocument *document() const
    {
        return m_document;
    }
    void setDocument(QQuickTextDocument *);
    qreal lineHeight() const
    {
        return m_lineHeight;
    }
    void setLineHeight(qreal height);
    qreal firstLineBaseline() const;
signals:
    void documentChanged();
    void lineHeightChanged();
    void layoutChanged();

private:
    void attach();
    void apply();
    QPointer<QQuickTextDocument> m_document;
    QPointer<QTextDocument> m_text;
    QMetaObject::Connection m_documentChanged, m_contents, m_layoutUpdate;
    bool m_applying = false;
    qreal m_lineHeight = 24.5;
};
} // namespace EchoMusic
