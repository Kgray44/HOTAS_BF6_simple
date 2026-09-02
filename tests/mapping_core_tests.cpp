#include "axis_transform.h"
#include "axis_mapping_transition.h"
#include "adaptive_response.h"
#include "button_mapping.h"
#include "config_store.h"
#include "controller_manager.h"
#include "event_log.h"
#include "input_learning.h"
#include "physical_input_monitor.h"
#include "profile_model.h"
#include "profile_portability.h"
#include "profile_trigger_runtime.h"
#include "response_curve.h"

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

using namespace hotas;

namespace {
bool nearlyEqual(float left, float right)
{
    return std::abs(left - right) < 0.0001F;
}

int profileIndexFor(const MapperConfiguration &configuration, const QString &id)
{
    for (int index = 0; index < static_cast<int>(configuration.profiles.size()); ++index) {
        if (configuration.profiles[static_cast<size_t>(index)].id == id) return index;
    }
    return -1;
}

void setProfileTrigger(MapperConfiguration &configuration, int physicalButton,
                       const QString &targetProfileId, ProfileTriggerMode mode)
{
    const int source = physicalButton - 1;
    if (configuration.profileTriggers.size() <= static_cast<size_t>(source)) {
        configuration.profileTriggers.resize(static_cast<size_t>(source + 1));
    }
    configuration.profileTriggers[static_cast<size_t>(source)] = {targetProfileId, mode};
}

void setPovProfileTrigger(MapperConfiguration &configuration, int povHat, PovDirection direction,
                          const QString &targetProfileId, ProfileTriggerMode mode)
{
    const int hat = povHat - 1;
    const int index = povDirectionIndex(direction);
    if (configuration.povProfileTriggers.size() <= static_cast<size_t>(hat)) {
        configuration.povProfileTriggers.resize(static_cast<size_t>(hat + 1));
    }
    configuration.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(index)] =
        {targetProfileId, mode};
}

QJsonObject legacyConfigurationJson(const MapperConfiguration &configuration, int version)
{
    const ControllerProfile &profile = activeProfile(configuration);
    QJsonArray axes;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const AxisMapping &mapping = profile.axes[index];
        const Calibration &calibration = configuration.calibration[index];
        axes.append(QJsonObject{
            {QStringLiteral("target"), virtualAxisLabel(mapping.target)},
            {QStringLiteral("inverted"), mapping.inverted},
            {QStringLiteral("deadzone"), mapping.deadzone},
            {QStringLiteral("calibration"), QJsonObject{
                {QStringLiteral("enabled"), calibration.enabled},
                {QStringLiteral("minimum"), calibration.minimum},
                {QStringLiteral("center"), calibration.center},
                {QStringLiteral("maximum"), calibration.maximum},
            }},
        });
    }
    QJsonObject json{
        {QStringLiteral("version"), version},
        {QStringLiteral("preferredDeviceId"), configuration.preferredDeviceId},
        {QStringLiteral("vjoyDeviceId"), configuration.vjoyDeviceId},
        {QStringLiteral("startMappingOnLaunch"), configuration.startMappingOnLaunch},
        {QStringLiteral("axes"), axes},
    };
    if (version >= 2) {
        QJsonArray buttons;
        for (const ButtonBinding &binding : profile.buttons) {
            buttons.append(binding.type == ButtonActionType::VirtualButton
                ? QJsonObject{{QStringLiteral("type"), QStringLiteral("virtualButton")},
                              {QStringLiteral("target"), binding.target}}
                : QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}});
        }
        json.insert(QStringLiteral("buttons"), buttons);
    }
    return json;
}
}

class MappingCoreTests final : public QObject {
    Q_OBJECT

private slots:
    void calibrationNormalizesBothSides();
    void calibrationCenterRejectsSingleOutlier();
    void offsetCenterBecomesUserFacingZero();
    void nonCenteringCalibrationUsesRangeWithoutInventedNeutral();
    void invalidCalibrationFallsBackToRaw();
    void deadzoneIsRescaled();
    void oneSidedTransferUsesFormerCenterAsOrigin();
    void oneSidedDeadzoneAndInversionStayInUnipolarDomain();
    void oneSidedCurveDomainKeepsThePositiveCenteredHalf();
    void inversionIsAppliedAfterDeadzone();
    void rangeIsClamped();
    void processingOrderIncludesLimitsAfterInversion();
    void hysteresisSuppressesNoiseWithoutTemporalSmoothing();
    void hysteresisHandlesDeadzoneAndFullScaleBoundaries();
    void outputLimitsSupportAsymmetryAndInversion();
    void staticPreviewSharesTheTransferEvaluator();
    void curveTransitionLinearToPrecisionIsBumpless();
    void curveTransitionPrecisionToLinearIsBumpless();
    void curveTransitionFollowsPhysicalMovementThroughNewMapping();
    void curveTransitionRapidToggleUsesActualOutputAnchor();
    void curveTransitionTinyDifferenceBypassesState();
    void curveTransitionOppositeSignRemainsClamped();
    void curveTransitionZeroDurationIsImmediate();
    void curveTransitionDisabledIsImmediate();
    void curveTransitionUserSlamCancelsCorrection();
    void curveTransitionsRemainIndependentAcrossAxes();
    void curveTransitionSettingsPersistAndMigrate();
    void adaptiveResponsePredictsThenConvergesToPhysicalInput();
    void adaptiveResponseUsesDistinctAlphaBetaAndAlphaBetaGammaEstimators();
    void adaptiveResponseVelocityAndSettlingControlsChangePrediction();
    void adaptiveResponseResetClearsSessionMotionState();
    void adaptiveResponseDetectsPositiveNegativeAndCenterCrossingReversals();
    void adaptiveResponseCancelsStaleLeadAtReversal();
    void adaptiveResponseTapersAndClampsAtEndpoint();
    void adaptiveResponsePersistsAndResolvesLayeredSettings();
    void adaptiveResponseRecognizesSlowMotionAcrossSampleRates();
    void adaptiveResponsePreservesSlowWobbleAcrossSampleRates();
    void adaptiveResponseHandlesSampleAndHoldSourcesThenSettles();
    void adaptiveResponseRejectsStationaryJitterAndSingleOppositeSpike();
    void adaptiveResponseHandlesHardGentleAndRepeatedReversals();
    void adaptiveResponseConvergesEstimatorsAfterStopAndResume();
    void adaptiveResponseHumanLikeTurningPointShape();
    void adaptiveResponseOnsetAssistIsBoundedSymmetricAndQuiet();
    void adaptiveResponseSustainedHorizonAndTurningProtection();
    void responseCurveFamiliesAreBoundedMonotonicAndCompiled();
    void universalStrengthUsesIdentityAtZeroAndFullResponseAtOne();
    void strengthAndAxisSelectionPersistPerProfile();
    void advancedPresetsAreDistinctAndQuantified();
    void curvePointEditingSupportsNonuniformPointsLocksAndResampling();
    void curveGainUsesAuthoritativeEvaluation();
    void personalCurvePresetsPersistAsIndependentDefinitions();
    void v12ProfileConfigurationMigratesWithSafeAxisDefaults();
    void virtualControllersAreNeverEligibleAsPhysicalInput();
    void implicitButtonsDefaultToMatchingVjoyTargets();
    void inputLearningSelectsDeliberateAxisWithoutGuessing();
    void inputLearningSelectsAReleasedThenPressedButton();
    void configurationRoundTrips();
    void outputLimitsRoundTripAcrossDomainsAndSchemaMigration();
    void controllerRegistryPersistsPerDeviceCalibrationAndRequirements();
    void controllerIdentityUsesLayeredMatchingWithoutAmbiguousAutoSelection();
    void vjoyAxisDescriptorsMustMatchExactlyWhileCapacitySupersetsAreAccepted();
    void physicalAxisActivityRequiresCompletedCalibrationTravel();
    void v17ConfigurationMigratesToPreservedOutputLayout();
    void disabledAxisValueDefaultsMigratesAndPersistsGlobally();
    void disabledAxisValueClampsSafely();
    void disabledAxisOutputPlanParksUnusedTargetsWithoutChangingMappedAxes();
    void v185AxisMetadataAndMappingControlsRoundTrip();
    void expandedVirtualAxesArePlannedAndUnavailableRoutesStayParked();
    void duplicateMappingIsRejectedAndNormalized();
    void buttonCapacityMismatchIsReported();
    void defaultButtonPassthroughIsCapacityBounded();
    void fifteenButtonPassthroughUsesButtonsOneThroughFifteen();
    void buttonMappingPropagatesPressAndRelease();
    void buttonsNineThroughFifteenPropagatePressAndRelease();
    void stoppingMappingReleasesVirtualButtons();
    void disabledAndInvalidButtonsDoNotMap();
    void buttonRouteDecisionsAreAtomicAndSupportFanIn();
    void buttonConfigurationRoundTripsAndMigratesV1();
    void malformedButtonConfigurationPreservesAxisConfiguration();
    void povRawValuesCompileToLogicalDirections();
    void povMappingsPressTransitionAndRelease();
    void povMappingsRejectDuplicatesAndRoundTrip();
    void legacyControlsMigrateToAutomationWithoutHiddenPaths();
    void eventLogIsBoundedAndOrdered();
    void physicalMonitorPublishesWhenMappingIsStoppedAndVJoyIsUnavailable();
    void physicalMonitorRetainsFullOfflineButtonCount();
    void physicalMonitorPropagatesPressReleaseAndDisconnect();
    void physicalMonitorRecoversAfterReconnect();
    void legacyConfigurationMigratesToNormalAndPrecision();
    void profileMigrationIsIdempotent();
    void categoryMigrationPreservesExistingProfiles();
    void categoryScopedNamesAndStableReferencesSurviveMove();
    void portableProfileRoundTripIsAtomicAndRemapsIds();
    void portablePackRoundTripPreservesCategoryAndSkipsHardwareByDefault();
    void portablePackSelectionsConflictsAndDependenciesAreSafe();
    void portableDeviceMatchingAndCalibrationRequireExplicitIntent();
    void portableFormatValidationRejectsFutureAndInvalidDependenciesAtomically();
    void gameCategoryDetectionIsPureLowFrequencyControlPlaneLogic();
    void profileCrudValidatesNamesAndProtectsNormal();
    void newProfileClonesRequestedSourceIndependently();
    void profilesRemainIsolatedAndPersist();
    void profileSwitchCompilesCompleteAxisConfiguration();
    void profileSwitchReevaluatesHeldButtons();
    void calibrationRemainsGlobalAcrossProfiles();
    void malformedProfileConfigurationFallsBackSafely();
    void profileTriggerConfigurationRoundTripsAndMigrates();
    void holdProfileTriggerSelectsPrecompiledRuntimeAndConsumesButton();
    void multipleHoldProfileTriggersUseMostRecentPress();
    void toggleProfileTriggerUsesRisingEdgesAndActivationOrder();
    void holdOverridesToggleAndManualBaseChangeClearsToggle();
    void profileTriggerConfigChangesReconcileHeldState();
    void missingAndRenamedProfileTriggerTargetsAreSafe();
    void povProfileControlsShareGlobalPrecedenceAndConsumeDirectionRoute();
    void povProfileAndNativePovConfigurationRoundTripWithSafeMigration();
};

void MappingCoreTests::calibrationNormalizesBothSides()
{
    const Calibration calibration{true, -0.80F, 0.10F, 0.90F};
    QVERIFY(nearlyEqual(normalizeCalibrated(-0.35F, calibration), -0.50F));
    QVERIFY(nearlyEqual(normalizeCalibrated(0.50F, calibration), 0.50F));
    QVERIFY(nearlyEqual(normalizeCalibrated(-1.0F, calibration), -1.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(1.0F, calibration), 1.0F));
}

void MappingCoreTests::calibrationCenterRejectsSingleOutlier()
{
    // Center capture is bounded and sampled on the UI control plane. A single
    // bump must not move the persisted neutral away from the quiet samples.
    std::array<float, 32> samples{};
    samples[0] = 0.010F;
    samples[1] = 0.012F;
    samples[2] = 0.009F;
    samples[3] = 0.011F;
    samples[4] = 0.010F;
    samples[5] = 0.013F;
    samples[6] = 0.008F;
    samples[7] = 0.012F;
    samples[8] = 0.750F; // brief accidental movement
    QVERIFY(nearlyEqual(robustCalibrationCenter(samples, 9), 0.011F));
}

void MappingCoreTests::offsetCenterBecomesUserFacingZero()
{
    const Calibration calibration{true, -1.0F, -0.04F, 1.0F};
    QVERIFY(nearlyEqual(normalizeCalibrated(-1.0F, calibration), -1.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(-0.04F, calibration), 0.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(1.0F, calibration), 1.0F));

    // The graph now receives calibrated coordinates, so its Linear trace is
    // the visual identity rather than a raw-sensor piecewise correction.
    RuntimeAxisMapping graphMapping;
    graphMapping.profile.deadzone = 0.0F;
    graphMapping.profile.hysteresis = 0.0F;
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-1.0F, graphMapping), -1.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.0F, graphMapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, graphMapping), 1.0F));
}

void MappingCoreTests::nonCenteringCalibrationUsesRangeWithoutInventedNeutral()
{
    Calibration calibration{true, -0.82F, 0.0F, 0.91F};
    calibration.centered = false;
    QVERIFY(nearlyEqual(normalizeCalibrated(-0.82F, calibration), -1.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(0.91F, calibration), 1.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(0.045F, calibration), 0.0F));
}

void MappingCoreTests::invalidCalibrationFallsBackToRaw()
{
    const Calibration calibration{true, -0.20F, -0.20F, 0.80F};
    QVERIFY(nearlyEqual(normalizeCalibrated(0.37F, calibration), 0.37F));
}

void MappingCoreTests::deadzoneIsRescaled()
{
    QVERIFY(nearlyEqual(applyRescaledDeadzone(0.20F, 0.20F), 0.0F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(0.60F, 0.20F), 0.50F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(-0.60F, 0.20F), -0.50F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(1.0F, 0.20F), 1.0F));
}

void MappingCoreTests::oneSidedTransferUsesFormerCenterAsOrigin()
{
    RuntimeAxisMapping mapping;
    mapping.profile.rangeMode = AxisRangeMode::OneSided;
    mapping.profile.deadzone = 0.0F;
    mapping.profile.hysteresis = 0.0F;
    mapping.responseCurve = compileResponseCurve(linearCurveDefinition(), true);

    // The old centered origin is now the bottom-left 0% point. Negative
    // travel parks there; no part of the centered transfer survives.
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-1.0F, mapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.0F, mapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.50F, mapping), 0.50F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, mapping), 1.0F));

    AxisHysteresisState state;
    QVERIFY(nearlyEqual(transformAxisLive(0.0F, mapping, state), 0.0F));
    QVERIFY(nearlyEqual(transformAxisLive(1.0F, mapping, state), 1.0F));
}

void MappingCoreTests::oneSidedDeadzoneAndInversionStayInUnipolarDomain()
{
    RuntimeAxisMapping mapping;
    mapping.profile.rangeMode = AxisRangeMode::OneSided;
    mapping.profile.deadzone = 0.03F;
    mapping.profile.hysteresis = 0.0F;
    mapping.responseCurve = compileResponseCurve(linearCurveDefinition(), true);

    QVERIFY(nearlyEqual(applyRescaledUnipolarDeadzone(0.03F, 0.03F), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-0.25F, mapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.0F, mapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.03F, mapping), 0.0F));
    QVERIFY(evaluateStaticAxisTransfer(0.04F, mapping) > 0.0F);
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, mapping), 1.0F));

    mapping.profile.inverted = true;
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.0F, mapping), 1.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, mapping), 0.0F));
}

void MappingCoreTests::oneSidedCurveDomainKeepsThePositiveCenteredHalf()
{
    CurveDefinition centered = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("precision-tracking")), false, 9);
    const float oldPositiveHalf = evaluateCurveDefinition(0.50F, centered, false);
    const CurveDefinition oneSided = convertCurveDefinitionDomain(centered, false, true);
    QVERIFY(curveDefinitionIsValid(oneSided, true));
    QVERIFY(nearlyEqual(oneSided.points.front().input, 0.0F));
    QVERIFY(nearlyEqual(oneSided.points.front().output, 0.0F));
    QVERIFY(nearlyEqual(oneSided.points.back().input, 1.0F));
    QVERIFY(nearlyEqual(oneSided.points.back().output, 1.0F));
    for (const CurvePoint &point : oneSided.points) {
        QVERIFY(point.input >= 0.0F);
        QVERIFY(point.output >= 0.0F);
    }
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.50F, oneSided, true), oldPositiveHalf));
}

void MappingCoreTests::inversionIsAppliedAfterDeadzone()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.10F;
    mapping.profile.inverted = true;
    QVERIFY(nearlyEqual(transformAxis(0.55F, mapping), -0.50F));
    QVERIFY(nearlyEqual(transformAxis(0.02F, mapping), 0.0F));
}

void MappingCoreTests::rangeIsClamped()
{
    QCOMPARE(clampUnit(5.0F), 1.0F);
    QCOMPARE(clampUnit(-5.0F), -1.0F);
    QVERIFY(nearlyEqual(applyRescaledDeadzone(4.0F, 0.03F), 1.0F));
}

void MappingCoreTests::processingOrderIncludesLimitsAfterInversion()
{
    RuntimeAxisMapping mapping;
    mapping.calibration = {true, -0.80F, 0.10F, 0.90F};
    mapping.profile.deadzone = 0.10F;
    mapping.profile.inverted = true;
    mapping.profile.outputMinimum = -0.40F;
    mapping.profile.outputMaximum = 0.70F;

    // 0.50 raw calibrates to +0.50, rescales beyond the 10% deadzone to
    // +0.444…, inverts, then clamps at the final output minimum.
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.50F, mapping), -0.40F));
}

void MappingCoreTests::hysteresisSuppressesNoiseWithoutTemporalSmoothing()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.0F;
    mapping.profile.hysteresis = 0.002F;
    AxisHysteresisState state;

    QVERIFY(nearlyEqual(transformAxisLive(0.2000F, mapping, state), 0.2000F));
    QVERIFY(nearlyEqual(transformAxisLive(0.2005F, mapping, state), 0.2000F));
    QVERIFY(nearlyEqual(transformAxisLive(0.1986F, mapping, state), 0.2000F));
    QVERIFY(nearlyEqual(transformAxisLive(0.2040F, mapping, state), 0.2040F));
    QVERIFY(nearlyEqual(transformAxisLive(-0.3000F, mapping, state), -0.3000F));
    QVERIFY(nearlyEqual(transformAxisLive(-0.3010F, mapping, state), -0.3000F));
}

void MappingCoreTests::hysteresisHandlesDeadzoneAndFullScaleBoundaries()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.10F;
    mapping.profile.hysteresis = 0.02F;
    AxisHysteresisState state;

    QVERIFY(nearlyEqual(transformAxisLive(0.30F, mapping, state), 0.222222F));
    // Crossing into the deadzone clears the prior accepted command; a later
    // tiny movement remains centered rather than permanently sticking.
    QVERIFY(nearlyEqual(transformAxisLive(0.095F, mapping, state), 0.0F));
    QVERIFY(nearlyEqual(transformAxisLive(0.105F, mapping, state), 0.0F));
    QVERIFY(nearlyEqual(transformAxisLive(0.20F, mapping, state), 0.111111F));
    QVERIFY(nearlyEqual(transformAxisLive(1.0F, mapping, state), 1.0F));
    QVERIFY(nearlyEqual(transformAxisLive(-1.0F, mapping, state), -1.0F));
}

void MappingCoreTests::outputLimitsSupportAsymmetryAndInversion()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.0F;
    mapping.profile.outputMinimum = -0.70F;
    mapping.profile.outputMaximum = 0.80F;

    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-1.0F, mapping), -0.70F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, mapping), 0.80F));
    mapping.profile.inverted = true;
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(1.0F, mapping), -0.70F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-1.0F, mapping), 0.80F));
}

void MappingCoreTests::staticPreviewSharesTheTransferEvaluator()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.20F;
    mapping.profile.inverted = true;
    mapping.profile.outputMinimum = -0.60F;
    mapping.profile.outputMaximum = 0.50F;

    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.10F, mapping), 0.0F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(0.60F, mapping), -0.50F));
    QVERIFY(nearlyEqual(evaluateStaticAxisTransfer(-1.0F, mapping), 0.50F));
    // The compatibility wrapper remains the same static evaluator used for
    // the v1.3 output trace; hysteresis is deliberately live-only.
    QVERIFY(nearlyEqual(transformAxis(0.60F, mapping), evaluateStaticAxisTransfer(0.60F, mapping)));
}

void MappingCoreTests::curveTransitionLinearToPrecisionIsBumpless()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, {});
    QVERIFY(transitions.active(1));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 0), 0.50F));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 50'000), 0.375F));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 100'000), 0.25F));
    QVERIFY(!transitions.active(1));
}

void MappingCoreTests::curveTransitionPrecisionToLinearIsBumpless()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.25F, 0.50F, 0.50F, 0, 0, {});
    QVERIFY(nearlyEqual(transitions.apply(1, 0.50F, 0.50F, 0, 0), 0.25F));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.50F, 0.50F, 0, 50'000), 0.375F));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.50F, 0.50F, 0, 100'000), 0.50F));
}

void MappingCoreTests::curveTransitionFollowsPhysicalMovementThroughNewMapping()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, {});
    // At 25 ms the remaining correction is retained, but the 0.64 new-map
    // target is used immediately. This is not interpolation toward 0.25.
    const float moved = transitions.apply(1, 0.64F, 0.62F, 0, 25'000);
    QVERIFY(moved > 0.64F);
    QVERIFY(moved < 1.0F);
}

void MappingCoreTests::curveTransitionRapidToggleUsesActualOutputAnchor()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, {});
    const float actualMidTransition = transitions.apply(1, 0.25F, 0.50F, 0, 50'000);
    QVERIFY(nearlyEqual(actualMidTransition, 0.375F));
    transitions.begin(1, actualMidTransition, 0.50F, 0.50F, 0, 50'000, {});
    QVERIFY(nearlyEqual(transitions.apply(1, 0.50F, 0.50F, 0, 50'000), actualMidTransition));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.50F, 0.50F, 0, 150'000), 0.50F));
}

void MappingCoreTests::curveTransitionTinyDifferenceBypassesState()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.000F, 0.002F, 0.0F, 0, 0, {});
    QVERIFY(!transitions.active(1));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.002F, 0.0F, 0, 0), 0.002F));
}

void MappingCoreTests::curveTransitionOppositeSignRemainsClamped()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.40F, -0.40F, 0.40F, 0, 0, {});
    QVERIFY(nearlyEqual(transitions.apply(1, -0.40F, 0.40F, 0, 0), 0.40F));
    QVERIFY(nearlyEqual(transitions.apply(1, -0.40F, 0.40F, 0, 50'000), 0.0F));
    QVERIFY(nearlyEqual(transitions.apply(1, -0.40F, 0.40F, 0, 100'000), -0.40F));
}

void MappingCoreTests::curveTransitionZeroDurationIsImmediate()
{
    AxisMappingTransitionEngine transitions;
    CurveTransitionSmoothingSettings settings;
    settings.durationMs = 0;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, settings);
    QVERIFY(!transitions.active(1));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 0), 0.25F));
}

void MappingCoreTests::curveTransitionDisabledIsImmediate()
{
    AxisMappingTransitionEngine transitions;
    CurveTransitionSmoothingSettings settings;
    settings.enabled = false;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, settings);
    QVERIFY(!transitions.active(1));
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 0), 0.25F));
}

