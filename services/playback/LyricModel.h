#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QRegularExpression>
namespace EchoMusic
{
class LyricModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 offsetMs READ offsetMs NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(bool scrollable READ scrollable NOTIFY changed)
    Q_PROPERTY(bool hasTranslation READ hasTranslation NOTIFY changed)
    Q_PROPERTY(bool hasRomanization READ hasRomanization NOTIFY changed)
public:
    qint64 offsetMs() const
    {
        return m_offset;
    }
    explicit LyricModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_lines.size());
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "lineText"},
                {Qt::UserRole + 2, "startMs"},
                {Qt::UserRole + 3, "durationMs"},
                {Qt::UserRole + 4, "words"},
                {Qt::UserRole + 5, "translation"},
                {Qt::UserRole + 6, "romanization"},
                {Qt::UserRole + 7, "filtered"},
                {Qt::UserRole + 8, "wordTimed"},
                {Qt::UserRole + 9, "id"},
                {Qt::UserRole + 10, "agentId"},
                {Qt::UserRole + 11, "songPart"},
                {Qt::UserRole + 12, "blockIndex"},
                {Qt::UserRole + 13, "isChorus"},
                {Qt::UserRole + 14, "backgroundVocals"},
                {Qt::UserRole + 15, "alternateTexts"}};
    }
    Q_INVOKABLE QVariantMap lineAt(int index) const;
    Q_INVOKABLE QVariantList verticalAt(int index) const;
    Q_INVOKABLE QVariantList verticalText(const QString &text) const;
    Q_INVOKABLE QVariantList rubyAt(int index) const;
    Q_INVOKABLE int visibleIndex(int index) const;
    Q_INVOKABLE int nextVisible(int index) const;
    void setFilter(bool enabled, const QString &pattern);
    Q_INVOKABLE int indexAt(qint64 positionMs) const;
    /// The line a strip should show at this playback position: the visible line
    /// under the playhead, or the first visible line when the lyric has no
    /// timeline to follow. One call, so call sites never walk the model.
    Q_INVOKABLE int visibleIndexAt(qint64 positionMs) const;
    Q_INVOKABLE qint64 startAt(int index) const;
    Q_INVOKABLE qint64 durationAt(int index) const;
    /// Latest ending among all rows through index, including the lyric offset.
    Q_INVOKABLE qint64 endThrough(int index) const;
    bool replace(const QJsonObject &lyrics);
    /// Retain exactly the already parsed/converted presentation, with implicit
    /// sharing until either model changes. No parser or timing reconstruction.
    void copyPresentationFrom(const LyricModel &source);
    /// Validated source facts only; no voice or section is inferred by this model.
    QVariantMap metadata() const
    {
        return m_metadata;
    }
    void clear();
    Q_INVOKABLE void setPosition(qint64 positionMs);
    bool scrollable() const
    {
        return m_scrollable;
    }
    bool hasTranslation() const
    {
        return m_hasTranslation;
    }
    bool hasRomanization() const
    {
        return m_hasRomanization;
    }
    int currentIndex() const
    {
        return m_current;
    }
signals:
    void changed();
    void currentChanged();

private:
    struct Line
    {
        QString text;
        qint64 start, duration;
        QVariantList words;
        QString translation, romanization;
        bool filtered = false;
        QVariantMap metadata;
        QVariantList backgroundVocals;
    };
    static QVariantList verticalUnits(const Line &line);
    QList<Line> m_lines;
    QRegularExpression m_filter;
    bool m_filterEnabled = false;
    QList<qint64> m_endPrefix;
    qint64 m_offset = 0;
    int m_current = -1;
    bool m_scrollable = true;
    bool m_hasTranslation = false, m_hasRomanization = false;
    QVariantMap m_metadata;
};
} // namespace EchoMusic
