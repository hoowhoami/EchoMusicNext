#pragma once
#include "LyricModel.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// View-local presentation schedule. Source lyrics and word timestamps remain
/// immutable: other lyric surfaces, seeking and persistence use the source model.
class LyricTimeline : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *rows READ rows CONSTANT)
    Q_PROPERTY(EchoMusic::LyricModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool filter READ filter WRITE setFilter NOTIFY modelChanged)
    Q_PROPERTY(double timeMs READ timeMs WRITE setTimeMs NOTIFY timeChanged)
    Q_PROPERTY(int focusIndex READ focusIndex NOTIFY changed)
    Q_PROPERTY(int latestIndex READ latestIndex NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int gapAnchor READ gapAnchor NOTIFY changed)
    Q_PROPERTY(double gapStart READ gapStart NOTIFY changed)
    Q_PROPERTY(double gapEnd READ gapEnd NOTIFY changed)
    Q_PROPERTY(bool dynamic READ dynamic NOTIFY changed)
public:
    explicit LyricTimeline(QObject *parent = nullptr);
    QAbstractItemModel *rows() const
    {
        return m_rows;
    }
    LyricModel *model() const
    {
        return m_model;
    }
    void setModel(LyricModel *model);
    bool filter() const
    {
        return m_filter;
    }
    void setFilter(bool filter);
    double timeMs() const
    {
        return m_time;
    }
    void setTimeMs(double time);
    int focusIndex() const;
    int latestIndex() const;
    int revision() const
    {
        return m_revision;
    }
    int gapAnchor() const;
    double gapStart() const;
    double gapEnd() const;
    bool dynamic() const
    {
        return m_dynamic;
    }
    Q_INVOKABLE bool active(int row) const;
    Q_INVOKABLE bool highlighted(int row) const;
    Q_INVOKABLE double startAt(int row) const;
    Q_INVOKABLE double endAt(int row) const;
    Q_INVOKABLE int ordinal(int row) const;
    Q_INVOKABLE int nextRow(int row) const
    {
        return rowFor(ordinal(row) + 1);
    }
    Q_INVOKABLE int previousRow(int row) const
    {
        return rowFor(ordinal(row) - 1);
    }
    Q_INVOKABLE void reset(double time);
signals:
    void modelChanged();
    void timeChanged();
    void changed();

private:
    struct Bounds
    {
        int row;
        double start, end;
    };
    struct Gap
    {
        int anchor;
        double start, end;
    };
    void rebuild();
    void update(double time, bool seek);
    int rowFor(int order) const;
    QPointer<LyricModel> m_model;
    QSortFilterProxyModel *m_rows;
    QList<Bounds> m_bounds;
    QList<Gap> m_gaps;
    QList<int> m_orders;
    QSet<int> m_playing, m_highlighted;
    int m_cursor = 0, m_gapCursor = 0, m_focus = 0, m_latest = 0, m_gap = -1, m_revision = 0;
    bool m_filter = true, m_dynamic = false;
    double m_time = 0, m_maxEnd = 0;
};
} // namespace EchoMusic