void MappingCoreTests::curveTransitionUserSlamCancelsCorrection()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, {});
    QVERIFY(nearlyEqual(transitions.apply(1, 0.90F, 1.0F, 0, 25'000), 0.90F));
    QVERIFY(!transitions.active(1));
}

void MappingCoreTests::curveTransitionsRemainIndependentAcrossAxes()
{
    AxisMappingTransitionEngine transitions;
    transitions.begin(1, 0.50F, 0.25F, 0.50F, 0, 0, {});
    transitions.begin(2, -0.40F, -0.80F, -0.40F, 1, 0, {});
    QVERIFY(nearlyEqual(transitions.apply(1, 0.25F, 0.50F, 0, 50'000), 0.375F));
    QVERIFY(nearlyEqual(transitions.apply(2, -0.80F, -0.40F, 1, 50'000), -0.60F));
    QVERIFY(transitions.active(1));
    QVERIFY(transitions.active(2));
}

void MappingCoreTests::curveTransitionSettingsPersistAndMigrate()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.curveTransitionSmoothing = {false, 275};
    ControllerProfile &precision = *findProfile(configuration, precisionProfileId());
    precision.curveTransitionSmoothingOverride = true;
    precision.curveTransitionSmoothing = {true, 150};

    bool valid = false;
    const QJsonObject serialized = ConfigStore::toJson(configuration);
    const MapperConfiguration restored = ConfigStore::fromJson(serialized, &valid);
    QVERIFY(valid);
    QVERIFY(!restored.curveTransitionSmoothing.enabled);
    QCOMPARE(restored.curveTransitionSmoothing.durationMs, 275);
    const ControllerProfile *restoredPrecision = findProfile(restored, precisionProfileId());
    QVERIFY(restoredPrecision && restoredPrecision->curveTransitionSmoothingOverride);
    QVERIFY(restoredPrecision->curveTransitionSmoothing.enabled);
    QCOMPARE(restoredPrecision->curveTransitionSmoothing.durationMs, 150);

    QJsonObject legacy = serialized;
    legacy.insert(QStringLiteral("version"), 19);
    legacy.remove(QStringLiteral("curveTransitionSmoothing"));
    QJsonArray profiles = legacy.value(QStringLiteral("profiles")).toArray();
    for (QJsonValueRef value : profiles) {
        QJsonObject profile = value.toObject();
        profile.remove(QStringLiteral("curveTransitionSmoothingOverride"));
        profile.remove(QStringLiteral("curveTransitionSmoothing"));
        value = profile;
    }
    legacy.insert(QStringLiteral("profiles"), profiles);
    const MapperConfiguration migrated = ConfigStore::fromJson(legacy, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.curveTransitionSmoothing.enabled);
    QCOMPARE(migrated.curveTransitionSmoothing.durationMs, 100);
    const ControllerProfile *migratedPrecision = findProfile(migrated, precisionProfileId());
    QVERIFY(migratedPrecision && !migratedPrecision->curveTransitionSmoothingOverride);
}

void MappingCoreTests::adaptiveResponsePredictsThenConvergesToPhysicalInput()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Velocity;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.001F;

    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    QVERIFY(nearlyEqual(processor.process(0.0F, configuration, origin).predicted, 0.0F));
    const AdaptiveResponseTelemetry moving = processor.process(
        0.12F, configuration, origin + std::chrono::milliseconds(4));
    QVERIFY(moving.predicted > moving.physical);
    QVERIFY(moving.activeHorizonSeconds > 0.0F);

    const AdaptiveResponseTelemetry held = processor.process(
        0.12F, configuration, origin + std::chrono::milliseconds(8));
    QVERIFY(held.predicted > held.physical);
    QVERIFY(held.velocity > 0.0F);
    const AdaptiveResponseTelemetry stopped = processor.process(
        0.12F, configuration, origin + std::chrono::milliseconds(60));
    QVERIFY(nearlyEqual(stopped.predicted, stopped.physical));
    QVERIFY(nearlyEqual(stopped.velocity, 0.0F));
}

void MappingCoreTests::adaptiveResponseUsesDistinctAlphaBetaAndAlphaBetaGammaEstimators()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.maximumHorizonSeconds = 0.020F;
    configuration.maximumLead = 0.35F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.76F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.001F;
    const auto origin = std::chrono::steady_clock::time_point{};

    configuration.model = AdaptiveResponseModel::AlphaBeta;
    AdaptiveResponseProcessor alphaBeta;
    alphaBeta.process(0.0F, configuration, origin);
    const AdaptiveResponseTelemetry abFirst = alphaBeta.process(
        0.18F, configuration, origin + std::chrono::milliseconds(4));
    const AdaptiveResponseTelemetry abSecond = alphaBeta.process(
        0.34F, configuration, origin + std::chrono::milliseconds(8));
    QVERIFY(!nearlyEqual(abFirst.estimated, abFirst.physical));
    QVERIFY(!nearlyEqual(abSecond.estimated, abSecond.physical));
    QVERIFY(nearlyEqual(abSecond.acceleration, 0.0F));

    configuration.model = AdaptiveResponseModel::AlphaBetaGamma;
    AdaptiveResponseProcessor alphaBetaGamma;
    alphaBetaGamma.process(0.0F, configuration, origin);
    alphaBetaGamma.process(0.18F, configuration, origin + std::chrono::milliseconds(4));
    const AdaptiveResponseTelemetry abgSecond = alphaBetaGamma.process(
        0.34F, configuration, origin + std::chrono::milliseconds(8));
    const AdaptiveResponseTelemetry abThird = alphaBeta.process(
        0.46F, configuration, origin + std::chrono::milliseconds(12));
    const AdaptiveResponseTelemetry abgThird = alphaBetaGamma.process(
        0.46F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(!nearlyEqual(abgSecond.estimated, abgSecond.physical));
    QVERIFY(std::abs(abgSecond.acceleration) > 0.001F);
    QVERIFY(!nearlyEqual(abgThird.estimated, abThird.estimated));

    configuration.model = AdaptiveResponseModel::Auto;
    AdaptiveResponseProcessor automatic;
    automatic.process(0.0F, configuration, origin);
    automatic.process(0.18F, configuration, origin + std::chrono::milliseconds(4));
    const AdaptiveResponseTelemetry autoSecond = automatic.process(
        0.34F, configuration, origin + std::chrono::milliseconds(8));
    QVERIFY(std::abs(autoSecond.acceleration) > 0.001F);
    QVERIFY(!nearlyEqual(autoSecond.estimated, autoSecond.physical));
}

void MappingCoreTests::adaptiveResponseVelocityAndSettlingControlsChangePrediction()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Velocity;
    configuration.maximumHorizonSeconds = 0.024F;
    configuration.maximumLead = 0.40F;
    configuration.motionSensitivity = 0.008F;
    configuration.noiseRejection = 0.0001F;
    const auto origin = std::chrono::steady_clock::time_point{};

    configuration.velocityResponse = 0.10F;
    AdaptiveResponseProcessor slowVelocity;
    slowVelocity.process(0.0F, configuration, origin);
    slowVelocity.process(0.08F, configuration, origin + std::chrono::milliseconds(4));
    const AdaptiveResponseTelemetry slow = slowVelocity.process(
        0.16F, configuration, origin + std::chrono::milliseconds(8));

    configuration.velocityResponse = 1.0F;
    AdaptiveResponseProcessor fastVelocity;
    fastVelocity.process(0.0F, configuration, origin);
    fastVelocity.process(0.08F, configuration, origin + std::chrono::milliseconds(4));
    const AdaptiveResponseTelemetry fast = fastVelocity.process(
        0.16F, configuration, origin + std::chrono::milliseconds(8));
    QVERIFY(fast.predicted > slow.predicted + 0.001F);

    configuration.velocityResponse = 0.80F;
    configuration.settlingResponse = 0.0F;
    AdaptiveResponseProcessor looseSettling;
    looseSettling.process(0.0F, configuration, origin);
    looseSettling.process(0.20F, configuration, origin + std::chrono::milliseconds(4));
    looseSettling.process(0.32F, configuration, origin + std::chrono::milliseconds(8));
    looseSettling.process(0.40F, configuration, origin + std::chrono::milliseconds(12));
    const AdaptiveResponseTelemetry loose = looseSettling.process(
        0.44F, configuration, origin + std::chrono::milliseconds(16));

    configuration.settlingResponse = 1.0F;
    AdaptiveResponseProcessor tightSettling;
    tightSettling.process(0.0F, configuration, origin);
    tightSettling.process(0.20F, configuration, origin + std::chrono::milliseconds(4));
    tightSettling.process(0.32F, configuration, origin + std::chrono::milliseconds(8));
    tightSettling.process(0.40F, configuration, origin + std::chrono::milliseconds(12));
    const AdaptiveResponseTelemetry tight = tightSettling.process(
        0.44F, configuration, origin + std::chrono::milliseconds(16));
    QVERIFY(tight.activeHorizonSeconds < loose.activeHorizonSeconds);
    // Stop policy shapes the prediction horizon; it must not rewrite the
    // estimator's physical-motion state merely to make the graph look quiet.
    QVERIFY(nearlyEqual(tight.velocity, loose.velocity));
}

void MappingCoreTests::adaptiveResponseResetClearsSessionMotionState()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::AlphaBetaGamma;
    configuration.maximumHorizonSeconds = 0.020F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.0001F;
    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    processor.process(0.0F, configuration, origin);
    processor.process(0.22F, configuration, origin + std::chrono::milliseconds(4));
    processor.process(0.12F, configuration, origin + std::chrono::milliseconds(8));
    processor.process(0.02F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(processor.reversalCount() > 0);
    processor.reset();
    const AdaptiveResponseTelemetry restarted = processor.process(
        0.37F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(nearlyEqual(restarted.estimated, 0.37F));
    QVERIFY(nearlyEqual(restarted.predicted, 0.37F));
    QVERIFY(nearlyEqual(restarted.velocity, 0.0F));
    QCOMPARE(processor.reversalCount(), std::uint64_t{0});
}

void MappingCoreTests::adaptiveResponseDetectsPositiveNegativeAndCenterCrossingReversals()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::AlphaBetaGamma;
    configuration.maximumHorizonSeconds = 0.020F;
    configuration.maximumLead = 0.40F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.0001F;
    configuration.reversalDetection = 0.020F;
    const auto origin = std::chrono::steady_clock::time_point{};
    const auto reverses = [&configuration, origin](float start, float away, float returnValue) {
        AdaptiveResponseProcessor processor;
        processor.process(start, configuration, origin);
        processor.process(away, configuration, origin + std::chrono::milliseconds(4));
        processor.process(returnValue, configuration, origin + std::chrono::milliseconds(8));
        return processor.process(returnValue + (returnValue - away), configuration,
                                 origin + std::chrono::milliseconds(12));
    };
    QVERIFY(reverses(0.45F, 0.85F, 0.62F).reversal);
    QVERIFY(reverses(-0.45F, -0.85F, -0.62F).reversal);
    QVERIFY(reverses(-0.20F, 0.30F, -0.12F).reversal);
}

void MappingCoreTests::adaptiveResponseCancelsStaleLeadAtReversal()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::AlphaBetaGamma;
    configuration.maximumHorizonSeconds = 0.024F;
    configuration.maximumLead = 0.35F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.001F;
    configuration.reversalDetection = 0.020F;

    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    processor.process(0.0F, configuration, origin);
    processor.process(0.20F, configuration, origin + std::chrono::milliseconds(4));
    processor.process(0.15F, configuration, origin + std::chrono::milliseconds(8));
    const AdaptiveResponseTelemetry reversed = processor.process(
        0.10F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(reversed.reversal);
    QVERIFY(reversed.predicted < reversed.physical);
    QCOMPARE(reversed.state, AdaptiveMotionState::Reversing);
    QCOMPARE(processor.reversalCount(), std::uint64_t{1});
}

void MappingCoreTests::adaptiveResponseTapersAndClampsAtEndpoint()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Velocity;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.50F;
    configuration.motionSensitivity = 0.005F;
    configuration.noiseRejection = 0.0001F;
    configuration.endpointTaper = 0.20F;

    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    processor.process(0.80F, configuration, origin);
    const AdaptiveResponseTelemetry endpoint = processor.process(
        1.0F, configuration, origin + std::chrono::milliseconds(4));
    QCOMPARE(endpoint.predicted, 1.0F);
    QVERIFY(endpoint.safetyLimited);
    QVERIFY(processor.safetyClampCount() > 0);

    configuration.enabled = false;
    const AdaptiveResponseTelemetry disabled = processor.process(
        0.42F, configuration, origin + std::chrono::milliseconds(8));
    QVERIFY(nearlyEqual(disabled.predicted, 0.42F));
}

void MappingCoreTests::adaptiveResponsePersistsAndResolvesLayeredSettings()
{
    const auto &builtIns = builtInAdaptiveResponsePresets();
    const auto settingsFor = [&builtIns](size_t index) { return builtIns[index].axes[0].settings; };
    QCOMPARE(settingsFor(0).sustainedAssist, 0.0F);
    QCOMPARE(settingsFor(0).horizonExtensionCapMs, 0.0F);
    QCOMPARE(settingsFor(1).sustainedAssist, 0.12F);
    QCOMPARE(settingsFor(1).sustainedCap, 0.06F);
    QCOMPARE(settingsFor(1).horizonExtension, 0.10F);
    QCOMPARE(settingsFor(1).horizonExtensionCapMs, 4.0F);
    QCOMPARE(settingsFor(1).turningPointProtection, 0.75F);
    QCOMPARE(settingsFor(5).sustainedAssist, 0.55F);
    QCOMPARE(settingsFor(5).sustainedCap, 0.28F);
    QCOMPARE(settingsFor(5).horizonExtension, 0.65F);
    QCOMPARE(settingsFor(5).horizonExtensionCapMs, 24.0F);
    QCOMPARE(settingsFor(5).turningPointProtection, 1.0F);
    QCOMPARE(settingsFor(5).turningPointMargin, 0.08F);

    AdaptiveResponseSettings malformed;
    malformed.onsetAssist = std::numeric_limits<float>::infinity();
    malformed.onsetCap = std::numeric_limits<float>::quiet_NaN();
    malformed.sustainedAssist = std::numeric_limits<float>::infinity();
    malformed.sustainedCap = std::numeric_limits<float>::quiet_NaN();
    malformed.horizonExtension = std::numeric_limits<float>::infinity();
    malformed.horizonExtensionCapMs = std::numeric_limits<float>::quiet_NaN();
    malformed.turningPointProtection = std::numeric_limits<float>::infinity();
    malformed.turningPointMargin = std::numeric_limits<float>::quiet_NaN();
    const AdaptiveResponseSettings safeFallback = sanitizedAdaptiveResponseSettings(malformed);
    QCOMPARE(safeFallback.onsetAssist, 0.0F);
    QCOMPARE(safeFallback.onsetCap, 0.0F);
    QCOMPARE(safeFallback.sustainedAssist, 0.0F);
    QCOMPARE(safeFallback.sustainedCap, 0.0F);
    QCOMPARE(safeFallback.horizonExtension, 0.0F);
    QCOMPARE(safeFallback.horizonExtensionCapMs, 0.0F);
    QCOMPARE(safeFallback.turningPointProtection, 0.0F);
    QCOMPARE(safeFallback.turningPointMargin, 0.0F);
    malformed.onsetAssist = 2.0F;
    malformed.onsetCap = 2.0F;
    malformed.sustainedAssist = 2.0F;
    malformed.sustainedCap = 2.0F;
    malformed.horizonExtension = 2.0F;
    malformed.horizonExtensionCapMs = 40.0F;
    malformed.turningPointProtection = 2.0F;
    malformed.turningPointMargin = 2.0F;
    const AdaptiveResponseSettings clamped = sanitizedAdaptiveResponseSettings(malformed);
    QCOMPARE(clamped.onsetAssist, 1.0F);
    QCOMPARE(clamped.onsetCap, 0.40F);
    QCOMPARE(clamped.sustainedAssist, 1.0F);
    QCOMPARE(clamped.sustainedCap, 0.35F);
    QCOMPARE(clamped.horizonExtension, 1.0F);
    QCOMPARE(clamped.horizonExtensionCapMs, 30.0F);
    QCOMPARE(clamped.turningPointProtection, 1.0F);
    QCOMPARE(clamped.turningPointMargin, 0.30F);

    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &profile = activeProfile(configuration);
    configuration.adaptiveResponseGlobal.axes[0].presetId = QStringLiteral("light");
    profile.adaptiveResponse.axes[0].properties = AdaptiveResponseMaximumHorizon
        | AdaptiveResponseOnsetAssist | AdaptiveResponseOnsetCap
        | AdaptiveResponseSustainedAssist | AdaptiveResponseSustainedCap
        | AdaptiveResponseHorizonExtension | AdaptiveResponseHorizonExtensionCap
        | AdaptiveResponseTurningPointProtection | AdaptiveResponseTurningPointMargin;
    profile.adaptiveResponse.axes[0].settings.maximumHorizonMs = 16.0F;
    profile.adaptiveResponse.axes[0].settings.onsetAssist = 0.72F;
    profile.adaptiveResponse.axes[0].settings.onsetCap = 0.31F;
    profile.adaptiveResponse.axes[0].settings.sustainedAssist = 0.62F;
    profile.adaptiveResponse.axes[0].settings.sustainedCap = 0.27F;
    profile.adaptiveResponse.axes[0].settings.horizonExtension = 0.71F;
    profile.adaptiveResponse.axes[0].settings.horizonExtensionCapMs = 21.0F;
    profile.adaptiveResponse.axes[0].settings.turningPointProtection = 0.87F;
    profile.adaptiveResponse.axes[0].settings.turningPointMargin = 0.19F;

    AdaptiveResponsePreset custom;
    custom.id = QStringLiteral("test-response");
    custom.name = QStringLiteral("Test Response");
    custom.description = QStringLiteral("Core persistence fixture");
    custom.axes = configuration.adaptiveResponseGlobal.axes;
    configuration.adaptiveResponsePresets.push_back(custom);

    const RuntimeAdaptiveResponseConfig effective =
        resolveAdaptiveResponseConfiguration(configuration, profile, 0);
    QVERIFY(effective.enabled);
    QCOMPARE(effective.maximumHorizonSeconds, 0.016F);
    QCOMPARE(effective.onsetAssist, 0.72F);
    QCOMPARE(effective.onsetCap, 0.31F);
    QCOMPARE(effective.sustainedAssist, 0.62F);
    QCOMPARE(effective.sustainedCap, 0.27F);
    QCOMPARE(effective.horizonExtension, 0.71F);
    QCOMPARE(effective.horizonExtensionCapSeconds, 0.021F);
    QCOMPARE(effective.turningPointProtection, 0.87F);
    QCOMPARE(effective.turningPointMargin, 0.19F);

    bool valid = false;
    const QJsonObject json = ConfigStore::toJson(configuration);
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 21);
    QCOMPARE(json.value(QStringLiteral("adaptiveResponseSchemaVersion")).toInt(), 1);
    const MapperConfiguration restored = ConfigStore::fromJson(json, &valid);
    QVERIFY(valid);
    QCOMPARE(restored.adaptiveResponsePresets.size(), size_t{1});
    QCOMPARE(restored.adaptiveResponseGlobal.axes[0].presetId, QStringLiteral("light"));
    const RuntimeAdaptiveResponseConfig restoredEffective =
        resolveAdaptiveResponseConfiguration(restored, activeProfile(restored), 0);
    QCOMPARE(restoredEffective.maximumHorizonSeconds, 0.016F);
    QCOMPARE(restoredEffective.onsetAssist, 0.72F);
    QCOMPARE(restoredEffective.onsetCap, 0.31F);
    QCOMPARE(restoredEffective.sustainedAssist, 0.62F);
    QCOMPARE(restoredEffective.sustainedCap, 0.27F);
    QCOMPARE(restoredEffective.horizonExtension, 0.71F);
    QCOMPARE(restoredEffective.horizonExtensionCapSeconds, 0.021F);
    QCOMPARE(restoredEffective.turningPointProtection, 0.87F);
    QCOMPARE(restoredEffective.turningPointMargin, 0.19F);

    QJsonObject preOnset = json;
    QJsonObject adaptiveGlobal = preOnset.value(QStringLiteral("adaptiveResponseGlobal")).toObject();
    QJsonArray globalAxes = adaptiveGlobal.value(QStringLiteral("axes")).toArray();
    QJsonObject globalAxis = globalAxes.at(0).toObject();
    QJsonObject globalSettings = globalAxis.value(QStringLiteral("settings")).toObject();
    globalSettings.remove(QStringLiteral("onsetAssist"));
    globalSettings.remove(QStringLiteral("onsetCap"));
    globalSettings.remove(QStringLiteral("sustainedAssist"));
    globalSettings.remove(QStringLiteral("sustainedCap"));
    globalSettings.remove(QStringLiteral("horizonExtension"));
    globalSettings.remove(QStringLiteral("horizonExtensionCapMs"));
    globalSettings.remove(QStringLiteral("turningPointProtection"));
    globalSettings.remove(QStringLiteral("turningPointMargin"));
    globalAxis.insert(QStringLiteral("settings"), globalSettings);
    globalAxes.replace(0, globalAxis);
    adaptiveGlobal.insert(QStringLiteral("axes"), globalAxes);
    preOnset.insert(QStringLiteral("adaptiveResponseGlobal"), adaptiveGlobal);
    QJsonArray preOnsetProfiles = preOnset.value(QStringLiteral("profiles")).toArray();
    QJsonObject firstProfile = preOnsetProfiles.at(0).toObject();
    QJsonObject profileAdaptive = firstProfile.value(QStringLiteral("adaptiveResponse")).toObject();
    QJsonArray profileAxes = profileAdaptive.value(QStringLiteral("axes")).toArray();
    QJsonObject profileAxis = profileAxes.at(0).toObject();
    QJsonObject profileSettings = profileAxis.value(QStringLiteral("settings")).toObject();
    profileSettings.remove(QStringLiteral("onsetAssist"));
    profileSettings.remove(QStringLiteral("onsetCap"));
    profileSettings.remove(QStringLiteral("sustainedAssist"));
    profileSettings.remove(QStringLiteral("sustainedCap"));
    profileSettings.remove(QStringLiteral("horizonExtension"));
    profileSettings.remove(QStringLiteral("horizonExtensionCapMs"));
    profileSettings.remove(QStringLiteral("turningPointProtection"));
    profileSettings.remove(QStringLiteral("turningPointMargin"));
    profileAxis.insert(QStringLiteral("settings"), profileSettings);
    profileAxes.replace(0, profileAxis);
    profileAdaptive.insert(QStringLiteral("axes"), profileAxes);
    firstProfile.insert(QStringLiteral("adaptiveResponse"), profileAdaptive);
    preOnsetProfiles.replace(0, firstProfile);
    preOnset.insert(QStringLiteral("profiles"), preOnsetProfiles);
    const MapperConfiguration restoredPreOnset = ConfigStore::fromJson(preOnset, &valid);
    QVERIFY(valid);
    const RuntimeAdaptiveResponseConfig preOnsetEffective =
        resolveAdaptiveResponseConfiguration(restoredPreOnset, activeProfile(restoredPreOnset), 0);
    QCOMPARE(preOnsetEffective.onsetAssist, 0.0F);
    QCOMPARE(preOnsetEffective.onsetCap, 0.0F);
    QCOMPARE(preOnsetEffective.sustainedAssist, 0.0F);
    QCOMPARE(preOnsetEffective.sustainedCap, 0.0F);
    QCOMPARE(preOnsetEffective.horizonExtension, 0.0F);
    QCOMPARE(preOnsetEffective.horizonExtensionCapSeconds, 0.0F);
    QCOMPARE(preOnsetEffective.turningPointProtection, 0.0F);
    QCOMPARE(preOnsetEffective.turningPointMargin, 0.0F);

    QJsonObject legacy = json;
    legacy.insert(QStringLiteral("version"), 20);
    legacy.remove(QStringLiteral("adaptiveResponseSchemaVersion"));
    legacy.remove(QStringLiteral("adaptiveResponseGlobal"));
    legacy.remove(QStringLiteral("adaptiveResponsePresets"));
    const MapperConfiguration migrated = ConfigStore::fromJson(legacy, &valid);
    QVERIFY(valid);
    QVERIFY(!resolveAdaptiveResponseConfiguration(migrated, activeProfile(migrated), 0).enabled);
}

void MappingCoreTests::adaptiveResponseRecognizesSlowMotionAcrossSampleRates()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    const auto origin = std::chrono::steady_clock::time_point{};
    const std::array<int, 6> rates{50, 100, 200, 250, 500, 1000};
    float lowestPeakLead = std::numeric_limits<float>::max();
    float highestPeakLead = 0.0F;

    for (const int rate : rates) {
        AdaptiveResponseProcessor processor;
        processor.process(0.75F, configuration, origin);
        float peakLead = 0.0F;
        for (int sample = 1; sample <= rate * 3; ++sample) {
            const float seconds = static_cast<float>(sample) / static_cast<float>(rate);
            const float physical = 0.75F - 0.50F * seconds / 3.0F;
            const AdaptiveResponseTelemetry telemetry = processor.process(physical, configuration,
                origin + std::chrono::microseconds(static_cast<long long>(seconds * 1000000.0F)));
            peakLead = std::max(peakLead, std::abs(telemetry.lead));
            QVERIFY(!telemetry.reversal);
        }
        QVERIFY2(peakLead > 0.0002F, "A three-second slow sweep was classified as stationary.");
        qInfo().nospace() << "Slow sweep rate=" << rate << "Hz peak=" << peakLead;
        lowestPeakLead = std::min(lowestPeakLead, peakLead);
        highestPeakLead = std::max(highestPeakLead, peakLead);
    }
    qInfo().nospace() << "Slow sweep peak lead range=" << lowestPeakLead << ".." << highestPeakLead;
    QVERIFY2(highestPeakLead < lowestPeakLead * 2.50F + 0.0005F,
             "Slow-sweep lead is not sample-rate equivalent.");
}

void MappingCoreTests::adaptiveResponsePreservesSlowWobbleAcrossSampleRates()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    const auto origin = std::chrono::steady_clock::time_point{};
    const std::array<int, 2> rates{100, 1000};

    for (const int rate : rates) {
        AdaptiveResponseProcessor processor;
        processor.process(0.80F, configuration, origin);
        float peakLead = 0.0F;
        for (int sample = 1; sample <= rate * 3; ++sample) {
            const float seconds = static_cast<float>(sample) / static_cast<float>(rate);
            const float physical = 0.80F - 0.45F * seconds / 3.0F
                + 0.0015F * std::sin(seconds * 6.0F * 3.14159265F);
            const AdaptiveResponseTelemetry telemetry = processor.process(physical, configuration,
                origin + std::chrono::microseconds(static_cast<long long>(seconds * 1000000.0F)));
            peakLead = std::max(peakLead, std::abs(telemetry.lead));
            QVERIFY(!telemetry.reversal);
        }
        QVERIFY2(peakLead > 0.0002F, "Slow motion with human-sized wobble lost its lead.");
    }
}

void MappingCoreTests::adaptiveResponseHandlesSampleAndHoldSourcesThenSettles()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    const auto origin = std::chrono::steady_clock::time_point{};
    const std::array<int, 4> sourceRates{250, 125, 60, 30};

    for (const int sourceRate : sourceRates) {
        AdaptiveResponseProcessor processor;
        constexpr int mapperRate = 250;
        const int sourceDivider = mapperRate / sourceRate;
        float heldPhysical = 0.65F;
        processor.process(heldPhysical, configuration, origin);
        float peakLead = 0.0F;
        int heldReportsWithLead = 0;
        AdaptiveResponseTelemetry telemetry;
        for (int sample = 1; sample <= mapperRate; ++sample) {
            const float seconds = static_cast<float>(sample) / mapperRate;
            if (sample % sourceDivider == 0) heldPhysical = 0.65F - 0.35F * seconds;
            telemetry = processor.process(heldPhysical, configuration,
                origin + std::chrono::microseconds(static_cast<long long>(seconds * 1000000.0F)));
            peakLead = std::max(peakLead, std::abs(telemetry.lead));
            if (sample % sourceDivider != 0 && seconds > 0.12F && std::abs(telemetry.lead) > 0.0001F) {
                ++heldReportsWithLead;
            }
            QVERIFY(!telemetry.reversal);
        }
        qInfo().nospace() << "Hold source=" << sourceRate << "Hz peak=" << peakLead
                          << " heldReports=" << heldReportsWithLead;
        QVERIFY2(peakLead > 0.0002F && (sourceDivider == 1 || heldReportsWithLead > 0),
                 "Expected source holds were treated as a stop.");
        for (int sample = mapperRate + 1; sample <= mapperRate + 125; ++sample) {
            const float seconds = static_cast<float>(sample) / mapperRate;
            telemetry = processor.process(heldPhysical, configuration,
                origin + std::chrono::microseconds(static_cast<long long>(seconds * 1000000.0F)));
        }
        QVERIFY(std::abs(telemetry.lead) < 0.0001F);
        QVERIFY(std::abs(telemetry.velocity) < 0.0001F);
    }
}

