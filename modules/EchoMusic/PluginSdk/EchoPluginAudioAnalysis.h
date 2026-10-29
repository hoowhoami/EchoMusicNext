// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QJsonObject>
#include <QHash>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoPluginContext;
namespace AudioAnalysis
{
class Job;
class Service;
} // namespace AudioAnalysis
class EchoPluginAnalysisLease final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by audio.analysis.request")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool finished READ finished NOTIFY changed)
    Q_PROPERTY(QVariant result READ result NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY changed)
    Q_PROPERTY(QString sourceSequence READ sourceSequence NOTIFY sourceIdentityChanged)
    Q_PROPERTY(QString transitionEpoch READ transitionEpoch NOTIFY sourceIdentityChanged)
public:
    EchoPluginAnalysisLease(AudioAnalysis::Job *, QObject *parent);
    ~EchoPluginAnalysisLease() override;
    bool active() const
    {
        return m_active;
    }
    QString state() const;
    bool finished() const;
    QVariant result() const;
    QString errorCode() const;
    QString errorMessage() const;
    QString sourceSequence() const;
    QString transitionEpoch() const;
    QVariantMap progress() const
    {
        return m_progress;
    }
    Q_INVOKABLE void dispose();
    void retire();
signals:
    void changed();
    void completed();
    void sourceIdentityChanged();

private:
    QPointer<AudioAnalysis::Job> m_job;
    QVariantMap m_progress;
    bool m_active = true;
};
class EchoPluginAudioAnalysis final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin audio")
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
public:
    explicit EchoPluginAudioAnalysis(EchoPluginContext &);
    ~EchoPluginAudioAnalysis() override;
    QVariantMap status() const;
    Q_INVOKABLE EchoMusic::EchoPluginAnalysisLease *request(const QVariantMap &options);
    void shutdown();
    bool isDrained() const;
signals:
    void statusChanged();
    void drained();

private:
    void invalidate();
    void cleanupOwners();
    EchoPluginContext &m_scope;
    AudioAnalysis::Service &m_service;
    QList<QPointer<EchoPluginAnalysisLease>> m_leases;
    QHash<QObject *, QPointer<EchoPluginAnalysisLease>> m_owners;
    bool m_closed = false;
};
} // namespace EchoMusic
