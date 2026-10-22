#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantMap>
#include <functional>

namespace EchoMusic
{
/// GUI-thread extension boundary shared by live and preloaded resolution.
/// Jobs own callback deadlines; destroying a job cancels its plugin operations.
/// No JS engine or plugin runtime dependency enters the playback service.
class PlaybackSourceExtensions final : public QObject
{
    Q_OBJECT
public:
    using Reply = std::function<void(bool, const QVariant &)>;
    struct Resolver
    {
        QString key, revision, position = "before-catalog";
        int order = 1000;
        QStringList stages;
        bool match = false, resolve = false, transform = false;
        std::function<bool()> valid;
        std::function<void(const QString &, const QVariantMap &, QObject *, Reply)> invoke;
        std::function<void(const QString &)> report;
    };
    explicit PlaybackSourceExtensions(QObject *parent) : QObject(parent) {}
    bool add(Resolver resolver);
    void remove(const QString &key, const QString &revision);
    QObject *resolve(const QString &stage, const QVariantMap &context, QObject *owner,
                     std::function<void(QJsonObject)> done);
    QObject *transform(const QString &stage, const QVariantMap &context, const QJsonObject &source,
                       QObject *owner, std::function<void(QJsonObject)> done);
    static QStringList stages();
    /// SDK value projection, never credentials or the underlying API response.
    static QVariantMap publicSource(const QJsonObject &source);
    /// Empty means invalid. A transform starts from the current valid source.
    static QJsonObject normalize(const QVariant &value, const QJsonObject &current = {});
    static qint64 audioTrack(const QJsonObject &source, const QString &url);
signals:
    void changed();

private:
    QList<Resolver> m_resolvers;
};
} // namespace EchoMusic