void MappingCoreTests::adaptiveResponseRejectsStationaryJitterAndSingleOppositeSpike()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    const auto origin = std::chrono::steady_clock::time_point{};

    AdaptiveResponseProcessor jitter;
    jitter.process(0.30F, configuration, origin);
    float jitterPeakLead = 0.0F;
    for (int sample = 1; sample <= 500; ++sample) {
        const float physical = 0.30F + (sample % 2 == 0 ? 0.005F : -0.005F);
        const AdaptiveResponseTelemetry telemetry = jitter.process(physical, configuration,
            origin + std::chrono::milliseconds(sample * 4));
        jitterPeakLead = std::max(jitterPeakLead, std::abs(telemetry.lead));
        QVERIFY(!telemetry.reversal);
    }
    QVERIFY2(jitterPeakLead < 0.001F, "Stationary jitter amplified into meaningful lead.");

    AdaptiveResponseProcessor spike;
    spike.process(0.0F, configuration, origin);
    spike.process(0.08F, configuration, origin + std::chrono::milliseconds(4));
    spike.process(0.16F, configuration, origin + std::chrono::milliseconds(8));
    const AdaptiveResponseTelemetry isolatedOpposite = spike.process(
        0.11F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(!isolatedOpposite.reversal);
    QVERIFY(isolatedOpposite.safetyLimited);
    QVERIFY(std::abs(isolatedOpposite.lead) < 0.0001F);
    const AdaptiveResponseTelemetry resumed = spike.process(
        0.24F, configuration, origin + std::chrono::milliseconds(16));
    QVERIFY(!resumed.reversal);
    QVERIFY(resumed.predicted > resumed.physical);
    QCOMPARE(spike.reversalCount(), std::uint64_t{0});
}

void MappingCoreTests::adaptiveResponseHandlesHardGentleAndRepeatedReversals()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    configuration.reversalResponse = 0.0F;
    const auto origin = std::chrono::steady_clock::time_point{};

    AdaptiveResponseProcessor hard;
    hard.process(0.0F, configuration, origin);
    hard.process(0.20F, configuration, origin + std::chrono::milliseconds(4));
    hard.process(0.40F, configuration, origin + std::chrono::milliseconds(8));
    const AdaptiveResponseTelemetry hardCandidate = hard.process(
        0.20F, configuration, origin + std::chrono::milliseconds(12));
    QVERIFY(!hardCandidate.reversal);
    QVERIFY(hardCandidate.safetyLimited);
    const AdaptiveResponseTelemetry hardReversal = hard.process(
        0.0F, configuration, origin + std::chrono::milliseconds(16));
    QVERIFY(hardReversal.reversal);
    QVERIFY(hardReversal.predicted < hardReversal.physical);
    QCOMPARE(hard.reversalCount(), std::uint64_t{1});

    AdaptiveResponseProcessor gentle;
    gentle.process(0.0F, configuration, origin);
    gentle.process(0.03F, configuration, origin + std::chrono::milliseconds(4));
    gentle.process(0.06F, configuration, origin + std::chrono::milliseconds(8));
    bool gentleReversal = false;
    for (int sample = 1; sample <= 16; ++sample) {
        const AdaptiveResponseTelemetry telemetry = gentle.process(0.06F - sample * 0.005F, configuration,
            origin + std::chrono::milliseconds(8 + sample * 4));
        gentleReversal = gentleReversal || telemetry.reversal;
    }
    QVERIFY2(gentleReversal, "Coherent gentle reversal never reacquired its new direction.");

    AdaptiveResponseProcessor repeated;
    const std::array<float, 7> positions{0.0F, 0.25F, 0.50F, 0.25F, 0.0F, 0.25F, 0.50F};
    AdaptiveResponseTelemetry finalTelemetry;
    for (size_t index = 0; index < positions.size(); ++index) {
        finalTelemetry = repeated.process(positions[index], configuration,
            origin + std::chrono::milliseconds(static_cast<int>(index) * 4));
    }
    QVERIFY(repeated.reversalCount() >= std::uint64_t{2});
    QVERIFY(finalTelemetry.predicted > finalTelemetry.physical);
}

void MappingCoreTests::adaptiveResponseConvergesEstimatorsAfterStopAndResume()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    const auto origin = std::chrono::steady_clock::time_point{};
    const std::array<AdaptiveResponseModel, 2> models{
        AdaptiveResponseModel::AlphaBeta, AdaptiveResponseModel::AlphaBetaGamma};

    for (const AdaptiveResponseModel model : models) {
        configuration.model = model;
        AdaptiveResponseProcessor processor;
        processor.process(0.0F, configuration, origin);
        processor.process(0.15F, configuration, origin + std::chrono::milliseconds(4));
        processor.process(0.30F, configuration, origin + std::chrono::milliseconds(8));
        AdaptiveResponseTelemetry settled;
        for (int sample = 3; sample <= 100; ++sample) {
            settled = processor.process(0.30F, configuration,
                origin + std::chrono::milliseconds(sample * 4));
        }
        QVERIFY(std::abs(settled.lead) < 0.0001F);
        QVERIFY(std::abs(settled.velocity) < 0.0001F);
        QVERIFY(std::abs(settled.estimated - 0.30F) < 0.0001F);
        const AdaptiveResponseTelemetry resumed = processor.process(
            0.34F, configuration, origin + std::chrono::milliseconds(404));
        QVERIFY(resumed.predicted >= resumed.physical - 0.0001F);
        QVERIFY(std::abs(resumed.estimated - resumed.physical) < 0.20F);
    }
}

void MappingCoreTests::adaptiveResponseHumanLikeTurningPointShape()
{
    const std::vector<float> physical = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Human-Like Rapid Reversal"), -1.0F, 1.0F);
    const auto origin = std::chrono::steady_clock::time_point{};
    Q_UNUSED(origin);
    for (const auto &[name, horizon, maximumLead, velocity, acceleration, onsetAssist, onsetCap] : {
             std::tuple<const char *, float, float, float, float, float, float>{"Fast", 0.012F, 0.18F, 0.80F, 0.68F, 0.28F, 0.16F},
             std::tuple<const char *, float, float, float, float, float, float>{"Aggressive", 0.018F, 0.27F, 0.91F, 0.82F, 0.38F, 0.22F},
             std::tuple<const char *, float, float, float, float, float, float>{"Extreme", 0.030F, 0.40F, 1.00F, 0.95F, 0.50F, 0.30F}}) {
        RuntimeAdaptiveResponseConfig configuration;
        configuration.enabled = true;
        configuration.model = AdaptiveResponseModel::Auto;
        configuration.maximumHorizonSeconds = horizon;
        configuration.maximumLead = maximumLead;
        configuration.velocityResponse = velocity;
        configuration.accelerationResponse = acceleration;
        configuration.motionSensitivity = 0.035F;
        configuration.noiseRejection = 0.012F;
        configuration.reversalDetection = 0.075F;
        configuration.reversalResponse = 1.0F;
        configuration.decelerationResponse = 0.85F;
        configuration.settlingResponse = 0.92F;
        configuration.endpointTaper = 0.16F;
        configuration.onsetAssist = onsetAssist;
        configuration.onsetCap = onsetCap;
        const AdaptiveResponseSimulation simulated = simulateAdaptiveResponse(configuration, physical, 0.004F);
        constexpr size_t turn = 85; // 340 ms, authored zero-velocity apex.
        constexpr size_t finalStop = 150; // 600 ms, authored stationary target.
        float peakBeforeTurn = 0.0F;
        float halfSpeedLead = 0.0F;
        float turningLead = 0.0F;
        float finalStopLead = 0.0F;
        float stationaryLead = 0.0F;
        float maximumStep = 0.0F;
        size_t maximumStepIndex = 0;
        float maximumPhysicalStep = 0.0F;
        float maximumPredictedStep = 0.0F;
        size_t maximumPredictedStepIndex = 0;
        size_t reversalSample = simulated.size();
        for (size_t index = 1; index < simulated.size(); ++index) {
            const float lead = simulated[index].telemetry.lead;
            if (index < turn) peakBeforeTurn = std::max(peakBeforeTurn, lead);
            if (index >= 70 && index < turn && std::abs(physical[index] - physical[index - 1])
                <= 0.5F * std::abs(physical[55] - physical[54])) {
                halfSpeedLead = std::max(halfSpeedLead, lead);
            }
            if (index >= turn - 3 && index <= turn) turningLead = std::max(turningLead, lead);
            if (index >= finalStop && index < finalStop + 20) finalStopLead = std::min(finalStopLead, lead);
            if (index >= finalStop + 20) stationaryLead = std::max(stationaryLead, std::abs(lead));
            const float leadStep = std::abs(lead - simulated[index - 1].telemetry.lead);
            if (leadStep > maximumStep) {
                maximumStep = leadStep;
                maximumStepIndex = index;
            }
            maximumPhysicalStep = std::max(maximumPhysicalStep,
                std::abs(simulated[index].telemetry.physical - simulated[index - 1].telemetry.physical));
            const float predictedStep = std::abs(simulated[index].telemetry.predicted
                - simulated[index - 1].telemetry.predicted);
            if (predictedStep > maximumPredictedStep) {
                maximumPredictedStep = predictedStep;
                maximumPredictedStepIndex = index;
            }
            if (reversalSample == simulated.size() && simulated[index].telemetry.reversal) reversalSample = index;
        }
        qInfo().nospace() << "HumanLike " << name << " peak=" << peakBeforeTurn
                          << " half_speed=" << halfSpeedLead << " turning_lead=" << turningLead
                          << " final_stop_lead=" << finalStopLead << " stationary=" << stationaryLead
                          << " lead_step=" << maximumStep << " physical_step=" << maximumPhysicalStep
                          << " predicted_step=" << maximumPredictedStep
                          << " artificial_step=" << std::max(0.0F, maximumPredictedStep - maximumPhysicalStep);
        qInfo().nospace() << "HumanLike " << name << " predicted_step_index=" << maximumPredictedStepIndex
                          << " lead_before=" << simulated[maximumPredictedStepIndex - 1].telemetry.lead
                          << " lead_after=" << simulated[maximumPredictedStepIndex].telemetry.lead
                          << " reacquire_before=" << simulated[maximumPredictedStepIndex - 1].telemetry.reacquisitionAuthority
                          << " reacquire_after=" << simulated[maximumPredictedStepIndex].telemetry.reacquisitionAuthority
                          << " onset_before=" << simulated[maximumPredictedStepIndex - 1].telemetry.onsetAuthority
                          << " onset_after=" << simulated[maximumPredictedStepIndex].telemetry.onsetAuthority;
        qInfo().nospace() << "HumanLike " << name << " lead_step_index=" << maximumStepIndex
                          << " lead_before=" << simulated[maximumStepIndex - 1].telemetry.lead
                          << " lead_after=" << simulated[maximumStepIndex].telemetry.lead
                          << " state_before=" << adaptiveMotionStateLabel(simulated[maximumStepIndex - 1].telemetry.state)
                          << " state_after=" << adaptiveMotionStateLabel(simulated[maximumStepIndex].telemetry.state)
                          << " reacquire_before=" << simulated[maximumStepIndex - 1].telemetry.reacquisitionAuthority
                          << " reacquire_after=" << simulated[maximumStepIndex].telemetry.reacquisitionAuthority;
        QVERIFY2(halfSpeedLead >= peakBeforeTurn * 0.45F,
                 "Predictive lead collapsed before most of the physical brake was complete.");
        for (size_t index = 75; index <= turn; ++index) {
            QVERIFY2(simulated[index].telemetry.lead <= simulated[index - 1].telemetry.lead + 0.0005F,
                     "Positive turning-point lead developed an artificial secondary pulse.");
        }
        for (size_t index = turn + 1; index < reversalSample; ++index) {
            QVERIFY2(simulated[index].telemetry.lead <= 0.0001F,
                     "Stale positive lead reappeared after credible opposite motion.");
        }
        QVERIFY2(reversalSample <= turn + 15 && simulated[reversalSample].telemetry.lead < -0.0001F,
                 "Confirmed reversal did not reacquire a clean opposite-direction lead promptly.");
        for (size_t index = finalStop + 1; index < finalStop + 20; ++index) {
            QVERIFY2(std::abs(simulated[index].telemetry.lead)
                         <= std::abs(simulated[index - 1].telemetry.lead) + 0.0005F,
                     "Final-stop lead rebounded into an isolated pulse.");
        }
        QVERIFY(std::abs(finalStopLead) < peakBeforeTurn * 0.30F);
        QVERIFY(stationaryLead < 0.0001F);
        QVERIFY(maximumStep < maximumLead * 0.16F);
        if (QString::fromLatin1(name) == QStringLiteral("Aggressive")) {
            // The remaining terminal safety cancellation is intentionally
            // immediate; this bounds it below 2.3% while preserving that
            // stale-direction safety invariant.
            QVERIFY2(maximumStep < 0.023F,
                     "Aggressive Human-Like predictor-only step regressed.");
        }
        if (QString::fromLatin1(name) == QStringLiteral("Extreme")) {
            QVERIFY2(maximumStep < 0.045F,
                     "Extreme Human-Like predictive reacquisition step regressed.");
        }
    }
}

void MappingCoreTests::adaptiveResponseOnsetAssistIsBoundedSymmetricAndQuiet()
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.018F;
    configuration.maximumLead = 0.27F;
    configuration.velocityResponse = 0.91F;
    configuration.accelerationResponse = 0.82F;
    configuration.motionSensitivity = 0.035F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    configuration.reversalResponse = 1.0F;
    configuration.decelerationResponse = 0.85F;
    configuration.settlingResponse = 0.92F;
    configuration.endpointTaper = 0.16F;
    std::vector<float> accelerating(96, -0.55F);
    for (size_t index = 12; index < 56; ++index) {
        const float progress = static_cast<float>(index - 12) / 43.0F;
        // Small, fast correction: strong coherent acceleration but a modest
        // eventual speed, precisely where unused velocity authority exists.
        accelerating[index] = -0.55F + 0.28F * progress * progress;
    }
    for (size_t index = 56; index < accelerating.size(); ++index) accelerating[index] = -0.27F;
    const AdaptiveResponseSimulation withoutOnset = simulateAdaptiveResponse(configuration, accelerating, 0.004F);
    configuration.onsetAssist = 1.0F;
    configuration.onsetCap = 0.40F;
    const AdaptiveResponseSimulation withOnset = simulateAdaptiveResponse(configuration, accelerating, 0.004F);
    float earlyWithout = 0.0F;
    float earlyWith = 0.0F;
    float maximumOnset = 0.0F;
    float maximumGain = 0.0F;
    float maximumEarlyGain = 0.0F;
    size_t maximumGainIndex = 0;
    size_t firstOnsetIndex = withOnset.size();
    for (size_t index = 12; index < withOnset.size(); ++index) {
        earlyWithout = index < 36 ? std::max(earlyWithout, std::abs(withoutOnset[index].telemetry.lead)) : earlyWithout;
        earlyWith = index < 36 ? std::max(earlyWith, std::abs(withOnset[index].telemetry.lead)) : earlyWith;
        maximumOnset = std::max(maximumOnset, withOnset[index].telemetry.onsetAuthority);
        if (withOnset[index].telemetry.onsetAuthority > 0.00001F && firstOnsetIndex == withOnset.size()) {
            firstOnsetIndex = index;
        }
        const float gain = std::abs(withOnset[index].telemetry.lead)
            - std::abs(withoutOnset[index].telemetry.lead);
        if (gain > maximumGain) { maximumGain = gain; maximumGainIndex = index; }
        if (index < 36) maximumEarlyGain = std::max(maximumEarlyGain, gain);
        QVERIFY(withOnset[index].telemetry.motionUrgency >= -0.00001F);
        QVERIFY(withOnset[index].telemetry.motionUrgency <= 1.00001F);
        QVERIFY(withOnset[index].telemetry.activeHorizonSeconds
                 <= configuration.maximumHorizonSeconds + 0.00001F);
        QVERIFY(std::abs(withOnset[index].telemetry.lead)
                 <= configuration.maximumLead + 0.00001F);
    }
    qInfo().nospace() << "Onset accelerating early_off=" << earlyWithout
                      << " early_on=" << earlyWith << " maximum_authority=" << maximumOnset
                      << " first_onset=" << firstOnsetIndex << " max_gain=" << maximumGain
                      << " max_gain_index=" << maximumGainIndex
                      << " early_gain=" << maximumEarlyGain;
    QVERIFY(maximumOnset > 0.0005F);
    QVERIFY(maximumEarlyGain > 0.0002F);

    std::vector<float> mirrored;
    mirrored.reserve(accelerating.size());
    for (const float value : accelerating) mirrored.push_back(-value);
    const AdaptiveResponseSimulation negative = simulateAdaptiveResponse(configuration, mirrored, 0.004F);
    QVERIFY(negative.size() == withOnset.size());
    for (size_t index = 0; index < withOnset.size(); ++index) {
        QVERIFY(std::abs(withOnset[index].telemetry.lead + negative[index].telemetry.lead) < 0.0002F);
        QVERIFY(std::abs(withOnset[index].telemetry.motionUrgency
                         - negative[index].telemetry.motionUrgency) < 0.0002F);
        QVERIFY(std::abs(withOnset[index].telemetry.onsetAuthority
                         - negative[index].telemetry.onsetAuthority) < 0.0002F);
    }

    std::vector<float> jitter(120, 0.0F);
    for (size_t index = 1; index < jitter.size(); ++index) {
        jitter[index] = index % 2 == 0 ? 0.0015F : -0.0015F;
    }
    const AdaptiveResponseSimulation noisy = simulateAdaptiveResponse(configuration, jitter, 0.004F);
    for (const AdaptiveResponseSimulationSample &sample : noisy) {
        QVERIFY(sample.telemetry.onsetAuthority < 0.00001F);
        QVERIFY(std::abs(sample.telemetry.lead) < 0.0001F);
    }

    const std::vector<float> human = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Human-Like Rapid Reversal"), -1.0F, 1.0F);
    const AdaptiveResponseSimulation braking = simulateAdaptiveResponse(configuration, human, 0.004F);
    for (size_t index = 70; index <= 85; ++index) {
        QVERIFY2(braking[index].telemetry.onsetAuthority < 0.00001F,
                 "Onset authority must be absent while the source is braking toward the turn.");
    }
    for (size_t index = 170; index < braking.size(); ++index) {
        QVERIFY(std::abs(braking[index].telemetry.lead) < 0.0001F);
        QVERIFY(braking[index].telemetry.onsetAuthority < 0.00001F);
    }
}

