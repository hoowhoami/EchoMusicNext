#pragma once
#include <QJsonObject>
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <atomic>
namespace EchoMusic
{
/// Value-only codecs shared by downloaded, embedded and plugin-authored lyrics.
/// No network, account, selection, model or plugin runtime state lives here.
class LyricParser final : public QObject
{
    Q_OBJECT
public:
    struct Result
    {
        QJsonObject lyrics;
        QString error, code;
    };
    enum class Source
    {
        Document,
        Downloaded,
        Embedded
    };
    using Done = std::function<void(Result)>;
    explicit LyricParser(QObject *parent = nullptr);
    ~LyricParser() override;
    /// Runs on this service's serial worker, at most eight accepted jobs. Deleting
    /// the returned owner cancels delivery and bounded parsing at safe points.
    QObject *submit(Source, QVariantMap input, QObject *owner, Done);
    static Result parse(Source, const QVariantMap &, const std::atomic_bool *canceled = nullptr);

private:
    QThreadPool m_pool;
    QList<std::shared_ptr<std::atomic_bool>> m_pending;
};
} // namespace EchoMusic
