#pragma once
#include <QObject>

class PluginRuntimeTest : public QObject
{
    Q_OBJECT
private slots:
    void uiStyleLayersRestoreLiveBindings();
    void uiPartsIdentityObservationAndPublicActions();
    void uiPartMountsReplaceAndRetire();
    void uiPartsProductionPlaybackAndMini();
    void uiPartInstanceStylesAndLeasesStayBounded();
    void sharedPluginVersionWarnings_data();
    void sharedPluginVersionWarnings();
    void sharedPluginPagePresentationAndRedirect();
    void catalogInstallationSourceIsNotCompatibility_data();
    void catalogInstallationSourceIsNotCompatibility();
    void initTestCase();
    void dependencyGraphAndVersionFailuresNeverExecuteCode();
    void dependentServicesActivationCancellationAndProviderReload();
    void optionalDependencyAndServiceLeasePreserveOwnership();
    void rootDirectoryDiscoveryUpdatesAndRemovesOwnedInput();
    void rootDiscoverySkipsStoresAndContinuesPastInvalidInput();
    void refreshReloadsAllInstalledPluginsAndPreservesDisabledState();
    void directoryActionOpensTheDisplayedRoot();
    void managementUpdateRollsBackPackageAndPrivateData();
    void metadataCommitFailureRetiresNewCodeAndRetainsRecoverySnapshot();
    void localBatchContinuesAfterInvalidPackage();
    void arrivingInstallFilesCompleteDependencyBatch();
    void qtFileOpenQueuesBeforeServiceInitialization();
    void githubMirrorFallbackAndPostShareUnifiedProxy();
    void sourceCatalogNetworkCacheAndOfficialProtection();
    void settingsContributionRetiresWithScope();
    void managementHeaderAndLoadingContentStayAligned();
    void managementPageUsesProductionModelsAndDialogs();
    void statisticsUseSampledWireAndRejectInvalidNumbers();
    void marketplaceInstallsActualPackageAndRejectsTampering();
    void spectrumCapabilityAndOwnerLifetime();
    void builtAuthorPackageCommands();
    void packageIntegrityAndCompatibility();
    void backupBarrierDrainsScopesWithoutChangingEnabledPreference();
    void realActivationSdkAndRetirement();
    void failureRollsBackAndPendingActivationCancels();
    void applicationQuitDrainsPluginIoFixture();
    void applicationQuitDrainsPluginIo_data();
    void applicationQuitDrainsPluginIo();
    void ioDestructionJoinsAcceptedStorageWrites();
    void interruptedProcessFixture();
    void killedProcessRecovery_data();
    void killedProcessRecovery();
    void promiseCanceledBeforeCommit();
    void rejectedPromiseAndLifecycleTimeout();
    void reloadUsesNewCodeUrlAndIdentity();
    void actualHttpAndCancellation();
    void persistedInstallAndSafeMode();
};