void MappingCoreTests::adaptiveResponseSustainedHorizonAndTurningProtection()
{
    const auto aggressiveConfiguration = [] {
        RuntimeAdaptiveResponseConfig configuration;
        configuration.enabled = true;
        configuration.model = AdaptiveResponseModel::Auto;
        configuration.maximumHorizonSeconds = 0.018F;
        configuration.maximumLead = 0.27F;
        configuration.velocityResponse = 0.91F;
        configuration.accelerationResponse = 0.82F;
        configuration.motionSensitivity = 0.035F;
        configuration.noiseRejection = 0.012F;
        configuration.reversalDetection = 0.075F;
        configuration.reversalResponse = 1.0F;
        configuration.decelerationResponse = 0.85F;
        configuration.settlingResponse = 0.92F;
        configuration.endpointTaper = 0.16F;
        configuration.onsetAssist = 0.38F;
        configuration.onsetCap = 0.22F;
        configuration.sustainedAssist = 0.42F;
        configuration.sustainedCap = 0.20F;
        configuration.horizonExtension = 0.48F;
        configuration.horizonExtensionCapSeconds = 0.018F;
        configuration.turningPointProtection = 0.94F;
        configuration.turningPointMargin = 0.10F;
        return configuration;
    };
    const auto maximum = [](const AdaptiveResponseSimulation &simulation, auto selector) {
        float result = 0.0F;
        for (const AdaptiveResponseSimulationSample &sample : simulation) result = std::max(result, selector(sample.telemetry));
        return result;
    };
    const std::vector<float> waggle = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Slow Coherent Waggle"), -1.0F, 1.0F);
    RuntimeAdaptiveResponseConfig disabledSustained = aggressiveConfiguration();
    disabledSustained.sustainedAssist = 0.0F;
    disabledSustained.sustainedCap = 0.0F;
    disabledSustained.horizonExtension = 0.0F;
    disabledSustained.horizonExtensionCapSeconds = 0.0F;
    const RuntimeAdaptiveResponseConfig sustained = aggressiveConfiguration();
    const AdaptiveResponseSimulation waggleOff = simulateAdaptiveResponse(disabledSustained, waggle, 0.004F);
    const AdaptiveResponseSimulation waggleOn = simulateAdaptiveResponse(sustained, waggle, 0.004F);
    const float maxSustainedEvidence = maximum(waggleOn, [](const auto &t) { return t.sustainedEvidence; });
    const float maxSustainedAuthority = maximum(waggleOn, [](const auto &t) { return t.sustainedAuthority; });
    const float maxExtensionEligibility = maximum(waggleOn, [](const auto &t) { return t.horizonExtensionEligibility; });
    const float maxAllowedHorizon = maximum(waggleOn, [](const auto &t) { return t.allowedMaximumHorizonSeconds; });
    const float maxActiveHorizonOff = maximum(waggleOff, [](const auto &t) { return t.activeHorizonSeconds; });
    const float maxActiveHorizonOn = maximum(waggleOn, [](const auto &t) { return t.activeHorizonSeconds; });
    qInfo().nospace() << "Sustained waggle evidence=" << maxSustainedEvidence
                      << " authority=" << maxSustainedAuthority << " extension_eligibility="
                      << maxExtensionEligibility << " normal_ms="
                      << sustained.maximumHorizonSeconds * 1000.0F << " allowed_ms="
                      << maxAllowedHorizon * 1000.0F << " active_off_ms="
                      << maxActiveHorizonOff * 1000.0F << " active_on_ms="
                      << maxActiveHorizonOn * 1000.0F;
    QVERIFY(maxSustainedEvidence > 0.20F);
    QVERIFY(maxSustainedAuthority > 0.002F);
    QVERIFY(maxExtensionEligibility > 0.001F);
    QVERIFY(maxAllowedHorizon > sustained.maximumHorizonSeconds + 0.00001F);
    QVERIFY(maxAllowedHorizon <= 0.06001F);
    QVERIFY(maxActiveHorizonOn >= maxActiveHorizonOff - 0.00001F);
    for (const AdaptiveResponseSimulationSample &sample : waggleOn) {
        QVERIFY(std::isfinite(sample.telemetry.activeHorizonSeconds));
        QVERIFY(sample.telemetry.activeHorizonSeconds <= sample.telemetry.allowedMaximumHorizonSeconds + 0.00001F);
        QVERIFY(sample.telemetry.allowedMaximumHorizonSeconds <= 0.06001F);
        QVERIFY(sample.telemetry.motionUrgency >= -0.00001F && sample.telemetry.motionUrgency <= 1.00001F);
    }

    std::vector<float> mirroredWaggle;
    mirroredWaggle.reserve(waggle.size());
    for (const float value : waggle) mirroredWaggle.push_back(-value);
    const AdaptiveResponseSimulation mirrored = simulateAdaptiveResponse(sustained, mirroredWaggle, 0.004F);
    QVERIFY(mirrored.size() == waggleOn.size());
    for (size_t index = 0; index < waggleOn.size(); ++index) {
        QVERIFY(std::abs(waggleOn[index].telemetry.lead + mirrored[index].telemetry.lead) < 0.00025F);
        QVERIFY(std::abs(waggleOn[index].telemetry.sustainedEvidence
                         - mirrored[index].telemetry.sustainedEvidence) < 0.00025F);
        QVERIFY(std::abs(waggleOn[index].telemetry.horizonExtensionEligibility
                         - mirrored[index].telemetry.horizonExtensionEligibility) < 0.00025F);
    }

    const std::vector<float> slowSweep = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Slow One-Way Sweep"), -1.0F, 1.0F);
    const AdaptiveResponseSimulation slowSweepOff = simulateAdaptiveResponse(disabledSustained, slowSweep, 0.004F);
    const AdaptiveResponseSimulation slowSweepOn = simulateAdaptiveResponse(sustained, slowSweep, 0.004F);
    const float sweepAllowedHorizon = maximum(slowSweepOn, [](const auto &t) { return t.allowedMaximumHorizonSeconds; });
    const float sweepActiveOff = maximum(slowSweepOff, [](const auto &t) { return t.activeHorizonSeconds; });
    const float sweepActiveOn = maximum(slowSweepOn, [](const auto &t) { return t.activeHorizonSeconds; });
    float sweepMaximumLeadGain = 0.0F;
    for (size_t index = 0; index < slowSweepOn.size(); ++index) {
        sweepMaximumLeadGain = std::max(sweepMaximumLeadGain, std::abs(slowSweepOn[index].telemetry.lead)
            - std::abs(slowSweepOff[index].telemetry.lead));
    }
    qInfo().nospace() << "Sustained slow_sweep allowed_ms=" << sweepAllowedHorizon * 1000.0F
                      << " active_off_ms=" << sweepActiveOff * 1000.0F
                      << " active_on_ms=" << sweepActiveOn * 1000.0F
                      << " max_lead_gain=" << sweepMaximumLeadGain;
    QVERIFY(sweepAllowedHorizon > sustained.maximumHorizonSeconds + 0.00001F);
    QVERIFY(sweepActiveOn >= sweepActiveOff - 0.00001F);
    QVERIFY(sweepMaximumLeadGain > 0.00005F);

    const std::vector<float> fastSweep = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Fast Sweep"), -1.0F, 1.0F);
    const AdaptiveResponseSimulation fastOff = simulateAdaptiveResponse(disabledSustained, fastSweep, 0.004F);
    const AdaptiveResponseSimulation fastOn = simulateAdaptiveResponse(sustained, fastSweep, 0.004F);
    float fastMaximumDifference = 0.0F;
    for (size_t index = 0; index < fastOn.size(); ++index) {
        fastMaximumDifference = std::max(fastMaximumDifference, std::abs(fastOn[index].telemetry.lead
            - fastOff[index].telemetry.lead));
        QVERIFY(fastOn[index].telemetry.horizonExtensionEligibility < 0.0001F);
        QVERIFY(fastOn[index].telemetry.allowedMaximumHorizonSeconds
                 <= sustained.maximumHorizonSeconds + 0.00001F);
    }
    qInfo().nospace() << "Sustained fast_noninterference max_lead_difference=" << fastMaximumDifference;
    QVERIFY(fastMaximumDifference < 0.003F);

    const std::vector<float> correction = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Small Slow Correction"), -1.0F, 1.0F);
    const AdaptiveResponseSimulation precision = simulateAdaptiveResponse(sustained, correction, 0.004F);
    QVERIFY(maximum(precision, [](const auto &t) { return std::abs(t.lead); }) < 0.020F);
    std::vector<float> jitter(220, 0.0F);
    for (size_t index = 1; index < jitter.size(); ++index) jitter[index] = index % 2 == 0 ? 0.0015F : -0.0015F;
    const AdaptiveResponseSimulation noise = simulateAdaptiveResponse(sustained, jitter, 0.004F);
    for (const AdaptiveResponseSimulationSample &sample : noise) {
        QVERIFY(sample.telemetry.sustainedEvidence < 0.0001F);
        QVERIFY(sample.telemetry.horizonExtensionEligibility < 0.0001F);
    }

    // Hold deterministic 250 Hz processing samples at each simulated source
    // rate. This proves neither sustained evidence nor extension comes from
    // repeated values alone, while preserving the learned cadence path.
    for (const int sourceHz : {30, 60, 125, 250}) {
        std::vector<float> held;
        held.reserve(waggle.size());
        const int sourceStride = std::max(1, static_cast<int>(std::lround(250.0 / sourceHz)));
        for (size_t index = 0; index < waggle.size(); ++index) held.push_back(
            waggle[(index / static_cast<size_t>(sourceStride)) * static_cast<size_t>(sourceStride)]);
        const AdaptiveResponseSimulation rateSimulation = simulateAdaptiveResponse(sustained, held, 0.004F);
        for (const AdaptiveResponseSimulationSample &sample : rateSimulation) {
            QVERIFY(std::isfinite(sample.telemetry.lead));
            QVERIFY(sample.telemetry.allowedMaximumHorizonSeconds <= 0.06001F);
        }
    }

    RuntimeAdaptiveResponseConfig extreme = aggressiveConfiguration();
    extreme.maximumHorizonSeconds = 0.030F;
    extreme.maximumLead = 0.40F;
    extreme.velocityResponse = 1.0F;
    extreme.accelerationResponse = 0.95F;
    extreme.onsetAssist = 0.50F;
    extreme.onsetCap = 0.30F;
    extreme.sustainedAssist = 0.55F;
    extreme.sustainedCap = 0.28F;
    extreme.horizonExtension = 0.65F;
    extreme.horizonExtensionCapSeconds = 0.024F;
    extreme.turningPointProtection = 1.0F;
    extreme.turningPointMargin = 0.08F;
    const std::vector<float> torture = adaptiveResponseScenarioPhysicalSamples(
        QStringLiteral("Extreme Turning-Point Torture"), -1.0F, 1.0F);
    const AdaptiveResponseSimulation protectedTorture = simulateAdaptiveResponse(extreme, torture, 0.004F);
    float physicalApex = -1.0F;
    float predictedApex = -1.0F;
    float maximumTurnConfidence = 0.0F;
    float maximumTurnLeadLimit = 0.0F;
    AdaptiveResponseTelemetry predictedApexTelemetry;
    float predictedApexPhysical = 0.0F;
    for (size_t index = 1; index < protectedTorture.size(); ++index) {
        if (index < 101) { // authored 400 ms apex
            physicalApex = std::max(physicalApex, protectedTorture[index].telemetry.physical);
            if (protectedTorture[index].telemetry.predicted > predictedApex) {
                predictedApex = protectedTorture[index].telemetry.predicted;
                predictedApexTelemetry = protectedTorture[index].telemetry;
                predictedApexPhysical = protectedTorture[index].telemetry.physical;
            }
        }
        maximumTurnConfidence = std::max(maximumTurnConfidence,
            protectedTorture[index].telemetry.turningPointConfidence);
        maximumTurnLeadLimit = std::max(maximumTurnLeadLimit,
            protectedTorture[index].telemetry.turningPointLeadLimit);
    }
    const float turningOvershoot = std::max(0.0F, predictedApex - physicalApex);
    qInfo().nospace() << "Turning torture physical_apex=" << physicalApex
                      << " predicted_apex=" << predictedApex << " overshoot=" << turningOvershoot
                      << " apex_physical=" << predictedApexPhysical
                      << " turn_confidence=" << maximumTurnConfidence
                      << " turn_lead_limit=" << maximumTurnLeadLimit
                      << " apex_active_ms=" << predictedApexTelemetry.activeHorizonSeconds * 1000.0F
                      << " apex_turn_confidence=" << predictedApexTelemetry.turningPointConfidence
                      << " apex_turn_ms=" << predictedApexTelemetry.estimatedTimeToTurnSeconds * 1000.0F
                      << " apex_limit_ms=" << predictedApexTelemetry.turningPointHorizonLimitSeconds * 1000.0F
                      << " apex_lead_limit=" << predictedApexTelemetry.turningPointLeadLimit;
    QVERIFY(maximumTurnConfidence > 0.01F);
    QVERIFY(maximumTurnLeadLimit > 0.0F);
    QVERIFY(turningOvershoot <= 0.0201F);
}

void MappingCoreTests::responseCurveFamiliesAreBoundedMonotonicAndCompiled()
{
    const CurveDefinition linear = linearCurveDefinition();
    QVERIFY(nearlyEqual(evaluateCurveDefinition(-0.4F, linear, false), -0.4F));
    QVERIFY(nearlyEqual(evaluateCurveGain(0.2F, linear, false), 1.0F));

    const CurveDefinition jCurve = standardCurveDefinition(CurveFamily::JCurve, QStringLiteral("medium"));
    const CurveDefinition sCurve = standardCurveDefinition(CurveFamily::SCurve, QStringLiteral("medium"));
    QVERIFY(evaluateCurveDefinition(0.35F, jCurve, false) < 0.35F);
    QVERIFY(evaluateCurveDefinition(0.25F, sCurve, false) < 0.25F);
    QVERIFY(evaluateCurveDefinition(0.75F, sCurve, false) < 0.75F);
    QVERIFY(nearlyEqual(evaluateCurveDefinition(-1.0F, jCurve, false), -1.0F));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(1.0F, sCurve, false), 1.0F));

    float previousJ = -1.0F;
    float previousS = -1.0F;
    for (int index = 0; index <= 200; ++index) {
        const float input = -1.0F + static_cast<float>(index) / 100.0F;
        const float jValue = evaluateCurveDefinition(input, jCurve, false);
        const float sValue = evaluateCurveDefinition(input, sCurve, false);
        QVERIFY(jValue >= previousJ - 0.00001F);
        QVERIFY(sValue >= previousS - 0.00001F);
        QVERIFY(jValue >= -1.0F && jValue <= 1.0F);
        QVERIFY(sValue >= -1.0F && sValue <= 1.0F);
        previousJ = jValue;
        previousS = sValue;
    }
    const auto compiled = compileResponseCurve(jCurve, false);
    QCOMPARE(static_cast<int>(compiled->samples.size()), kResponseCurveLutSamples);
    QVERIFY(nearlyEqual(evaluateCompiledResponseCurve(0.35F, compiled),
                         evaluateCurveDefinition(0.35F, jCurve, false)));
}

void MappingCoreTests::advancedPresetsAreDistinctAndQuantified()
{
    QCOMPARE(static_cast<int>(advancedCurvePresets().size()), 15);
    QCOMPARE(static_cast<int>(std::count_if(advancedCurvePresets().cbegin(), advancedCurvePresets().cend(),
        [](const AdvancedCurvePresetInfo &preset) { return preset.category == QStringLiteral("Shooter / Flight Derived"); })), 5);
    std::vector<float> signatures;
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        const CurveDefinition definition = advancedCurveDefinition(preset.id);
        const CurveAnalysis analysis = analyzeCurveDefinition(definition, false);
        QVERIFY(analysis.valid);
        QVERIFY(analysis.monotonic);
        QVERIFY(analysis.fullAuthority);
        QVERIFY(std::isfinite(analysis.peakGain));
        signatures.push_back(evaluateCurveDefinition(0.20F, definition, false)
            + evaluateCurveDefinition(0.50F, definition, false) * 2.0F
            + evaluateCurveDefinition(0.80F, definition, false) * 3.0F);
    }
    std::sort(signatures.begin(), signatures.end());
    for (size_t index = 1; index < signatures.size(); ++index) {
        QVERIFY(std::abs(signatures[index] - signatures[index - 1]) > 0.003F);
    }
    const CurveDefinition precision = advancedCurveDefinition(QStringLiteral("precision-tracking"));
    const CurveDefinition acquisition = advancedCurveDefinition(QStringLiteral("fast-acquisition"));
    QVERIFY(evaluateCurveGain(0.0F, precision, false) < evaluateCurveGain(0.0F, acquisition, false));
}

void MappingCoreTests::universalStrengthUsesIdentityAtZeroAndFullResponseAtOne()
{
    const CurveDefinition jZero = standardCurveDefinition(CurveFamily::JCurve, 0.0F);
    const CurveDefinition jHigh = standardCurveDefinition(CurveFamily::JCurve, 1.0F);
    QVERIFY(nearlyEqual(evaluateCurveDefinition(-0.45F, jZero, false), -0.45F));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.55F, jZero, true), 0.55F));
    QVERIFY(evaluateCurveDefinition(0.55F, jHigh, true) < 0.55F);
    QVERIFY(!nearlyEqual(evaluateCurveDefinition(0.0F, jHigh, false), 0.0F));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(-1.0F, jHigh, false), -1.0F));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(1.0F, jHigh, false), 1.0F));

    const CurveDefinition sZero = standardCurveDefinition(CurveFamily::SCurve, 0.0F);
    const CurveDefinition sHigh = standardCurveDefinition(CurveFamily::SCurve, 1.0F);
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.37F, sZero, false), 0.37F));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.38F, sHigh, false),
                         -evaluateCurveDefinition(-0.38F, sHigh, false)));
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.5F, sHigh, true), 0.5F));
    QVERIFY(analyzeCurveDefinition(sHigh, false).valid);
    QVERIFY(analyzeCurveDefinition(sHigh, true).valid);

    CurveDefinition advanced = advancedCurveDefinition(QStringLiteral("precision-tracking"));
    const float advancedFull = evaluateCurveDefinition(0.42F, advanced, false);
    advanced.strength = 0.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, advanced, false), 0.42F));
    advanced.strength = 0.5F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, advanced, false), (0.42F + advancedFull) * 0.5F));
    advanced.strength = 1.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, advanced, false), advancedFull));

    CurveDefinition custom = materializeCurveDefinition(advanced, false, 13);
    const float customFull = evaluateCurveDefinition(0.42F, custom, false);
    custom.strength = 0.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, custom, false), 0.42F));
    custom.strength = 1.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, custom, false), customFull));
    custom.family = CurveFamily::Personal;
    custom.strength = 0.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, custom, false), 0.42F));
    custom.strength = 1.0F;
    QVERIFY(nearlyEqual(evaluateCurveDefinition(0.42F, custom, false), customFull));
}

void MappingCoreTests::strengthAndAxisSelectionPersistPerProfile()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.selectedAxisIndex = static_cast<int>(PhysicalAxis::Ry);
    activeProfile(configuration).axes[static_cast<int>(PhysicalAxis::Ry)].curve =
        advancedCurveDefinition(QStringLiteral("precision-tracking"));
    activeProfile(configuration).axes[static_cast<int>(PhysicalAxis::Ry)].curve.strength = 0.27F;
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::Ry)].curve = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("aircraft-gun-tracking")), false, 13);
    precision->axes[static_cast<int>(PhysicalAxis::Ry)].curve.strength = 0.73F;

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.selectedAxisIndex, static_cast<int>(PhysicalAxis::Ry));
    QVERIFY(nearlyEqual(activeProfile(restored).axes[static_cast<int>(PhysicalAxis::Ry)].curve.strength, 0.27F));
    const ControllerProfile *restoredPrecision = findProfile(restored, precisionProfileId());
    QVERIFY(restoredPrecision);
    QVERIFY(nearlyEqual(restoredPrecision->axes[static_cast<int>(PhysicalAxis::Ry)].curve.strength, 0.73F));
}

void MappingCoreTests::curvePointEditingSupportsNonuniformPointsLocksAndResampling()
{
    CurveDefinition editable = materializeCurveDefinition(
        standardCurveDefinition(CurveFamily::JCurve, QStringLiteral("medium")), false, 9);
    QVERIFY(editable.pointEditing);
    QVERIFY(editable.symmetry);
    QVERIFY(editable.points.front().locked);
    QVERIFY(editable.points[4].locked);
    QVERIFY(updateCurvePoint(editable, false, 6, 0.43F, 0.20F));
    QVERIFY(nearlyEqual(editable.points[6].input, 0.43F));
    QVERIFY(nearlyEqual(editable.points[2].input, -0.43F));
    QVERIFY(!updateCurvePoint(editable, false, 4, 0.1F, 0.1F));
    QVERIFY(setCurvePointLocked(editable, false, 6, true));
    QVERIFY(!updateCurvePoint(editable, false, 6, 0.48F, 0.24F));
    QVERIFY(setCurvePointLocked(editable, false, 6, false));
    int selected = -1;
    QVERIFY(addCurvePoint(editable, false, 0.31F, 0.13F, &selected));
    QCOMPARE(static_cast<int>(editable.points.size()), 11);
    QVERIFY(selected > 0);
    QVERIFY(removeCurvePoint(editable, false, selected));
    QCOMPARE(static_cast<int>(editable.points.size()), 9);
    editable = resampleCurveDefinition(editable, false, 17);
    QCOMPARE(static_cast<int>(editable.points.size()), 17);
    QVERIFY(curveDefinitionIsValid(editable, false));
}

void MappingCoreTests::curveGainUsesAuthoritativeEvaluation()
{
    const CurveDefinition linear = linearCurveDefinition();
    QVERIFY(nearlyEqual(evaluateCurveGain(-0.5F, linear, false), 1.0F));
    const CurveDefinition jCurve = standardCurveDefinition(CurveFamily::JCurve, QStringLiteral("strong"));
    QVERIFY(evaluateCurveGain(0.05F, jCurve, false) < 1.0F);
    QVERIFY(evaluateCurveGain(0.80F, jCurve, false) > evaluateCurveGain(0.20F, jCurve, false));
    CurveDefinition custom = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("hybrid-precision")), false, 13);
    custom.interpolation = CurveInterpolation::Smooth;
    QVERIFY(std::isfinite(evaluateCurveGain(0.42F, custom, false)));
    QVERIFY(analyzeCurveDefinition(custom, false).valid);
}

void MappingCoreTests::personalCurvePresetsPersistAsIndependentDefinitions()
{
    MapperConfiguration configuration = defaultConfiguration();
    PersonalCurvePreset preset;
    preset.id = QStringLiteral("curve-fine-aim");
    preset.name = QStringLiteral("Fine Aim");
    preset.definition = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("precision-tracking")), false, 9);
    configuration.personalCurvePresets.push_back(preset);
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(restored.personalCurvePresets.size()), 1);
    QCOMPARE(restored.personalCurvePresets.front().name, QStringLiteral("Fine Aim"));
    CurveDefinition applied = restored.personalCurvePresets.front().definition;
    QVERIFY(updateCurvePoint(applied, false, 6, 0.45F, 0.22F));
    QVERIFY(!nearlyEqual(applied.points[6].input,
                         restored.personalCurvePresets.front().definition.points[6].input));
}

