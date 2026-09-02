#include "app_backend.h"
#include "axis_transform.h"
#include "response_curve.h"
#include "theme_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStringList>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

bool evaluateEditorFunction(QObject *root, const QString &expression);

bool failAutomationEditorTest(const QString &message)
{
    const QByteArray encoded = message.toUtf8();
    std::fputs(encoded.constData(), stderr);
    std::fputc('\n', stderr);
    qCritical().noquote() << QStringLiteral("Automation QML interaction test failed: %1").arg(message);
    return false;
}

bool failPresentationLifecycleTest(const QString &message)
{
    const QByteArray encoded = message.toUtf8();
    std::fputs(encoded.constData(), stderr);
    std::fputc('\n', stderr);
    qCritical().noquote() << QStringLiteral("Presentation lifecycle test failed: %1").arg(message);
    return false;
}

void settlePresentation()
{
    for (int iteration = 0; iteration < 3; ++iteration) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        // A visible QML scene can continually post polish/paint work. Process
        // a bounded slice needed to instantiate or unload a page rather than
        // draining that replenished queue indefinitely in a test.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    }
}

struct ProcessMemoryFootprint {
    quint64 workingSetBytes = 0;
    quint64 privateBytes = 0;
};

ProcessMemoryFootprint currentProcessMemoryFootprint()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
        return {};
    }
    return {static_cast<quint64>(counters.WorkingSetSize), static_cast<quint64>(counters.PrivateUsage)};
}

QObject *pageItem(QObject *surface, int page)
{
    QQmlExpression expression(qmlContext(surface), surface,
        QStringLiteral("pageItem(%1)").arg(page));
    const QVariant value = expression.evaluate();
    if (expression.hasError()) return nullptr;
    return qvariant_cast<QObject *>(value);
}

bool selectPage(QObject *surface, int page)
{
    if (!surface->setProperty("currentPage", page)) {
        return failPresentationLifecycleTest(QStringLiteral("currentPage was not writable"));
    }
    settlePresentation();
    if (surface->property("loadedPageCount").toInt() != 1) {
        return failPresentationLifecycleTest(QStringLiteral("page %1 left more than one loaded page").arg(page));
    }
    if (!pageItem(surface, page)) {
        return failPresentationLifecycleTest(QStringLiteral("page %1 did not load on entry").arg(page));
    }
    return true;
}

QQuickItem *findVisualItemByObjectName(QQuickItem *item, const QString &objectName)
{
    if (!item) return nullptr;
    if (item->objectName() == objectName) return item;
    for (QQuickItem *child : item->childItems()) {
        if (QQuickItem *found = findVisualItemByObjectName(child, objectName)) return found;
    }
    return nullptr;
}

bool clickResponseComboRow(QQuickWindow *window, QObject *surface, QObject *combo, int row)
{
    auto *comboItem = qobject_cast<QQuickItem *>(combo);
    auto *scroll = surface->findChild<QQuickItem *>(QStringLiteral("adaptiveResponseScroll"));
    if (!comboItem || !scroll) return failPresentationLifecycleTest(QStringLiteral("ResponseCombo did not expose a clickable item and scroll viewport"));
    // A real pointer sequence is deliberately used here. A retry covers the
    // transient frame where the popup is promoted into QQuickOverlay between
    // the initial button release and the first offscreen paint.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QPointF relative = comboItem->mapToScene(QPointF{}) - scroll->mapToScene(QPointF{});
        const qreal contentY = scroll->property("contentY").toReal();
        scroll->setProperty("contentY", std::max<qreal>(0.0, contentY + relative.y() - 96.0));
        settlePresentation();
        const QPointF comboPoint = comboItem->mapToScene(QPointF(comboItem->width() * 0.5,
                                                                  comboItem->height() * 0.5));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, comboPoint.toPoint());
        QTest::qWait(8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, comboPoint.toPoint());
        settlePresentation();
        QObject *popup = combo->findChild<QObject *>(combo->objectName() + QStringLiteral("Popup"));
        if (!popup || !popup->property("visible").toBool()) continue;
        auto *popupContent = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
        auto *delegate = findVisualItemByObjectName(popupContent, combo->objectName()
            + QStringLiteral("Choice_%1").arg(row));
        if (!delegate) continue;
        const QPointF rowPoint = delegate->mapToScene(QPointF(delegate->width() * 0.5,
                                                               delegate->height() * 0.5));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rowPoint.toPoint());
        QTest::qWait(8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, rowPoint.toPoint());
        settlePresentation();
        if (!popup->property("visible").toBool() && combo->property("currentIndex").toInt() == row) return true;
        QTest::keyClick(window, Qt::Key_Escape);
        settlePresentation();
    }
    return false;
}

