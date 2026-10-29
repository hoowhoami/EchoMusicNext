#include "AudioEngine.h"
#include "StorageFence.h"
#include "ImpulseResponseInstaller.h"
#include "NativeBridge.h"
#include "PlaybackAudioControls.h"
#include "echo_dsp_provider.h"
#include <QAbstractItemModelTester>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QLibrary>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace EchoMusic;
class DspProviderTest : public QObject
{
    Q_OBJECT
    static bool seen(const QSignalSpy &events, const QString &kind)
    {
        for (const auto &entry : events)
            if (entry[0].toJsonObject().value("event").toString() == kind)
                return true;
        return false;
    }
    static double position(const QSignalSpy &events)
    {
        for (auto it = events.crbegin(); it != events.crend(); ++it)
            if ((*it)[0].toJsonObject().value("event").toString() == "time-update")
                return (*it)[0].toJsonObject().value("time").toDouble();
        return 0;
    }
private slots:
    void managedEffectInstallerRejectsFrozenStorageAndReleasesOnFailure()
    {
        QTemporaryDir directory;
        SettingsStore settings(directory.filePath("portable/settings.ini"));
        const auto destination =
            settings.files("audio-effects", SettingsDomain::Portable) + "/downloads/test.irs";
        ImpulseResponseInstaller installer;
        QSignalSpy finished(&installer, &ImpulseResponseInstaller::finished);
        auto frozen = StorageFence::freeze(settings.portableFilesRoot());
        QVERIFY(frozen);
        installer.install(1, {}, destination);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.last()[1].toBool());
        QVERIFY(finished.last()[3].toString().contains("备份"));
        QVERIFY(!QFileInfo::exists(destination));
        frozen.reset();
        installer.install(2, {}, destination);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(!finished.last()[1].toBool());
        // Failure must retire the accepted writer before notifying its caller.
        auto nextSnapshot = StorageFence::freeze(settings.portableFilesRoot());
        QVERIFY(nextSnapshot);
        std::atomic_bool canceled = false;
        QVERIFY(nextSnapshot->waitForWriters(canceled));
        installer.shutdown();
    }
    void importLifecycle_data()
    {
        QTest::addColumn<QString>("library");
        QTest::addColumn<QString>("id");
        QTest::newRow("offline-abi") << QString(DSP_TEST_LIBRARY) << QString("test-dsp");
        const auto viper = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
        if (!viper.isEmpty())
            QTest::newRow("actual-viper") << viper << QString("echomusic-viper");
    }
    void importLifecycle()
    {
        QFETCH(QString, library);
        QFETCH(QString, id);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        NativeBridge native;
        AudioEngine engine;
        QSignalSpy events(&engine, &AudioEngine::eventReceived),
            closed(&engine, &AudioEngine::closed);
        QTRY_VERIFY_WITH_TIMEOUT(seen(events, "ready"), 15000);
        PlaybackAudioControls audio(engine, native, settings);
        QAbstractItemModelTester modelTester(
            audio.providers(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        connect(&engine, &AudioEngine::eventReceived, &audio, &PlaybackAudioControls::handleEvent);
        audio.initialize();
        QTRY_VERIFY_WITH_TIMEOUT(!audio.providers()->busy(), 5000);
        // Group labels are a local catalog projection, with no API wire samples.
        // Register real temporary files without selecting/decoding them.
        for (const auto &source :
             {QString("local"), QString("artist"), QString("headphone"), QString("market")}) {
            const auto path = directory.filePath(source + ".irs");
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            audio.registerImpulseResponse(path, source,
                                          source == "local" ? QString() : source + "-42");
            const auto projected = audio.impulseResponses().first().toMap();
            QCOMPARE(projected.value("source").toString(), source);
            QCOMPARE(projected.value("name").toString(), source);
        }
        const auto owned = audio.effectFilesRoot() + "/downloads/owned.irs";
        QVERIFY(QDir().mkpath(QFileInfo(owned).absolutePath()));
        QFile ownedFile(owned);
        QVERIFY(ownedFile.open(QIODevice::WriteOnly));
        ownedFile.close();
        audio.registerImpulseResponse(owned, "owned", "market-owned");
        const auto beforeRemoval = audio.impulseResponses();
        QSignalSpy fileNotices(&audio, &PlaybackAudioControls::fileNotice);
        auto frozen = StorageFence::freeze(settings.portableFilesRoot());
        QVERIFY(frozen);
        audio.removeImpulseResponse(owned);
        QCOMPARE(audio.impulseResponses(), beforeRemoval);
        QVERIFY(QFileInfo::exists(owned));
        QVERIFY(!fileNotices.last()[1].toBool());
        frozen.reset();
        audio.removeImpulseResponse(owned);
        QVERIFY(!QFileInfo::exists(owned));
        QCOMPARE(audio.impulseResponses().size(), beforeRemoval.size() - 1);
        audio.providers()->importFile(QUrl("https://example.invalid/engine"));
        QVERIFY(!audio.providers()->errorMessage().isEmpty());
        QCOMPARE(audio.providers()->count(), 0);
        audio.providers()->importFile(QUrl::fromLocalFile(library));
        QTRY_COMPARE_WITH_TIMEOUT(audio.providers()->count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 15000);
        QCOMPARE(audio.providerId(), id);
        QCOMPARE(settings.value("audio/providerId").toString(), id);
        const auto record = audio.providers()->record(id);
        QVERIFY(record.value("path").toString() != library);
        QFile original(library);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QVERIFY(hash.addData(&original));
        QCOMPARE(record.value("contentHash").toString(),
                 QString::fromLatin1(hash.result().toHex()));
        QVERIFY(!audio.providerPresets().isEmpty());
        const auto preset = id == "echomusic-viper" ? QString("kugou-super-bass") : QString("gain");
        audio.selectProviderPreset(preset);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
        QCOMPARE(audio.providerPresetId(), preset);
        if (id == "test-dsp") {
            audio.setProviderControl("gain", 0.5);
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 0.5);
            audio.setProviderControl("gain", 8);
            QVERIFY(!audio.effectPending());
            audio.setProviderControl("gain", 1.2);
            QVERIFY(!audio.effectPending());
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 0.5);
            audio.selectProviderPreset("");
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            audio.editProviderPreset("gain");
            audio.setProviderControl("gain", 1.5);
            QVERIFY(!audio.effectPending());
            QCOMPARE(audio.providerPresetId(), QString());
            audio.selectProviderPreset("gain");
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 1.5);
            audio.resetProviderControls();
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 2.);
            audio.setProviderControl("gain", 0.5);
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
        } else {
            QCOMPARE(record.value("version").toString(), QString("0.30.0"));
            QCOMPARE(audio.providerPresets().size(), 12);
            for (const auto &entry : audio.providerPresets()) {
                const auto presetId = entry.toMap().value("id").toString();
                audio.selectProviderPreset(presetId);
                QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
                QCOMPARE(audio.providerPresetId(), presetId);
                QVERIFY2(audio.errorMessage().isEmpty(), qPrintable(audio.errorMessage()));
            }
            audio.selectProviderPreset("kugou-vinyl");
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            audio.setProviderControl("aging", 10);
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            QVERIFY(audio.errorMessage().isEmpty());
        }
        audio.setProviderMode("headphone");
        QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
        QCOMPARE(audio.providerMode(), QString("headphone"));
        if (id == "test-dsp") {
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 2.);
            audio.setProviderMode("speaker");
            QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending(), 10000);
            QCOMPARE(audio.providerControls()[0].toMap().value("value").toDouble(), 0.5);
        }
        const auto rejected = directory.filePath("invalid." + QFileInfo(library).suffix());
        QFile invalid(rejected);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("invalid native library");
        invalid.close();
        audio.providers()->importFile(QUrl::fromLocalFile(rejected));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.providers()->busy(), 5000);
        QVERIFY(!audio.providers()->errorMessage().isEmpty());
        QCOMPARE(audio.providerId(), id);
        QCOMPARE(audio.providers()->record(id).value("path"), record.value("path"));
        // Same hash is reimported while loaded, never overwritten or duplicated.
        audio.providers()->importFile(QUrl::fromLocalFile(library));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.providers()->busy() && !audio.effectPending(), 15000);
        QCOMPARE(audio.providers()->count(), 1);
        QCOMPARE(audio.providers()->record(id).value("path"), record.value("path"));
        {
            DspProviderRegistry restored(native,
                                         settings.files("audio-engines", SettingsDomain::Device));
            QTRY_VERIFY_WITH_TIMEOUT(!restored.busy(), 5000);
            QCOMPARE(restored.count(), 1);
            QCOMPARE(restored.record(id).value("contentHash"), record.value("contentHash"));
        }
        const auto wave = directory.filePath("quiet.wav");
        QFile output(wave);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QDataStream stream(&output);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 length = 48000 * 4 * 4;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + length);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
               << quint16(4) << quint16(16);
        stream.writeRawData("data", 4);
        stream << length;
        for (int i = 0; i < 48000 * 4; ++i) {
            const auto sample = qint16(80 * std::sin(i * 0.03));
            stream << sample << sample;
        }
        output.close();
        engine.load(wave, 1);
        QTRY_VERIFY_WITH_TIMEOUT(seen(events, "file-loaded"), 15000);
        engine.command(AudioEngine::Play);
        QTRY_VERIFY_WITH_TIMEOUT(position(events) > 0.2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(audio.graph().value("provider_id").toString(), id, 5000);
        audio.removeProvider(id);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.effectPending() && !audio.providers()->busy(), 10000);
        QCOMPARE(audio.providerId(), QString());
        QCOMPARE(audio.providers()->count(), 0);
        QCOMPARE(settings.value("audio/providerId").toString(), QString());
        const auto before = position(events);
        QTRY_VERIFY_WITH_TIMEOUT(position(events) > before + 0.1, 5000);
        engine.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        native.shutdown();
    }
    void acceptsProviderWithoutManifest()
    {
        QTemporaryDir directory;
        NativeBridge native;
        DspProviderRegistry registry(native, directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(!registry.busy(), 5000);
        registry.importFile(QUrl::fromLocalFile(DSP_BARE_TEST_LIBRARY));
        QTRY_COMPARE_WITH_TIMEOUT(registry.count(), 1, 10000);
        const auto record = registry.record("test-dsp-bare");
        QVERIFY(!record.isEmpty());
        QVERIFY(!record.value("manifestDeclared").toBool());
        QVERIFY(record.value("manifest").toMap().value("presets").toList().isEmpty());
        QVERIFY(registry.errorMessage().isEmpty());
    }
    void rejectsUnsupportedCatalog()
    {
        QTemporaryDir directory;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "future-dsp-test");
            db.setDatabaseName(directory.filePath("catalog.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("PRAGMA user_version=2"));
            db.close();
        }
        QSqlDatabase::removeDatabase("future-dsp-test");
        NativeBridge native;
        DspProviderRegistry registry(native, directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(!registry.busy(), 5000);
        QVERIFY(!registry.errorMessage().isEmpty());
        registry.importFile(QUrl::fromLocalFile(DSP_TEST_LIBRARY));
        QTRY_VERIFY_WITH_TIMEOUT(!registry.busy(), 5000);
        QVERIFY(!registry.errorMessage().isEmpty());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(QDir(directory.path()).entryList({"*.dll", "*.dylib", "*.so"}, QDir::Files).size(),
                 0);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "future-dsp-check");
            db.setDatabaseName(directory.filePath("catalog.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("PRAGMA user_version"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 2);
            db.close();
        }
        QSqlDatabase::removeDatabase("future-dsp-check");
    }
    void actualPcmProcessing()
    {
        const auto path = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
        if (path.isEmpty())
            QSKIP("Set ECHO_DSP_TEST_PROVIDER to verify a real ViPER artifact");
        QLibrary library(path);
        QVERIFY2(library.load(), qPrintable(library.errorString()));
        auto getter =
            reinterpret_cast<const EchoDspApi *(*)()>(library.resolve("echo_dsp_get_api"));
        QVERIFY(getter);
        const auto *api = getter();
        QCOMPARE(api->abi_version, quint32(2));
        const EchoDspConfig config{
            2, 48000, 2, 512, 1, nullptr, "{\"presetId\":\"kugou-super-bass\"}"};
        EchoDspInfo info{};
        void *instance = api->create(&config, &info);
        QVERIFY(instance);
        double difference = 0;
        for (int block = 0; block < 64; ++block) {
            float pcm[1024], original[1024];
            for (int i = 0; i < 1024; ++i)
                pcm[i] = original[i] = float(0.02 * std::sin((block * 512 + i / 2) * 0.009));
            const auto code = api->process(instance, pcm, 512, 2);
            if (code) {
                api->destroy(instance);
                QFAIL("ViPER PCM processing failed");
            }
            for (int i = 0; i < 1024; ++i) {
                QVERIFY(std::isfinite(pcm[i]));
                difference += std::abs(pcm[i] - original[i]);
            }
        }
        api->destroy(instance);
        QVERIFY2(difference > 1, "Selected ViPER preset must actually change PCM");
    }
};
QTEST_MAIN(DspProviderTest)
#include "DspProviderTest.moc"
