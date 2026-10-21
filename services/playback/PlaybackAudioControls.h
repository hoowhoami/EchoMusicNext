#pragma once
#include "AudioEngine.h"
#include "NativeBridge.h"
#include "DspProviderRegistry.h"
#include "ImpulseResponseImporter.h"
#include <QSettings>
#include <QTimer>
#include <QVariantList>
namespace EchoMusic
{
/// Preferences belong to Qt. Rust owns the applied signal graph; snapshots and
/// failures reconcile that graph without interrupting track transport.
class PlaybackAudioControls final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList gains READ gains NOTIFY changed)
    Q_PROPERTY(QVariantList equalizerPresets READ equalizerPresets CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY changed)
    Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY changed)
    Q_PROPERTY(
        bool pauseOnDisconnect READ pauseOnDisconnect WRITE setPauseOnDisconnect NOTIFY changed)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY changed)
    Q_PROPERTY(QString appliedOutputDevice READ appliedOutputDevice NOTIFY changed)
    Q_PROPERTY(bool exclusive READ exclusive WRITE setExclusive NOTIFY changed)
    Q_PROPERTY(QString impulseResponse READ impulseResponse NOTIFY changed)
    Q_PROPERTY(double impulseMix READ impulseMix WRITE setImpulseMix NOTIFY changed)
    Q_PROPERTY(bool impulseEnabled READ impulseEnabled WRITE setImpulseEnabled NOTIFY changed)
    Q_PROPERTY(QVariantList impulseResponses READ impulseResponses NOTIFY changed)
    Q_PROPERTY(bool importingImpulseResponses READ importingImpulseResponses NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY changed)
    Q_PROPERTY(bool volumeNormalization READ volumeNormalization WRITE setVolumeNormalization NOTIFY
                   changed)
    Q_PROPERTY(double referenceLufs READ referenceLufs WRITE setReferenceLufs NOTIFY changed)
    Q_PROPERTY(double normalizationGainDb READ normalizationGainDb NOTIFY changed)
    Q_PROPERTY(QString normalizationStatus READ normalizationStatus NOTIFY changed)
    Q_PROPERTY(EchoMusic::DspProviderRegistry *providers READ providers CONSTANT)
    Q_PROPERTY(QString providerId READ providerId NOTIFY changed)
    Q_PROPERTY(QString providerMode READ providerMode NOTIFY changed)
    Q_PROPERTY(QString providerName READ providerName NOTIFY changed)
    Q_PROPERTY(QVariantMap providerInfo READ providerInfo NOTIFY changed)
    Q_PROPERTY(QVariantList providerPresets READ providerPresets NOTIFY changed)
    Q_PROPERTY(QString providerPresetId READ providerPresetId NOTIFY changed)
    Q_PROPERTY(bool providerEffectActive READ providerEffectActive NOTIFY changed)
    Q_PROPERTY(bool effectPending READ effectPending NOTIFY changed)
    Q_PROPERTY(QVariantList providerControls READ providerControls NOTIFY changed)
    Q_PROPERTY(QString providerEditingPreset READ providerEditingPreset NOTIFY changed)