bool verifyAdaptiveResponseAxisSelection(hotas::AppBackend &backend, QObject *surface,
                                         QQuickWindow *window)
{
    if (!window) return failPresentationLifecycleTest(QStringLiteral("Adaptive Response pointer test did not receive a QQuickWindow"));
    if (!selectPage(surface, 9)) return false;
    QObject *adaptive = pageItem(surface, 9);
    if (!adaptive) return failPresentationLifecycleTest(QStringLiteral("Adaptive Response page was not available"));
    QObject *axisSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptiveAxisSelector"));
    if (!axisSelector) return failPresentationLifecycleTest(QStringLiteral("Adaptive Response axis selector was not available"));
    const QVariantList axes = backend.axes();
    if (axes.size() < 3) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response selector did not expose Roll, Pitch, and Yaw"));
    }
    const int originalAxis = backend.selectedAxisIndex();
    for (const int physicalAxis : {0, 1, 2}) {
        int modelIndex = -1;
        for (qsizetype index = 0; index < axes.size(); ++index) {
            const QVariantMap entry = axes.at(index).toMap();
            if (entry.value(QStringLiteral("index")).toInt() == physicalAxis) {
                modelIndex = static_cast<int>(index);
                break;
            }
        }
        if (modelIndex < 0) {
            return failPresentationLifecycleTest(QStringLiteral("Adaptive Response selector lacks physical axis %1").arg(physicalAxis));
        }
        if (!clickResponseComboRow(window, surface, axisSelector, modelIndex)) {
            return failPresentationLifecycleTest(QStringLiteral("Adaptive Response selector could not click model index %1").arg(modelIndex));
        }
        const QVariantMap state = adaptive->property("state").toMap();
        const QVariantList preview = adaptive->property("previewSamples").toList();
        if (backend.selectedAxisIndex() != physicalAxis
            || state.value(QStringLiteral("axis")).toInt() != physicalAxis
            || state.value(QStringLiteral("axisLabel")).toString().isEmpty()
            || preview.isEmpty()) {
            return failPresentationLifecycleTest(QStringLiteral("Adaptive Response Roll/Pitch/Yaw selection did not refresh backend, context, and preview together"));
        }
    }
    const auto selector = [adaptive](const QString &name) {
        return adaptive->findChild<QObject *>(name);
    };
    QObject *editScope = selector(QStringLiteral("adaptiveEditScopeSelector"));
    QObject *target = selector(QStringLiteral("adaptiveTargetSelector"));
    QObject *sourceRate = selector(QStringLiteral("adaptiveSourceRateSelector"));
    if (!editScope || !target || !sourceRate
        || !clickResponseComboRow(window, surface, editScope, 0)
        || adaptive->property("editScope").toString() != QStringLiteral("global")
        || !clickResponseComboRow(window, surface, target, 0)
        || !clickResponseComboRow(window, surface, editScope, 2)
        || adaptive->property("editScope").toString() != QStringLiteral("profile")) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response Edit Level or Target popup rows did not complete a real selection"));
    }
    if (!adaptive->setProperty("simulatorExpanded", true)) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response simulator could not expand for pointer testing"));
    }
    settlePresentation();
    if (!clickResponseComboRow(window, surface, sourceRate, 2)
        || adaptive->property("simulatorSourceRate").toInt() != 60) {
        return failPresentationLifecycleTest(QStringLiteral("Synthetic Source Rate popup row did not update its selected rate"));
    }
    if (!clickResponseComboRow(window, surface, sourceRate, 0)) {
        return failPresentationLifecycleTest(QStringLiteral("Synthetic Source Rate could not restore 250 Hz for manual drag testing"));
    }
    QQmlExpression applyLight(qmlContext(adaptive), adaptive, QStringLiteral("applySimplePreset('light')"));
    const QVariant applyResult = applyLight.evaluate();
    if (applyLight.hasError() || !applyResult.toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response Light preset action did not resolve the selected axis"));
    }
    settlePresentation();
    if (!adaptive->property("state").toMap().value(QStringLiteral("effective")).toMap()
            .value(QStringLiteral("enabled")).toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response Light preset did not apply to the selected axis"));
    }
    auto *manualInput = adaptive->findChild<QQuickItem *>(QStringLiteral("adaptiveSimulatorManualInput"));
    if (!manualInput || manualInput->mapToScene(QPointF(manualInput->width() * 0.5,
            manualInput->height() * 0.5)).x() <= window->width() * 0.5) {
        return failPresentationLifecycleTest(QStringLiteral("Interactive simulator manual input was not placed to the right of its graphs"));
    }
    adaptive->setProperty("simulatorPaused", true);
    adaptive->setProperty("simulatorExpanded", false);
    settlePresentation();
    backend.setSelectedAxis(originalAxis);
    settlePresentation();
    return true;
}