void MappingCoreTests::v12ProfileConfigurationMigratesWithSafeAxisDefaults()
{
    QJsonObject v12 = ConfigStore::toJson(defaultConfiguration());
    v12.insert(QStringLiteral("version"), 3);
    v12.remove(QStringLiteral("selectedAxisIndex"));
    QJsonArray profiles = v12.value(QStringLiteral("profiles")).toArray();
    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        QJsonObject profile = profiles.at(profileIndex).toObject();
        QJsonArray axes = profile.value(QStringLiteral("axes")).toArray();
        for (int axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            QJsonObject axis = axes.at(axisIndex).toObject();
            axis.remove(QStringLiteral("hysteresis"));
            axis.remove(QStringLiteral("outputMinimum"));
            axis.remove(QStringLiteral("outputMaximum"));
            axes.replace(axisIndex, axis);
        }
        profile.insert(QStringLiteral("axes"), axes);
        profiles.replace(profileIndex, profile);
    }
    v12.insert(QStringLiteral("profiles"), profiles);

    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(v12, &valid);
    QVERIFY(valid);
    QCOMPARE(migrated.selectedAxisIndex, static_cast<int>(PhysicalAxis::X));
    const AxisMapping &axis = activeProfile(migrated).axes[static_cast<int>(PhysicalAxis::X)];
    QVERIFY(nearlyEqual(axis.hysteresis, 0.002F));
    QVERIFY(nearlyEqual(axis.outputMinimum, -1.0F));
    QVERIFY(nearlyEqual(axis.outputMaximum, 1.0F));
}

void MappingCoreTests::virtualControllersAreNeverEligibleAsPhysicalInput()
{
    QVERIFY(isVirtualControllerName(QStringLiteral("vJoy Device")));
    QVERIFY(isVirtualControllerName(QStringLiteral("Virtual Joystick 1")));
    QVERIFY(!isVirtualControllerName(QStringLiteral("T.Flight HOTAS One")));
    QVERIFY(!isVirtualControllerName(QStringLiteral("Thrustmaster T.16000M")));
}

void MappingCoreTests::implicitButtonsDefaultToMatchingVjoyTargets()
{
    ButtonBindings bindings(4);
    bindings[1] = {ButtonActionType::Disabled, 0, true}; // User chose Unused.
    bindings[2] = {ButtonActionType::VirtualButton, 12, true}; // User route.

    QVERIFY(ensureDefaultButtonMappings(bindings, 4, 32));
    QCOMPARE(bindings[0].target, 1);
    QCOMPARE(bindings[1].type, ButtonActionType::Disabled);
    QCOMPARE(bindings[2].target, 12);
    QCOMPARE(bindings[3].target, 4);
    QVERIFY(!bindings[0].explicitlyConfigured);
    QVERIFY(!needsDefaultButtonMappings(bindings, 4, 32));
}

void MappingCoreTests::configurationRoundTrips()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &normal = activeProfile(configuration);
    configuration.preferredDeviceId = QStringLiteral("{0D15EA5E-0000-0000-0000-000000000001}");
    configuration.vjoyDeviceId = 2;
    configuration.outputLayouts.front().requirements.deviceId = 2;
    configuration.outputLayouts.front().hidHideDeviceInstanceId =
        QStringLiteral("HID\\VID_1234&PID_BEAD\\OWNED-VJOY-2");
    configuration.outputLayouts.front().hidhideManaged = true;
    configuration.startMappingOnLaunch = true;
    configuration.disabledAxisValue = -0.25F;
    normal.axes[0].inverted = true;
    normal.axes[0].rangeMode = AxisRangeMode::OneSided;
    normal.axes[0].curve = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("precision-tracking")), true, 9);
    normal.axes[0].centeredCurveBackup = materializeCurveDefinition(
        advancedCurveDefinition(QStringLiteral("precision-tracking")), false, 9);
    normal.axes[0].hasCenteredCurveBackup = true;
    normal.axes[0].oneSidedCurveBackup = normal.axes[0].curve;
    normal.axes[0].hasOneSidedCurveBackup = true;
    normal.axes[1].deadzone = 0.12F;
    configuration.calibration[2] = {true, -0.9F, 0.1F, 0.8F};
    configuration.calibration[2].centered = false;
    CalibrationHistoryEntry calibrationRecord;
    calibrationRecord.controllerRecordId = QStringLiteral("controller-a");
    calibrationRecord.controllerDisplayName = QStringLiteral("T.Flight HOTAS One");
    calibrationRecord.controllerIdentity = QStringLiteral("HID\\VID_044F");
    calibrationRecord.completedAtUtc = QStringLiteral("2026-08-29T12:00:00.000Z");
    calibrationRecord.applicationVersion = QStringLiteral("2.0.8");
    calibrationRecord.calibratedAxisCount = 4;
    calibrationRecord.calibration = configuration.calibration;
    configuration.calibrationHistory.push_back(calibrationRecord);
    normal.buttons = defaultButtonMappings(4, 4);
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter")));
    QVERIFY(activateProfile(configuration, QStringLiteral("profile-normal")));

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.preferredDeviceId, configuration.preferredDeviceId);
    QCOMPARE(restored.vjoyDeviceId, 2);
    QCOMPARE(restored.outputLayouts.front().hidHideDeviceInstanceId,
             QStringLiteral("HID\\VID_1234&PID_BEAD\\OWNED-VJOY-2"));
    QVERIFY(restored.outputLayouts.front().hidhideManaged);
    QVERIFY(restored.startMappingOnLaunch);
    QCOMPARE(restored.disabledAxisValue, -0.25F);
    QCOMPARE(static_cast<int>(restored.profiles.size()), 3);
    QVERIFY(activeProfile(restored).axes[0].inverted);
    QCOMPARE(activeProfile(restored).axes[0].rangeMode, AxisRangeMode::OneSided);
    QVERIFY(activeProfile(restored).axes[0].hasCenteredCurveBackup);
    QVERIFY(activeProfile(restored).axes[0].hasOneSidedCurveBackup);
    QVERIFY(curveDefinitionIsValid(activeProfile(restored).axes[0].centeredCurveBackup, false));
    QVERIFY(curveDefinitionIsValid(activeProfile(restored).axes[0].oneSidedCurveBackup, true));
    QCOMPARE(activeProfile(restored).axes[1].deadzone, 0.12F);
    QVERIFY(restored.calibration[2].enabled);
    QCOMPARE(restored.calibration[2].center, 0.1F);
    QVERIFY(!restored.calibration[2].centered);
    QCOMPARE(static_cast<int>(restored.calibrationHistory.size()), 1);
    QCOMPARE(restored.calibrationHistory.front().controllerDisplayName,
             QStringLiteral("T.Flight HOTAS One"));
    QCOMPARE(restored.calibrationHistory.front().calibratedAxisCount, 4);
    QCOMPARE(restored.calibrationHistory.front().calibration[2].center, 0.1F);
    QCOMPARE(activeProfile(restored).buttons[3].target, 4);
}

void MappingCoreTests::outputLimitsRoundTripAcrossDomainsAndSchemaMigration()
{
    AxisMapping mapping;
    mapping.outputMinimum = -0.80F;
    mapping.outputMaximum = 0.90F;
    switchAxisOutputLimitDomain(mapping, AxisRangeMode::OneSided);
    QCOMPARE(mapping.outputMinimum, 0.0F);
    QCOMPARE(mapping.outputMaximum, 1.0F);
    mapping.outputMinimum = 0.10F;
    mapping.outputMaximum = 0.85F;
    switchAxisOutputLimitDomain(mapping, AxisRangeMode::Centered);
    QCOMPARE(mapping.outputMinimum, -0.80F);
    QCOMPARE(mapping.outputMaximum, 0.90F);
    switchAxisOutputLimitDomain(mapping, AxisRangeMode::OneSided);
    QCOMPARE(mapping.outputMinimum, 0.10F);
    QCOMPARE(mapping.outputMaximum, 0.85F);

    MapperConfiguration configuration = defaultConfiguration();
    AxisMapping &persisted = activeProfile(configuration).axes[static_cast<int>(PhysicalAxis::X)];
    persisted = mapping;
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    const AxisMapping &roundTripped = activeProfile(restored).axes[static_cast<int>(PhysicalAxis::X)];
    QCOMPARE(roundTripped.rangeMode, AxisRangeMode::OneSided);
    QCOMPARE(roundTripped.outputMinimum, 0.10F);
    QCOMPARE(roundTripped.outputMaximum, 0.85F);
    QCOMPARE(roundTripped.centeredOutputMinimum, -0.80F);
    QCOMPARE(roundTripped.centeredOutputMaximum, 0.90F);

    QJsonObject schema16 = ConfigStore::toJson(configuration);
    schema16.insert(QStringLiteral("version"), 16);
    QJsonArray profiles = schema16.value(QStringLiteral("profiles")).toArray();
    QJsonObject profile = profiles.first().toObject();
    QJsonArray axes = profile.value(QStringLiteral("axes")).toArray();
    QJsonObject axis = axes.first().toObject();
    axis.remove(QStringLiteral("centeredOutputMinimum"));
    axis.remove(QStringLiteral("centeredOutputMaximum"));
    axis.remove(QStringLiteral("oneSidedOutputMinimum"));
    axis.remove(QStringLiteral("oneSidedOutputMaximum"));
    axes.replace(0, axis);
    profile.insert(QStringLiteral("axes"), axes);
    profiles.replace(0, profile);
    schema16.insert(QStringLiteral("profiles"), profiles);
    const MapperConfiguration migrated = ConfigStore::fromJson(schema16, &valid);
    QVERIFY(valid);
    const AxisMapping &migratedAxis = activeProfile(migrated).axes[static_cast<int>(PhysicalAxis::X)];
    QCOMPARE(migratedAxis.oneSidedOutputMinimum, 0.10F);
    QCOMPARE(migratedAxis.oneSidedOutputMaximum, 0.85F);
    QCOMPARE(migratedAxis.centeredOutputMinimum, -1.0F);
    QCOMPARE(migratedAxis.centeredOutputMaximum, 1.0F);
}

void MappingCoreTests::controllerRegistryPersistsPerDeviceCalibrationAndRequirements()
{
    DiscoveredController controller;
    controller.name = QStringLiteral("VKB Gladiator NXT EVO");
    controller.directInputId = QStringLiteral("{INSTANCE-A}");
    controller.productGuid = QStringLiteral("{PRODUCT-A}");
    controller.hidInstanceId = QStringLiteral("HID\\VID_231D&PID_0200\\ONE");
    controller.vendorId = 0x231D;
    controller.productId = 0x0200;
    controller.connected = true;
    controller.axes[0] = true;
    controller.axes[1] = true;
    controller.axisCount = 2;
    controller.buttonCount = 29;
    controller.povCount = 1;
    std::array<Calibration, kPhysicalAxisCount> calibration{};
    calibration[0] = {true, -0.9F, 0.05F, 0.9F};
    ControllerVJoyRequirements requirements;
    requirements.axes[1] = true;
    requirements.axes[2] = true;
    requirements.buttons = 29;
    requirements.continuousPovs = 1;
    requirements.deviceId = 2;

    MapperConfiguration configuration = defaultConfiguration();
    configuration.savedControllers.push_back(ControllerManager::verifiedRecord(controller, calibration, requirements));
    configuration.activeControllerRecordId = configuration.savedControllers.front().id;
    configuration.calibration = calibration;
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(restored.savedControllers.size()), 1);
    QCOMPARE(restored.activeControllerRecordId, configuration.activeControllerRecordId);
    QCOMPARE(restored.savedControllers.front().displayName, controller.name);
    QCOMPARE(restored.savedControllers.front().calibration[0].center, 0.05F);
    QCOMPARE(restored.savedControllers.front().vjoyRequirements.buttons, 29);
    QCOMPARE(restored.savedControllers.front().vjoyRequirements.deviceId, 2);
}

void MappingCoreTests::controllerIdentityUsesLayeredMatchingWithoutAmbiguousAutoSelection()
{
    DiscoveredController first;
    first.name = QStringLiteral("Identical Joystick");
    first.directInputId = QStringLiteral("{INSTANCE-NEW}");
    first.productGuid = QStringLiteral("{PRODUCT}");
    first.vendorId = 1;
    first.productId = 2;
    first.axisCount = 4;
    first.buttonCount = 12;
    first.povCount = 1;
    first.connected = true;
    first.axes[0] = true;
    SavedControllerRecord remembered = ControllerManager::verifiedRecord(first, {}, {});
    remembered.lastDirectInputId = QStringLiteral("{INSTANCE-OLD}");
    remembered.hidInstanceId.clear();

    ControllerMatch changedInstance = ControllerManager::match(first, {remembered});
    QCOMPARE(changedInstance.recordId, remembered.id);
    QCOMPARE(changedInstance.strength, ControllerMatchStrength::Product);

    SavedControllerRecord duplicate = remembered;
    duplicate.id = QStringLiteral("other-record");
    const ControllerMatch ambiguous = ControllerManager::match(first, {remembered, duplicate});
    QVERIFY(ambiguous.ambiguous);
    QVERIFY(ambiguous.recordId.isEmpty());
    QVERIFY(ControllerManager::autoSelect({first}, {remembered, duplicate}, remembered.id).isEmpty());
}

void MappingCoreTests::vjoyAxisDescriptorsMustMatchExactlyWhileCapacitySupersetsAreAccepted()
{
    ControllerVJoyRequirements available;
    available.axes.fill(true);
    available.buttons = 32;
    available.continuousPovs = 2;
    available.discretePovs = 1;
    ControllerVJoyRequirements required;
    required.axes[1] = true;
    required.buttons = 15;
    required.continuousPovs = 1;
    QVERIFY(!ControllerManager::isVjoySufficient(available, required));
    available.axes[2] = available.axes[3] = available.axes[4] = available.axes[5] = false;
    available.axes[6] = available.axes[7] = available.axes[8] = false;
    QVERIFY(ControllerManager::isVjoySufficient(available, required));
    required.buttons = 33;
    QVERIFY(!ControllerManager::isVjoySufficient(available, required));
}

void MappingCoreTests::physicalAxisActivityRequiresCompletedCalibrationTravel()
{
    QCOMPARE(physicalAxisActivityForObservedSpan(-1.0F, 1.0F, false), PhysicalAxisActivity::Unknown);
    QCOMPARE(physicalAxisActivityForObservedSpan(-0.01F, 0.02F, true), PhysicalAxisActivity::Fixed);
    QCOMPARE(physicalAxisActivityForObservedSpan(-0.09F, 0.01F, true), PhysicalAxisActivity::Active);
    QCOMPARE(physicalAxisActivityForObservedSpan(std::numeric_limits<float>::quiet_NaN(), 0.5F, true),
             PhysicalAxisActivity::Unknown);
}

void MappingCoreTests::v17ConfigurationMigratesToPreservedOutputLayout()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.vjoyDeviceId = 2;
    ControllerProfile &profile = activeProfile(configuration);
    profile.axes[static_cast<size_t>(PhysicalAxis::Rx)].target = VirtualAxis::Slider0;
    profile.buttons = {{ButtonActionType::VirtualButton, 16, true, QStringLiteral("Legacy Fire")}};
    AutomationDefinition automation;
    automation.id = QStringLiteral("legacy-layout-action");
    automation.name = QStringLiteral("Legacy Layout Action");
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::Always;
    automation.conditions = {condition};
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonTap;
    action.virtualButton = 20;
    automation.actions = {action};
    configuration.automations = {automation};
    configuration.calibration[static_cast<size_t>(PhysicalAxis::Rx)] = {true, -0.8F, 0.0F, 0.9F};

    QJsonObject v17 = ConfigStore::toJson(configuration);
    v17.insert(QStringLiteral("version"), 17);
    v17.remove(QStringLiteral("axisActivity"));
    v17.remove(QStringLiteral("outputLayouts"));
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(v17, &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(migrated.outputLayouts.size()), 1);
    const VirtualOutputLayout &layout = migrated.outputLayouts.front();
    QCOMPARE(layout.id, defaultOutputLayoutId());
    QCOMPARE(layout.requirements.deviceId, 2);
    QVERIFY(layout.requirements.axes[static_cast<size_t>(VirtualAxis::Slider0)]);
    QCOMPARE(layout.requirements.buttons, 20);
    QCOMPARE(activeProfile(migrated).outputLayoutId, defaultOutputLayoutId());
    QCOMPARE(activeProfile(migrated).axes[static_cast<size_t>(PhysicalAxis::Rx)].target,
             VirtualAxis::Slider0);
    QCOMPARE(migrated.calibration[static_cast<size_t>(PhysicalAxis::Rx)].maximum, 0.9F);
    QCOMPARE(migrated.axisActivity[static_cast<size_t>(PhysicalAxis::Rx)], PhysicalAxisActivity::Unknown);
}

void MappingCoreTests::disabledAxisValueDefaultsMigratesAndPersistsGlobally()
{
    MapperConfiguration configuration = defaultConfiguration();
    QCOMPARE(configuration.disabledAxisValue, 0.0F);

    QJsonObject priorRelease = ConfigStore::toJson(configuration);
    priorRelease.insert(QStringLiteral("version"), 10);
    priorRelease.remove(QStringLiteral("disabledAxisValue"));
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(priorRelease, &valid);
    QVERIFY(valid);
    QCOMPARE(migrated.disabledAxisValue, 0.0F);

    configuration.disabledAxisValue = -0.25F;
    QString createdId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), precisionProfileId(), &createdId));
    QVERIFY(findProfile(configuration, createdId));
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.disabledAxisValue, -0.25F);
    QVERIFY(activateProfile(configuration, createdId));
    QCOMPARE(configuration.disabledAxisValue, -0.25F);
}

void MappingCoreTests::disabledAxisValueClampsSafely()
{
    QJsonObject json = ConfigStore::toJson(defaultConfiguration());
    bool valid = false;
    json.insert(QStringLiteral("disabledAxisValue"), 4.0);
    QCOMPARE(ConfigStore::fromJson(json, &valid).disabledAxisValue, 1.0F);
    QVERIFY(valid);
    json.insert(QStringLiteral("disabledAxisValue"), -4.0);
    QCOMPARE(ConfigStore::fromJson(json, &valid).disabledAxisValue, -1.0F);
    QVERIFY(valid);
    json.insert(QStringLiteral("disabledAxisValue"), QStringLiteral("invalid"));
    QCOMPARE(ConfigStore::fromJson(json, &valid).disabledAxisValue, 0.0F);
    QVERIFY(valid);
}

void MappingCoreTests::disabledAxisOutputPlanParksUnusedTargetsWithoutChangingMappedAxes()
{
    MapperConfiguration configuration = defaultConfiguration();
    RuntimeMappingConfiguration mapping = compileActiveProfile(configuration);
    std::array<bool, kPhysicalAxisCount> available{};
    std::array<float, kPhysicalAxisCount> transformed{};
    available.fill(true);
    transformed[static_cast<size_t>(PhysicalAxis::X)] = 0.62F;
    transformed[static_cast<size_t>(PhysicalAxis::Y)] = -0.40F;
    transformed[static_cast<size_t>(PhysicalAxis::Z)] = 0.18F;
    transformed[static_cast<size_t>(PhysicalAxis::Rz)] = -0.75F;

    // Roll has no active route. Its physical input must not leak onto a
    // virtual axis, and its former target remains safely parked.
    mapping.axes[static_cast<size_t>(PhysicalAxis::X)].profile.target = VirtualAxis::Disabled;
    const VirtualAxisOutputPlan plan = buildVirtualAxisOutputPlan(mapping, available, transformed, -0.25F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::X)], -0.25F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Y)], -0.40F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Z)], 0.18F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Rz)], -0.75F);
    QCOMPARE(plan.sourceIndexes[static_cast<size_t>(VirtualAxis::X)], -1);
    QCOMPARE(plan.sourceIndexes[static_cast<size_t>(VirtualAxis::Y)], static_cast<int>(PhysicalAxis::Y));
}

void MappingCoreTests::v185AxisMetadataAndMappingControlsRoundTrip()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &profile = activeProfile(configuration);
    AxisMapping &roll = profile.axes[static_cast<size_t>(PhysicalAxis::X)];
    roll.customName = QStringLiteral("Stick Roll");
    roll.rangeMode = AxisRangeMode::OneSided;
    roll.target = VirtualAxis::Ry;
    profile.virtualAxisAliases[static_cast<size_t>(VirtualAxis::Ry)] = QStringLiteral("R Up/Down");
    profile.buttons.resize(3);
    profile.buttons[2].customName = QStringLiteral("Master Arm");
    configuration.mappingControls.resize(3);
    configuration.mappingControls[2] = MappingControlAction::ToggleMapping;

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    const ControllerProfile &restoredProfile = activeProfile(restored);
    QCOMPARE(restoredProfile.axes[static_cast<size_t>(PhysicalAxis::X)].customName,
             QStringLiteral("Stick Roll"));
    QCOMPARE(restoredProfile.axes[static_cast<size_t>(PhysicalAxis::X)].rangeMode,
             AxisRangeMode::OneSided);
    QCOMPARE(restoredProfile.axes[static_cast<size_t>(PhysicalAxis::X)].target, VirtualAxis::Ry);
    QCOMPARE(restoredProfile.virtualAxisAliases[static_cast<size_t>(VirtualAxis::Ry)],
             QStringLiteral("R Up/Down"));
    QCOMPARE(restoredProfile.buttons[2].customName, QStringLiteral("Master Arm"));
    QCOMPARE(restored.mappingControls[2], MappingControlAction::ToggleMapping);
    QCOMPARE(compileRuntimeProfileCache(restored).mappingControls[2],
             MappingControlAction::ToggleMapping);

    // Older records retain the historical throttle behavior only at migration;
    // newly configured profiles are explicit rather than tied to physical Z.
    QJsonObject v12 = ConfigStore::toJson(configuration);
    v12.insert(QStringLiteral("version"), 12);
    v12.remove(QStringLiteral("mappingControls"));
    QJsonArray profiles = v12.value(QStringLiteral("profiles")).toArray();
    for (int profileIndex = 0; profileIndex < profiles.size(); ++profileIndex) {
        QJsonObject serialized = profiles.at(profileIndex).toObject();
        serialized.remove(QStringLiteral("virtualAxisAliases"));
        QJsonArray axes = serialized.value(QStringLiteral("axes")).toArray();
        for (int axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            QJsonObject axis = axes.at(axisIndex).toObject();
            axis.remove(QStringLiteral("rangeMode"));
            axis.remove(QStringLiteral("customName"));
            axes[axisIndex] = axis;
        }
        serialized.insert(QStringLiteral("axes"), axes);
        profiles[profileIndex] = serialized;
    }
    v12.insert(QStringLiteral("profiles"), profiles);
    const MapperConfiguration migrated = ConfigStore::fromJson(v12, &valid);
    QVERIFY(valid);
    QCOMPARE(activeProfile(migrated).axes[static_cast<size_t>(PhysicalAxis::Z)].rangeMode,
             AxisRangeMode::OneSided);
    QCOMPARE(activeProfile(migrated).axes[static_cast<size_t>(PhysicalAxis::X)].rangeMode,
             AxisRangeMode::Centered);
    QVERIFY(migrated.mappingControls.empty());
}

