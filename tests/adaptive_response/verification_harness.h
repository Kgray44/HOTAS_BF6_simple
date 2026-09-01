#pragma once

#include "adaptive_response.h"

#include <cstdint>
#include <string>
#include <vector>

class QString;
class QStringList;

namespace hotas::verification {

inline constexpr int kAdaptiveVerificationSchemaVersion = 1;
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
    std::uint32_t seed = 0;
};

struct TraceSample {
    float timeSeconds = 0.0F;
    float intended = 0.0F;
    float physical = 0.0F;
    float velocity = 0.0F;
    float dtSeconds = 0.0F;
    bool intendedMoving = false;
};

struct ScenarioMetrics {
    std::uint64_t samples = 0;
    std::uint64_t trueReversals = 0;
    std::uint64_t detectedReversals = 0;
    std::uint64_t falseReversals = 0;
    std::uint64_t missedReversals = 0;
    std::uint64_t dropouts = 0;
    std::uint64_t falseStops = 0;
    std::uint64_t stableChatter = 0;
    std::uint64_t nonFinite = 0;
    std::uint64_t illegalOutput = 0;
    std::uint64_t stationaryDrift = 0;
    double rmsLead = 0.0;
    double peakLead = 0.0;
    double meanHorizonMs = 0.0;
    double peakHorizonMs = 0.0;
    double meanConfidence = 0.0;
    double wrongDirectionLeadArea = 0.0;
    double noiseRms = 0.0;
    double predictedNoiseRms = 0.0;
    double maximumOutputStep = 0.0;
    double stopRecognitionMs = -1.0;
    double settlingMs = -1.0;
    double reversalLatencyMs = -1.0;
};

struct ScenarioResult {
    ScenarioDefinition scenario;
    std::string configuration;
    ScenarioMetrics metrics;
    std::vector<std::string> failures;
    std::vector<TraceSample> trace;
    std::vector<AdaptiveResponseTelemetry> telemetry;
};

std::uint32_t deriveSeed(std::uint32_t masterSeed, const std::string &campaign,
                         const std::string &family, std::uint32_t scenarioIndex,
                         const std::string &configuration);
std::vector<ScenarioDefinition> canonicalScenarioCatalog(std::uint32_t masterSeed);
std::vector<TraceSample> generateTrace(const ScenarioDefinition &scenario);
ScenarioResult replayScenario(const ScenarioDefinition &scenario,
                              const RuntimeAdaptiveResponseConfig &configuration,
                              const std::string &configurationName);

bool selfValidate(QStringList *failures = nullptr);
bool compareCampaignDirectories(const QString &baselineDirectory, const QString &candidateDirectory,
                                const QString &outputPath, QString *error = nullptr);
int runAdaptiveResponseVerification(int argc, char *argv[]);

} // namespace hotas::verification