bool verifyAdaptiveResponseSimulator(hotas::AppBackend &backend)
{
    const QVariantMap mapperBefore = backend.adaptiveResponseTelemetry();
    backend.adaptiveResponseSimulatorClear();
    backend.adaptiveResponseSimulatorStartRecording();
    for (int index = 0; index < 8; ++index) {
        QThread::msleep(8);
        backend.adaptiveResponseSimulatorStepAtContext(-0.75 + index * 0.20,
            QStringLiteral("profile"), backend.activeProfileId(), 0, 250);
    }
    backend.adaptiveResponseSimulatorStopRecording();
    const QVariantList history = backend.adaptiveResponseSimulatorHistory();
    const QVariantList recording = backend.adaptiveResponseSimulatorRecording();
    const QVariantMap mapperAfter = backend.adaptiveResponseTelemetry();
    if (history.isEmpty() || recording.isEmpty()
        || history.constLast().toMap().value(QStringLiteral("physical")).toDouble()
            != recording.constLast().toMap().value(QStringLiteral("physical")).toDouble()
        || mapperBefore.value(QStringLiteral("predicted")).toDouble()
            != mapperAfter.value(QStringLiteral("predicted")).toDouble()) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response simulator did not retain isolated recorded production samples"));
    }
    qint64 previous = -1;
    for (const QVariant &entry : recording) {
        const qint64 timestamp = entry.toMap().value(QStringLiteral("recordedElapsedMs")).toLongLong();
        if (timestamp < previous || !entry.toMap().contains(QStringLiteral("virtualOutput"))) {
            return failPresentationLifecycleTest(QStringLiteral("Adaptive Response simulator recording lost original timestamps or results"));
        }
        previous = timestamp;
    }
    if (recording != backend.adaptiveResponseSimulatorRecording()) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response replay data recomputed instead of returning the stored recording"));
    }
    int priorPhysicalUpdateCount = std::numeric_limits<int>::max();
    for (const int sourceRate : {250, 125, 60, 30}) {
        backend.adaptiveResponseSimulatorClear();
        int physicalUpdateCount = 0;
        int physicalDirection = 0;
        int accelerationSign = 0;
        int accelerationSignFlips = 0;
        for (int index = 0; index <= 30; ++index) {
            QThread::msleep(16);
            backend.adaptiveResponseSimulatorStepAtContext(-0.80 + index * (1.60 / 30.0),
                QStringLiteral("profile"), backend.activeProfileId(), 0, sourceRate);
        }
        const QVariantList smoothHistory = backend.adaptiveResponseSimulatorHistory();
        if (smoothHistory.size() < 20) {
            return failPresentationLifecycleTest(QStringLiteral("Simulator produced too few samples at %1 Hz").arg(sourceRate));
        }
        for (qsizetype index = 1; index < smoothHistory.size(); ++index) {
            const QVariantMap current = smoothHistory.at(index).toMap();
            const QVariantMap previous = smoothHistory.at(index - 1).toMap();
            const float physicalDelta = static_cast<float>(current.value(QStringLiteral("physical")).toDouble()
                - previous.value(QStringLiteral("physical")).toDouble());
            const int direction = physicalDelta > 0.0001F ? 1 : physicalDelta < -0.0001F ? -1 : 0;
            if (direction != 0) {
                if (physicalDirection == 0) physicalDirection = direction;
                if (direction != physicalDirection) {
                    return failPresentationLifecycleTest(QStringLiteral("Smooth simulator trajectory reversed artificially at %1 Hz").arg(sourceRate));
                }
                ++physicalUpdateCount;
            }
            const float acceleration = static_cast<float>(current.value(QStringLiteral("acceleration")).toDouble());
            const int sign = acceleration > 3.0F ? 1 : acceleration < -3.0F ? -1 : 0;
            if (sign != 0 && accelerationSign != 0 && sign != accelerationSign) ++accelerationSignFlips;
            if (sign != 0) accelerationSign = sign;
        }
        if (physicalUpdateCount == 0 || accelerationSignFlips > 3) {
            return failPresentationLifecycleTest(QStringLiteral("Simulator interpolation left a QML-cadence pulse train at %1 Hz").arg(sourceRate));
        }
        if (physicalUpdateCount > priorPhysicalUpdateCount) {
            return failPresentationLifecycleTest(QStringLiteral("Lower source-rate emulation produced more reports than a higher rate"));
        }
        priorPhysicalUpdateCount = physicalUpdateCount;
        qInfo().noquote() << QStringLiteral("simulator_fidelity source_hz=%1 samples=%2 physical_updates=%3 acceleration_sign_flips=%4")
            .arg(sourceRate).arg(smoothHistory.size()).arg(physicalUpdateCount).arg(accelerationSignFlips);
    }
    const QVariantList humanPreview = backend.adaptiveResponsePreviewAtContext(
        QStringLiteral("Human-Like Rapid Reversal"), QStringLiteral("profile"), backend.activeProfileId(), 0);
    if (humanPreview.size() < 60) {
        return failPresentationLifecycleTest(QStringLiteral("Human-Like Rapid Reversal preview did not contain a complete trajectory"));
    }
    const double finalPhysical = humanPreview.constLast().toMap().value(QStringLiteral("physical")).toDouble();
    for (qsizetype index = humanPreview.size() - 50; index < humanPreview.size(); ++index) {
        if (std::abs(humanPreview.at(index).toMap().value(QStringLiteral("physical")).toDouble()
                - finalPhysical) > 0.00001) {
            return failPresentationLifecycleTest(QStringLiteral("Human-Like Rapid Reversal lacks its stationary settling tail"));
        }
    }
    for (const QString &preset : {QStringLiteral("fast"), QStringLiteral("balanced"), QStringLiteral("aggressive")}) {
        if (!backend.setAdaptiveResponsePresetAtContext(QStringLiteral("profile"), backend.activeProfileId(), 0, preset)) {
            return failPresentationLifecycleTest(QStringLiteral("Built-in Adaptive Response preset %1 was unavailable").arg(preset));
        }
        const QVariantMap human = backend.adaptiveResponseTestLabAtContext(
            QStringLiteral("Human-Like Rapid Reversal"), QStringLiteral("profile"), backend.activeProfileId(), 0);
        const QVariantMap torture = backend.adaptiveResponseTestLabAtContext(
            QStringLiteral("Instant Reversal Torture"), QStringLiteral("profile"), backend.activeProfileId(), 0);
        for (const QString &metric : {QStringLiteral("meanAbsolutePredictionError"),
             QStringLiteral("rmsPredictionError"), QStringLiteral("p95PredictionError"),
             QStringLiteral("maximumPredictionError"), QStringLiteral("physicalReversalMs"),
             QStringLiteral("reversalDetectionLatencyMs"), QStringLiteral("oppositeDirectionReacquisitionMs"),
             QStringLiteral("maximumArtificialPredictorStep")}) {
            if (!human.contains(metric)) {
                return failPresentationLifecycleTest(QStringLiteral("Corrected Test Lab metric %1 was missing").arg(metric));
            }
        }
        const double predictorOnlyStep = human.value(QStringLiteral("maximumArtificialPredictorStep")).toDouble();
        qInfo().noquote() << QStringLiteral("test_lab preset=%1 scenario=human_like mae=%2 rms=%3 p95=%4 max=%5 physical_reversal_ms=%6 detection_latency_ms=%7 opposite_lead_ms=%8 false_reversals=%9 predictor_only_step=%10 virtual_output_step=%11 settling_ms=%12")
            .arg(preset).arg(human.value(QStringLiteral("meanAbsolutePredictionError")).toDouble(), 0, 'f', 5)
            .arg(human.value(QStringLiteral("rmsPredictionError")).toDouble(), 0, 'f', 5)
            .arg(human.value(QStringLiteral("p95PredictionError")).toDouble(), 0, 'f', 5)
            .arg(human.value(QStringLiteral("maximumPredictionError")).toDouble(), 0, 'f', 5)
            .arg(human.value(QStringLiteral("physicalReversalMs")).toDouble(), 0, 'f', 1)
            .arg(human.value(QStringLiteral("reversalDetectionLatencyMs")).toDouble(), 0, 'f', 1)
            .arg(human.value(QStringLiteral("oppositeDirectionReacquisitionMs")).toDouble(), 0, 'f', 1)
            .arg(human.value(QStringLiteral("falseReversalCount")).toInt())
            .arg(predictorOnlyStep, 0, 'f', 5)
            .arg(human.value(QStringLiteral("maximumVirtualOutputStep")).toDouble(), 0, 'f', 5)
            .arg(human.value(QStringLiteral("settlingTimeMs")).toDouble(), 0, 'f', 1);
        qInfo().noquote() << QStringLiteral("test_lab preset=%1 scenario=instant_torture predictor_only_step=%2 virtual_output_step=%3")
            .arg(preset).arg(torture.value(QStringLiteral("maximumArtificialPredictorStep")).toDouble(), 0, 'f', 5)
            .arg(torture.value(QStringLiteral("maximumVirtualOutputStep")).toDouble(), 0, 'f', 5);
        if (predictorOnlyStep > 0.030) {
            qWarning().noquote() << QStringLiteral("V2.3.T CORE FINDING: Human-Like Rapid Reversal still has a predictor-only step above 3% for %1; inspect the logged local state before modifying predictor math.").arg(preset);
        }
    }
    return true;
}

