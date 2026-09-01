#include "verification_harness.h"

#include <QtTest>

class AdaptiveResponseVerificationTests final : public QObject {
    Q_OBJECT

private slots:
    void harnessSelfValidation();
    void sameSeedProducesSameTrace();
    void adaptiveOffIsIdentity();
    void sampleHoldRetainsContinuousMotionEvidence();
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

QTEST_APPLESS_MAIN(AdaptiveResponseVerificationTests)

#include "verification_tests.moc"