void MappingCoreTests::expandedVirtualAxesArePlannedAndUnavailableRoutesStayParked()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &profile = activeProfile(configuration);
    profile.axes[static_cast<size_t>(PhysicalAxis::Rx)].target = VirtualAxis::Slider1;
    profile.axes[static_cast<size_t>(PhysicalAxis::Y)].target = VirtualAxis::Ry;
    RuntimeMappingConfiguration mapping = compileActiveProfile(configuration);
    std::array<bool, kPhysicalAxisCount> available{};
    std::array<float, kPhysicalAxisCount> transformed{};
    available.fill(true);
    transformed[static_cast<size_t>(PhysicalAxis::Rx)] = 0.42F;
    transformed[static_cast<size_t>(PhysicalAxis::Y)] = -0.30F;
    const VirtualAxisOutputPlan plan = buildVirtualAxisOutputPlan(mapping, available, transformed, -0.15F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Slider1)], 0.42F);
    QCOMPARE(plan.sourceIndexes[static_cast<size_t>(VirtualAxis::Slider1)],
             static_cast<int>(PhysicalAxis::Rx));
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Ry)], -0.30F);
    QCOMPARE(plan.values[static_cast<size_t>(VirtualAxis::Slider0)], -0.15F);
    QCOMPARE(plan.sourceIndexes[static_cast<size_t>(VirtualAxis::Slider0)], -1);
}

void MappingCoreTests::duplicateMappingIsRejectedAndNormalized()
{
    AxisMappings mappings = defaultAxisMappings();
    QVERIFY(hasMappingConflict(mappings, static_cast<int>(PhysicalAxis::Z), VirtualAxis::X));
    QVERIFY(!hasMappingConflict(mappings, static_cast<int>(PhysicalAxis::Z), VirtualAxis::Disabled));
    mappings[static_cast<int>(PhysicalAxis::Z)].target = VirtualAxis::X;
    QVERIFY(!normalizeMappingConflicts(mappings));
    QCOMPARE(mappings[static_cast<int>(PhysicalAxis::Z)].target, VirtualAxis::Disabled);
}

void MappingCoreTests::buttonCapacityMismatchIsReported()
{
    const ButtonCapacityStatus insufficient = assessButtonCapacity(15, 8);
    QCOMPARE(insufficient.physicalButtons, 15);
    QCOMPARE(insufficient.virtualButtons, 8);
    QVERIFY(!insufficient.sufficient);
    QVERIFY(!insufficient.recommended);

    const ButtonCapacityStatus ready = assessButtonCapacity(15, 32);
    QVERIFY(ready.sufficient);
    QVERIFY(ready.recommended);
}

void MappingCoreTests::defaultButtonPassthroughIsCapacityBounded()
{
    const ButtonBindings bindings = defaultButtonMappings(5, 3);
    QCOMPARE(static_cast<int>(bindings.size()), 5);
    QCOMPARE(bindings[0].target, 1);
    QCOMPARE(bindings[2].target, 3);
    QCOMPARE(bindings[3].type, ButtonActionType::Disabled);
}

void MappingCoreTests::fifteenButtonPassthroughUsesButtonsOneThroughFifteen()
{
    const ButtonBindings bindings = defaultButtonMappings(15, 32);
    QCOMPARE(static_cast<int>(bindings.size()), 15);
    for (int source = 0; source < 15; ++source) {
        QCOMPARE(bindings[static_cast<size_t>(source)].type, ButtonActionType::VirtualButton);
        QCOMPARE(bindings[static_cast<size_t>(source)].target, source + 1);
    }
}

void MappingCoreTests::buttonMappingPropagatesPressAndRelease()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(4, 4), 4);
    PhysicalButtonStates physical{};
    physical[1] = true;
    QVERIFY(mapButtonStates(physical, targets, 4)[2]);
    physical[1] = false;
    QVERIFY(!mapButtonStates(physical, targets, 4)[2]);
}

void MappingCoreTests::buttonsNineThroughFifteenPropagatePressAndRelease()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(15, 32), 32);
    for (int source = 8; source < 15; ++source) {
        PhysicalButtonStates physical{};
        physical[static_cast<size_t>(source)] = true;
        const VirtualButtonStates pressed = mapButtonStates(physical, targets, 32);
        QVERIFY(pressed[static_cast<size_t>(source + 1)]);

        physical[static_cast<size_t>(source)] = false;
        const VirtualButtonStates released = mapButtonStates(physical, targets, 32);
        QVERIFY(!released[static_cast<size_t>(source + 1)]);
    }
}

void MappingCoreTests::stoppingMappingReleasesVirtualButtons()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(2, 2), 2);
    PhysicalButtonStates physical{};
    physical[0] = true;
    QVERIFY(mapButtonStates(physical, targets, 2)[1]);

    // Stopping mapping clears the virtual output snapshot before releasing vJoy.
    physical.fill(false);
    const VirtualButtonStates released = mapButtonStates(physical, targets, 2);
    QVERIFY(!released[1]);
    QVERIFY(!released[2]);
}

void MappingCoreTests::disabledAndInvalidButtonsDoNotMap()
{
    ButtonBindings bindings{{ButtonActionType::Disabled, 0}, {ButtonActionType::VirtualButton, 9}};
    QVERIFY(!normalizeButtonMappings(bindings, 4));
    QCOMPARE(bindings[1].type, ButtonActionType::Disabled);
    const PhysicalButtonStates physical{true, true};
    const auto output = mapButtonStates(physical, buildRuntimeButtonTargets(bindings, 4), 4);
    for (bool pressed : output) QVERIFY(!pressed);
}

void MappingCoreTests::buttonRouteDecisionsAreAtomicAndSupportFanIn()
{
    // B3 -> V1 displaces B1 -> V1 while B3 previously owned V3: Replace is
    // an atomic swap, not a silent disable of B1.
    ButtonBindings swap{{ButtonActionType::VirtualButton, 1}, {}, {ButtonActionType::VirtualButton, 3}};
    const ButtonRouteChange swapChange = analyzeButtonRouteChange(swap, 2, 1, 32);
    QVERIFY(swapChange.valid);
    QVERIFY(swapChange.requiresResolution);
    QVERIFY(swapChange.canSwap);
    const ButtonBindings beforeCancel = swap;
    QVERIFY(!applyButtonRouteChange(swap, swapChange, ButtonRouteResolution::Cancel));
    QCOMPARE(swap[0].target, beforeCancel[0].target);
    QCOMPARE(swap[2].target, beforeCancel[2].target);
    QVERIFY(applyButtonRouteChange(swap, swapChange, ButtonRouteResolution::Replace));
    QCOMPARE(swap[0].target, 3);
    QCOMPARE(swap[2].target, 1);

    // A disabled incoming source has no reciprocal output, so Replace moves
    // the route and leaves only the displaced source disabled.
    ButtonBindings fallback{{ButtonActionType::VirtualButton, 1}, {}};
    const ButtonRouteChange fallbackChange = analyzeButtonRouteChange(fallback, 1, 1, 32);
    QVERIFY(fallbackChange.requiresResolution);
    QVERIFY(!fallbackChange.canSwap);
    QVERIFY(applyButtonRouteChange(fallback, fallbackChange, ButtonRouteResolution::Replace));
    QCOMPARE(fallback[0].type, ButtonActionType::Disabled);
    QCOMPARE(fallback[1].target, 1);

    // Ignore deliberately creates fan-in; persisted configuration and the
    // fixed table must retain both inputs and aggregate with logical OR.
    ButtonBindings shared{{ButtonActionType::VirtualButton, 1}, {}, {ButtonActionType::VirtualButton, 3}};
    const ButtonRouteChange sharedChange = analyzeButtonRouteChange(shared, 2, 1, 32);
    QVERIFY(applyButtonRouteChange(shared, sharedChange, ButtonRouteResolution::Ignore));
    QVERIFY(normalizeButtonMappings(shared, 32));
    QCOMPARE(shared[0].target, 1);
    QCOMPARE(shared[2].target, 1);
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(shared, 32);
    PhysicalButtonStates physical{};
    QVERIFY(!mapButtonStates(physical, targets, 32)[1]);
    physical[0] = true;
    QVERIFY(mapButtonStates(physical, targets, 32)[1]);
    physical[0] = false;
    physical[2] = true;
    QVERIFY(mapButtonStates(physical, targets, 32)[1]);
    physical[0] = true;
    QVERIFY(mapButtonStates(physical, targets, 32)[1]);
    physical[0] = false; // Releasing B1 must not overwrite B3's pressed state.
    QVERIFY(mapButtonStates(physical, targets, 32)[1]);

    MapperConfiguration persisted = defaultConfiguration();
    activeProfile(persisted).buttons = shared;
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(persisted), &valid);
    QVERIFY(valid);
    QCOMPARE(activeProfile(restored).buttons[0].target, 1);
    QCOMPARE(activeProfile(restored).buttons[2].target, 1);

    // The target is independent from the detected physical source number.
    ButtonBindings frozen{{ButtonActionType::VirtualButton, 3}};
    const ButtonRouteChange frozenChange = analyzeButtonRouteChange(frozen, 6, 3, 32);
    QVERIFY(frozenChange.valid);
    QCOMPARE(frozenChange.targetVirtualButton, 3);
    QVERIFY(applyButtonRouteChange(frozen, frozenChange, ButtonRouteResolution::Replace));
    QCOMPARE(frozen[6].target, 3);

    // A high selected destination remains independent from a low detected
    // source, which is the destination-first Learn Button contract.
    ButtonBindings highDestination{};
    const ButtonRouteChange highDestinationChange = analyzeButtonRouteChange(highDestination, 1, 28, 32);
    QVERIFY(highDestinationChange.valid);
    QCOMPARE(highDestinationChange.targetVirtualButton, 28);
    QVERIFY(applyButtonRouteChange(highDestination, highDestinationChange, ButtonRouteResolution::Replace));
    QCOMPARE(highDestination[1].target, 28);
}

void MappingCoreTests::buttonConfigurationRoundTripsAndMigratesV1()
{
    MapperConfiguration configuration = defaultConfiguration();
    activeProfile(configuration).buttons = defaultButtonMappings(4, 4);
    bool valid = false;
    const QJsonObject v2 = legacyConfigurationJson(configuration, 2);
    const MapperConfiguration restored = ConfigStore::fromJson(v2, &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(activeProfile(restored).buttons.size()), 4);
    QCOMPARE(activeProfile(restored).buttons[3].target, 4);
    const ControllerProfile *precision = findProfile(restored, precisionProfileId());
    QVERIFY(precision);
    QCOMPARE(precision->buttons[3].target, 4);

    const QJsonObject v1 = legacyConfigurationJson(configuration, 1);
    const MapperConfiguration migrated = ConfigStore::fromJson(v1, &valid);
    QVERIFY(valid);
    QVERIFY(activeProfile(migrated).buttons.empty());
    QCOMPARE(activeProfile(migrated).axes[static_cast<int>(PhysicalAxis::X)].target, VirtualAxis::X);
}

void MappingCoreTests::malformedButtonConfigurationPreservesAxisConfiguration()
{
    MapperConfiguration configuration = defaultConfiguration();
    activeProfile(configuration).axes[static_cast<int>(PhysicalAxis::Y)].inverted = true;
    QJsonObject malformed = legacyConfigurationJson(configuration, 2);
    malformed.insert(QStringLiteral("buttons"), QStringLiteral("not an array"));
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(malformed, &valid);
    QVERIFY(valid);
    QVERIFY(activeProfile(restored).buttons.empty());
    QVERIFY(activeProfile(restored).axes[static_cast<int>(PhysicalAxis::Y)].inverted);
}

void MappingCoreTests::povRawValuesCompileToLogicalDirections()
{
    const std::array<std::pair<int, PovDirection>, 10> cases{{
        {-1, PovDirection::Centered}, {0, PovDirection::Up}, {4500, PovDirection::UpRight},
        {9000, PovDirection::Right}, {13500, PovDirection::DownRight}, {18000, PovDirection::Down},
        {22500, PovDirection::DownLeft}, {27000, PovDirection::Left}, {31500, PovDirection::UpLeft},
        {45000, PovDirection::Centered},
    }};
    for (const auto &[raw, expected] : cases) {
        QCOMPARE(static_cast<int>(povDirectionFromRaw(raw)), static_cast<int>(expected));
    }
}

void MappingCoreTests::povMappingsPressTransitionAndRelease()
{
    PovBindings bindings(1);
    bindings[0][static_cast<size_t>(povDirectionIndex(PovDirection::Up))] =
        {ButtonActionType::VirtualButton, 16, true};
    bindings[0][static_cast<size_t>(povDirectionIndex(PovDirection::UpRight))] =
        {ButtonActionType::VirtualButton, 17, true};
    const RuntimePovTargets targets = buildRuntimePovTargets(bindings, 32);
    PhysicalPovValues raw{};
    raw.fill(-1);

    raw[0] = 0;
    VirtualButtonStates up{};
    mapPovStates(up, raw, 1, targets, 32);
    QVERIFY(up[16]);
    QVERIFY(!up[17]);

    raw[0] = 4500;
    VirtualButtonStates diagonal{};
    mapPovStates(diagonal, raw, 1, targets, 32);
    QVERIFY(!diagonal[16]);
    QVERIFY(diagonal[17]);

    raw[0] = -1;
    VirtualButtonStates centered{};
    mapPovStates(centered, raw, 1, targets, 32);
    QVERIFY(!centered[16]);
    QVERIFY(!centered[17]);
}

void MappingCoreTests::povMappingsRejectDuplicatesAndRoundTrip()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &profile = activeProfile(configuration);
    profile.buttons.resize(1);
    profile.buttons[0] = {ButtonActionType::VirtualButton, 16, true};
    profile.povs.resize(1);
    profile.povs[0][static_cast<size_t>(povDirectionIndex(PovDirection::Up))] =
        {ButtonActionType::VirtualButton, 16, true};
    QVERIFY(hasPovMappingConflict(profile.buttons, profile.povs, 0,
                                  povDirectionIndex(PovDirection::Up), 16, 32));
    QVERIFY(!normalizePovMappings(profile.povs, profile.buttons, 32));
    QCOMPARE(profile.povs[0][static_cast<size_t>(povDirectionIndex(PovDirection::Up))].type,
             ButtonActionType::Disabled);

    profile.povs[0][static_cast<size_t>(povDirectionIndex(PovDirection::Right))] =
        {ButtonActionType::VirtualButton, 17, true};
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(activeProfile(restored).povs.size(), size_t{1});
    QCOMPARE(activeProfile(restored).povs[0][static_cast<size_t>(povDirectionIndex(PovDirection::Right))].target,
             17);

    QJsonObject v16 = ConfigStore::toJson(configuration);
    v16.insert(QStringLiteral("version"), 8);
    QJsonArray profiles = v16.value(QStringLiteral("profiles")).toArray();
    for (int index = 0; index < profiles.size(); ++index) {
        QJsonObject profileObject = profiles[index].toObject();
        profileObject.remove(QStringLiteral("povs"));
        profiles[index] = profileObject;
    }
    v16.insert(QStringLiteral("profiles"), profiles);
    const MapperConfiguration migrated = ConfigStore::fromJson(v16, &valid);
    QVERIFY(valid);
    QVERIFY(activeProfile(migrated).povs.empty());
}

void MappingCoreTests::legacyControlsMigrateToAutomationWithoutHiddenPaths()
{
    MapperConfiguration legacy = defaultConfiguration();
    QString alternateId;
    QVERIFY(createProfile(legacy, QStringLiteral("Landing"), normalProfileId(), &alternateId));
    legacy.mappingControls.resize(2);
    legacy.mappingControls[0] = MappingControlAction::ToggleMapping;
    legacy.profileTriggers.resize(2);
    legacy.profileTriggers[1] = {alternateId, ProfileTriggerMode::Hold};
    legacy.povProfileTriggers.resize(1);
    legacy.povProfileTriggers[0][static_cast<size_t>(povDirectionIndex(PovDirection::Right))] =
        {alternateId, ProfileTriggerMode::Toggle};

    QJsonObject v15 = ConfigStore::toJson(legacy);
    v15.insert(QStringLiteral("version"), 15);
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(v15, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.legacyControlMigrationWarning.isEmpty());
    QVERIFY(std::all_of(migrated.mappingControls.cbegin(), migrated.mappingControls.cend(),
                        [](MappingControlAction action) { return action == MappingControlAction::None; }));
    QVERIFY(std::all_of(migrated.profileTriggers.cbegin(), migrated.profileTriggers.cend(),
                        [](const ProfileTriggerBinding &binding) { return !profileTriggerBindingEnabled(binding); }));
    QVERIFY(!profileTriggerBindingEnabled(
        migrated.povProfileTriggers[0][static_cast<size_t>(povDirectionIndex(PovDirection::Right))]));

    QCOMPARE(static_cast<int>(migrated.automations.size()), 3);
    const auto byId = [&migrated](const QString &id) -> const AutomationDefinition * {
        const auto found = std::find_if(migrated.automations.cbegin(), migrated.automations.cend(),
            [&id](const AutomationDefinition &automation) { return automation.id == id; });
        return found == migrated.automations.cend() ? nullptr : &*found;
    };
    const AutomationDefinition *mapping = byId(QStringLiteral("migration-v16-mapping-button-1"));
    QVERIFY(mapping);
    QCOMPARE(mapping->conditions.front().type, AutomationConditionType::ButtonPressed);
    QCOMPARE(mapping->actions.front().type, AutomationActionType::ToggleMapping);
    const AutomationDefinition *profile = byId(QStringLiteral("migration-v16-profile-button-2"));
    QVERIFY(profile);
    QCOMPARE(profile->conditions.front().type, AutomationConditionType::ButtonHeld);
    QCOMPARE(profile->actions.front().type, AutomationActionType::ProfileHold);
    QCOMPARE(profile->actions.front().profileId, alternateId);
    const AutomationDefinition *pov = byId(QStringLiteral("migration-v16-profile-pov-1-3"));
    QVERIFY(pov);
    QCOMPARE(pov->conditions.front().type, AutomationConditionType::PovActive);
    QCOMPARE(pov->actions.front().type, AutomationActionType::ProfileToggle);
    QCOMPARE(pov->actions.front().profileId, alternateId);
}

void MappingCoreTests::eventLogIsBoundedAndOrdered()
{
    EventLog events(3);
    events.append(QStringLiteral("first"));
    events.append(QStringLiteral("second"));
    events.append(QStringLiteral("third"));
    events.append(QStringLiteral("fourth"));
    QCOMPARE(events.maximumEntries(), 3);
    QCOMPARE(events.entries(), QStringList({QStringLiteral("second"), QStringLiteral("third"),
                                             QStringLiteral("fourth")}));
}

void MappingCoreTests::inputLearningSelectsDeliberateAxisWithoutGuessing()
{
    std::array<float, kPhysicalAxisCount> baseline{};
    std::array<float, kPhysicalAxisCount> current{};
    std::array<bool, kPhysicalAxisCount> available{};
    std::array<PhysicalAxisActivity, kPhysicalAxisCount> activity{};
    available.fill(true);
    activity.fill(PhysicalAxisActivity::Active);

    current[0] = 0.03F;
    QCOMPARE(selectLearnedAxis(baseline, current, available, activity).result,
             AxisLearningResult::Waiting);

    current[0] = 0.74F;
    current[1] = 0.04F;
    const AxisLearningSelection selected = selectLearnedAxis(baseline, current, available, activity);
    QCOMPARE(selected.result, AxisLearningResult::Candidate);
    QCOMPARE(selected.axis, 0);

    current[1] = 0.57F;
    QCOMPARE(selectLearnedAxis(baseline, current, available, activity).result,
             AxisLearningResult::Ambiguous);

    activity[0] = PhysicalAxisActivity::Fixed;
    current[1] = 0.74F;
    const AxisLearningSelection fixedExcluded = selectLearnedAxis(baseline, current, available, activity);
    QCOMPARE(fixedExcluded.result, AxisLearningResult::Candidate);
    QCOMPARE(fixedExcluded.axis, 1);

    available[1] = false;
    QCOMPARE(selectLearnedAxis(baseline, current, available, activity).result,
             AxisLearningResult::Waiting);
}

void MappingCoreTests::inputLearningSelectsAReleasedThenPressedButton()
{
    std::array<bool, kMaximumPhysicalButtons> baseline{};
    std::array<bool, kMaximumPhysicalButtons> current{};
    std::array<bool, kMaximumPhysicalButtons> available{};
    available.fill(true);

    baseline[5] = true;
    current[5] = true;
    QCOMPARE(selectLearnedButton(baseline, current, available), 0);

    current[18] = true;
    QCOMPARE(selectLearnedButton(baseline, current, available), 19);

    available[18] = false;
    QCOMPARE(selectLearnedButton(baseline, current, available), 0);
}

void MappingCoreTests::physicalMonitorPublishesWhenMappingIsStoppedAndVJoyIsUnavailable()
{
    // PhysicalInputMonitor intentionally has no mapping/vJoy dependency. The
    // worker uses it before attempting virtual-device acquisition.
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    axes[static_cast<int>(PhysicalAxis::X)] = true;
    buttons[0] = true;
    monitor.configure(axes, buttons, 1);

    PhysicalInputReport report;
    report.axes[static_cast<int>(PhysicalAxis::X)] = 0.421F;
    report.buttons[0] = true;
    report.povs[0] = 9000;
    monitor.accept(report);

    QVERIFY(nearlyEqual(monitor.snapshot().axes[static_cast<int>(PhysicalAxis::X)], 0.421F));
    QVERIFY(monitor.snapshot().buttons[0]);
    QCOMPARE(monitor.snapshot().povs[0], 9000);
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{1});
}

void MappingCoreTests::physicalMonitorRetainsFullOfflineButtonCount()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    for (int index = 0; index < 15; ++index) buttons[index] = true;
    monitor.configure(axes, buttons, 0);

    QCOMPARE(static_cast<int>(std::count(monitor.availableButtons().begin(),
                                         monitor.availableButtons().end(), true)), 15);
    QVERIFY(defaultButtonMappings(15, 0).size() == 15);
}

void MappingCoreTests::physicalMonitorPropagatesPressReleaseAndDisconnect()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    buttons[4] = true;
    monitor.configure(axes, buttons, 0);

    PhysicalInputReport pressed;
    pressed.buttons[4] = true;
    monitor.accept(pressed);
    QVERIFY(monitor.snapshot().buttons[4]);
    QCOMPARE(monitor.snapshot().lastChangedButton, 5);

    monitor.accept({});
    QVERIFY(!monitor.snapshot().buttons[4]);
    QCOMPARE(monitor.snapshot().lastChangedButton, 5);

    monitor.disconnect();
    QVERIFY(std::none_of(monitor.availableButtons().begin(), monitor.availableButtons().end(),
                         [](bool available) { return available; }));
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{0});
}

void MappingCoreTests::physicalMonitorRecoversAfterReconnect()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    axes[static_cast<int>(PhysicalAxis::X)] = true;
    buttons[14] = true;
    monitor.configure(axes, buttons, 1);

    PhysicalInputReport beforeDisconnect;
    beforeDisconnect.axes[static_cast<int>(PhysicalAxis::X)] = -0.25F;
    beforeDisconnect.buttons[14] = true;
    monitor.accept(beforeDisconnect);
    monitor.disconnect();

    monitor.configure(axes, buttons, 1);
    PhysicalInputReport afterReconnect;
    afterReconnect.axes[static_cast<int>(PhysicalAxis::X)] = 0.75F;
    afterReconnect.buttons[14] = false;
    afterReconnect.povs[0] = 27000;
    monitor.accept(afterReconnect);

    QVERIFY(nearlyEqual(monitor.snapshot().axes[static_cast<int>(PhysicalAxis::X)], 0.75F));
    QVERIFY(!monitor.snapshot().buttons[14]);
    QCOMPARE(monitor.snapshot().povs[0], 27000);
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{1});
}