bool verifyAdaptiveResponsePreviewTruth(hotas::AppBackend &backend)
{
    const int axis = 0;
    const int originalAxis = backend.selectedAxisIndex();
    backend.setSelectedAxis(axis);
    const QString profileId = backend.activeProfileId();
    const auto configurationForPreset = [](const QString &preset) {
        hotas::RuntimeAdaptiveResponseConfig configuration;
        configuration.enabled = preset != QStringLiteral("off");
        configuration.model = hotas::AdaptiveResponseModel::Auto;
        configuration.maximumLead = preset == QStringLiteral("extreme") ? 0.40F
            : preset == QStringLiteral("fast") ? 0.18F : 0.12F;
        configuration.maximumHorizonSeconds = preset == QStringLiteral("extreme") ? 0.030F
            : preset == QStringLiteral("fast") ? 0.012F : 0.008F;
        configuration.velocityResponse = preset == QStringLiteral("extreme") ? 1.0F
            : preset == QStringLiteral("fast") ? 0.80F : 0.72F;
        configuration.accelerationResponse = preset == QStringLiteral("extreme") ? 0.95F
            : preset == QStringLiteral("fast") ? 0.68F : 0.58F;
        configuration.motionSensitivity = 0.035F;
        configuration.noiseRejection = 0.012F;
        configuration.reversalDetection = 0.075F;
        configuration.reversalResponse = 1.0F;
        configuration.decelerationResponse = 0.85F;
        configuration.settlingResponse = 0.92F;
        configuration.endpointTaper = 0.16F;
        return configuration;
    };
    const auto verifyProductionPredictor = [&](const QString &preset) {
        if (!backend.setAdaptiveResponsePresetAtContext(QStringLiteral("profile"), profileId, axis, preset)) {
            return failPresentationLifecycleTest(QStringLiteral("Preview parity could not select %1").arg(preset));
        }
        const QVariantList preview = backend.adaptiveResponsePreviewAtContext(
            QStringLiteral("Human-Like Rapid Reversal"), QStringLiteral("profile"), profileId, axis);
        std::vector<float> physical;
        physical.reserve(static_cast<size_t>(preview.size()));
        for (const QVariant &entry : preview) physical.push_back(
            static_cast<float>(entry.toMap().value(QStringLiteral("physical")).toDouble()));
        const hotas::AdaptiveResponseSimulation direct = hotas::simulateAdaptiveResponse(
            configurationForPreset(preset), physical, 0.004F);
        if (direct.size() != static_cast<size_t>(preview.size())) {
            return failPresentationLifecycleTest(QStringLiteral("Preview parity sample count differed for %1").arg(preset));
        }
        for (size_t index = 0; index < direct.size(); ++index) {
            const QVariantMap sample = preview.at(static_cast<qsizetype>(index)).toMap();
            const auto equal = [](float actual, const QVariantMap &value, const QString &key) {
                return std::abs(actual - static_cast<float>(value.value(key).toDouble())) < 0.00002F;
            };
            if (!equal(direct[index].telemetry.physical, sample, QStringLiteral("physical"))
                || !equal(direct[index].telemetry.estimated, sample, QStringLiteral("estimated"))
                || !equal(direct[index].telemetry.predicted, sample, QStringLiteral("predicted"))
                || !equal(direct[index].telemetry.velocity, sample, QStringLiteral("velocity"))
                || !equal(direct[index].telemetry.acceleration, sample, QStringLiteral("acceleration"))
                || !equal(direct[index].telemetry.lead, sample, QStringLiteral("lead"))
                || !equal(direct[index].telemetry.activeHorizonSeconds * 1000.0F, sample, QStringLiteral("horizonMs"))
                || !equal(direct[index].telemetry.confidence, sample, QStringLiteral("confidence"))
                || hotas::adaptiveMotionStateLabel(direct[index].telemetry.state)
                    != sample.value(QStringLiteral("state")).toString()) {
                return failPresentationLifecycleTest(QStringLiteral("Preview diverged from AdaptiveResponseProcessor for %1 at sample %2")
                    .arg(preset).arg(index));
            }
        }
        return true;
    };
    for (const QString &preset : {QStringLiteral("off"), QStringLiteral("fast"), QStringLiteral("extreme")}) {
        if (!verifyProductionPredictor(preset)) return false;
    }
    for (const QString &scenario : {QStringLiteral("Human-Like Rapid Reversal"),
         QStringLiteral("Instant Reversal Torture"), QStringLiteral("Positive-Side Reversal"),
         QStringLiteral("Negative-Side Reversal"), QStringLiteral("Center-Crossing Reversal"),
         QStringLiteral("Micro Adjustments"), QStringLiteral("Sudden Stop"),
         QStringLiteral("Center Fighting"), QStringLiteral("Fast Sweep")}) {
        const QVariantList samples = backend.adaptiveResponsePreviewAtContext(
            scenario, QStringLiteral("profile"), profileId, axis);
        const QVariantMap metrics = backend.adaptiveResponseTestLabAtContext(
            scenario, QStringLiteral("profile"), profileId, axis);
        if (samples.size() != 211 || metrics.value(QStringLiteral("sampleCount")).toInt() != samples.size()
            || !std::isfinite(metrics.value(QStringLiteral("peakLead")).toDouble())
            || !std::isfinite(metrics.value(QStringLiteral("maximumArtificialPredictorStep")).toDouble())) {
            return failPresentationLifecycleTest(QStringLiteral("Static scenario %1 was incomplete or non-finite")
                .arg(scenario));
        }
    }

    backend.setAxisDeadzone(axis, 0.0);
    if (!backend.setAxisOutputLimits(axis, -0.15, 0.15)) {
        return failPresentationLifecycleTest(QStringLiteral("Preview context base mapping could not be prepared"));
    }
    backend.resetCurveLinear();
    const QString targetName = QStringLiteral("Preview Truth Mapping");
    if (!backend.createProfile(targetName, profileId)) {
        return failPresentationLifecycleTest(QStringLiteral("Preview context profile could not be created"));
    }
    QString targetId;
    for (const QVariant &entry : backend.profiles()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("name")).toString() == targetName) {
            targetId = profile.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (targetId.isEmpty() || !backend.activateProfile(targetId)
        || !backend.setAxisOutputLimits(axis, 0.55, 0.70)) {
        return failPresentationLifecycleTest(QStringLiteral("Preview context target mapping could not be prepared"));
    }
    backend.setAxisInverted(axis, true);
    backend.resetCurveLinear();
    const QVariantList targetPreview = backend.adaptiveResponsePreviewAtContext(
        QStringLiteral("Human-Like Rapid Reversal"), QStringLiteral("profile"), targetId, axis);
    if (!backend.activateProfile(profileId)) {
        return failPresentationLifecycleTest(QStringLiteral("Preview context base profile could not be restored"));
    }
    const QVariantList inactiveTargetPreview = backend.adaptiveResponsePreviewAtContext(
        QStringLiteral("Human-Like Rapid Reversal"), QStringLiteral("profile"), targetId, axis);
    if (targetPreview.size() != inactiveTargetPreview.size()) {
        return failPresentationLifecycleTest(QStringLiteral("Inactive profile preview lost samples"));
    }
    hotas::RuntimeAxisMapping targetMapping;
    targetMapping.profile.inverted = true;
    targetMapping.profile.deadzone = 0.0F;
    targetMapping.profile.hysteresis = 0.0F;
    targetMapping.profile.outputMinimum = 0.55F;
    targetMapping.profile.outputMaximum = 0.70F;
    targetMapping.responseCurve = hotas::compileResponseCurve(hotas::linearCurveDefinition(), false);
    for (qsizetype index = 0; index < targetPreview.size(); ++index) {
        const QVariantMap active = targetPreview.at(index).toMap();
        const QVariantMap inactive = inactiveTargetPreview.at(index).toMap();
        const float expected = hotas::evaluateStaticAxisTransfer(
            static_cast<float>(active.value(QStringLiteral("predicted")).toDouble()), targetMapping);
        if (std::abs(static_cast<float>(active.value(QStringLiteral("virtualOutput")).toDouble()) - expected) > 0.00002F
            || std::abs(static_cast<float>(inactive.value(QStringLiteral("virtualOutput")).toDouble()) - expected) > 0.00002F) {
            return failPresentationLifecycleTest(QStringLiteral("Static pipeline mixed live and requested mapping contexts"));
        }
    }
    backend.setSelectedAxis(originalAxis);
    return true;
}

