#include "verification_harness.h"
#include "physical_trace_io.h"

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>

using namespace Qt::StringLiterals;

class AdaptiveResponseVerificationTests final : public QObject {
    Q_OBJECT

private slots:
    void harnessSelfValidation();
    void sameSeedProducesSameTrace();
    void adaptiveOffIsIdentity();
    void sampleHoldRetainsContinuousMotionEvidence();
    void physicalTraceRoundTripPreservesRecordedCadence();
};

void AdaptiveResponseVerificationTests::harnessSelfValidation()
{
    QStringList failures;
    QVERIFY2(hotas::verification::selfValidate(&failures), qPrintable(failures.join('\n')));
}

void AdaptiveResponseVerificationTests::sameSeedProducesSameTrace()
{
    const auto catalog = hotas::verification::canonicalScenarioCatalog(0xBFA62300U);
    const auto first = hotas::verification::generateTrace(catalog.back());
    const auto second = hotas::verification::generateTrace(catalog.back());
    QCOMPARE(first.size(), second.size());
    for (size_t index = 0; index < first.size(); ++index) {
        QCOMPARE(first[index].physical, second[index].physical);
        QCOMPARE(first[index].timeSeconds, second[index].timeSeconds);
    }
}

void AdaptiveResponseVerificationTests::adaptiveOffIsIdentity()
{
    hotas::verification::ScenarioDefinition scenario;
    scenario.id = "self/off";
    scenario.family = "self";
    scenario.points = {{0.0F, -0.8F}, {0.5F, 0.6F}, {1.0F, 0.6F}};
    scenario.durationSeconds = 1.0F;
    hotas::RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = false;
    const auto result = hotas::verification::replayScenario(scenario, configuration, "Off");
    QVERIFY(result.failures.empty());
    QCOMPARE(result.metrics.peakLead, 0.0);
    QCOMPARE(result.metrics.peakHorizonMs, 0.0);
}

void AdaptiveResponseVerificationTests::sampleHoldRetainsContinuousMotionEvidence()
{
    hotas::verification::ScenarioDefinition scenario;
    scenario.id = "self/sample-hold";
    scenario.family = "sample-hold";
    scenario.points = {{0.0F, 0.7F}, {1.0F, 0.2F}, {1.5F, 0.2F}};
    scenario.durationSeconds = 1.5F;
    scenario.mapperRateHz = 250;
    scenario.sourceRateHz = 60;
    hotas::RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = hotas::AdaptiveResponseModel::Auto;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.motionSensitivity = 0.010F;
    const auto result = hotas::verification::replayScenario(scenario, configuration, "Auto");
    QVERIFY(result.metrics.peakLead > 0.0002);
}

void AdaptiveResponseVerificationTests::physicalTraceRoundTripPreservesRecordedCadence()
{
    using namespace hotas::verification;
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    PhysicalAxisTrace physical;
    physical.captureLabel = u"physical-fixture"_qs;
    physical.selectedAxis = 0;
    physical.selectedAxisLabel = u"x"_qs;
    physical.calibrationEnabled = true;
    physical.directInputRangeMinimum = 0;
    physical.directInputRangeMaximum = 65535;
    physical.rawNormalizationMinimum = 0;
    physical.rawNormalizationMaximum = 65535;
    physical.samples = {
        {1000, 0.00F, 0.00F, true, true, 0, 0.0, 0},
        {5500, 0.25F, 0.20F, true, true, 4500, 222.222222, 16384},
        {12000, 0.50F, 0.45F, true, true, 6500, 153.846154, 32768},
        {21000, 0.50F, 0.45F, true, false, 9000, 111.111111, 32768},
    };
    const QString path = temporary.filePath(u"physical.csv"_qs);
    QString error;
    QVERIFY2(writePhysicalAxisTraceCsv(path, physical, &error), qPrintable(error));
    PhysicalAxisTrace reloaded;
    QVERIFY2(readPhysicalAxisTraceCsv(path, &reloaded, &error), qPrintable(error));
    QCOMPARE(reloaded.samples.size(), physical.samples.size());
    QCOMPARE(reloaded.samples[1].timestampUs, std::uint64_t{5500});
    QCOMPARE(reloaded.samples[2].elapsedUs, std::uint64_t{6500});
    QCOMPARE(reloaded.samples[3].sourceChanged, false);
    QCOMPARE(reloaded.directInputRangeMaximum, 65535);
    QCOMPARE(reloaded.rawNormalizationMaximum, 65535);
    QCOMPARE(reloaded.samples[1].rawDirectInput, std::int32_t{16384});
    const std::vector<TraceSample> trace = traceSamplesFromPhysicalAxisTrace(reloaded);
    QCOMPARE(trace.size(), physical.samples.size());
    QVERIFY(std::abs(trace[1].timeSeconds - 0.0045F) < 0.000001F);
    QVERIFY(std::abs(trace[1].dtSeconds - 0.0065F) < 0.000001F);
    ScenarioDefinition scenario;
    scenario.id = "physical-fixture";
    scenario.family = "recorded-physical";
    scenario.mapperRateHz = 222;
    scenario.sourceRateHz = 222;
    hotas::RuntimeAdaptiveResponseConfig disabled;
    disabled.enabled = false;
    const ScenarioResult replayed = replayTraceSamples(scenario, trace, disabled, "Off", true);
    QCOMPARE(replayed.trace.size(), trace.size());
    QCOMPARE(replayed.telemetry.size(), trace.size());
    QCOMPARE(replayed.trace[2].timeSeconds, trace[2].timeSeconds);
    QCOMPARE(replayed.telemetry[2].predicted, trace[2].physical);
}

QTEST_APPLESS_MAIN(AdaptiveResponseVerificationTests)

#include "verification_tests.moc"
