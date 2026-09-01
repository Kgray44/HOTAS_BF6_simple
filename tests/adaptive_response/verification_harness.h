#pragma once

#include "adaptive_response.h"

#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hotas::verification {

inline constexpr int kAdaptiveVerificationSchemaVersion = 2;
inline constexpr int kAdaptiveScenarioCatalogVersion = 2;
inline constexpr std::uint32_t kDefaultMasterSeed = 0xBFA62300U;

enum class NoiseModel {
    None,
    WhiteJitter,
    Quantized,
    PositiveSpike,
    OppositeSpike,
    Burst,
    Drift,
};

struct ControlPoint {
    float timeSeconds = 0.0F;
    float position = 0.0F;
};

struct ScenarioDefinition {
    std::string id;
    // The stable physical trajectory identity intentionally excludes sampling
    // rate, allowing rate variants to be paired scientifically.
    std::string trajectoryId;
    std::string family;
    std::vector<ControlPoint> points;
    float durationSeconds = 1.0F;
    int mapperRateHz = 250;
    int sourceRateHz = 0;
    NoiseModel noise = NoiseModel::None;
    float noiseAmplitude = 0.0F;
    bool unipolar = false;
    bool variableDt = false;
    bool retainTrace = false;
    bool adversarialPiecewise = false;
    std::uint32_t seed = 0;
};

struct TraceSample {
    float timeSeconds = 0.0F;
    float intended = 0.0F;
    float physical = 0.0F;
    float velocity = 0.0F;
    float dtSeconds = 0.0F;
    bool intendedMoving = false;
    bool sourceSampleUpdated = false;
    float sourceSampleTimeSeconds = 0.0F;
    bool targetArrival = false;
    bool physicalStop = false;
    bool trueReversal = false;
};

struct ScenarioMetrics {
    std::uint64_t samples = 0;
    std::uint64_t trueReversals = 0;
    std::uint64_t detectedReversals = 0;
    std::uint64_t falseReversals = 0;
    std::uint64_t missedReversals = 0;
    std::uint64_t dropouts = 0;
    double dropoutTotalMs = 0.0;
    double longestDropoutMs = 0.0;
    std::uint64_t falseStops = 0;
    double falseStopTotalMs = 0.0;
    std::uint64_t stableChatter = 0;
    std::uint64_t nonFinite = 0;
    std::uint64_t illegalOutput = 0;
    std::uint64_t stationaryDrift = 0;
    double rmsLead = 0.0;
    double meanLead = 0.0;
    double medianLead = 0.0;
    double p95Lead = 0.0;
    double peakLead = 0.0;
    double meanHorizonMs = 0.0;
    double medianHorizonMs = 0.0;
    double p95HorizonMs = 0.0;
    double peakHorizonMs = 0.0;
    double meanConfidence = 0.0;
    double wrongDirectionLeadArea = 0.0;
    double noiseRms = 0.0;
    double predictedNoiseRms = 0.0;
    double noiseAmplificationRatio = 0.0;
    double stationaryLeadRms = 0.0;
    double stationaryLeadPeak = 0.0;
    double maximumOutputStep = 0.0;
    double physicalStopTimeMs = -1.0;
    double stopRecognitionMs = -1.0;
    double settlingMs = -1.0;
    double leadAtPhysicalStop = 0.0;
    double horizonAtPhysicalStopMs = 0.0;
    double residualVelocityAtStop = 0.0;
    double residualAccelerationAtStop = 0.0;
    double reversalLatencyMs = -1.0;
    double motionRecognitionLatencyMs = -1.0;
    double predictionActivationLatencyMs = -1.0;
    double staleLeadCancellationMs = -1.0;
    double oppositeDirectionReacquisitionMs = -1.0;
    double targetOvershootPeak = 0.0;
    double targetOvershootArea = 0.0;
    double targetOvershootDurationMs = 0.0;
    std::uint64_t sourceUpdateCount = 0;
    double effectiveSourceRateHz = 0.0;
    double sourceCadenceErrorMs = 0.0;
    double confidenceOscillation = 0.0;
    double horizonOscillationMs = 0.0;
    std::array<double, 8> stateDurationMs{};
    std::array<std::uint64_t, 8> stateTransitions{};
};

struct ReversalEvent {
    float groundTruthTimeSeconds = 0.0F;
    float detectedTimeSeconds = -1.0F;
    float staleLeadCancellationTimeSeconds = -1.0F;
    float reacquisitionTimeSeconds = -1.0F;
    float peakStaleLead = 0.0F;
    double wrongDirectionLeadArea = 0.0;
    bool missed = false;
    bool surroundedByFalseDetection = false;
};

struct RecordedHotasTrace {
    std::vector<std::uint64_t> timestampsUs;
    std::vector<std::array<float, 8>> axes;
};

struct ScenarioResult {
    ScenarioDefinition scenario;
    std::string configuration;
    ScenarioMetrics metrics;
    std::vector<ReversalEvent> reversalEvents;
    std::vector<std::string> failures;
    std::vector<TraceSample> trace;
    std::vector<AdaptiveResponseTelemetry> telemetry;
    // This result was deterministically replayed after aggregation because it
    // ranked in a bounded forensic category. Kept separate from authored
    // retainTrace so large campaigns stay bounded while still preserving the
    // evidence that led the report.
    bool retainedForWorstCase = false;
    bool retainedForFailure = false;
};

std::uint32_t deriveSeed(std::uint32_t masterSeed, const std::string &campaign,
                         const std::string &family, std::uint32_t scenarioIndex,
                         const std::string &configuration);
std::vector<ScenarioDefinition> canonicalScenarioCatalog(std::uint32_t masterSeed);
std::vector<TraceSample> generateTrace(const ScenarioDefinition &scenario);
bool writeRecordedHotasTraceCsv(const QString &path, const RecordedHotasTrace &trace, QString *error = nullptr);
bool readRecordedHotasTraceCsv(const QString &path, RecordedHotasTrace *trace, QString *error = nullptr);
ScenarioDefinition replayScenarioFromRecordedAxis(const RecordedHotasTrace &trace, int axis,
                                                  const std::string &id, bool unipolar = false);
ScenarioResult replayScenario(const ScenarioDefinition &scenario,
                              const RuntimeAdaptiveResponseConfig &configuration,
                              const std::string &configurationName,
                              bool retainSamples = true);

bool selfValidate(QStringList *failures = nullptr);
bool compareCampaignDirectories(const QString &baselineDirectory, const QString &candidateDirectory,
                                const QString &outputPath, QString *error = nullptr);
int runAdaptiveResponseVerification(int argc, char *argv[]);

} // namespace hotas::verification
