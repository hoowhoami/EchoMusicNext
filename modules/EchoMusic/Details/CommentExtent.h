#pragma once
#include "CommentsController.h"
#include <QFont>
#include <QHash>
#include <QPointer>
#include <QTimer>

namespace EchoMusic
{
// View geometry only. Unknown rows use Qt text layout, never the current
// viewport's average height multiplied by the whole comment count.
class CommentExtent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::CommentsController *controller READ controller WRITE setController NOTIFY
                   configurationChanged)
    Q_PROPERTY(qreal layoutWidth READ layoutWidth WRITE setLayoutWidth NOTIFY configurationChanged)
    Q_PROPERTY(
        qreal viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY configurationChanged)
    Q_PROPERTY(QFont bodyFont READ bodyFont WRITE setBodyFont NOTIFY configurationChanged)
    Q_PROPERTY(QFont timeFont READ timeFont WRITE setTimeFont NOTIFY configurationChanged)
    Q_PROPERTY(QFont quoteFont READ quoteFont WRITE setQuoteFont NOTIFY configurationChanged)
    Q_PROPERTY(qreal totalHeight READ totalHeight NOTIFY changed)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY configurationChanged)
public:
    explicit CommentExtent(QObject *parent = nullptr);
    CommentsController *controller() const
    {
        return m_controller;
    }
    void setController(CommentsController *);
    qreal layoutWidth() const
    {
        return m_width;
    }
    void setLayoutWidth(qreal);
    qreal viewportWidth() const
    {
        return m_viewportWidth;
    }
    void setViewportWidth(qreal);
    QFont bodyFont() const
    {
        return m_bodyFont;
    }
    void setBodyFont(const QFont &);
    QFont timeFont() const
    {
        return m_timeFont;
    }
    void setTimeFont(const QFont &);
    QFont quoteFont() const
    {
        return m_quoteFont;
    }
    void setQuoteFont(const QFont &);
    qreal totalHeight() const
    {
        return m_total;
    }
    Q_INVOKABLE void record(const QString &key, qreal height);
    Q_INVOKABLE qreal heightAt(int row) const;
    bool compact() const
    {
        return m_compact;
    }
    void setCompact(bool);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
signals:
    void changed();
    void configurationChanged();

private:
    struct Height
    {
        qreal value;
    };
    void invalidate();
    void schedule();
    void sync();
    void notify();
    qreal estimate(const QVariantMap &, const QString &) const;
    static QString key(const QVariantMap &, const QString &, const QString &);
    QPointer<CommentsController> m_controller;
    QList<QMetaObject::Connection> m_connections;
    QList<QString> m_keys;
    QHash<QString, Height> m_heights;
    QHash<QString, int> m_occurrences;
    QFont m_bodyFont, m_timeFont, m_quoteFont;
    QTimer m_reflow;
    qreal m_width = 0, m_total = 0;
    qreal m_viewportWidth = 1e9;
    bool m_pending = false, m_notifying = false;
    bool m_compact = false;
    bool m_active = true, m_dirty = false;
};
} // namespace EchoMusic