public:
    PlaybackAudioControls(AudioEngine &engine, NativeBridge &native, QSettings &settings,
                          QObject *parent = nullptr);
    QVariantList gains() const;
    QVariantList equalizerPresets() const;
    QVariantList inputDevices() const
    {
        return m_inputs;
    }
    QString inputDevice() const
    {
        return m_input;
    }
    bool pauseOnDisconnect() const
    {
        return m_pauseOnDisconnect;
    }
    void setInputDevice(const QString &name);
    void setPauseOnDisconnect(bool pause);
    Q_INVOKABLE void refreshInputs();
    /// Concrete output choices, deduplicated by display label. The UI adds
    /// its one localized default selection; native auto/null entries stay out.
    QVariantList devices() const
    {
        return m_devices;
    }
    QVariantList impulseResponses() const;
    QString outputDevice() const
    {
        return m_device;
    }
    QString appliedOutputDevice() const
    {
        return m_appliedDevice;
    }
    bool exclusive() const
    {
        return m_exclusive;
    }
    QString impulseResponse() const
    {
        return m_irs;
    }
    double impulseMix() const
    {
        return m_mix;
    }
    bool impulseEnabled() const
    {
        return m_irsEnabled;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantMap graph() const
    {
        return m_graph;
    }
    void initialize();
    bool volumeNormalization() const
    {
        return m_normalization;
    }
    double referenceLufs() const
    {
        return m_referenceLufs;
    }
    double normalizationGainDb() const;
    QString normalizationStatus() const;
    double gainForSource(const QJsonObject &source) const;
    void setVolumeNormalization(bool enabled);
    void setReferenceLufs(double lufs);
    void setTrackLoudness(const QJsonObject &source);
    void registerImpulseResponse(const QString &path, const QString &name,
                                 const QString &identity = {});
    QString downloadedImpulseResponse(const QString &identity) const;

    void handleEvent(const QJsonObject &event);
    Q_INVOKABLE void setEqualizerBand(int index, double gain);
    Q_INVOKABLE void applyEqualizerPreset(int index);
    Q_INVOKABLE void resetEqualizer();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void setOutputDevice(const QString &name);
    Q_INVOKABLE void setExclusive(bool exclusive);
    Q_INVOKABLE void importImpulseResponseFiles(const QList<QUrl> &files);
    Q_INVOKABLE void renameImpulseResponse(const QString &path, const QString &name);
    bool importingImpulseResponses() const
    {
        return m_importer.busy();
    }
    void shutdownImports()
    {
        m_importer.shutdown();
    }
    Q_INVOKABLE void selectImpulseResponse(const QString &path);
    Q_INVOKABLE void removeImpulseResponse(const QString &path);
    void setImpulseMix(double mix);
    void setImpulseEnabled(bool enabled);
    void applyEffect();
    DspProviderRegistry *providers()
    {
        return &m_providers;
    }
    QString providerId() const
    {
        return m_providerId;
    }
    QString providerMode() const
    {
        return m_providerMode;
    }
    QString providerName() const;
    QVariantMap providerInfo() const
    {
        return m_providers.record(m_providerId);
    }
    QVariantList providerPresets() const;
    QString providerPresetId() const;
    // A custom provider recipe remains active even without a named preset ID.
    bool providerEffectActive() const
    {
        return !m_providerPreset.trimmed().isEmpty();
    }
    bool effectPending() const
    {
        return m_effectPending;
    }
    QVariantList providerControls() const;
    QString providerEditingPreset() const
    {
        return m_editingPreset;
    }
    Q_INVOKABLE void activateProvider(const QString &id);
    Q_INVOKABLE void deactivateProvider();
    Q_INVOKABLE void removeProvider(const QString &id);
    Q_INVOKABLE void setProviderMode(const QString &mode);
    Q_INVOKABLE void selectProviderPreset(const QString &id);
    Q_INVOKABLE void setProviderControl(const QString &id, const QVariant &value);
    Q_INVOKABLE void editProviderPreset(const QString &id);
    Q_INVOKABLE void resetProviderControls();
signals:
    void changed();
    void impulseFilesImported();
    void fileNotice(const QString &message, bool success);
    void importsClosed();
    void selectedOutputDisconnected();

private:
    ImpulseResponseImporter m_importer;
    friend class PlaybackAudioControlsTestAccess;
    void saveEqualizer();
    void requestOutput();
    void applyNormalization();
    AudioEngine &m_engine;
    NativeBridge &m_native;
    QVariantList m_inputs;
    QString m_input = "default";
    quint64 m_inputRequest = 0;
    bool m_pauseOnDisconnect = true;
    QSettings &m_settings;
    DspProviderRegistry m_providers;
    QString m_providerId, m_targetProvider, m_providerMode = "speaker", m_targetMode = "speaker";
    QString m_providerPreset, m_targetPreset, m_removingProvider;
    QString m_editingPreset;
    QString m_providerNotice;
    quint64 m_effectRevision = 0;
    bool m_effectPending = false;
    QList<double> m_gains;
    QVariantList m_devices;
    QVariantMap m_graph;
    QString m_device, m_irs, m_error, m_appliedDevice;
    bool m_appliedExclusive = false;
    quint64 m_outputRevision = 0;
    bool m_outputPending = false;
    bool m_normalization = true, m_hasLoudness = false;
    double m_referenceLufs = -14, m_trackLufs = 0, m_trackPeak = 0;
    QTimer m_equalizerCommit;
    QStringList m_irsPaths, m_importBatchPaths;
    QString uniqueEffectName(const QString &name, const QString &exceptPath) const;
    bool m_exclusive = false, m_ready = false, m_irsEnabled = false;
    double m_mix = .5;
};
} // namespace EchoMusic
