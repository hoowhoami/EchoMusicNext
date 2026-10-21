#include "SettingsKeys.h"
#include "PlaybackAudioControls.h"
#include "EqualizerPresets.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <cmath>
#include <QCryptographicHash>
#include <QSet>
#include <QRegularExpression>
namespace EchoMusic
{
namespace
{
QString effectDisplayName(QString name)
{
    static const QRegularExpression extension(
        "\\.(irs|wav|wave|flac|aif|aiff|caf|ogg|oga|mp3|m4a|aac|opus|vpf)$",
        QRegularExpression::CaseInsensitiveOption);
    name = name.trimmed().remove(extension).trimmed();
    return name.isEmpty() ? PlaybackAudioControls::tr("未命名音效") : name;
}
QString effectNameKey(const QString &path)
{
    return EchoMusic::SettingsKeys::Audio::ImpulseNamesPrefix +
           QString::fromLatin1(
               QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString outputSelection(const QString &name)
{
    // The engine's auto/default selectors represent the single empty Qt
    // selection. They are not physical devices or additional UI options.
    return name == "auto" || name == "default" ? QString() : name;
}
QString presetBankKey(const QString &provider, const QString &mode, const QString &preset)
{
    const auto hash = [](const QString &v) {
        return QString::fromLatin1(
            QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha256).toHex());
    };
    return EchoMusic::SettingsKeys::Audio::ProviderPresetsPrefix + hash(provider) + "/" + mode +
           "/" + hash(preset);
}
QString defaultPreset(const QString &id)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"presetId", id}}).toJson(QJsonDocument::Compact));
}
} // namespace