bool verifyPageLifecycle(hotas::AppBackend &backend, QWindow *shell, const QString &theme)
{
    QObject *presentation = shell->findChild<QObject *>(QStringLiteral("presentationLoader"));
    if (!presentation) return failPresentationLifecycleTest(QStringLiteral("presentation Loader was not found"));
    QObject *surface = qvariant_cast<QObject *>(presentation->property("item"));
    if (!surface) return failPresentationLifecycleTest(QStringLiteral("theme surface was not loaded"));

    // QML may finish one-time component-cache initialization when each page
    // is first visited. Warm every page once, then measure repeated navigation
    // so the contract detects retained state rather than first-use setup.
    for (int page = 0; page <= 9; ++page) {
        if (!selectPage(surface, page)) return false;
    }
    if (!selectPage(surface, 8)) return false;
    settlePresentation();
    const ProcessMemoryFootprint fresh = currentProcessMemoryFootprint();
    const int freshObjectCount = surface->findChildren<QObject *>().size();
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int page = 0; page <= 9; ++page) {
            if (!selectPage(surface, page)) return false;
        }
    }
    // Return to the baseline Overview page before comparing object counts;
    // the new page is intentionally richer than the landing page.
    if (!selectPage(surface, 8)) return false;
    settlePresentation();
    const ProcessMemoryFootprint afterNavigation = currentProcessMemoryFootprint();
    const int afterNavigationObjectCount = surface->findChildren<QObject *>().size();
    const QString memoryLog = QStringLiteral("presentation_lifecycle_memory theme=%1 fresh_working_set_mb=%2 fresh_private_mb=%3 fresh_objects=%4 after_20_cycles_working_set_mb=%5 after_20_cycles_private_mb=%6 after_20_cycles_objects=%7")
        .arg(theme)
        .arg(fresh.workingSetBytes / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(fresh.privateBytes / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(freshObjectCount)
        .arg(afterNavigation.workingSetBytes / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(afterNavigation.privateBytes / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(afterNavigationObjectCount);
    qInfo().noquote() << memoryLog;
    std::fprintf(stderr, "%s\n", qPrintable(memoryLog));
    if (fresh.workingSetBytes == 0 || fresh.privateBytes == 0) {
        return failPresentationLifecycleTest(QStringLiteral("Windows memory counters were unavailable"));
    }
    if (afterNavigationObjectCount != freshObjectCount) {
        return failPresentationLifecycleTest(QStringLiteral("unloaded pages retained %1 QML objects")
            .arg(afterNavigationObjectCount - freshObjectCount));
    }

    if (!selectPage(surface, 7)) return false;
    const QString automationId = backend.createAutomation();
    settlePresentation();
    QObject *automation = pageItem(surface, 7);
    if (automationId.isEmpty()) return failPresentationLifecycleTest(QStringLiteral("backend could not create an Automation draft"));
    if (!automation) return failPresentationLifecycleTest(QStringLiteral("Automation page was not available for its draft"));
    if (!evaluateEditorFunction(automation,
            QStringLiteral("openRuleById('%1'); setBehaviorMode(1)").arg(automationId))) {
        return failPresentationLifecycleTest(QStringLiteral("Automation page could not open its draft"));
    }
    if (!automation->property("editing").toBool() || !automation->property("draftDirty").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("automation draft was not dirty before unload"));
    }
    if (!selectPage(surface, 8) || pageItem(surface, 7)) {
        return failPresentationLifecycleTest(QStringLiteral("automation page remained loaded after navigation"));
    }
    if (!selectPage(surface, 7)) return false;
    automation = pageItem(surface, 7);
    const QVariantMap restoredAutomationDraft = automation ? automation->property("draft").toMap() : QVariantMap{};
    if (!automation || !automation->property("editing").toBool()
        || !automation->property("draftDirty").toBool()
        || automation->property("editingId").toString() != automationId
        || restoredAutomationDraft.value(QStringLiteral("activationMode")).toInt() != 1) {
        return failPresentationLifecycleTest(QStringLiteral("automation draft was not preserved across unload"));
    }

    if (!selectPage(surface, 5)) return false;
    QObject *profiles = pageItem(surface, 5);
    if (!profiles
        || !profiles->setProperty("view", QStringLiteral("category"))
        || !profiles->setProperty("transferFile", QStringLiteral("C:/draft.hbf6pack"))
        || !profiles->setProperty("categoryConflictMode", QStringLiteral("replace"))
        || !profiles->setProperty("applyImportedCalibration", true)) {
        return failPresentationLifecycleTest(QStringLiteral("profile import state could not be prepared"));
    }
    if (!selectPage(surface, 8) || pageItem(surface, 5)) {
        return failPresentationLifecycleTest(QStringLiteral("profile page remained loaded after navigation"));
    }
    if (!selectPage(surface, 5)) return false;
    profiles = pageItem(surface, 5);
    if (!profiles || profiles->property("view").toString() != QStringLiteral("category")
        || profiles->property("transferFile").toString() != QStringLiteral("C:/draft.hbf6pack")
        || profiles->property("categoryConflictMode").toString() != QStringLiteral("replace")
        || !profiles->property("applyImportedCalibration").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("profile import state was not preserved across unload"));
    }
    if (!verifyAdaptiveResponseAxisSelection(backend, surface, qobject_cast<QQuickWindow *>(shell))) return false;
    return selectPage(surface, 8);
}

QVariantMap draftRow(QObject *root, const char *collection, int index)
{
    const QVariantList rows = root->property("draft").toMap().value(QString::fromLatin1(collection)).toList();
    return index >= 0 && index < rows.size() ? rows.at(index).toMap() : QVariantMap{};
}

bool evaluateEditorFunction(QObject *root, const QString &expression)
{
    QQmlExpression call(qmlContext(root), root,
        QStringLiteral("(function() { %1; return true; })()").arg(expression));
    call.evaluate();
    if (call.hasError()) return failAutomationEditorTest(call.error().toString());
    QCoreApplication::processEvents();
    return true;
}

bool verifyAutomationEditorInteraction(hotas::AppBackend &backend)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule(u"HOTASMapperStartupTest"_qs, u"AutomationPage"_qs);
    if (component.status() != QQmlComponent::Ready) {
        return failAutomationEditorTest(component.errorString());
    }

    QObject *root = component.createWithInitialProperties(
        {{QStringLiteral("backendObject"), QVariant::fromValue(static_cast<QObject *>(&backend))}});
    if (!root) return failAutomationEditorTest(component.errorString());
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root);
    if (!rootItem) {
        delete root;
        return failAutomationEditorTest(QStringLiteral("AutomationPage did not create a QQuickItem"));
    }
    QQuickWindow window;
    window.resize(1280, 720);
    rootItem->setParentItem(window.contentItem());
    rootItem->setSize(window.size());
    window.show();
    const QString automationId = backend.createAutomation();
    if (automationId.isEmpty()) {
        delete root;
        return failAutomationEditorTest(QStringLiteral("backend could not create an Automation draft"));
    }
    settlePresentation();
    QQmlExpression openEditor(qmlContext(root), root,
        QStringLiteral("openRuleById('%1')").arg(automationId));
    openEditor.evaluate();
    if (openEditor.hasError() || !root->property("editing").toBool()) {
        delete root;
        return failAutomationEditorTest(openEditor.hasError() ? openEditor.error().toString()
                                                               : QStringLiteral("Automation editor did not open"));
    }
    QQmlExpression prepare(qmlContext(root), root,
        u"(function() { addEffect(); addEffect(); setTriggerMode(false); addRequirement(0); addRequirement(0); return true; })()"_qs);
    prepare.evaluate();
    if (prepare.hasError()) {
        delete root;
        return failAutomationEditorTest(prepare.error().toString());
    }
    QCoreApplication::processEvents();

    struct ActionCase {
        int choice;
        int type;
    };
    constexpr ActionCase actionCases[] = {
        {1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {7, 6},
        {8, 7}, {9, 8}, {10, 9}, {11, 10}, {12, 11}, {13, 12}, {14, 13},
        {15, 14}, {16, 15}, {17, 16},
    };
    bool passed = true;
    for (const ActionCase &test : actionCases) {
        if (!evaluateEditorFunction(root, QStringLiteral("setEffectType(0, %1)").arg(test.choice))) {
            passed = false;
            break;
        }
        if (draftRow(root, "actions", 0).value(QStringLiteral("type")).toInt() != test.type) {
            passed = failAutomationEditorTest(QStringLiteral("action choice %1 updated the wrong draft type").arg(test.choice));
            break;
        }
    }

    if (passed) {
        passed = evaluateEditorFunction(root, u"setEffectType(0, 4); setEffectType(1, 11)"_qs)
            && draftRow(root, "actions", 0).value(QStringLiteral("type")).toInt() == 3
            && draftRow(root, "actions", 1).value(QStringLiteral("type")).toInt() == 10;
        if (!passed) passed = failAutomationEditorTest(QStringLiteral("multiple action rows did not update independently"));
    }
    if (passed) {
        passed = evaluateEditorFunction(root, u"setRequirementKind(0, 1); setRequirementKind(1, 2)"_qs)
            && draftRow(root, "conditions", 0).value(QStringLiteral("type")).toInt() == 5
            && draftRow(root, "conditions", 1).value(QStringLiteral("type")).toInt() == 1;
        if (!passed) passed = failAutomationEditorTest(QStringLiteral("multiple condition rows did not update independently"));
    }
    delete root;
    return passed;
}

}

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // ConfigStore owns an explicit INI under AppConfigLocation rather than
    // QSettings' default location. The lifecycle test creates persisted
    // Automation drafts, so remove that test-only INI before AppBackend loads
    // it and a prior run can exhaust the rule cap.
    QSettings testSettings;
    testSettings.clear();
    testSettings.sync();
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/settings.ini"));

    hotas::AppBackend backend;
    hotas::ThemeManager themeManager;
    const QStringList themes{
        QStringLiteral("Legacy"),
        QStringLiteral("Standard"),
        QStringLiteral("Top Gun"),
    };

    for (const QString &theme : themes) {
        themeManager.setCurrentTheme(theme);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
        engine.loadFromModule(u"HOTASMapperStartupTest"_qs, u"Main"_qs);
        if (engine.rootObjects().isEmpty()
            || !qobject_cast<QWindow *>(engine.rootObjects().constFirst())) return 1;

        settlePresentation();
        if (!verifyPageLifecycle(backend,
                qobject_cast<QWindow *>(engine.rootObjects().constFirst()), theme)) return 1;
    }

    if (!verifyAdaptiveResponseSimulator(backend)) return 1;
    if (!verifyAdaptiveResponsePreviewTruth(backend)) return 1;
    if (!verifyAutomationEditorInteraction(backend)) return 1;

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