void MappingCoreTests::legacyConfigurationMigratesToNormalAndPrecision()
{
    MapperConfiguration legacy = defaultConfiguration();
    ControllerProfile &normal = activeProfile(legacy);
    normal.axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.08F;
    normal.axes[static_cast<int>(PhysicalAxis::Rz)].inverted = true;
    normal.buttons = defaultButtonMappings(15, 32);
    legacy.calibration[static_cast<int>(PhysicalAxis::X)] = {true, -0.8F, 0.1F, 0.9F};

    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacyConfigurationJson(legacy, 2), &valid);
    QVERIFY(valid);
    QCOMPARE(migrated.activeProfileId, normalProfileId());
    const ControllerProfile *migratedNormal = findProfile(migrated, normalProfileId());
    const ControllerProfile *precision = findProfile(migrated, precisionProfileId());
    QVERIFY(migratedNormal);
    QVERIFY(precision);
    QCOMPARE(migratedNormal->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.08F);
    QVERIFY(migratedNormal->axes[static_cast<int>(PhysicalAxis::Rz)].inverted);
    QCOMPARE(migratedNormal->buttons[14].target, 15);
    QCOMPARE(precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.08F);
    QCOMPARE(precision->buttons[14].target, 15);
    QVERIFY(migrated.calibration[static_cast<int>(PhysicalAxis::X)].enabled);
    QCOMPARE(migrated.calibration[static_cast<int>(PhysicalAxis::X)].center, 0.1F);
}

void MappingCoreTests::profileMigrationIsIdempotent()
{
    MapperConfiguration legacy = defaultConfiguration();
    activeProfile(legacy).axes[0].deadzone = 0.06F;
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacyConfigurationJson(legacy, 2), &valid);
    QVERIFY(valid);
    const MapperConfiguration reread = ConfigStore::fromJson(ConfigStore::toJson(migrated), &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(reread.profiles.size()), 2);
    QCOMPARE(reread.activeProfileId, normalProfileId());
    QCOMPARE(activeProfile(reread).axes[0].deadzone, 0.06F);
    QVERIFY(findProfile(reread, precisionProfileId()));
}

void MappingCoreTests::categoryMigrationPreservesExistingProfiles()
{
    MapperConfiguration source = defaultConfiguration();
    QVERIFY(activateProfile(source, precisionProfileId()));
    ControllerProfile *precision = findProfile(source, precisionProfileId());
    QVERIFY(precision);
    precision->buttons = defaultButtonMappings(2, 32);
    precision->povs.resize(1);
    precision->povs[0][0] = {ButtonActionType::VirtualButton, 9, true};
    PersonalCurvePreset curve;
    curve.id = QStringLiteral("legacy-migration-curve");
    curve.name = QStringLiteral("Legacy Migration Curve");
    curve.definition = linearCurveDefinition();
    source.personalCurvePresets.push_back(curve);
    precision->axes[static_cast<int>(PhysicalAxis::X)].curve.family = CurveFamily::Personal;
    precision->axes[static_cast<int>(PhysicalAxis::X)].curve.presetId = curve.id;
    setProfileTrigger(source, 4, precisionProfileId(), ProfileTriggerMode::Toggle);
    setPovProfileTrigger(source, 1, PovDirection::Up, precisionProfileId(), ProfileTriggerMode::Hold);
    AutomationDefinition automation;
    automation.id = QStringLiteral("legacy-profile-automation");
    automation.name = QStringLiteral("Legacy Profile Automation");
    automation.conditions.push_back({AutomationConditionType::Always, 0, 0.0F, 0.0F, 0.0F, 1, 1,
                                    PovDirection::Up, precisionProfileId()});
    automation.actions.push_back({AutomationActionType::ProfileToggle, 1, normalProfileId()});
    source.automations.push_back(automation);
    SavedControllerRecord controller;
    controller.id = QStringLiteral("migration-controller");
    controller.displayName = QStringLiteral("Migration HOTAS");
    controller.vjoyRequirements.deviceId = 1;
    controller.calibration[0] = {true, -0.83F, 0.02F, 0.94F};
    source.savedControllers.push_back(controller);
    source.activeControllerRecordId = controller.id;
    QJsonObject legacy = ConfigStore::toJson(source);
    legacy.insert(QStringLiteral("version"), 18);
    legacy.remove(QStringLiteral("profileCategories"));
    legacy.remove(QStringLiteral("automaticGameDetection"));
    QJsonArray profiles = legacy.value(QStringLiteral("profiles")).toArray();
    for (int index = 0; index < profiles.size(); ++index) {
        QJsonObject profile = profiles.at(index).toObject();
        profile.remove(QStringLiteral("categoryId"));
        profile.remove(QStringLiteral("enabled"));
        profiles[index] = profile;
    }
    legacy.insert(QStringLiteral("profiles"), profiles);

    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacy, &valid);
    QVERIFY(valid);
    QCOMPARE(migrated.profileCategories.size(), size_t{1});
    QCOMPARE(migrated.profileCategories.front().name, QStringLiteral("General"));
    QCOMPARE(migrated.profileCategories.front().profileIds.size(), source.profiles.size());
    QCOMPARE(migrated.activeProfileId, precisionProfileId());
    QCOMPARE(migrated.personalCurvePresets.size(), size_t{1});
    QCOMPARE(findProfile(migrated, precisionProfileId())->buttons.size(), size_t{2});
    QCOMPARE(findProfile(migrated, precisionProfileId())->povs.size(), size_t{1});
    QCOMPARE(migrated.profileTriggers[3].targetProfileId, precisionProfileId());
    QCOMPARE(migrated.povProfileTriggers[0][0].targetProfileId, precisionProfileId());
    QCOMPARE(migrated.automations.size(), size_t{1});
    QCOMPARE(migrated.savedControllers.size(), size_t{1});
    QCOMPARE(migrated.savedControllers.front().calibration[0].minimum, -0.83F);
    for (const ControllerProfile &profile : migrated.profiles) {
        QCOMPARE(profile.categoryId, generalProfileCategoryId());
        QVERIFY(profile.enabled);
    }
}

void MappingCoreTests::categoryScopedNamesAndStableReferencesSurviveMove()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString flightCategory;
    QVERIFY(createProfileCategory(configuration, QStringLiteral("Battlefield 6"), &flightCategory));
    QString copiedNormal;
    QVERIFY(duplicateProfileToCategory(configuration, normalProfileId(), QStringLiteral("Normal"),
                                       flightCategory, &copiedNormal));
    QVERIFY(findProfile(configuration, copiedNormal));
    setProfileTrigger(configuration, 7, precisionProfileId(), ProfileTriggerMode::Hold);
    QVERIFY(moveProfileToCategory(configuration, precisionProfileId(), flightCategory));
    QCOMPARE(configuration.profileTriggers[6].targetProfileId, precisionProfileId());
    QVERIFY(renameProfile(configuration, precisionProfileId(), QStringLiteral("Precision")));
    QCOMPARE(categoryProfileLabel(configuration, precisionProfileId()),
             QStringLiteral("Battlefield 6 / Precision"));
    findProfileCategory(configuration, flightCategory)->defaultProfileId = precisionProfileId();
    QVERIFY(activateCategoryProfile(configuration, flightCategory));
    QCOMPARE(configuration.activeProfileId, precisionProfileId());
    QCOMPARE(findProfileCategory(configuration, flightCategory)->lastActiveProfileId, precisionProfileId());
}

void MappingCoreTests::portableProfileRoundTripIsAtomicAndRemapsIds()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MapperConfiguration source = defaultConfiguration();
    QString category;
    QVERIFY(createProfileCategory(source, QStringLiteral("Battlefield 6"), &category));
    QString profileId;
    QVERIFY(createProfileInCategory(source, QStringLiteral("Helicopter"), category, precisionProfileId(), &profileId));
    findProfile(source, profileId)->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.21F;
    const QString fileName = temporary.filePath(QStringLiteral("helicopter.hbf6profile"));
    QString error;
    QVERIFY2(ProfilePortability::exportProfile(source, profileId, fileName, &error), qPrintable(error));

    PortableConfigurationBundle bundle;
    QVERIFY2(ProfilePortability::inspect(fileName, &bundle, &error), qPrintable(error));
    QVERIFY(bundle.kind == PortableConfigurationKind::Profile);
    QCOMPARE(bundle.profiles.size(), size_t{1});
    MapperConfiguration target = defaultConfiguration();
    QStringList warnings;
    QVERIFY2(ProfilePortability::apply(&target, bundle, {}, &warnings, &error), qPrintable(error));
    QCOMPARE(target.profiles.size(), size_t{3});
    const auto imported = std::find_if(target.profiles.cbegin(), target.profiles.cend(),
        [](const ControllerProfile &profile) { return profile.name == QStringLiteral("Helicopter"); });
    QVERIFY(imported != target.profiles.cend());
    QVERIFY(imported->id != profileId);
    QCOMPARE(imported->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.21F);
    bool valid = false;
    QVERIFY(ConfigStore::fromJson(ConfigStore::toJson(target), &valid).profiles.size() == target.profiles.size());
    QVERIFY(valid);
}

void MappingCoreTests::portablePackRoundTripPreservesCategoryAndSkipsHardwareByDefault()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MapperConfiguration source = defaultConfiguration();
    QString categoryId;
    QVERIFY(createProfileCategory(source, QStringLiteral("Battlefield 6"), &categoryId));
    QString profileId;
    QVERIFY(createProfileInCategory(source, QStringLiteral("Vehicle"), categoryId,
                                    normalProfileId(), &profileId));
    findProfileCategory(source, categoryId)->executableRules = {QStringLiteral("bf6.exe")};
    AdaptiveResponsePreset responsePreset;
    responsePreset.id = QStringLiteral("pack-response");
    responsePreset.name = QStringLiteral("Pack Response");
    responsePreset.description = QStringLiteral("Portable Adaptive Response dependency");
    for (AdaptiveResponseAxisOverride &axis : responsePreset.axes) {
        axis.properties = kAdaptiveResponseAllProperties;
        axis.settings.enabled = true;
        axis.settings.maximumHorizonMs = 12.0F;
    }
    source.adaptiveResponsePresets.push_back(responsePreset);
    findProfileCategory(source, categoryId)->adaptiveResponse.axes[0].presetId = responsePreset.id;
    AutomationDefinition responseAutomation;
    responseAutomation.id = QStringLiteral("pack-response-automation");
    responseAutomation.name = QStringLiteral("Pack Response Automation");
    responseAutomation.conditions.push_back({AutomationConditionType::BaseProfileIs, 0, 0.0F,
        0.0F, 0.0F, 1, 1, PovDirection::Up, profileId});
    AutomationActionDefinition responseAction;
    responseAction.type = AutomationActionType::AdaptiveResponsePreset;
    responseAction.targetAxis = static_cast<int>(PhysicalAxis::X);
    responseAction.adaptiveResponsePresetId = responsePreset.id;
    responseAutomation.actions.push_back(responseAction);
    source.automations.push_back(responseAutomation);
    source.calibration[static_cast<size_t>(PhysicalAxis::X)].minimum = -0.91F;

    const QString fileName = temporary.filePath(QStringLiteral("bf6.hbf6pack"));
    QString error;
    QVERIFY2(ProfilePortability::exportPack(source, {categoryId}, {}, QStringLiteral("BF6 Pack"),
                                             QStringLiteral("Portable vehicle setup"), false, false,
                                             true, true, true,
                                             fileName, &error), qPrintable(error));
    PortableConfigurationBundle bundle;
    QVERIFY2(ProfilePortability::inspect(fileName, &bundle, &error), qPrintable(error));
    QVERIFY(bundle.kind == PortableConfigurationKind::Pack);
    QVERIFY(!bundle.includesDevices);
    QVERIFY(!bundle.includesCalibration);
    QCOMPARE(bundle.categories.size(), size_t{1});
    QCOMPARE(bundle.profiles.size(), size_t{1});
    QCOMPARE(bundle.adaptiveResponsePresets.size(), size_t{1});
    QCOMPARE(bundle.automations.size(), size_t{1});

    MapperConfiguration target = defaultConfiguration();
    const float originalMinimum = target.calibration[static_cast<size_t>(PhysicalAxis::X)].minimum;
    QStringList warnings;
    QVERIFY2(ProfilePortability::apply(&target, bundle, {}, &warnings, &error), qPrintable(error));
    const auto category = std::find_if(target.profileCategories.cbegin(), target.profileCategories.cend(),
        [](const ProfileCategory &item) { return item.name == QStringLiteral("Battlefield 6"); });
    QVERIFY(category != target.profileCategories.cend());
    QCOMPARE(category->executableRules, QStringList{QStringLiteral("bf6.exe")});
    QVERIFY(findAdaptiveResponsePreset(target, QStringLiteral("pack-response")));
    QVERIFY(std::any_of(target.automations.cbegin(), target.automations.cend(),
        [](const AutomationDefinition &item) { return item.name == QStringLiteral("Pack Response Automation"); }));
    QCOMPARE(target.calibration[static_cast<size_t>(PhysicalAxis::X)].minimum, originalMinimum);
}

void MappingCoreTests::portablePackSelectionsConflictsAndDependenciesAreSafe()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MapperConfiguration source = defaultConfiguration();
    QString categoryId;
    QVERIFY(createProfileCategory(source, QStringLiteral("Battlefield 6"), &categoryId));
    QString groundId;
    QString flightId;
    QVERIFY(createProfileInCategory(source, QStringLiteral("Ground"), categoryId, normalProfileId(), &groundId));
    QVERIFY(createProfileInCategory(source, QStringLiteral("Flight"), categoryId, precisionProfileId(), &flightId));
    PersonalCurvePreset curve;
    curve.id = QStringLiteral("curve-landing");
    curve.name = QStringLiteral("Landing Curve");
    curve.definition = linearCurveDefinition();
    source.personalCurvePresets.push_back(curve);
    ControllerProfile *ground = findProfile(source, groundId);
    QVERIFY(ground);
    ground->axes[static_cast<int>(PhysicalAxis::X)].curve.family = CurveFamily::Personal;
    ground->axes[static_cast<int>(PhysicalAxis::X)].curve.presetId = curve.id;
    AutomationDefinition automation;
    automation.id = QStringLiteral("automation-ground-flight");
    automation.name = QStringLiteral("Ground to Flight");
    automation.conditions.push_back({AutomationConditionType::Always, 0, 0.0F, 0.0F, 0.0F, 1, 1,
                                    PovDirection::Up, groundId});
    automation.actions.push_back({AutomationActionType::ProfileToggle, 1, flightId});
    source.automations.push_back(automation);
    setProfileTrigger(source, 8, groundId, ProfileTriggerMode::Hold);

    const QString fileName = temporary.filePath(QStringLiteral("selected.hbf6pack"));
    QString error;
    QVERIFY2(ProfilePortability::exportPack(source, {}, {groundId}, QStringLiteral("Selected"), {}, false,
                                             false, true, true, true, fileName, &error), qPrintable(error));
    PortableConfigurationBundle bundle;
    QVERIFY2(ProfilePortability::inspect(fileName, &bundle, &error), qPrintable(error));
    // The chosen profile and its Automation target travel together; no
    // package-local relationship is emitted dangling.
    QCOMPARE(bundle.profiles.size(), size_t{2});
    QCOMPARE(bundle.curves.size(), size_t{1});
    QCOMPARE(bundle.automations.size(), size_t{1});

    MapperConfiguration mergeTarget = defaultConfiguration();
    QVERIFY(createProfileCategory(mergeTarget, QStringLiteral("Battlefield 6")));
    PortableImportOptions merge;
    merge.categoryConflictMode = PortableCategoryConflictMode::Merge;
    QStringList warnings;
    QVERIFY2(ProfilePortability::apply(&mergeTarget, bundle, merge, &warnings, &error), qPrintable(error));
    QCOMPARE(std::count_if(mergeTarget.profileCategories.cbegin(), mergeTarget.profileCategories.cend(),
        [](const ProfileCategory &category) { return category.name == QStringLiteral("Battlefield 6"); }), 1);
    QVERIFY(std::any_of(mergeTarget.automations.cbegin(), mergeTarget.automations.cend(),
        [](const AutomationDefinition &item) { return item.name == QStringLiteral("Ground to Flight"); }));

    MapperConfiguration newTarget = defaultConfiguration();
    QVERIFY(createProfileCategory(newTarget, QStringLiteral("Battlefield 6")));
    PortableImportOptions importAsNew;
    importAsNew.categoryConflictMode = PortableCategoryConflictMode::ImportAsNew;
    QVERIFY2(ProfilePortability::apply(&newTarget, bundle, importAsNew, &warnings, &error), qPrintable(error));
    QVERIFY(std::any_of(newTarget.profileCategories.cbegin(), newTarget.profileCategories.cend(),
        [](const ProfileCategory &category) { return category.name == QStringLiteral("Battlefield 6 (Imported)"); }));

    MapperConfiguration replaceTarget = defaultConfiguration();
    QString replaceCategory;
    QVERIFY(createProfileCategory(replaceTarget, QStringLiteral("Battlefield 6"), &replaceCategory));
    QString oldProfile;
    QVERIFY(createProfileInCategory(replaceTarget, QStringLiteral("Old"), replaceCategory,
                                    normalProfileId(), &oldProfile));
    PortableImportOptions replace;
    replace.categoryConflictMode = PortableCategoryConflictMode::Replace;
    QVERIFY2(ProfilePortability::apply(&replaceTarget, bundle, replace, &warnings, &error), qPrintable(error));
    QVERIFY(!findProfile(replaceTarget, oldProfile));
    const ProfileCategory *replaced = findProfileCategory(replaceTarget, replaceCategory);
    QVERIFY(replaced);
    QCOMPARE(replaced->profileIds.size(), size_t{2});

    // Pack exports include only the custom Response Presets actually reached
    // from selected content.  Import conflicts intentionally provide three
    // deterministic choices: retain the local preset, replace it, or copy and
    // remap every imported reference.
    AdaptiveResponsePreset responsePreset;
    responsePreset.id = QStringLiteral("pack-response");
    responsePreset.name = QStringLiteral("Pack Response");
    responsePreset.description = QStringLiteral("Pack-only dependency");
    responsePreset.axes[static_cast<size_t>(PhysicalAxis::X)].properties = kAdaptiveResponseAllProperties;
    responsePreset.axes[static_cast<size_t>(PhysicalAxis::X)].settings.maximumHorizonMs = 24.0F;
    source.adaptiveResponsePresets.push_back(responsePreset);
    ground = findProfile(source, groundId);
    QVERIFY(ground);
    ground->adaptiveResponse.axes[static_cast<size_t>(PhysicalAxis::X)].presetId = responsePreset.id;

    const QString responsePack = temporary.filePath(QStringLiteral("response-dependency.hbf6pack"));
    QVERIFY2(ProfilePortability::exportPack(source, {categoryId}, {}, QStringLiteral("Response dependency"), {},
                                             false, false, true, true, true, responsePack, &error), qPrintable(error));
    PortableConfigurationBundle responseBundle;
    QVERIFY2(ProfilePortability::inspect(responsePack, &responseBundle, &error), qPrintable(error));
    QCOMPARE(responseBundle.adaptiveResponsePresets.size(), size_t{1});
    QCOMPARE(responseBundle.adaptiveResponsePresets.front().id, responsePreset.id);

    AdaptiveResponsePreset localPreset = responsePreset;
    localPreset.name = QStringLiteral("Local Response");
    localPreset.axes[static_cast<size_t>(PhysicalAxis::X)].settings.maximumHorizonMs = 5.0F;
    const auto horizonFor = [](const MapperConfiguration &configuration, const QString &presetId) {
        const AdaptiveResponsePreset *preset = findAdaptiveResponsePreset(configuration, presetId);
        return preset ? preset->axes[static_cast<size_t>(PhysicalAxis::X)].settings.maximumHorizonMs : -1.0F;
    };

    MapperConfiguration keepLocalTarget = defaultConfiguration();
    keepLocalTarget.adaptiveResponsePresets.push_back(localPreset);
    PortableImportOptions keepLocal;
    keepLocal.adaptiveResponsePresetConflictMode = PortableAdaptiveResponsePresetConflictMode::KeepLocal;
    warnings.clear();
    QVERIFY2(ProfilePortability::apply(&keepLocalTarget, responseBundle, keepLocal, &warnings, &error), qPrintable(error));
    QCOMPARE(horizonFor(keepLocalTarget, responsePreset.id), 5.0F);

    MapperConfiguration replacePresetTarget = defaultConfiguration();
    replacePresetTarget.adaptiveResponsePresets.push_back(localPreset);
    PortableImportOptions replacePreset;
    replacePreset.adaptiveResponsePresetConflictMode = PortableAdaptiveResponsePresetConflictMode::Replace;
    warnings.clear();
    QVERIFY2(ProfilePortability::apply(&replacePresetTarget, responseBundle, replacePreset, &warnings, &error), qPrintable(error));
    QCOMPARE(horizonFor(replacePresetTarget, responsePreset.id), 24.0F);

    MapperConfiguration copyPresetTarget = defaultConfiguration();
    copyPresetTarget.adaptiveResponsePresets.push_back(localPreset);
    PortableImportOptions copyPreset;
    copyPreset.adaptiveResponsePresetConflictMode = PortableAdaptiveResponsePresetConflictMode::ImportAsCopy;
    warnings.clear();
    QVERIFY2(ProfilePortability::apply(&copyPresetTarget, responseBundle, copyPreset, &warnings, &error), qPrintable(error));
    QCOMPARE(horizonFor(copyPresetTarget, responsePreset.id), 5.0F);
    const auto copiedGround = std::find_if(copyPresetTarget.profiles.cbegin(), copyPresetTarget.profiles.cend(),
        [](const ControllerProfile &profile) { return profile.name == QStringLiteral("Ground"); });
    QVERIFY(copiedGround != copyPresetTarget.profiles.cend());
    const QString remappedPresetId = copiedGround->adaptiveResponse.axes[static_cast<size_t>(PhysicalAxis::X)].presetId;
    QVERIFY(remappedPresetId != responsePreset.id);
    QCOMPARE(horizonFor(copyPresetTarget, remappedPresetId), 24.0F);
}