PlaybackAudioControls::PlaybackAudioControls(AudioEngine &engine, NativeBridge &native,
                                             QSettings &settings, QObject *parent)
    : QObject(parent),
      m_importer(native, QFileInfo(settings.fileName()).absolutePath() + "/audio-effects/local",
                 this),
      m_engine(engine), m_native(native), m_settings(settings),
      m_providers(native, QFileInfo(settings.fileName()).absolutePath() + "/audio-engines", this)
{
    connect(&m_importer, &ImpulseResponseImporter::changed, this, &PlaybackAudioControls::changed);
    connect(&m_importer, &ImpulseResponseImporter::closed, this,
            &PlaybackAudioControls::importsClosed);
    connect(&m_importer, &ImpulseResponseImporter::imported, this,
            [this](const QString &path, const QString &name) {
                registerImpulseResponse(path, name);
                if (!m_importBatchPaths.contains(path))
                    m_importBatchPaths.append(path);
            });
    connect(&m_importer, &ImpulseResponseImporter::finished, this,
            [this](int count, const QString &error) {
                if (count > 0) {
                    // Insert the selected batch at the front in picker order.
                    // Select its first member without changing the enabled state.
                    for (const auto &path : m_importBatchPaths)
                        m_irsPaths.removeAll(path);
                    m_irsPaths = m_importBatchPaths + m_irsPaths;
                    m_irs = m_importBatchPaths.first();
                    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponses,
                                        m_irsPaths);
                    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponse, m_irs);
                    if (m_irsEnabled)
                        applyEffect();
                    emit changed();
                    emit impulseFilesImported();
                    emit fileNotice(tr("已导入 %1 个音效文件").arg(count), true);
                } else
                    emit fileNotice(error, false);
            });
    m_providerMode =
        settings.value(EchoMusic::SettingsKeys::Audio::ProviderMode, "speaker").toString();
    if (m_providerMode != "headphone")
        m_providerMode = "speaker";
    m_targetMode = m_providerMode;
    connect(&m_providers, &DspProviderRegistry::restored, this, [this] {
        const auto saved = m_settings.value(EchoMusic::SettingsKeys::Audio::ProviderId).toString();
        if (saved.isEmpty())
            return;
        if (m_providers.record(saved).isEmpty()) {
            m_error = tr("已保存的音效引擎不可用，使用内置音效引擎");
            m_settings.remove(EchoMusic::SettingsKeys::Audio::ProviderId);
            emit changed();
            return;
        }
        m_targetProvider = saved;
        m_targetPreset =
            m_settings.value(EchoMusic::SettingsKeys::Audio::ProviderPreset).toString();
        applyEffect();
    });
    connect(&m_providers, &DspProviderRegistry::notice, this, &PlaybackAudioControls::fileNotice);
    connect(&m_providers, &DspProviderRegistry::imported, this, [this](const QString &id) {
        // EchoMusic's explicit import selects speaker context and original
        // sound, rather than replaying a preset from a different binary version.
        if (m_effectPending)
            return;
        m_targetProvider = id;
        m_targetPreset.clear();
        m_targetMode = "speaker";
        m_providerNotice = tr("音效引擎已导入并启用");
        applyEffect();
    });
    connect(&m_providers, &DspProviderRegistry::changed, this, &PlaybackAudioControls::changed);
    m_equalizerCommit.setInterval(100);
    m_equalizerCommit.setSingleShot(true);
    connect(&m_equalizerCommit, &QTimer::timeout, this, [this] {
        if (m_ready)
            m_engine.equalizer(m_gains);
    });
    m_input = settings.value(EchoMusic::SettingsKeys::Audio::InputDevice, "default").toString();
    m_pauseOnDisconnect =
        settings.value(EchoMusic::SettingsKeys::Audio::PauseOnDisconnect, true).toBool();
    m_normalization = settings.value(EchoMusic::SettingsKeys::Audio::Normalization, true).toBool();
    const auto lufs = settings.value(EchoMusic::SettingsKeys::Audio::ReferenceLufs, -14).toDouble();
    m_referenceLufs = std::isfinite(lufs) ? qBound(-20., lufs, -8.) : -14.;
    connect(&native, &NativeBridge::completed, this,
            [this](quint64 id, quint64, bool success, const QByteArray &bytes) {
                if (!m_inputRequest || id != m_inputRequest)
                    return;
                m_inputRequest = 0;
                if (success) {
                    const auto document = QJsonDocument::fromJson(bytes);
                    if (document.isArray())
                        m_inputs = document.array().toVariantList();
                } else
                    m_error = tr("无法读取输入设备");
                emit changed();
            });
    const auto gains = settings.value(EchoMusic::SettingsKeys::Audio::Equalizer).toList();
    for (int i = 0; i < 10; ++i) {
        const double v = i < gains.size() ? gains[i].toDouble() : 0.;
        m_gains.append(std::isfinite(v) ? qBound(-12., v, 12.) : 0.);
    }
    m_device =
        outputSelection(settings.value(EchoMusic::SettingsKeys::Audio::OutputDevice).toString());
    m_exclusive = settings.value(EchoMusic::SettingsKeys::Audio::Exclusive, false).toBool() &&
                  !m_device.startsWith("pipewire:") && !m_device.startsWith("pulse:") &&
                  !m_device.startsWith("pulseaudio:");
    m_irs = settings.value(EchoMusic::SettingsKeys::Audio::ImpulseResponse).toString();
    m_irsEnabled = settings.value(EchoMusic::SettingsKeys::Audio::ImpulseEnabled, false).toBool();
    m_irsPaths = settings.value(EchoMusic::SettingsKeys::Audio::ImpulseResponses).toStringList();
    const auto mix = settings.value(EchoMusic::SettingsKeys::Audio::ImpulseMix, .5).toDouble();
    m_mix = std::isfinite(mix) ? qBound(0., mix, 1.) : .5;
}
QVariantList PlaybackAudioControls::gains() const
{
    QVariantList v;
    for (double gain : m_gains)
        v.append(gain);
    return v;
}
QVariantList PlaybackAudioControls::equalizerPresets() const
{
    return QJsonDocument::fromJson(equalizerPresetsJson).array().toVariantList();
}
QVariantList PlaybackAudioControls::impulseResponses() const
{
    QVariantList result;
    QHash<QString, QString> sources;
    // The local download identity is authored by AudioEffectCatalog, not an
    // upstream response. Project it here so QML never parses storage keys.
    const QString prefix = EchoMusic::SettingsKeys::Audio::DownloadedEffectsPrefix;
    for (const auto &key : m_settings.allKeys()) {
        if (!key.startsWith(prefix))
            continue;
        const auto identity = key.mid(prefix.size());
        const auto source = identity.section('-', 0, 0);
        if (QStringList{"artist", "headphone", "market"}.contains(source))
            sources.insert(m_settings.value(key).toString(), source);
    }
    for (const auto &path : m_irsPaths)
        result.append(QVariantMap{
            {"path", path},
            {"source", sources.value(path, QStringLiteral("local"))},
            {"name",
             effectDisplayName(
                 m_settings.value(effectNameKey(path), QFileInfo(path).fileName()).toString())},
            {"available", QFileInfo(path).isReadable()}});
    return result;
}
void PlaybackAudioControls::registerImpulseResponse(const QString &path, const QString &name,
                                                    const QString &identity)
{
    if (!QFileInfo(path).isReadable())
        return;
    const auto displayName = uniqueEffectName(name, path);
    m_irsPaths.removeAll(path);
    m_irsPaths.prepend(path);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponses, m_irsPaths);
    m_settings.setValue(effectNameKey(path), displayName);
    if (!identity.isEmpty())
        m_settings.setValue(EchoMusic::SettingsKeys::Audio::DownloadedEffectsPrefix + identity,
                            path);
    emit changed();
}
QString PlaybackAudioControls::downloadedImpulseResponse(const QString &identity) const
{
    const auto path =
        m_settings.value(EchoMusic::SettingsKeys::Audio::DownloadedEffectsPrefix + identity)
            .toString();
    return m_irsPaths.contains(path) && QFileInfo(path).isReadable() ? path : QString();
}
void PlaybackAudioControls::initialize()
{
    m_ready = true;
    m_engine.equalizer(m_gains);
    m_engine.disconnectPolicy(m_pauseOnDisconnect);
    applyNormalization();
    requestOutput();
    m_engine.refreshDevices();
    if (m_irsEnabled || !m_targetProvider.isEmpty())
        applyEffect();
}
double PlaybackAudioControls::normalizationGainDb() const
{
    if (!m_normalization || !m_hasLoudness)
        return 0;
    // Upstream gain is metadata rather than another multiplier.
    // Peak is already dB; retain the -0.5 dB ceiling and 3x cap.
    return qBound(-40., std::min(m_referenceLufs - m_trackLufs, -.5 - m_trackPeak),
                  20. * std::log10(3.));
}
void PlaybackAudioControls::applyNormalization()
{
    if (m_ready)
        m_engine.normalization(normalizationGainDb());
    emit changed();
}
QString PlaybackAudioControls::normalizationStatus() const
{
    if (!m_hasLoudness)
        return tr("当前歌曲暂无响度信息，不会自动调整");
    const auto gain = normalizationGainDb();
    if (std::abs(gain) < .05)
        return tr("当前歌曲 %1 LUFS，无需调整").arg(m_trackLufs, 0, 'f', 1);
    return gain > 0 ? tr("当前歌曲 %1 LUFS，将提高 %2 dB")
                          .arg(m_trackLufs, 0, 'f', 1)
                          .arg(gain, 0, 'f', 1)
                    : tr("当前歌曲 %1 LUFS，将降低 %2 dB")
                          .arg(m_trackLufs, 0, 'f', 1)
                          .arg(-gain, 0, 'f', 1);
}
double PlaybackAudioControls::gainForSource(const QJsonObject &source) const
{
    if (!m_normalization)
        return 0;
    const auto lufs = source.value("loudness").toDouble(std::numeric_limits<double>::quiet_NaN());
    const auto peak = source.value("peak").toDouble(std::numeric_limits<double>::quiet_NaN());
    const auto gain = source.value("gain").toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(lufs) || lufs <= -70 || lufs >= 0 || !std::isfinite(peak) || peak < -120 ||
        peak > 24 || !std::isfinite(gain) || gain < -24 || gain > 24)
        return 0;
    return qBound(-40., std::min(m_referenceLufs - lufs, -.5 - peak), 20. * std::log10(3.));
}
void PlaybackAudioControls::setTrackLoudness(const QJsonObject &source)
{
    const auto lufs = source.value("loudness").toDouble(std::numeric_limits<double>::quiet_NaN());
    const auto peak = source.value("peak").toDouble(std::numeric_limits<double>::quiet_NaN());
    const auto gain = source.value("gain").toDouble(std::numeric_limits<double>::quiet_NaN());
    m_hasLoudness = std::isfinite(lufs) && lufs > -70 && lufs < 0 && std::isfinite(peak) &&
                    peak >= -120 && peak <= 24 && std::isfinite(gain) && gain >= -24 && gain <= 24;
    m_trackLufs = lufs;
    m_trackPeak = peak;
    applyNormalization();
}
void PlaybackAudioControls::setVolumeNormalization(bool enabled)
{
    if (m_normalization == enabled)
        return;
    m_normalization = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::Normalization, enabled);
    applyNormalization();
}
void PlaybackAudioControls::setReferenceLufs(double lufs)
{
    if (!std::isfinite(lufs))
        return;
    m_referenceLufs = qBound(-20., lufs, -8.);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ReferenceLufs, m_referenceLufs);
    applyNormalization();
}
void PlaybackAudioControls::handleEvent(const QJsonObject &event)
{
    const auto kind = event.value("event").toString();
    if (kind == "effect-applied" ||
        (kind == "control-error" && event.value("effect_revision").isDouble())) {
        if (!m_effectPending ||
            quint64(event.value("effect_revision").toInteger()) != m_effectRevision)
            return;
        m_effectPending = false;
        const bool removing = !m_removingProvider.isEmpty();
        if (kind == "effect-applied") {
            if (m_providerId != m_targetProvider || m_providerMode != m_targetMode)
                m_editingPreset.clear();
            m_providerId = m_targetProvider;
            m_providerPreset = m_targetPreset;
            m_providerMode = m_targetMode;
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::ProviderId, m_providerId);
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::ProviderPreset, m_providerPreset);
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::ProviderMode, m_providerMode);
            if (!providerPresetId().isEmpty())
                m_settings.setValue(presetBankKey(m_providerId, m_providerMode, providerPresetId()),
                                    m_providerPreset);
            m_error.clear();
            if (!m_removingProvider.isEmpty()) {
                m_providers.remove(std::exchange(m_removingProvider, {}));
            }
        } else {
            m_targetProvider = m_providerId;
            m_targetPreset = m_providerPreset;
            m_targetMode = m_providerMode;
            m_removingProvider.clear();
            m_error = event.value("message").toString(tr("音效引擎操作失败"));
        }
        if (!m_providerNotice.isEmpty()) {
            const auto message = std::exchange(m_providerNotice, {});
            if (kind != "effect-applied" || !removing)
                emit fileNotice(kind == "effect-applied" ? message : m_error,
                                kind == "effect-applied");
        }
        emit changed();
        return;
    }
    if (kind == "control-error") {
        if (!event.value("output_revision").isNull() && event.contains("output_revision")) {
            if (quint64(event.value("output_revision").toInteger()) != m_outputRevision)
                return;
            m_outputPending = false;
            m_device = m_appliedDevice;
            m_exclusive = m_appliedExclusive;
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::OutputDevice, m_device);
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::Exclusive, m_exclusive);
        }
        m_error = event.value("message").toString();
        emit changed();
    } else if (kind == "output-applied" || kind == "output-configuration") {
        if (kind == "output-applied") {
            if (quint64(event.value("output_revision").toInteger()) != m_outputRevision)
                return;
            m_outputPending = false;
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::OutputDevice, m_device);
            m_settings.setValue(EchoMusic::SettingsKeys::Audio::Exclusive, m_exclusive);
        } else if (m_outputPending)
            return;
        m_appliedDevice = outputSelection(event.value("output_device").toString());
        m_appliedExclusive = event.value("exclusive_output").toBool();
        m_error.clear();
        emit changed();
    } else if (kind == "audio-device-list-changed") {
        m_devices.clear();
        QSet<QString> labels;
        for (const auto &value : event.value("devices").toArray()) {
            const auto device = value.toObject();
            const auto name = device.value("name").toString();
            if (outputSelection(name).isEmpty() || name == "null")
                continue;
            const auto description = device.value("description").toString();
            const auto label = description.isEmpty() ? name : description;
            // Match EchoMusic's concrete-device projection and first-label
            // deduplication. Keep the actual default hardware (is_default=true).
            if (labels.contains(label))
                continue;
            labels.insert(label);
            m_devices.append(device.toVariantMap());
        }
        for (const auto &value : event.value("disconnected_devices").toArray()) {
            const auto device = value.toObject();
            if (device.value("name").toString() == m_device ||
                (m_device.isEmpty() && device.value("is_default").toBool())) {
                if (m_pauseOnDisconnect)
                    emit selectedOutputDisconnected();
                // Continue policy is handled by the native recovery worker.
                break;
            }
        }
        emit changed();
    } else if (kind == "audio-graph-change") {
        m_graph = event.value("audio_graph").toObject().toVariantMap();
        m_error.clear();
        emit changed();
    }
}
void PlaybackAudioControls::saveEqualizer()
{
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::Equalizer, gains());
    if (m_ready && !m_equalizerCommit.isActive())
        m_equalizerCommit.start();
    emit changed();
}
void PlaybackAudioControls::setEqualizerBand(int index, double gain)
{
    if (index < 0 || index >= 10 || !std::isfinite(gain))
        return;
    m_gains[index] = qBound(-12., gain, 12.);
    saveEqualizer();
}
void PlaybackAudioControls::applyEqualizerPreset(int index)
{
    const auto presets = equalizerPresets();
    if (index < 0 || index >= presets.size())
        return;
    const auto values = presets[index].toMap().value("gains").toList();
    for (int i = 0; i < 10; ++i)
        m_gains[i] = values[i].toDouble();
    saveEqualizer();
}
void PlaybackAudioControls::resetEqualizer()
{
    m_gains.fill(0);
    saveEqualizer();
}
void PlaybackAudioControls::setInputDevice(const QString &name)
{
    if (name.size() > 4096)
        return;
    m_input = name;
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::InputDevice, name);
    emit changed();
}
void PlaybackAudioControls::setPauseOnDisconnect(bool pause)
{
    m_pauseOnDisconnect = pause;
    if (m_ready)
        m_engine.disconnectPolicy(pause);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::PauseOnDisconnect, pause);
    emit changed();
}
void PlaybackAudioControls::refreshInputs()
{
    if (!m_inputRequest)
        m_inputRequest = m_native.submit(NativeBridge::CaptureDevices, QJsonObject{}, 0);
}
void PlaybackAudioControls::refreshDevices()
{
    if (m_ready)
        m_engine.refreshDevices();
}
void PlaybackAudioControls::setOutputDevice(const QString &selection)
{
    const auto name = outputSelection(selection);
    if (name.size() > 4096 || name == m_device)
        return;
    m_device = name;
    if (m_exclusive && (name.startsWith("pipewire:") || name.startsWith("pulse:") ||
                        name.startsWith("pulseaudio:"))) {
        m_exclusive = false;
    }
    if (m_ready)
        requestOutput();
    else {
        m_settings.setValue(EchoMusic::SettingsKeys::Audio::OutputDevice, name);
        m_settings.setValue(EchoMusic::SettingsKeys::Audio::Exclusive, m_exclusive);
    }
    emit changed();
}
void PlaybackAudioControls::setExclusive(bool exclusive)
{
    if (exclusive && (m_device.startsWith("pipewire:") || m_device.startsWith("pulse:") ||
                      m_device.startsWith("pulseaudio:")))
        return;
    if (m_exclusive == exclusive)
        return;
    m_exclusive = exclusive;
    if (m_ready)
        requestOutput();
    else
        m_settings.setValue(EchoMusic::SettingsKeys::Audio::Exclusive, exclusive);
    emit changed();
}
void PlaybackAudioControls::requestOutput()
{
    m_outputPending = true;
    m_error.clear();
    m_engine.output(m_device, m_exclusive, ++m_outputRevision);
}
void PlaybackAudioControls::importImpulseResponseFiles(const QList<QUrl> &files)
{
    if (m_importer.busy() || files.isEmpty())
        return;
    m_importBatchPaths.clear();
    m_importer.importFiles(files);
}
QString PlaybackAudioControls::uniqueEffectName(const QString &name,
                                                const QString &exceptPath) const
{
    QSet<QString> used;
    for (const auto &entry : impulseResponses()) {
        const auto file = entry.toMap();
        if (file.value("path").toString() != exceptPath)
            used.insert(file.value("name").toString().toLower());
    }
    const auto base = effectDisplayName(name);
    QString result = base;
    for (int index = 2; used.contains(result.toLower()); ++index)
        result = base + " " + QString::number(index);
    return result;
}
void PlaybackAudioControls::renameImpulseResponse(const QString &path, const QString &name)
{
    if (!m_irsPaths.contains(path) || name.trimmed().isEmpty())
        return;
    m_settings.setValue(effectNameKey(path), uniqueEffectName(name.left(80), path));
    emit changed();
}
void PlaybackAudioControls::selectImpulseResponse(const QString &path)
{
    if (m_effectPending || !m_irsPaths.contains(path))
        return;
    m_targetPreset.clear();
    m_irs = path;
    m_irsEnabled = true;
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponse, path);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseEnabled, true);
    applyEffect();
    emit changed();
}
void PlaybackAudioControls::removeImpulseResponse(const QString &path)
{
    if (!m_irsPaths.contains(path))
        return;
    m_irsPaths.removeAll(path);
    m_settings.remove(effectNameKey(path));
    for (const auto &key : m_settings.allKeys())
        if (key.startsWith(EchoMusic::SettingsKeys::Audio::DownloadedEffectsPrefix) &&
            m_settings.value(key).toString() == path)
            m_settings.remove(key);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponses, m_irsPaths);
    if (path == m_irs) {
        m_irs.clear();
        m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseResponse, m_irs);
        setImpulseEnabled(false);
    }
    // Remove owned resources without touching the user's original file.
    // Canonical containment excludes symlinks to external resources.
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const QStringList roots{
        QFileInfo(m_settings.fileName()).absolutePath() + "/audio-effects/local",
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/audio-effects"};
    for (const auto &root : roots) {
        const auto directory = QFileInfo(root).canonicalFilePath();
        if (!directory.isEmpty() && canonical.startsWith(directory + "/")) {
            QFile::remove(path);
            break;
        }
    }
    emit changed();
    emit fileNotice(tr("已移除音效"), true);
}
void PlaybackAudioControls::setImpulseMix(double mix)
{
    if (!std::isfinite(mix))
        return;
    m_mix = qBound(0., mix, 1.);
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseMix, m_mix);
    if (m_irsEnabled)
        applyEffect();
    emit changed();
}
void PlaybackAudioControls::setImpulseEnabled(bool enabled)
{
    m_irsEnabled = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseEnabled, enabled);
    applyEffect();
    emit changed();
}
void PlaybackAudioControls::applyEffect()
{
    if (!m_ready)
        return;
    if (m_irsEnabled && !QFileInfo(m_irs).isReadable()) {
        m_error = tr("脉冲响应文件不可读取");
        if (!m_providerNotice.isEmpty()) {
            m_providerNotice.clear();
            emit fileNotice(m_error, false);
        }
        emit changed();
        return;
    }
    const auto record = m_providers.record(m_targetProvider);
    QJsonObject configuration{
        {"impulseResponsePath", m_irsEnabled ? QJsonValue(m_irs) : QJsonValue()},
        {"impulseResponseMix", m_mix}};
    if (!m_targetProvider.isEmpty()) {
        if (record.isEmpty()) {
            m_error = tr("音效引擎不可用");
            if (!m_providerNotice.isEmpty()) {
                m_providerNotice.clear();
                emit fileNotice(m_error, false);
            }
            emit changed();
            return;
        }
        configuration.insert("providerPath", record.value("path").toString());
        configuration.insert("providerMode", m_targetMode);
        if (!m_targetPreset.isEmpty())
            configuration.insert("providerPresetJson", m_targetPreset);
    }
    m_effectPending = true;
    m_error.clear();
    m_engine.effect(configuration, ++m_effectRevision);
    emit changed();
}
QString PlaybackAudioControls::providerName() const
{
    return m_providers.record(m_providerId).value("name").toString();
}
QVariantList PlaybackAudioControls::providerPresets() const
{
    return m_providers.record(m_providerId).value("manifest").toMap().value("presets").toList();
}
QString PlaybackAudioControls::providerPresetId() const
{
    return QJsonDocument::fromJson(m_providerPreset.toUtf8()).object().value("presetId").toString();
}
void PlaybackAudioControls::activateProvider(const QString &id)
{
    if (m_effectPending || m_providers.record(id).isEmpty())
        return;
    m_targetProvider = id;
    m_providerNotice = tr("已启用“%1”").arg(m_providers.record(id).value("name").toString());
    m_targetPreset = id == m_providerId ? m_providerPreset : QString();
    applyEffect();
}
void PlaybackAudioControls::deactivateProvider()
{
    if (m_effectPending)
        return;
    if (m_removingProvider.isEmpty())
        m_providerNotice = tr("已停用“%1”").arg(providerName());
    m_targetProvider.clear();
    m_targetPreset.clear();
    applyEffect();
}
void PlaybackAudioControls::removeProvider(const QString &id)
{
    if (m_effectPending || m_providers.busy())
        return;
    if (id == m_providerId) {
        m_removingProvider = id;
        m_providerNotice = tr("音效引擎已删除");
        deactivateProvider();
    } else
        m_providers.remove(id);
}
void PlaybackAudioControls::setProviderMode(const QString &mode)
{
    if (m_effectPending || (mode != "headphone" && mode != "speaker"))
        return;
    m_targetMode = mode;
    m_editingPreset.clear();
    const auto id = providerPresetId();
    if (!id.isEmpty())
        m_targetPreset =
            m_settings.value(presetBankKey(m_providerId, mode, id), defaultPreset(id)).toString();
    applyEffect();
}
void PlaybackAudioControls::selectProviderPreset(const QString &id)
{
    if (m_effectPending || m_providerId.isEmpty())
        return;
    bool found = id.isEmpty();
    for (const auto &preset : providerPresets())
        if (preset.toMap().value("id").toString() == id)
            found = true;
    if (!found)
        return;
    m_irsEnabled = false;
    m_settings.setValue(EchoMusic::SettingsKeys::Audio::ImpulseEnabled, false);
    m_targetPreset =
        id.isEmpty()
            ? QString()
            : m_settings.value(presetBankKey(m_providerId, m_providerMode, id), defaultPreset(id))
                  .toString();
    applyEffect();
}
QVariantList PlaybackAudioControls::providerControls() const
{
    const auto editing = m_editingPreset.isEmpty() ? providerPresetId() : m_editingPreset;
    const auto recipe = editing == providerPresetId()
                            ? m_providerPreset
                            : m_settings
                                  .value(presetBankKey(m_providerId, m_providerMode, editing),
                                         defaultPreset(editing))
                                  .toString();
    const auto preset = QJsonDocument::fromJson(recipe.toUtf8()).object();
    const auto values = preset.value("controls").toObject();
    QVariantList controls;
    for (const auto &entry : providerPresets()) {
        const auto selected = entry.toMap();
        if (selected.value("id").toString() != editing)
            continue;
        auto rows = selected.value("controls").toList();
        for (auto &row : rows) {
            auto control = row.toMap();
            const auto id = control.value("id").toString();
            control.insert("value", values.contains(id)
                                        ? values.value(id).toObject().value("value").toVariant()
                                        : control.value("defaultValue"));
            const auto condition = control.value("visibleWhen").toMap();
            bool visible = true;
            if (!condition.isEmpty()) {
                const auto dependency = condition.value("controlId").toString();
                QVariant value = values.value(dependency).toObject().value("value").toVariant();
                if (!value.isValid())
                    for (const auto &candidate : rows)
                        if (candidate.toMap().value("id").toString() == dependency)
                            value = candidate.toMap().value("defaultValue");
                visible = QJsonValue::fromVariant(value) ==
                          QJsonValue::fromVariant(condition.value("value"));
            }
            control.insert("shown", visible);
            controls.append(control);
        }
    }
    return controls;
}
void PlaybackAudioControls::setProviderControl(const QString &id, const QVariant &value)
{
    if (m_effectPending || m_providerId.isEmpty())
        return;
    for (const auto &entry : providerControls()) {
        const auto control = entry.toMap();
        if (control.value("id").toString() != id || !control.value("shown").toBool() ||
            control.value("ownership").toString() != "provider")
            continue;
        const auto type = control.value("type").toString();
        if (type == "number") {
            bool ok = false;
            const auto number = value.toDouble(&ok);
            const auto range = control.value("range").toMap();
            if (!ok || !QJsonValue::fromVariant(value).isDouble() || !std::isfinite(number) ||
                number < range.value("min").toDouble() || number > range.value("max").toDouble())
                return;
            const auto step = range.value("step").toDouble();
            if (step > 0) {
                const auto ticks = (number - range.value("min").toDouble()) / step;
                if (!std::isfinite(ticks) || std::abs(ticks - std::round(ticks)) > 1e-8)
                    return;
            }
        } else if (type == "select" || type == "boolean") {
            bool allowed = type == "boolean" && value.metaType().id() == QMetaType::Bool &&
                           control.value("options").toList().isEmpty();
            for (const auto &option : control.value("options").toList())
                if (!option.toMap().value("disabled").toBool() &&
                    QJsonValue::fromVariant(option.toMap().value("value")) ==
                        QJsonValue::fromVariant(value))
                    allowed = true;
            if (!allowed)
                return;
        } else
            return;
        const auto editing = m_editingPreset.isEmpty() ? providerPresetId() : m_editingPreset;
        const auto recipe = editing == providerPresetId()
                                ? m_providerPreset
                                : m_settings
                                      .value(presetBankKey(m_providerId, m_providerMode, editing),
                                             defaultPreset(editing))
                                      .toString();
        auto preset = QJsonDocument::fromJson(recipe.toUtf8()).object();
        auto values = preset.value("controls").toObject();
        values.insert(id, QJsonObject{{"value", QJsonValue::fromVariant(value)}});
        preset.insert("controls", values);
        const auto json = QString::fromUtf8(QJsonDocument(preset).toJson(QJsonDocument::Compact));
        if (editing == providerPresetId()) {
            m_targetPreset = json;
            applyEffect();
        } else {
            m_settings.setValue(presetBankKey(m_providerId, m_providerMode, editing), json);
            emit changed();
        }
        return;
    }
}
void PlaybackAudioControls::editProviderPreset(const QString &id)
{
    if (m_effectPending)
        return;
    for (const auto &preset : providerPresets())
        if (preset.toMap().value("id").toString() == id) {
            m_editingPreset = id;
            emit changed();
            return;
        }
}
void PlaybackAudioControls::resetProviderControls()
{
    if (m_effectPending || m_providerId.isEmpty() || m_editingPreset.isEmpty())
        return;
    const auto json = defaultPreset(m_editingPreset);
    if (m_editingPreset == providerPresetId()) {
        m_targetPreset = json;
        applyEffect();
    } else {
        m_settings.setValue(presetBankKey(m_providerId, m_providerMode, m_editingPreset), json);
        emit changed();
    }
}

} // namespace EchoMusic
