// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AnalysisBroker.h"
#include <QHash>
#include <QPointer>
#include <QSet>

namespace EchoMusic::AudioAnalysis
{
struct Input
{
    Reader reader;
    QString localIdentity, code, error;
};
// The GUI supplies acknowledged source facts; capture receives only their immutable copy.
// Capture/Reader never retain a PlaybackService, SDK context, or other GUI QObject.
class Service final : public QObject
{
    Q_OBJECT
public:
    using Facts = std::function<QJsonObject()>;
    using Capture = std::function<Input(const QJsonObject &, Kind)>;
    using Resolve = std::function<QJsonObject(const QJsonObject &)>;
    using Authorize = std::function<QString(const QJsonObject &)>;
    Service(Config, Facts, Capture, QObject *parent = nullptr);
    ~Service() override;
    Job *request(QObject *scope, const QString &accountNamespace, Kind, Policy,
                 const QJsonObject &selector = {});
    void setSourceAuthority(Resolve, Authorize);
    Job *rejected(QObject *scope, const QString &code, const QString &message);
    void retireScope(QObject *);
    bool scopeDrained(QObject *) const;
    /// Only the acknowledged sequence and the sticky, still-live native epoch.
    /// Returns empty while a source update has not yet authorized this scope.
    QJsonObject sourceIdentity(QObject *) const;
    void sourceChanged();
    QJsonObject status() const;
signals:
    void drainedChanged();
    void authorizationRevoked(QObject *scope);
    void authorizationChanged(QObject *scope);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace EchoMusic::AudioAnalysis