void MappingCoreTests::portableDeviceMatchingAndCalibrationRequireExplicitIntent()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MapperConfiguration source = defaultConfiguration();
    QString categoryId;
    QVERIFY(createProfileCategory(source, QStringLiteral("Backup"), &categoryId));
    QString profileId;
    QVERIFY(createProfileInCategory(source, QStringLiteral("Travel"), categoryId, normalProfileId(), &profileId));
    SavedControllerRecord sourceRecord;
    sourceRecord.id = QStringLiteral("source-controller");
    sourceRecord.displayName = QStringLiteral("T.Flight Hotas One");
    sourceRecord.productGuid = QStringLiteral("PRODUCT-GUID");
    sourceRecord.vendorId = 1135;
    sourceRecord.productId = 47245;
    sourceRecord.axisCount = 5;
    sourceRecord.buttonCount = 12;
    sourceRecord.povCount = 1;
    sourceRecord.vjoyRequirements.deviceId = 1;
    sourceRecord.calibration[0] = {true, -0.82F, 0.04F, 0.91F};
    source.savedControllers.push_back(sourceRecord);
    source.activeControllerRecordId = sourceRecord.id;
    const QString fileName = temporary.filePath(QStringLiteral("hardware.hbf6pack"));
    QString error;
    QVERIFY2(ProfilePortability::exportPack(source, {categoryId}, {}, QStringLiteral("Hardware"), {}, true,
                                             true, true, true, true, fileName, &error), qPrintable(error));
    PortableConfigurationBundle bundle;
    QVERIFY2(ProfilePortability::inspect(fileName, &bundle, &error), qPrintable(error));
    QVERIFY(bundle.includesDevices);
    QVERIFY(bundle.includesCalibration);
    QCOMPARE(bundle.deviceDescriptors.size(), 1);

    MapperConfiguration target = defaultConfiguration();
    for (const QString &id : {QStringLiteral("local-a"), QStringLiteral("local-b")}) {
        SavedControllerRecord record = sourceRecord;
        record.id = id;
        record.calibration[0] = {true, -1.0F, 0.0F, 1.0F};
        target.savedControllers.push_back(record);
    }
    const QVariantMap preview = ProfilePortability::preview(bundle, target);
    const QVariantMap device = preview.value(QStringLiteral("devices")).toList().front().toMap();
    QCOMPARE(device.value(QStringLiteral("choices")).toList().size(), 2);
    QVERIFY(device.value(QStringLiteral("state")).toString().contains(QStringLiteral("USER SELECTION REQUIRED")));

    const QJsonObject before = ConfigStore::toJson(target);
    PortableImportOptions applyCalibration;
    applyCalibration.applyImportedCalibration = true;
    QVERIFY(!ProfilePortability::apply(&target, bundle, applyCalibration, nullptr, &error));
    QCOMPARE(ConfigStore::toJson(target), before);
    applyCalibration.deviceSelections.insert(0, QStringLiteral("local-a"));
    QVERIFY2(ProfilePortability::apply(&target, bundle, applyCalibration, nullptr, &error), qPrintable(error));
    const auto matched = std::find_if(target.savedControllers.cbegin(), target.savedControllers.cend(),
        [](const SavedControllerRecord &record) { return record.id == QStringLiteral("local-a"); });
    QVERIFY(matched != target.savedControllers.cend());
    QCOMPARE(matched->calibration[0].minimum, -0.82F);
    const auto untouched = std::find_if(target.savedControllers.cbegin(), target.savedControllers.cend(),
        [](const SavedControllerRecord &record) { return record.id == QStringLiteral("local-b"); });
    QVERIFY(untouched != target.savedControllers.cend());
    QCOMPARE(untouched->calibration[0].minimum, -1.0F);
}

void MappingCoreTests::portableFormatValidationRejectsFutureAndInvalidDependenciesAtomically()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MapperConfiguration source = defaultConfiguration();
    const QString fileName = temporary.filePath(QStringLiteral("normal.hbf6profile"));
    QString error;
    QVERIFY2(ProfilePortability::exportProfile(source, normalProfileId(), fileName, &error), qPrintable(error));
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject document = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    document.insert(QStringLiteral("schemaVersion"), 2);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(document).toJson());
    file.close();
    PortableConfigurationBundle future;
    QVERIFY(!ProfilePortability::inspect(fileName, &future, &error));
    QVERIFY(error.contains(QStringLiteral("newer unsupported format")));

    document.insert(QStringLiteral("schemaVersion"), 1);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(document).toJson());
    file.close();
    QJsonObject payload = document.value(QStringLiteral("payload")).toObject();
    QJsonArray profiles = payload.value(QStringLiteral("profiles")).toArray();
    QJsonObject profile = profiles.at(0).toObject();
    profile.insert(QStringLiteral("outputLayoutId"), QStringLiteral("missing-layout"));
    profiles[0] = profile;
    payload.insert(QStringLiteral("profiles"), profiles);
    document.insert(QStringLiteral("payload"), payload);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(document).toJson());
    file.close();
    MapperConfiguration target = defaultConfiguration();
    const QJsonObject before = ConfigStore::toJson(target);
    PortableConfigurationBundle invalidBundle;
    QVERIFY(!ProfilePortability::inspect(fileName, &invalidBundle, &error));
    QVERIFY(error.contains(QStringLiteral("vJoy contract")));
    QCOMPARE(ConfigStore::toJson(target), before);
}

void MappingCoreTests::gameCategoryDetectionIsPureLowFrequencyControlPlaneLogic()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString categoryId;
    QVERIFY(createProfileCategory(configuration, QStringLiteral("Battlefield 6"), &categoryId));
    QString helicopterId;
    QVERIFY(createProfileInCategory(configuration, QStringLiteral("Helicopter"), categoryId,
                                    normalProfileId(), &helicopterId));
    ProfileCategory *category = findProfileCategory(configuration, categoryId);
    QVERIFY(category);
    category->executableRules = {QStringLiteral("bf6.exe")};
    category->defaultProfileId = helicopterId;
    category->lastActiveProfileId = helicopterId;
    category->restoreLastProfile = true;
    const GameCategoryMatch match = categoryForForegroundExecutable(configuration, QStringLiteral("BF6.EXE"));
    QCOMPARE(match.categoryId, categoryId);
    QVERIFY(!match.ambiguous);
    QVERIFY(activateCategoryProfile(configuration, match.categoryId));
    QCOMPARE(configuration.activeProfileId, helicopterId);
    QString lastExecutable;
    QVERIFY(foregroundExecutableChanged(&lastExecutable, QStringLiteral("bf6.exe")));
    QVERIFY(!foregroundExecutableChanged(&lastExecutable, QStringLiteral("BF6.EXE")));
    QVERIFY(foregroundExecutableChanged(&lastExecutable, QStringLiteral("other.exe")));

    QString secondCategoryId;
    QVERIFY(createProfileCategory(configuration, QStringLiteral("Flight Simulator"), &secondCategoryId));
    QString secondProfileId;
    QVERIFY(createProfileInCategory(configuration, QStringLiteral("Airliner"), secondCategoryId,
                                    normalProfileId(), &secondProfileId));
    ProfileCategory *secondCategory = findProfileCategory(configuration, secondCategoryId);
    QVERIFY(secondCategory);
    secondCategory->executableRules = {QStringLiteral("BF6.EXE")};
    secondCategory->defaultProfileId = secondProfileId;
    // Basename matching ignores the installation path and chooses persisted
    // category order when several games match.
    const QStringList running = {QStringLiteral("C:/EA Games/Battlefield 6/bf6.exe")};
    QCOMPARE(categoryForRunningExecutables(configuration, running).categoryId, categoryId);
    // Once a matching category is active it wins the deterministic tie-break,
    // preventing a background game from making categories flap.
    QVERIFY(activateCategoryProfile(configuration, secondCategoryId));
    QCOMPARE(categoryForRunningExecutables(configuration, running, secondCategoryId).categoryId,
             secondCategoryId);
    QVERIFY(activateProfile(configuration, helicopterId)); // Manual in-category selection remains valid.
    // Creating the second category may grow the underlying vector, so reacquire
    // the first category instead of retaining the earlier pointer.
    findProfileCategory(configuration, categoryId)->enabled = false;
    secondCategory->enabled = false;
    QVERIFY(categoryForForegroundExecutable(configuration, QStringLiteral("bf6.exe")).categoryId.isEmpty());
}

void MappingCoreTests::profileCrudValidatesNamesAndProtectsNormal()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    QVERIFY(!helicopterId.isEmpty());
    QVERIFY(!createProfile(configuration, QStringLiteral(" helicopter ")));
    QVERIFY(!createProfile(configuration, QStringLiteral("   ")));
    QVERIFY(!renameProfile(configuration, normalProfileId(), QStringLiteral("Combat")));
    QVERIFY(renameProfile(configuration, helicopterId, QStringLiteral("Heli")));
    QString cloneId;
    QVERIFY(cloneProfile(configuration, helicopterId, &cloneId));
    QVERIFY(findProfile(configuration, cloneId));
    QVERIFY(!deleteProfile(configuration, normalProfileId()));
    QVERIFY(activateProfile(configuration, helicopterId));
    QVERIFY(!deleteProfile(configuration, helicopterId));
    QVERIFY(activateProfile(configuration, normalProfileId()));
    QVERIFY(deleteProfile(configuration, helicopterId));
    QVERIFY(!findProfile(configuration, helicopterId));
}

void MappingCoreTests::newProfileClonesRequestedSourceIndependently()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.14F;
    precision->axes[static_cast<int>(PhysicalAxis::Rz)].inverted = true;
    precision->buttons = defaultButtonMappings(6, 32);
    precision->buttons[2].target = 20;
    precision->povs.resize(1);
    precision->povs[0][0] = {ButtonActionType::VirtualButton, 21, true};

    QString createdId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), precisionProfileId(), &createdId));
    const ControllerProfile *created = findProfile(configuration, createdId);
    QVERIFY(created);
    QCOMPARE(created->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.14F);
    QVERIFY(created->axes[static_cast<int>(PhysicalAxis::Rz)].inverted);
    QCOMPARE(created->buttons[2].target, 20);
    QCOMPARE(created->povs[0][0].target, 21);

    ControllerProfile *mutableCreated = findProfile(configuration, createdId);
    QVERIFY(mutableCreated);
    mutableCreated->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.38F;
    mutableCreated->buttons[2].target = 6;
    mutableCreated->povs[0][0].target = 7;
    const ControllerProfile *sourceAfterInsert = findProfile(configuration, precisionProfileId());
    QVERIFY(sourceAfterInsert);
    QCOMPARE(sourceAfterInsert->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.14F);
    QCOMPARE(sourceAfterInsert->buttons[2].target, 20);
    QCOMPARE(sourceAfterInsert->povs[0][0].target, 21);
}

void MappingCoreTests::profilesRemainIsolatedAndPersist()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    ControllerProfile *helicopter = findProfile(configuration, helicopterId);
    QVERIFY(helicopter);
    helicopter->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.12F;
    helicopter->buttons = defaultButtonMappings(4, 32);
    helicopter->buttons[0].target = 10;
    QVERIFY(activateProfile(configuration, helicopterId));

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.activeProfileId, helicopterId);
    const ControllerProfile *restoredHelicopter = findProfile(restored, helicopterId);
    const ControllerProfile *normal = findProfile(restored, normalProfileId());
    QVERIFY(restoredHelicopter);
    QVERIFY(normal);
    QCOMPARE(restoredHelicopter->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.12F);
    QCOMPARE(restoredHelicopter->buttons[0].target, 10);
    QCOMPARE(normal->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.03F);
    QCOMPARE(normal->buttons.size(), size_t{0});
}

void MappingCoreTests::profileSwitchCompilesCompleteAxisConfiguration()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.08F;
    precision->axes[static_cast<int>(PhysicalAxis::Rz)].target = VirtualAxis::Disabled;

    const RuntimeMappingConfiguration normalRuntime = compileActiveProfile(configuration);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration precisionRuntime = compileActiveProfile(configuration);
    QCOMPARE(normalRuntime.axes[static_cast<int>(PhysicalAxis::X)].profile.deadzone, 0.03F);
    QCOMPARE(precisionRuntime.axes[static_cast<int>(PhysicalAxis::X)].profile.deadzone, 0.08F);
    QCOMPARE(normalRuntime.axes[static_cast<int>(PhysicalAxis::Rz)].profile.target, VirtualAxis::Rz);
    QCOMPARE(precisionRuntime.axes[static_cast<int>(PhysicalAxis::Rz)].profile.target, VirtualAxis::Disabled);
    QVERIFY(nearlyEqual(transformAxis(0.50F, normalRuntime.axes[0]), 0.484536F));
    QVERIFY(nearlyEqual(transformAxis(0.50F, precisionRuntime.axes[0]), 0.456522F));
}

void MappingCoreTests::profileSwitchReevaluatesHeldButtons()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &normal = activeProfile(configuration);
    normal.buttons.resize(5);
    normal.buttons[4] = {ButtonActionType::VirtualButton, 5};
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->buttons.resize(5);
    precision->buttons[4] = {ButtonActionType::VirtualButton, 10};
    PhysicalButtonStates held{};
    held[4] = true;

    const RuntimeMappingConfiguration before = compileActiveProfile(configuration);
    const VirtualButtonStates beforeStates = mapButtonStates(
        held, buildRuntimeButtonTargets(before.buttons, 32), 32);
    QVERIFY(beforeStates[5]);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration after = compileActiveProfile(configuration);
    const VirtualButtonStates afterStates = mapButtonStates(
        held, buildRuntimeButtonTargets(after.buttons, 32), 32);
    QVERIFY(!afterStates[5]);
    QVERIFY(afterStates[10]);
}

void MappingCoreTests::calibrationRemainsGlobalAcrossProfiles()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.calibration[static_cast<int>(PhysicalAxis::X)] = {true, -0.75F, 0.05F, 0.90F};
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.10F;
    const RuntimeMappingConfiguration normal = compileActiveProfile(configuration);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration precisionRuntime = compileActiveProfile(configuration);
    QVERIFY(normal.axes[0].calibration.enabled);
    QCOMPARE(normal.axes[0].calibration.center, 0.05F);
    QCOMPARE(precisionRuntime.axes[0].calibration.center, 0.05F);
    QCOMPARE(precisionRuntime.axes[0].profile.deadzone, 0.10F);
}

void MappingCoreTests::malformedProfileConfigurationFallsBackSafely()
{
    QJsonObject malformed = ConfigStore::toJson(defaultConfiguration());
    malformed.insert(QStringLiteral("profiles"), QStringLiteral("not an array"));
    bool valid = true;
    const MapperConfiguration restored = ConfigStore::fromJson(malformed, &valid);
    QVERIFY(!valid);
    QCOMPARE(restored.activeProfileId, normalProfileId());
    QVERIFY(findProfile(restored, normalProfileId()));
    QVERIFY(findProfile(restored, precisionProfileId()));
}

void MappingCoreTests::profileTriggerConfigurationRoundTripsAndMigrates()
{
    MapperConfiguration configuration = defaultConfiguration();
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);
    QJsonObject json = ConfigStore::toJson(configuration);
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 21);

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(json, &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(restored.profileTriggers.size()), 5);
    QCOMPARE(restored.profileTriggers[4].targetProfileId, precisionProfileId());
    QCOMPARE(restored.profileTriggers[4].mode, ProfileTriggerMode::Hold);

    json.insert(QStringLiteral("version"), 7);
    json.remove(QStringLiteral("profileTriggers"));
    const MapperConfiguration migrated = ConfigStore::fromJson(json, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.profileTriggers.empty());
    QCOMPARE(static_cast<int>(migrated.profiles.size()), static_cast<int>(configuration.profiles.size()));
}

void MappingCoreTests::holdProfileTriggerSelectsPrecompiledRuntimeAndConsumesButton()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &normal = activeProfile(configuration);
    normal.buttons = defaultButtonMappings(6, 32);
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->buttons = normal.buttons;
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.50F;
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QCOMPARE(static_cast<int>(cache.profiles.size()), 2);
    const std::uint64_t compileCount = responseCurveCompileCount();
    ProfileTriggerRuntime triggerRuntime;
    PhysicalButtonStates buttons{};
    triggerRuntime.initializeForMapping(cache, buttons);

    buttons[4] = true;
    const EffectiveProfileSelection held = triggerRuntime.processReport(cache, buttons);
    QCOMPARE(held.profileIndex, profileIndexFor(configuration, precisionProfileId()));
    QCOMPARE(held.sourceButton, 5);
    QCOMPARE(held.sourceMode, ProfileTriggerMode::Hold);
    QCOMPARE(responseCurveCompileCount(), compileCount);
    QVERIFY(std::abs(transformAxis(0.45F, cache.profiles[static_cast<size_t>(cache.baseProfileIndex)].axes[0])
                     - transformAxis(0.45F, cache.profiles[static_cast<size_t>(held.profileIndex)].axes[0])) > 0.1F);

    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(
        cache.profiles[static_cast<size_t>(held.profileIndex)].buttons, 32, cache.profileTriggers);
    buttons[0] = true;
    const VirtualButtonStates output = mapButtonStates(buttons, targets, 32);
    QVERIFY(output[1]);
    QVERIFY(!output[5]); // Button 5's saved game route is consumed, not deleted.

    buttons[4] = false;
    QCOMPARE(triggerRuntime.processReport(cache, buttons).profileIndex, cache.baseProfileIndex);
}

void MappingCoreTests::multipleHoldProfileTriggersUseMostRecentPress()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);
    setProfileTrigger(configuration, 6, helicopterId, ProfileTriggerMode::Hold);
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    runtime.initializeForMapping(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    buttons[5] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, helicopterId));
    buttons[5] = false;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    buttons[4] = false;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, cache.baseProfileIndex);
}

void MappingCoreTests::toggleProfileTriggerUsesRisingEdgesAndActivationOrder()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Toggle);
    setProfileTrigger(configuration, 6, helicopterId, ProfileTriggerMode::Toggle);
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    runtime.initializeForMapping(cache, buttons);

    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    buttons[4] = false;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    buttons[5] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, helicopterId));
    buttons[5] = false;
    runtime.processReport(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, helicopterId));
    buttons[4] = false;
    runtime.processReport(cache, buttons);
    buttons[5] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, cache.baseProfileIndex);
}

void MappingCoreTests::holdOverridesToggleAndManualBaseChangeClearsToggle()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);
    setProfileTrigger(configuration, 6, helicopterId, ProfileTriggerMode::Toggle);
    RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    runtime.initializeForMapping(cache, buttons);
    buttons[5] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, helicopterId));
    buttons[5] = false;
    runtime.processReport(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));

    QVERIFY(activateProfile(configuration, helicopterId));
    cache = compileRuntimeProfileCache(configuration);
    runtime.reconcileConfiguration(cache, buttons, true);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, profileIndexFor(configuration, precisionProfileId()));
    buttons[4] = false;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, helicopterId));
    runtime.reset(); // Stop Mapping clears both runtime-only override forms.
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, profileIndexFor(configuration, helicopterId));
}

void MappingCoreTests::profileTriggerConfigChangesReconcileHeldState()
{
    MapperConfiguration configuration = defaultConfiguration();
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);
    RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    runtime.initializeForMapping(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));

    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Toggle);
    cache = compileRuntimeProfileCache(configuration);
    runtime.reconcileConfiguration(cache, buttons, false);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, cache.baseProfileIndex);
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, cache.baseProfileIndex);
    buttons[4] = false;
    runtime.processReport(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));
}

void MappingCoreTests::missingAndRenamedProfileTriggerTargetsAreSafe()
{
    MapperConfiguration configuration = defaultConfiguration();
    setProfileTrigger(configuration, 5, precisionProfileId(), ProfileTriggerMode::Hold);
    QVERIFY(renameProfile(configuration, precisionProfileId(), QStringLiteral("Fine Aim")));
    RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QCOMPARE(cache.profileTriggers[4].targetProfileIndex, profileIndexFor(configuration, precisionProfileId()));

    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    runtime.initializeForMapping(cache, buttons);
    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons).profileIndex, profileIndexFor(configuration, precisionProfileId()));

    QVERIFY(activateProfile(configuration, normalProfileId()));
    QVERIFY(deleteProfile(configuration, precisionProfileId()));
    cache = compileRuntimeProfileCache(configuration);
    QVERIFY(!cache.profileTriggers[4].consumesInput);
    QCOMPARE(cache.profileTriggers[4].targetProfileIndex, -1);
    runtime.reconcileConfiguration(cache, buttons, false);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, cache.baseProfileIndex);
}

void MappingCoreTests::povProfileControlsShareGlobalPrecedenceAndConsumeDirectionRoute()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    ControllerProfile &normal = activeProfile(configuration);
    normal.povs.resize(1);
    normal.povs[0][static_cast<size_t>(povDirectionIndex(PovDirection::Up))] =
        {ButtonActionType::VirtualButton, 16, true};
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->povs = normal.povs;

    setProfileTrigger(configuration, 5, helicopterId, ProfileTriggerMode::Toggle);
    setPovProfileTrigger(configuration, 1, PovDirection::Up, precisionProfileId(),
                         ProfileTriggerMode::Hold);
    setPovProfileTrigger(configuration, 1, PovDirection::UpRight, helicopterId,
                         ProfileTriggerMode::Toggle);
    configuration.nativePovBindings.resize(1);
    configuration.nativePovBindings[0] = {true, NativePovTargetType::Continuous, 1};

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QVERIFY(cache.povProfileTriggers[0][0].consumesInput);
    QCOMPARE(cache.nativePovBindings[0].targetIndex, 1);
    const RuntimePovTargets targets = buildRuntimePovTargets(normal.povs, 32,
                                                              cache.povProfileTriggers);
    QCOMPARE(targets[0][0], 0); // Saved Up -> vJoy 16 route is consumed, not deleted.

    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    PhysicalPovValues povs{};
    povs.fill(-1);
    runtime.initializeForMapping(cache, buttons, povs, 1);

    buttons[4] = true;
    QCOMPARE(runtime.processReport(cache, buttons, povs, 1).profileIndex,
             profileIndexFor(configuration, helicopterId));
    buttons[4] = false;
    runtime.processReport(cache, buttons, povs, 1);

    povs[0] = 0; // Up Hold takes precedence over the active Button Toggle.
    const EffectiveProfileSelection up = runtime.processReport(cache, buttons, povs, 1);
    QCOMPARE(up.profileIndex, profileIndexFor(configuration, precisionProfileId()));
    QCOMPARE(up.sourcePovHat, 1);
    QCOMPARE(up.sourcePovDirection, 0);

    povs[0] = 4500; // Up releases and Up-Right gets one distinct entry edge.
    QCOMPARE(runtime.processReport(cache, buttons, povs, 1).profileIndex,
             profileIndexFor(configuration, helicopterId));
    povs[0] = -1;
    runtime.processReport(cache, buttons, povs, 1);
    povs[0] = 4500;
    QCOMPARE(runtime.processReport(cache, buttons, povs, 1).profileIndex,
             profileIndexFor(configuration, helicopterId));
}

void MappingCoreTests::povProfileAndNativePovConfigurationRoundTripWithSafeMigration()
{
    MapperConfiguration configuration = defaultConfiguration();
    setPovProfileTrigger(configuration, 1, PovDirection::Right, precisionProfileId(),
                         ProfileTriggerMode::Toggle);
    configuration.nativePovBindings.resize(1);
    configuration.nativePovBindings[0] = {true, NativePovTargetType::Discrete, 2};

    QJsonObject json = ConfigStore::toJson(configuration);
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 21);
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(json, &valid);
    QVERIFY(valid);
    QCOMPARE(restored.povProfileTriggers[0][2].targetProfileId, precisionProfileId());
    QCOMPARE(restored.povProfileTriggers[0][2].mode, ProfileTriggerMode::Toggle);
    QCOMPARE(restored.nativePovBindings[0].targetType, NativePovTargetType::Discrete);
    QCOMPARE(restored.nativePovBindings[0].targetIndex, 2);

    json.insert(QStringLiteral("version"), 9);
    json.remove(QStringLiteral("povProfileTriggers"));
    json.remove(QStringLiteral("nativePovBindings"));
    const MapperConfiguration migrated = ConfigStore::fromJson(json, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.povProfileTriggers.empty());
    QVERIFY(migrated.nativePovBindings.empty());
}

#define HOTAS_MAPPING_BENCHMARK_EMBEDDED
#include "mapping_hot_path_benchmark.cpp"
#undef HOTAS_MAPPING_BENCHMARK_EMBEDDED

int main(int argc, char *argv[])
{
    if (argc > 1 && QByteArray(argv[1]) == "--hot-path-benchmark") {
        return runMappingHotPathBenchmark(argc - 1, argv + 1);
    }
    MappingCoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mapping_core_tests.moc"
