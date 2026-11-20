#include "verification_harness.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTextStream>

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint32_t kHoldoutSeed = 0x23A10003U;
constexpr const char *kExpectedHash = "6c4e742eb17771d3310d25e336b7267fe1d80b54bb638ea2a448a30c803f63db";
constexpr size_t kExpectedScenarioCount = 100U;

QString corpusHash(const std::vector<hotas::verification::ScenarioDefinition> &scenarios)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &scenario : scenarios) {
        const auto trace = hotas::verification::generateTrace(scenario);
        hash.addData(scenario.id.data(), static_cast<qsizetype>(scenario.id.size()));
        for (const auto &sample : trace) {
            hash.addData(reinterpret_cast<const char *>(&sample.timeSeconds), sizeof(sample.timeSeconds));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

std::vector<hotas::verification::ScenarioDefinition> randomizedHoldout(std::uint32_t seed)
{
    using hotas::verification::NoiseModel;
    using hotas::verification::ScenarioDefinition;
    std::vector<ScenarioDefinition> result;
    size_t index = 0;
    for (const std::string &persona : {"precision-pilot", "combat-helicopter", "fixed-wing",
             "space-sim", "noisy-older-sensor"}) {
        for (int sample = 0; sample < 20; ++sample) {
            ScenarioDefinition item;
            item.id = "randomized/" + persona + "-" + std::to_string(sample);
            item.trajectoryId = item.id;
            item.family = "persona";
            item.durationSeconds = persona == "combat-helicopter" ? 3.0F : 5.0F;
            item.mapperRateHz = 250;
            item.sourceRateHz = 250;
            item.seed = hotas::verification::deriveSeed(seed, "randomized", persona,
                static_cast<std::uint32_t>(index++), "trajectory");
            if (persona == "noisy-older-sensor") {
                item.noise = NoiseModel::Quantized;
                item.noiseAmplitude = .005F;
                item.variableDt = sample % 2 == 0;
            }
            result.push_back(std::move(item));
        }
    }
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const auto scenarios = randomizedHoldout(kHoldoutSeed);
    const QString actualHash = corpusHash(scenarios);
    QTextStream out(stdout);
    out << "seed=0x23A10003 scenarios=" << scenarios.size()
        << " sha256=" << actualHash << '\n';
    if (scenarios.size() != kExpectedScenarioCount || actualHash != QString::fromLatin1(kExpectedHash)) {
        QTextStream(stderr) << "HOLDOUT_IDENTITY_FAILURE expected_scenarios="
                            << kExpectedScenarioCount << " expected_sha256=" << kExpectedHash << '\n';
        return 1;
    }
    out << "HOLDOUT_IDENTITY_VERIFIED\n";
    return 0;
}
