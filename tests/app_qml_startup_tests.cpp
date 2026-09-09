#include "app_backend.h"
#include "axis_transform.h"
#include "response_curve.h"
#include "theme_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
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
#include <array>
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

bool clickResponseComboRow(QQuickWindow *window, QObject *surface, QObject *combo, int row,
                           bool requireSelectedRow = true)
{
    auto *comboItem = qobject_cast<QQuickItem *>(combo);
    auto *scroll = surface->findChild<QQuickItem *>(QStringLiteral("adaptiveResponseScroll"));
    if (!scroll) scroll = qobject_cast<QQuickItem *>(surface);
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
        if (!popup->property("visible").toBool()
            && (!requireSelectedRow || combo->property("currentIndex").toInt() == row)) return true;
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
    if (!clickResponseComboRow(window, surface, sourceRate, 2)
        || adaptive->property("simulatorSourceRate").toInt() != 60) {
        return failPresentationLifecycleTest(QStringLiteral("Synthetic Source Rate popup row did not update its selected rate"));
    }
    if (!clickResponseComboRow(window, surface, sourceRate, 0)) {
        return failPresentationLifecycleTest(QStringLiteral("Synthetic Source Rate could not restore 250 Hz for manual drag testing"));
    }
    const QVariantMap responseConfigurationBeforeSourceSwitch = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), backend.activeProfileId(), backend.selectedAxisIndex());
    QQmlExpression selectLiveSource(qmlContext(adaptive), adaptive,
        QStringLiteral("setResponseLabSource('live'); responseLabSource"));
    if (selectLiveSource.evaluate().toString() != QStringLiteral("live")
        || selectLiveSource.hasError()
        || backend.adaptiveResponseContextState(
            QStringLiteral("profile"), backend.activeProfileId(), backend.selectedAxisIndex())
            != responseConfigurationBeforeSourceSwitch) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Response Lab Live source selection changed saved Adaptive Response configuration"));
    }
    const int liveAxis = backend.selectedAxisIndex();
    backend.injectAdaptiveResponseLiveSampleForTest(liveAxis, -0.65F);
    QQmlExpression refreshFirstLiveHistory(qmlContext(adaptive), adaptive,
        QStringLiteral("refreshHistory(true)"));
    refreshFirstLiveHistory.evaluate();
    const QVariantList firstLiveGraphSamples = adaptive->property("responseLabSamples").toList();
    backend.injectAdaptiveResponseLiveSampleForTest(liveAxis, 0.70F);
    QQmlExpression refreshLiveHistory(qmlContext(adaptive), adaptive,
        QStringLiteral("refreshHistory(false)"));
    refreshLiveHistory.evaluate();
    const QVariantList liveGraphSamples = adaptive->property("responseLabSamples").toList();
    const QVariantList capturedLiveHistory = backend.adaptiveResponseHistorySince(0, 2)
        .value(QStringLiteral("samples")).toList();
    bool capturedNegative = false;
    bool capturedPositive = false;
    for (const QVariant &sample : capturedLiveHistory) {
        const double physical = sample.toMap().value(QStringLiteral("physical")).toDouble();
        capturedNegative = capturedNegative || physical <= -0.64;
        capturedPositive = capturedPositive || physical >= 0.69;
    }
    // The worker may publish a newer physical snapshot between test injection
    // and a separate direct read. The Response Lab deliberately presents the
    // UI-side sampled history, so assert its selected-axis samples instead:
    // this proves that changing physical input reaches both the Lab state and
    // its graph without coupling the test to an instantaneous report race.
    if (refreshLiveHistory.hasError() || firstLiveGraphSamples.isEmpty()
        || firstLiveGraphSamples.constLast().toMap().value(QStringLiteral("physical")).toDouble() >= -0.64
        || liveGraphSamples.isEmpty()
        || liveGraphSamples.constLast().toMap().value(QStringLiteral("physical")).toDouble() <= 0.69
        || !capturedNegative || !capturedPositive
        || backend.mappingStatus() != QStringLiteral("MAPPING SUSPENDED")
        || backend.mappingActive() || backend.vjoyReady()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Live Controller snapshot did not update the unified Response Lab while mapping was suspended and vJoy unavailable "
            "(first_samples=%1 first_newest=%2 samples=%3 oldest=%4 newest=%5 status=%6 active=%7 vjoy=%8)")
            .arg(firstLiveGraphSamples.size())
            .arg(firstLiveGraphSamples.isEmpty() ? 0.0 : firstLiveGraphSamples.constLast().toMap().value(QStringLiteral("physical")).toDouble(), 0, 'f', 3)
            .arg(liveGraphSamples.size())
            .arg(liveGraphSamples.isEmpty() ? 0.0 : liveGraphSamples.constFirst().toMap().value(QStringLiteral("physical")).toDouble(), 0, 'f', 3)
            .arg(liveGraphSamples.isEmpty() ? 0.0 : liveGraphSamples.constLast().toMap().value(QStringLiteral("physical")).toDouble(), 0, 'f', 3)
            .arg(backend.mappingStatus()).arg(backend.mappingActive()).arg(backend.vjoyReady()));
    }
    QQmlExpression selectInteractiveSource(qmlContext(adaptive), adaptive,
        QStringLiteral("setResponseLabSource('interactive'); responseLabSource"));
    if (selectInteractiveSource.evaluate().toString() != QStringLiteral("interactive")
        || selectInteractiveSource.hasError()) {
        return failPresentationLifecycleTest(QStringLiteral("Response Lab Interactive source selection did not return to its local input feed"));
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
    if (!manualInput || manualInput->width() < 120) {
        return failPresentationLifecycleTest(QStringLiteral("Interactive manual input was not available in the unified Response Lab source strip"));
    }
    adaptive->setProperty("simulatorPaused", true);
    settlePresentation();
    const QByteArray visualEvidenceDirectory = qgetenv("HOTAS_VISUAL_EVIDENCE_DIR");
    if (!visualEvidenceDirectory.isEmpty()) {
        const QDir evidenceDirectory(QString::fromLocal8Bit(visualEvidenceDirectory));
        if (!evidenceDirectory.exists() && !QDir().mkpath(evidenceDirectory.absolutePath())) {
            return failPresentationLifecycleTest(QStringLiteral("Visual evidence directory could not be created"));
        }
        auto scrollAndCapture = [&](const QString &objectName, const QString &filename) {
            auto *scroll = adaptive->findChild<QQuickItem *>(QStringLiteral("adaptiveResponseScroll"));
            auto *target = adaptive->findChild<QQuickItem *>(objectName);
            if (!scroll || !target) return false;
            const QPointF relative = target->mapToScene(QPointF{}) - scroll->mapToScene(QPointF{});
            scroll->setProperty("contentY", std::max<qreal>(0.0,
                scroll->property("contentY").toReal() + relative.y() - 30.0));
            settlePresentation();
            return window->grabWindow().save(evidenceDirectory.filePath(filename));
        };
        if (!scrollAndCapture(QStringLiteral("staticResponsePreviewCard"),
                QStringLiteral("A-static-response-preview.png"))
            || !scrollAndCapture(QStringLiteral("responseLabCard"),
                QStringLiteral("B-response-lab-interactive.png"))) {
            return failPresentationLifecycleTest(QStringLiteral("Static or Interactive Response Lab visual evidence could not be captured"));
        }
        QQmlExpression selectLiveForEvidence(qmlContext(adaptive), adaptive,
            QStringLiteral("setResponseLabSource('live')"));
        selectLiveForEvidence.evaluate();
        backend.injectAdaptiveResponseLiveSampleForTest(backend.selectedAxisIndex(), 0.45F);
        QQmlExpression refreshLiveForEvidence(qmlContext(adaptive), adaptive,
            QStringLiteral("refreshHistory(true)"));
        refreshLiveForEvidence.evaluate();
        if (selectLiveForEvidence.hasError() || refreshLiveForEvidence.hasError()
            || !scrollAndCapture(QStringLiteral("responseLabCard"),
                QStringLiteral("C-response-lab-live-controller.png"))
            || !scrollAndCapture(QStringLiteral("responseLabEffectiveResponse"),
                QStringLiteral("D-effective-response.png"))) {
            return failPresentationLifecycleTest(QStringLiteral("Live Controller or Effective Response visual evidence could not be captured"));
        }
        adaptive->setProperty("responseMonitorVisible", true);
        settlePresentation();
        QQuickWindow *monitor = nullptr;
        for (QWindow *topLevel : QGuiApplication::topLevelWindows()) {
            if (topLevel && topLevel->title() == QStringLiteral("Adaptive Response Monitor")) {
                monitor = qobject_cast<QQuickWindow *>(topLevel);
                break;
            }
        }
        const bool monitorCaptured = monitor && monitor->grabWindow().save(
            evidenceDirectory.filePath(QStringLiteral("E-adaptive-response-monitor.png")));
        adaptive->setProperty("responseMonitorVisible", false);
        if (!monitorCaptured) {
            return failPresentationLifecycleTest(QStringLiteral("Detached Adaptive Response Monitor visual evidence could not be captured"));
        }
        QQmlExpression selectInteractiveAfterEvidence(qmlContext(adaptive), adaptive,
            QStringLiteral("setResponseLabSource('interactive')"));
        selectInteractiveAfterEvidence.evaluate();
    }
    backend.setSelectedAxis(originalAxis);
    settlePresentation();
    return true;
}

QString targetForAxis(const QVariantList &axes, int physicalAxis)
{
    for (const QVariant &entry : axes) {
        const QVariantMap axis = entry.toMap();
        if (axis.value(QStringLiteral("index")).toInt() == physicalAxis) {
            return axis.value(QStringLiteral("target")).toString();
        }
    }
    return {};
}

int targetForButton(const QVariantList &buttons, int physicalButton)
{
    for (const QVariant &entry : buttons) {
        const QVariantMap button = entry.toMap();
        if (button.value(QStringLiteral("index")).toInt() == physicalButton) {
            return button.value(QStringLiteral("target")).toInt();
        }
    }
    return -1;
}

int targetForPovDirection(const QVariantList &povInputs, int hat, int direction)
{
    for (const QVariant &entry : povInputs) {
        const QVariantMap input = entry.toMap();
        if (input.value(QStringLiteral("hat")).toInt() == hat
            && input.value(QStringLiteral("direction")).toInt() == direction) {
            return input.value(QStringLiteral("target")).toInt();
        }
    }
    return -1;
}

bool verifyAxisRouteTransactionAndPresentation(hotas::AppBackend &backend, QObject *surface)
{
    // This fixture represents a vJoy descriptor with every standard axis
    // available, without starting a driver or mapping a real controller.
    backend.setVirtualAxisAvailabilityForTest(true);
    for (const int axis : {3, 4, 6, 7}) {
        if (!backend.setMapping(axis, QStringLiteral("Disabled"), true)) {
            return failPresentationLifecycleTest(QStringLiteral("Axis route fixture could not clear optional axis %1").arg(axis));
        }
    }
    if (!backend.setMapping(0, QStringLiteral("X"), true)
        || !backend.setMapping(1, QStringLiteral("Y"), true)
        || !backend.setMapping(2, QStringLiteral("Z"), true)
        || !backend.setMapping(5, QStringLiteral("Rz"), true)) {
        return failPresentationLifecycleTest(QStringLiteral("Axis route fixture could not establish its initial mappings"));
    }

    // Every axis reported by the selected vJoy descriptor is represented in
    // the real QML dropdown model, not a historic X/Y/Z/Rz allowlist.
    if (!selectPage(surface, 0)) return false;
    const QVariant choicesValue = surface->property("outputChoices");
    QStringList dropdownChoices = choicesValue.toStringList();
    if (dropdownChoices.isEmpty()) {
        for (const QVariant &choice : choicesValue.toList()) {
            dropdownChoices.append(choice.toString());
        }
    }
    const QStringList expectedChoices{QStringLiteral("Disabled"), QStringLiteral("X"),
        QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("Rx"),
        QStringLiteral("Ry"), QStringLiteral("Rz"), QStringLiteral("Slider 0"),
        QStringLiteral("Slider 1")};
    if (dropdownChoices != expectedChoices) {
        return failPresentationLifecycleTest(QStringLiteral("Axis dropdown did not mirror the full selected-vJoy descriptor"));
    }

    // Exercise independent row ownership before the conflict flow: Roll
    // changes to Ry, then Pitch is disabled, then Throttle takes Slider 1.
    // No neighbouring source row may be changed by either update.
    if (!backend.setMapping(0, QStringLiteral("Ry"), true)
        || targetForAxis(backend.axes(), 0) != QStringLiteral("Ry")
        || targetForAxis(backend.axes(), 1) != QStringLiteral("Y")
        || targetForAxis(backend.axes(), 2) != QStringLiteral("Z")
        || targetForAxis(backend.axes(), 5) != QStringLiteral("Rz")) {
        return failPresentationLifecycleTest(QStringLiteral("Roll -> Ry changed an unrelated axis route"));
    }
    if (!backend.setMapping(1, QStringLiteral("Disabled"), true)
        || targetForAxis(backend.axes(), 0) != QStringLiteral("Ry")
        || targetForAxis(backend.axes(), 1) != QStringLiteral("Disabled")
        || targetForAxis(backend.axes(), 2) != QStringLiteral("Z")
        || targetForAxis(backend.axes(), 5) != QStringLiteral("Rz")) {
        return failPresentationLifecycleTest(QStringLiteral("Pitch disable changed an unrelated axis route"));
    }
    if (!backend.setMapping(2, QStringLiteral("Slider1"), true)
        || targetForAxis(backend.axes(), 0) != QStringLiteral("Ry")
        || targetForAxis(backend.axes(), 1) != QStringLiteral("Disabled")
        || targetForAxis(backend.axes(), 2) != QStringLiteral("Slider 1")
        || targetForAxis(backend.axes(), 5) != QStringLiteral("Rz")) {
        return failPresentationLifecycleTest(QStringLiteral("Throttle -> Slider 1 changed an unrelated axis route"));
    }
    if (!backend.setMapping(0, QStringLiteral("X"), true)
        || !backend.setMapping(1, QStringLiteral("Y"), true)
        || !backend.setMapping(2, QStringLiteral("Z"), true)) {
        return failPresentationLifecycleTest(QStringLiteral("Axis conflict fixture could not restore its initial mappings"));
    }

    // Cancel is a real transaction: it must not disturb the existing X/Y
    // routes or mutate the active output-layout descriptor.
    if (backend.setMapping(0, QStringLiteral("Y"), false)
        || targetForAxis(backend.axes(), 0) != QStringLiteral("X")
        || targetForAxis(backend.axes(), 1) != QStringLiteral("Y")) {
        return failPresentationLifecycleTest(QStringLiteral("Axis conflict cancel changed an authoritative route"));
    }
    // An explicit Allow retains the earlier Y mapping and applies A -> Y on
    // its first accepted attempt. The row-order policy is separately tested
    // in mapping_core_tests without a vJoy device.
    if (!backend.setMapping(0, QStringLiteral("Y"), true)
        || targetForAxis(backend.axes(), 0) != QStringLiteral("Y")
        || targetForAxis(backend.axes(), 1) != QStringLiteral("Y")) {
        return failPresentationLifecycleTest(QStringLiteral("Axis conflict allow did not retain both configured Y routes"));
    }

    if (!backend.setMapping(0, QStringLiteral("Y"), true)
        || !backend.setMapping(1, QStringLiteral("Disabled"), true)
        || !backend.setMapping(5, QStringLiteral("Rx"), true)
        || !backend.setMapping(2, QStringLiteral("Z"), true)) {
        return failPresentationLifecycleTest(QStringLiteral("Axis truth fixture could not configure Roll/Pitch/Yaw/Throttle"));
    }
    const QVariantList storedConfiguration = backend.axes();
    const std::array<int, 4> physicalAxes{0, 1, 5, 2};
    const std::array<QString, 4> expected{QStringLiteral("Y"), QStringLiteral("Disabled"),
                                          QStringLiteral("Rx"), QStringLiteral("Z")};
    for (size_t index = 0; index < physicalAxes.size(); ++index) {
        const int axis = physicalAxes[index];
        if (targetForAxis(storedConfiguration, axis) != expected[index]) {
            return failPresentationLifecycleTest(QStringLiteral("Stored axis mapping did not retain the requested route for axis %1").arg(axis));
        }
        if (targetForAxis(backend.runtimeAxisRoutesForTest(), axis) != expected[index]) {
            return failPresentationLifecycleTest(QStringLiteral("Compiled runtime route did not match stored axis mapping for axis %1").arg(axis));
        }
    }

    // Loading/unloading the Axis page exercises the QML-bound row model. It
    // must mirror the stored configuration after repeated re-rendering.
    if (!selectPage(surface, 0)) return false;
    const QVariantList firstPresentation = surface->property("allAxes").toList();
    if (!selectPage(surface, 8) || !selectPage(surface, 0)) return false;
    const QVariantList rerenderedPresentation = surface->property("allAxes").toList();
    for (size_t index = 0; index < physicalAxes.size(); ++index) {
        const int axis = physicalAxes[index];
        const QString wanted = expected[index];
        if (targetForAxis(firstPresentation, axis) != wanted
            || targetForAxis(rerenderedPresentation, axis) != wanted) {
            return failPresentationLifecycleTest(QStringLiteral("Axis row presentation diverged after a re-render for axis %1").arg(axis));
        }
    }
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
    for (const QString &preset : {QStringLiteral("fast"), QStringLiteral("balanced"),
         QStringLiteral("aggressive"), QStringLiteral("extreme")}) {
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
            : preset == QStringLiteral("aggressive") ? 0.27F
            : preset == QStringLiteral("fast") ? 0.18F : 0.12F;
        configuration.maximumHorizonSeconds = preset == QStringLiteral("extreme") ? 0.030F
            : preset == QStringLiteral("aggressive") ? 0.018F
            : preset == QStringLiteral("fast") ? 0.012F : 0.008F;
        configuration.velocityResponse = preset == QStringLiteral("extreme") ? 1.0F
            : preset == QStringLiteral("aggressive") ? 0.91F
            : preset == QStringLiteral("fast") ? 0.80F : 0.72F;
        configuration.accelerationResponse = preset == QStringLiteral("extreme") ? 0.95F
            : preset == QStringLiteral("aggressive") ? 0.82F
            : preset == QStringLiteral("fast") ? 0.68F : 0.58F;
        configuration.motionSensitivity = 0.035F;
        configuration.noiseRejection = 0.012F;
        configuration.reversalDetection = 0.075F;
        configuration.reversalResponse = 1.0F;
        configuration.decelerationResponse = 0.85F;
        configuration.settlingResponse = 0.92F;
        configuration.endpointTaper = 0.16F;
        configuration.onsetAssist = preset == QStringLiteral("extreme") ? 0.50F
            : preset == QStringLiteral("aggressive") ? 0.38F
            : preset == QStringLiteral("fast") ? 0.28F : 0.0F;
        configuration.onsetCap = preset == QStringLiteral("extreme") ? 0.30F
            : preset == QStringLiteral("aggressive") ? 0.22F
            : preset == QStringLiteral("fast") ? 0.16F : 0.0F;
        configuration.sustainedAssist = preset == QStringLiteral("extreme") ? 0.55F
            : preset == QStringLiteral("aggressive") ? 0.42F
            : preset == QStringLiteral("fast") ? 0.32F : 0.0F;
        configuration.sustainedCap = preset == QStringLiteral("extreme") ? 0.28F
            : preset == QStringLiteral("aggressive") ? 0.20F
            : preset == QStringLiteral("fast") ? 0.15F : 0.0F;
        configuration.horizonExtension = preset == QStringLiteral("extreme") ? 0.65F
            : preset == QStringLiteral("aggressive") ? 0.48F
            : preset == QStringLiteral("fast") ? 0.35F : 0.0F;
        configuration.horizonExtensionCapSeconds = preset == QStringLiteral("extreme") ? 0.024F
            : preset == QStringLiteral("aggressive") ? 0.018F
            : preset == QStringLiteral("fast") ? 0.012F : 0.0F;
        configuration.turningPointProtection = preset == QStringLiteral("extreme") ? 1.0F
            : preset == QStringLiteral("aggressive") ? 0.94F
            : preset == QStringLiteral("fast") ? 0.88F : 0.0F;
        configuration.turningPointMargin = preset == QStringLiteral("extreme") ? 0.08F
            : preset == QStringLiteral("aggressive") ? 0.10F
            : preset == QStringLiteral("fast") ? 0.12F : 0.0F;
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
        hotas::RuntimeAdaptiveResponseConfig physicalPrediction = configurationForPreset(preset);
        // The predictor is intentionally evaluated in physical-axis space
        // before V2.3.1 applies the configured Maximum Lead in mapped-output
        // space. Match the preview's safe physical candidate envelope here.
        physicalPrediction.maximumLead = 0.50F;
        const hotas::AdaptiveResponseSimulation direct = hotas::simulateAdaptiveResponse(
            physicalPrediction, physical, 0.004F);
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
                || !equal(direct[index].telemetry.accelerationIntent, sample, QStringLiteral("accelerationIntent"))
                || !equal(direct[index].telemetry.onsetAuthority, sample, QStringLiteral("onsetAuthority"))
                || !equal(direct[index].telemetry.sustainedEvidence, sample, QStringLiteral("sustainedEvidence"))
                || !equal(direct[index].telemetry.sustainedAuthority, sample, QStringLiteral("sustainedAuthority"))
                || !equal(direct[index].telemetry.motionUrgency, sample, QStringLiteral("motionUrgency"))
                || !equal(direct[index].telemetry.horizonExtensionEligibility, sample, QStringLiteral("horizonExtensionEligibility"))
                || !equal(direct[index].telemetry.normalMaximumHorizonSeconds * 1000.0F, sample, QStringLiteral("normalMaximumHorizonMs"))
                || !equal(direct[index].telemetry.allowedMaximumHorizonSeconds * 1000.0F, sample, QStringLiteral("allowedMaximumHorizonMs"))
                || !equal(direct[index].telemetry.turningPointConfidence, sample, QStringLiteral("turningPointConfidence"))
                || !equal(direct[index].telemetry.estimatedTimeToTurnSeconds * 1000.0F, sample, QStringLiteral("estimatedTimeToTurnMs"))
                || !equal(direct[index].telemetry.estimatedRemainingTravel, sample, QStringLiteral("estimatedRemainingTravel"))
                || !equal(direct[index].telemetry.turningPointHorizonLimitSeconds * 1000.0F, sample, QStringLiteral("turningPointHorizonLimitMs"))
                || !equal(direct[index].telemetry.turningPointLeadLimit, sample, QStringLiteral("turningPointLeadLimit"))
                || !equal(direct[index].telemetry.reacquisitionAuthority, sample, QStringLiteral("reacquisitionAuthority"))
                || hotas::adaptiveMotionStateLabel(direct[index].telemetry.state)
                    != sample.value(QStringLiteral("state")).toString()) {
                return failPresentationLifecycleTest(QStringLiteral("Preview diverged from AdaptiveResponseProcessor for %1 at sample %2")
                    .arg(preset).arg(index));
            }
        }
        return true;
    };
    for (const QString &preset : {QStringLiteral("off"), QStringLiteral("fast"),
         QStringLiteral("aggressive"), QStringLiteral("extreme")}) {
        if (!verifyProductionPredictor(preset)) return false;
    }
    for (const QString &scenario : {QStringLiteral("Human-Like Rapid Reversal"),
         QStringLiteral("Instant Reversal Torture"), QStringLiteral("Positive-Side Reversal"),
         QStringLiteral("Negative-Side Reversal"), QStringLiteral("Center-Crossing Reversal"),
         QStringLiteral("Micro Adjustments"), QStringLiteral("Sudden Stop"),
         QStringLiteral("Center Fighting"), QStringLiteral("Fast Sweep"),
         QStringLiteral("Slow Coherent Waggle"), QStringLiteral("Slow One-Way Sweep"),
         QStringLiteral("Small Slow Correction"), QStringLiteral("Extreme Turning-Point Torture")}) {
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
        || !backend.setAxisOutputLimits(axis, 0.55, 0.70)
        || !backend.setAdaptiveResponsePropertyAtContext(
            QStringLiteral("profile"), targetId, axis, QStringLiteral("enabled"), true)
        || !backend.setAdaptiveResponsePropertyAtContext(
            QStringLiteral("profile"), targetId, axis, QStringLiteral("maximumLead"), 0.04)) {
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
        const float expectedBaseline = hotas::evaluateStaticNormalizedAxisTransfer(
            static_cast<float>(active.value(QStringLiteral("physical")).toDouble()), targetMapping);
        const float expectedPredictedMapped = hotas::evaluateStaticNormalizedAxisTransfer(
            static_cast<float>(active.value(QStringLiteral("predicted")).toDouble()), targetMapping);
        const float baseline = static_cast<float>(active.value(QStringLiteral("baselineOutput")).toDouble());
        const float predictedMapped = static_cast<float>(active.value(QStringLiteral("predictedMappedOutput")).toDouble());
        const float adaptive = static_cast<float>(active.value(QStringLiteral("adaptiveOutput")).toDouble());
        const float appliedLead = static_cast<float>(active.value(QStringLiteral("appliedLead")).toDouble());
        const float inactiveAdaptive = static_cast<float>(inactive.value(QStringLiteral("adaptiveOutput")).toDouble());
        if (std::abs(baseline - expectedBaseline) > 0.00002F
            || std::abs(predictedMapped - expectedPredictedMapped) > 0.00002F
            || std::abs(adaptive - (baseline + appliedLead)) > 0.00002F
            || std::abs(appliedLead) > 0.04002F
            || std::abs(inactiveAdaptive - adaptive) > 0.00002F) {
            return failPresentationLifecycleTest(QStringLiteral(
                "Static preview output-domain mismatch at sample %1: baseline=%2 expected=%3 "
                "predictedMapped=%4 expected=%5 adaptive=%6 applied=%7 inactive=%8")
                .arg(index).arg(baseline, 0, 'f', 5).arg(expectedBaseline, 0, 'f', 5)
                .arg(predictedMapped, 0, 'f', 5).arg(expectedPredictedMapped, 0, 'f', 5)
                .arg(adaptive, 0, 'f', 5).arg(appliedLead, 0, 'f', 5)
                .arg(inactiveAdaptive, 0, 'f', 5));
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
    if (!verifyAxisRouteTransactionAndPresentation(backend, surface)) return false;
    if (!verifyAdaptiveResponseAxisSelection(backend, surface, qobject_cast<QQuickWindow *>(shell))) return false;
    return selectPage(surface, 8);
}

bool verifyFlightDeckShell(hotas::AppBackend &backend, hotas::ThemeManager &themeManager,
                           const QString &appearance)
{
    themeManager.setCurrentTheme(QStringLiteral("Standard"));
    themeManager.setFlightDeckAppearance(appearance);
    themeManager.setCurrentExperience(QStringLiteral("Flight Deck"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    engine.loadFromModule(u"HOTASMapperStartupTest"_qs, u"Main"_qs);
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) return failPresentationLifecycleTest(QStringLiteral("Flight Deck window did not load"));

    settlePresentation();
    QObject *surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    QObject *tokens = window->findChild<QObject *>(QStringLiteral("flightDeckTheme"));
    if (!surface || !tokens || tokens->property("light").toBool()
        != (appearance == QStringLiteral("Light"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 resources did not resolve")
            .arg(appearance));
    }

    QObject *readinessModel = surface->findChild<QObject *>(QStringLiteral("flightDeckReadinessModel"));
    QObject *overview = pageItem(surface, 8);
    if (!readinessModel || !overview || overview->objectName() != QStringLiteral("flightDeckOverview")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck did not load its native Overview state page"));
    }
    const auto readinessValue = [&](const QString &expression) {
        QQmlExpression call(qmlContext(readinessModel), readinessModel, expression);
        const QVariant value = call.evaluate();
        if (call.hasError()) {
            qCritical().noquote() << call.error().toString();
            return QVariant{};
        }
        return value;
    };
    const auto hasPresentation = [&](const QString &state, const QString &label, const QString &tone) {
        const QVariantMap presentation = readinessValue(
            QStringLiteral("presentationFor(%1)").arg(state)).toMap();
        return presentation.value(QStringLiteral("label")).toString() == label
            && presentation.value(QStringLiteral("tone")).toString() == tone;
    };
    if (!hasPresentation(QStringLiteral("({physicalConnected:true, vjoyReady:true, vjoyStatusSeverity:'ready', controllerReadinessState:'READY', mappingActive:true, mappingRequested:true})"),
            QStringLiteral("READY"), QStringLiteral("healthy"))
        || !hasPresentation(QStringLiteral("({physicalConnected:false, vjoyReady:true, vjoyStatusSeverity:'ready', controllerReadinessState:'READY', mappingActive:false, mappingRequested:false})"),
            QStringLiteral("NO CONTROLLER"), QStringLiteral("fault"))
        || !hasPresentation(QStringLiteral("({physicalConnected:true, vjoyReady:false, vjoyStatusSeverity:'error', controllerReadinessState:'READY', mappingActive:false, mappingRequested:true})"),
            QStringLiteral("ACTION NEEDED"), QStringLiteral("fault"))
        || !hasPresentation(QStringLiteral("({physicalConnected:true, vjoyReady:true, vjoyStatusSeverity:'ready', controllerReadinessState:'ATTENTION', mappingActive:false, mappingRequested:true})"),
            QStringLiteral("ACTION NEEDED"), QStringLiteral("attention"))
        || !hasPresentation(QStringLiteral("({physicalConnected:true, vjoyReady:true, vjoyStatusSeverity:'ready', controllerReadinessState:'READY', mappingActive:false, mappingRequested:true})"),
            QStringLiteral("PARTIALLY READY"), QStringLiteral("attention"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck readiness states are not classified consistently"));
    }
    const QVariantMap multipleInput = readinessValue(
        QStringLiteral("inputFor({physicalConnected:true, connectedControllerCount:3, deviceName:'Long controller name'})")).toMap();
    const QVariantMap noProfile = readinessValue(
        QStringLiteral("profileFor({effectiveProfileDisplayName:'', profileSourceLabel:''})")).toMap();
    const QVariantMap noGame = readinessValue(
        QStringLiteral("gameFor({automaticGameDetection:true, activeCategoryRules:['bf6.exe'], runningApplications:[]})")).toMap();
    const QVariantMap detectedGame = readinessValue(
        QStringLiteral("gameFor({automaticGameDetection:true, activeCategoryName:'Battlefield', activeCategoryRules:['bf6.exe'], runningApplications:[{name:'Battlefield 6', executable:'bf6.exe'}]})")).toMap();
    const QVariantMap hidHideAttention = readinessValue(
        QStringLiteral("isolationFor({checks:[{name:'HIDHIDE ISOLATION', state:'Attention', message:'Review access', severity:'warning'}]})")).toMap();
    if (multipleInput.value(QStringLiteral("title")).toString() != QStringLiteral("3 input devices")
        || noProfile.value(QStringLiteral("title")).toString() != QStringLiteral("No active profile")
        || noGame.value(QStringLiteral("title")).toString() != QStringLiteral("No supported game detected")
        || detectedGame.value(QStringLiteral("title")).toString() != QStringLiteral("Battlefield 6")
        || hidHideAttention.value(QStringLiteral("tone")).toString() != QStringLiteral("attention")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Overview state details are incomplete"));
    }
    QObject *inputHealth = overview->findChild<QObject *>(QStringLiteral("flightDeckHealthInput"));
    QObject *gameHealth = overview->findChild<QObject *>(QStringLiteral("flightDeckHealthGame"));
    if (!inputHealth || !gameHealth
        || !QMetaObject::invokeMethod(inputHealth, "actionRequested")
        || surface->property("currentPage").toInt() != 2) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck input action did not route to the existing setup page"));
    }
    settlePresentation();
    QObject *devices = pageItem(surface, 2);
    if (!devices || devices->objectName() != QStringLiteral("flightDeckDevices")
        || devices->property("requestedContext").toString() != QStringLiteral("controllers")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck input recovery did not load native Devices in controller context"));
    }
    const auto deviceValue = [&](const QString &expression) {
        QQmlExpression call(qmlContext(devices), devices, expression);
        const QVariant value = call.evaluate();
        if (call.hasError()) {
            qCritical().noquote() << call.error().toString();
            return QVariant{};
        }
        return value;
    };
    if (!devices->findChild<QObject *>(QStringLiteral("flightDeckNoControllers"))
        || deviceValue(QStringLiteral("controllerState({connected:false, verified:true})")).toString()
            != QStringLiteral("Disconnected")
        || deviceValue(QStringLiteral("controllerState({connected:true, verified:false, ambiguous:false})")).toString()
            != QStringLiteral("Setup required")
        || deviceValue(QStringLiteral("controllerState({connected:true, verified:true, active:true})")).toString()
            != QStringLiteral("Connected · active")
        || deviceValue(QStringLiteral("toneFor({state:'ACTION REQUIRED', severity:'error'})")).toString()
            != QStringLiteral("fault")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Devices state presentation is incomplete"));
    }
    if (!selectPage(surface, 8)) return false;
    overview = pageItem(surface, 8);
    QObject *outputHealth = overview ? overview->findChild<QObject *>(QStringLiteral("flightDeckHealthOutput")) : nullptr;
    if (!outputHealth || !QMetaObject::invokeMethod(outputHealth, "actionRequested")
        || surface->property("currentPage").toInt() != 2) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck virtual-output recovery did not deep-link to Devices"));
    }
    settlePresentation();
    devices = pageItem(surface, 2);
    if (!devices || devices->property("requestedContext").toString() != QStringLiteral("virtual-output")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck virtual-output recovery did not target virtual output"));
    }
    if (!selectPage(surface, 8)) return false;
    overview = pageItem(surface, 8);
    QObject *isolationHealth = overview ? overview->findChild<QObject *>(QStringLiteral("flightDeckHealthIsolation")) : nullptr;
    if (!isolationHealth || !QMetaObject::invokeMethod(isolationHealth, "actionRequested")
        || surface->property("currentPage").toInt() != 2) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck isolation recovery did not deep-link to Devices"));
    }
    settlePresentation();
    devices = pageItem(surface, 2);
    if (!devices || devices->property("requestedContext").toString() != QStringLiteral("isolation")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck isolation recovery did not target isolation"));
    }
    if (!selectPage(surface, 8)) return false;
    overview = pageItem(surface, 8);
    gameHealth = overview ? overview->findChild<QObject *>(QStringLiteral("flightDeckHealthGame")) : nullptr;
    if (!gameHealth || !QMetaObject::invokeMethod(gameHealth, "actionRequested")
        || surface->property("currentPage").toInt() != 5) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck game action did not route to the existing Profiles page"));
    }
    if (!selectPage(surface, 8)) return false;

    for (const int page : {8, 0, 5, 9}) {
        auto *nav = findVisualItemByObjectName(window->contentItem(),
            QStringLiteral("flightDeckNav_%1").arg(page));
        if (!nav) return failPresentationLifecycleTest(QStringLiteral("Flight Deck route %1 has no nav item")
            .arg(page));
        const QPoint clickPoint = nav->mapToScene(QPointF(nav->width() * 0.5,
            nav->height() * 0.5)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, clickPoint);
        settlePresentation();
        QObject *loaded = pageItem(surface, page);
        if (surface->property("currentPage").toInt() != page
            || !nav->property("selected").toBool() || !loaded) {
            return failPresentationLifecycleTest(QStringLiteral(
                "Flight Deck route %1 did not update selected state and page host (current=%2 selected=%3 loaded=%4)")
                .arg(page).arg(surface->property("currentPage").toInt())
                .arg(nav->property("selected").toBool()).arg(loaded != nullptr));
        }
    }
    if (!surface->setProperty("currentPage", 8)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck could not return to Overview"));
    }
    settlePresentation();
    const QString visualOutputDirectory = qEnvironmentVariable("HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR");
    const auto captureShell = [&](const QString &sizeLabel) {
        // Navigation route clicks intentionally receive focus during the
        // interaction test. Clear that transient focus/hover before capture
        // so a previously visited route cannot look selected beside Overview.
        for (const int page : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}) {
            if (auto *nav = findVisualItemByObjectName(window->contentItem(),
                    QStringLiteral("flightDeckNav_%1").arg(page))) {
                nav->setFocus(false);
                nav->setProperty("suppressTransientEmphasis", true);
            }
        }
        window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2));
        QTest::qWait(160);
        settlePresentation();
        const QImage capture = window->grabWindow();
        if (capture.isNull() || capture.width() < 880 || capture.height() < 620) return false;
        if (!visualOutputDirectory.isEmpty()) {
            QDir().mkpath(visualOutputDirectory);
            const QString capturePath = QDir(visualOutputDirectory).filePath(
                QStringLiteral("flight-deck-%1-%2.png").arg(appearance.toLower(), sizeLabel));
            if (!capture.save(capturePath)) return false;
        }
        return true;
    };
    if (!captureShell(QStringLiteral("normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not render at normal size")
            .arg(appearance));
    }

    // Profiles visual fixtures are presentation-only. They prove hierarchy,
    // selection treatment, empty/long-name resilience, and responsive layout
    // without creating profiles, running game detection, or activating a
    // runtime profile merely by rendering the page.
    if (!selectPage(surface, 5)) return false;
    QObject *profilesPage = pageItem(surface, 5);
    auto *profilesItem = qobject_cast<QQuickItem *>(profilesPage);
    if (!profilesPage || profilesPage->objectName() != QStringLiteral("flightDeckProfiles") || !profilesItem) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not load native Profiles")
            .arg(appearance));
    }
    // A route into Profiles may carry a real deep-link selection. Fixtures
    // intentionally render the library state, so they must not inherit that
    // selection from an earlier navigation assertion.
    QQmlExpression prepareProfilesFixture(qmlContext(profilesPage), profilesPage,
        QStringLiteral("returnToLibrary(); profileFilter = 'all'; searchText = ''"));
    prepareProfilesFixture.evaluate();
    if (prepareProfilesFixture.hasError()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles fixture could not reset its view")
            .arg(appearance));
    }
    const QVariantList profileVisualFixture{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-helicopter")},
            {QStringLiteral("name"), QStringLiteral("Helicopter")},
            {QStringLiteral("categoryId"), QStringLiteral("fixture-battlefield")},
            {QStringLiteral("categoryName"), QStringLiteral("Battlefield")},
            {QStringLiteral("displayName"), QStringLiteral("Battlefield / Helicopter")},
            {QStringLiteral("active"), true}, {QStringLiteral("enabled"), true},
            {QStringLiteral("mappedAxes"), 4}, {QStringLiteral("mappedButtons"), 9},
            {QStringLiteral("mappedPovs"), 2}, {QStringLiteral("automationCount"), 2},
            {QStringLiteral("adaptiveOverrideAxes"), 3}, {QStringLiteral("adaptiveSource"), QStringLiteral("Custom profile response")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-aircraft")},
            {QStringLiteral("name"), QStringLiteral("Aircraft Precision With A Deliberately Long Profile Name")},
            {QStringLiteral("categoryId"), QStringLiteral("fixture-battlefield")},
            {QStringLiteral("categoryName"), QStringLiteral("Battlefield")},
            {QStringLiteral("displayName"), QStringLiteral("Battlefield / Aircraft Precision With A Deliberately Long Profile Name")},
            {QStringLiteral("active"), false}, {QStringLiteral("enabled"), true},
            {QStringLiteral("mappedAxes"), 5}, {QStringLiteral("mappedButtons"), 12},
            {QStringLiteral("mappedPovs"), 0}, {QStringLiteral("automationCount"), 1},
            {QStringLiteral("adaptiveOverrideAxes"), 0}, {QStringLiteral("adaptiveSource"), QStringLiteral("Category response defaults")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-infantry")},
            {QStringLiteral("name"), QStringLiteral("Infantry")},
            {QStringLiteral("categoryId"), QStringLiteral("fixture-battlefield")},
            {QStringLiteral("categoryName"), QStringLiteral("Battlefield")},
            {QStringLiteral("displayName"), QStringLiteral("Battlefield / Infantry")},
            {QStringLiteral("active"), false}, {QStringLiteral("enabled"), true},
            {QStringLiteral("mappedAxes"), 1}, {QStringLiteral("mappedButtons"), 4},
            {QStringLiteral("mappedPovs"), 0}, {QStringLiteral("automationCount"), 0},
            {QStringLiteral("adaptiveOverrideAxes"), 0}, {QStringLiteral("adaptiveSource"), QStringLiteral("Global response defaults")}},
    };
    const QVariantList categoryVisualFixture{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-battlefield")},
            {QStringLiteral("name"), QStringLiteral("Battlefield")}, {QStringLiteral("profileCount"), 3},
            {QStringLiteral("defaultProfileId"), QStringLiteral("fixture-helicopter")},
            {QStringLiteral("defaultProfileName"), QStringLiteral("Battlefield / Helicopter")},
            {QStringLiteral("lastActiveProfileId"), QStringLiteral("fixture-helicopter")},
            {QStringLiteral("lastActiveProfileName"), QStringLiteral("Battlefield / Helicopter")},
            {QStringLiteral("active"), true}, {QStringLiteral("enabled"), true},
            {QStringLiteral("restoreLastProfile"), true}, {QStringLiteral("adaptiveOverrideAxes"), 2},
            {QStringLiteral("executableRules"), QStringList{QStringLiteral("bf6.exe")}}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-empty")},
            {QStringLiteral("name"), QStringLiteral("Long Category Name For A Future Simulator Collection")},
            {QStringLiteral("profileCount"), 0}, {QStringLiteral("defaultProfileId"), QString{}},
            {QStringLiteral("defaultProfileName"), QString{}}, {QStringLiteral("lastActiveProfileId"), QString{}},
            {QStringLiteral("lastActiveProfileName"), QString{}}, {QStringLiteral("active"), false},
            {QStringLiteral("enabled"), true}, {QStringLiteral("restoreLastProfile"), false},
            {QStringLiteral("adaptiveOverrideAxes"), 0}, {QStringLiteral("executableRules"), QStringList{}}},
    };
    QVariantMap fixtureDetails;
    fixtureDetails.insert(QStringLiteral("fixture-aircraft"), QVariantMap{
        {QStringLiteral("id"), QStringLiteral("fixture-aircraft")},
        {QStringLiteral("name"), QStringLiteral("Aircraft Precision With A Deliberately Long Profile Name")},
        {QStringLiteral("displayName"), QStringLiteral("Battlefield / Aircraft Precision With A Deliberately Long Profile Name")},
        {QStringLiteral("categoryId"), QStringLiteral("fixture-battlefield")}, {QStringLiteral("category"), QStringLiteral("Battlefield")},
        {QStringLiteral("categoryGames"), QStringList{QStringLiteral("bf6.exe")}},
        {QStringLiteral("categoryActivationBehavior"), QStringLiteral("Restore the last-used profile")},
        {QStringLiteral("active"), false}, {QStringLiteral("enabled"), true},
        {QStringLiteral("mappedAxes"), 5}, {QStringLiteral("mappedButtons"), 12}, {QStringLiteral("mappedPovs"), 0},
        {QStringLiteral("automationCount"), 1}, {QStringLiteral("adaptiveProfileOverrideAxes"), 0},
        {QStringLiteral("adaptiveSource"), QStringLiteral("Inherited from this category")},
        {QStringLiteral("curveTransitionSmoothingOverride"), false},
        {QStringLiteral("automations"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-rule")}, {QStringLiteral("name"), QStringLiteral("Precision Mode Toggle")}}}},
        {QStringLiteral("relationships"), QVariantMap{{QStringLiteral("referencedBy"), QVariantList{QVariantMap{{QStringLiteral("profile"), QStringLiteral("Button 3")}, {QStringLiteral("via"), QStringLiteral("Toggle profile")}}}}, {QStringLiteral("references"), QVariantList{}}}}
    });
    if (!profilesPage->setProperty("profilesPresentationOverride", profileVisualFixture)
        || !profilesPage->setProperty("categoriesPresentationOverride", categoryVisualFixture)
        || !profilesPage->setProperty("runningApplicationsPresentationOverride", QVariantList{
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Battlefield 6")}, {QStringLiteral("executable"), QStringLiteral("bf6.exe")}}})
        || !profilesPage->setProperty("profileDetailPresentationOverride", fixtureDetails)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    if (!findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckActiveProfileHero"))
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckCategoryCard_fixture-battlefield"))
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckProfileCard_fixture-aircraft"))
        || !captureShell(QStringLiteral("profiles-main"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles main fixture was incomplete")
            .arg(appearance));
    }
    QQmlExpression openFixtureCategory(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openCategory('fixture-battlefield')"));
    openFixtureCategory.evaluate();
    settlePresentation();
    if (openFixtureCategory.hasError() || profilesPage->property("view").toString() != QStringLiteral("category")
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckCategoryBehaviorSelector"))
        || !captureShell(QStringLiteral("profiles-category"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Category fixture did not render")
            .arg(appearance));
    }
    QQmlExpression openFixtureProfile(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('fixture-aircraft')"));
    openFixtureProfile.evaluate();
    settlePresentation();
    if (openFixtureProfile.hasError() || profilesPage->property("view").toString() != QStringLiteral("profile")
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckConfigureAdaptive"))
        || !captureShell(QStringLiteral("profiles-detail-selected-not-active"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profile fixture did not distinguish selected state")
            .arg(appearance));
    }
    const QSize profilesOriginalSize = window->size();
    window->resize(900, 650);
    settlePresentation();
    if (!captureShell(QStringLiteral("profiles-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles minimum layout did not render")
            .arg(appearance));
    }
    window->resize(1600, 980);
    settlePresentation();
    if (!captureShell(QStringLiteral("profiles-wide"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles wide layout did not render")
            .arg(appearance));
    }
    window->resize(profilesOriginalSize);
    profilesPage->setProperty("profilesPresentationOverride", QVariant{});
    profilesPage->setProperty("categoriesPresentationOverride", QVariant{});
    profilesPage->setProperty("runningApplicationsPresentationOverride", QVariant{});
    profilesPage->setProperty("profileDetailPresentationOverride", QVariant{});
    QQmlExpression returnToNativeLibrary(qmlContext(profilesPage), profilesPage, QStringLiteral("returnToLibrary()"));
    returnToNativeLibrary.evaluate();
    settlePresentation();

    // Functional coverage deliberately uses the authoritative Profile model.
    // It proves that selecting a profile for inspection is separate from the
    // existing activation command, and that category/game mutations stay
    // isolated to their intended records.
    const QString originalActiveProfileId = backend.activeProfileId();
    const QString testCategoryName = QStringLiteral("Flight Deck Profiles %1").arg(appearance);
    if (!backend.createProfileCategory(testCategoryName)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 could not create an isolated Profile category")
            .arg(appearance));
    }
    const auto categoryWithName = [&backend](const QString &name) {
        for (const QVariant &value : backend.profileCategories()) {
            const QVariantMap category = value.toMap();
            if (category.value(QStringLiteral("name")).toString() == name) return category;
        }
        return QVariantMap{};
    };
    const auto profileWithName = [&backend](const QString &name) {
        for (const QVariant &value : backend.profiles()) {
            const QVariantMap profile = value.toMap();
            if (profile.value(QStringLiteral("name")).toString() == name) return profile;
        }
        return QVariantMap{};
    };
    const QVariantMap testCategory = categoryWithName(testCategoryName);
    const QString testCategoryId = testCategory.value(QStringLiteral("id")).toString();
    const QString firstName = QStringLiteral("View Only %1").arg(appearance);
    const QString secondName = QStringLiteral("Activate Explicitly %1").arg(appearance);
    if (testCategoryId.isEmpty()
        || !backend.createProfileInCategory(firstName, testCategoryId, originalActiveProfileId)
        || !backend.createProfileInCategory(secondName, testCategoryId, originalActiveProfileId)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 could not create Profile CRUD fixtures")
            .arg(appearance));
    }
    const QString firstId = profileWithName(firstName).value(QStringLiteral("id")).toString();
    const QString secondId = profileWithName(secondName).value(QStringLiteral("id")).toString();
    const QString duplicateName = QStringLiteral("Duplicate %1").arg(appearance);
    const QString renamedDuplicateName = QStringLiteral("Renamed Duplicate %1").arg(appearance);
    if (firstId.isEmpty() || secondId.isEmpty()
        || !backend.duplicateProfileToCategory(firstId, duplicateName, testCategoryId)
        || profileWithName(duplicateName).isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 duplicate command did not create an isolated profile")
            .arg(appearance));
    }
    const QString duplicateId = profileWithName(duplicateName).value(QStringLiteral("id")).toString();
    const QString movedCategoryName = QStringLiteral("Flight Deck Moved %1").arg(appearance);
    if (duplicateId.isEmpty() || !backend.createProfileCategory(movedCategoryName)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 could not create a destination category for profile move")
            .arg(appearance));
    }
    const QString movedCategoryId = categoryWithName(movedCategoryName).value(QStringLiteral("id")).toString();
    if (movedCategoryId.isEmpty() || !backend.moveProfileToCategory(duplicateId, movedCategoryId)
        || profileWithName(duplicateName).value(QStringLiteral("categoryId")).toString() != movedCategoryId
        || !backend.renameProfile(duplicateId, renamedDuplicateName)
        || profileWithName(renamedDuplicateName).value(QStringLiteral("id")).toString() != duplicateId
        || !backend.deleteProfile(duplicateId)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 duplicate, move, rename, or delete command did not retain profile isolation")
            .arg(appearance));
    }
    const QString unaffectedCategoryName = backend.activeCategoryName();
    const QVariantMap unaffectedCategoryBefore = categoryWithName(unaffectedCategoryName);
    if (!backend.setCategoryRestoreLastProfile(testCategoryId, false)
        || !backend.setCategoryDefaultProfile(testCategoryId, firstId)
        || !backend.setCategoryGameDetectionRules(testCategoryId, {QStringLiteral("flight-deck-test.exe")})) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 could not configure its category fixture")
            .arg(appearance));
    }
    QQmlExpression openForViewing(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('%1')").arg(secondId));
    openForViewing.evaluate();
    settlePresentation();
    if (openForViewing.hasError() || backend.activeProfileId() != originalActiveProfileId
        || profilesPage->property("selectedProfileId").toString() != secondId
        || profilesPage->property("view").toString() != QStringLiteral("profile")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 viewing a profile changed active runtime state")
            .arg(appearance));
    }
    auto *activateButton = findVisualItemByObjectName(profilesItem,
        QStringLiteral("flightDeckSelectedProfileActivate"));
    if (!activateButton) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not expose explicit profile activation")
            .arg(appearance));
    }
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        activateButton->mapToScene(QPointF(activateButton->width() * 0.5, activateButton->height() * 0.5)).toPoint());
    settlePresentation();
    if (backend.activeProfileId() != secondId) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 explicit activation did not commit through AppBackend")
            .arg(appearance));
    }
    QQmlExpression openCategoryForBehavior(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openCategory('%1')").arg(testCategoryId));
    openCategoryForBehavior.evaluate();
    settlePresentation();
    QObject *behaviorSelector = findVisualItemByObjectName(profilesItem,
        QStringLiteral("flightDeckCategoryBehaviorSelector"));
    if (openCategoryForBehavior.hasError() || !behaviorSelector
        || !clickResponseComboRow(window, profilesPage, behaviorSelector, 0)
        || !categoryWithName(testCategoryName).value(QStringLiteral("restoreLastProfile")).toBool()
        || categoryWithName(unaffectedCategoryName).value(QStringLiteral("restoreLastProfile"))
            != unaffectedCategoryBefore.value(QStringLiteral("restoreLastProfile"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 category behavior selector was not isolated")
            .arg(appearance));
    }
    QQmlExpression openForDeepLinks(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('%1'); openActiveProfileEditor(0)").arg(secondId));
    openForDeepLinks.evaluate();
    settlePresentation();
    if (openForDeepLinks.hasError() || surface->property("currentPage").toInt() != 0
        || backend.activeProfileId() != secondId) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axes deep link changed profile activation")
            .arg(appearance));
    }
    if (!selectPage(surface, 5)) return false;
    profilesPage = pageItem(surface, 5);
    profilesItem = qobject_cast<QQuickItem *>(profilesPage);
    if (!profilesPage || !profilesItem) return false;
    QQmlExpression reopenForAdaptive(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('%1'); openAdaptiveForSelectedProfile()" ).arg(secondId));
    reopenForAdaptive.evaluate();
    settlePresentation();
    QObject *adaptive = pageItem(surface, 9);
    if (reopenForAdaptive.hasError() || !adaptive || backend.activeProfileId() != secondId
        || adaptive->property("editScope").toString() != QStringLiteral("profile")
        || adaptive->property("targetId").toString() != secondId) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive deep link lost profile context or activated unexpectedly")
            .arg(appearance));
    }
    if (!selectPage(surface, 5)) return false;
    profilesPage = pageItem(surface, 5);
    QQmlExpression openAutomationLink(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('%1'); openAutomationForSelectedProfile()").arg(secondId));
    openAutomationLink.evaluate();
    settlePresentation();
    if (openAutomationLink.hasError() || surface->property("currentPage").toInt() != 7
        || backend.activeProfileId() != secondId) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation deep link changed profile activation")
            .arg(appearance));
    }
    if (backend.deleteProfile(secondId)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 allowed deletion of the active profile")
            .arg(appearance));
    }
    if (!backend.activateProfile(originalActiveProfileId)
        || !backend.deleteProfile(secondId) || !backend.deleteProfile(firstId)
        || !backend.deleteProfileCategory(testCategoryId) || !backend.deleteProfileCategory(movedCategoryId)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profile CRUD cleanup did not preserve authoritative deletion rules")
            .arg(appearance));
    }
    if (!selectPage(surface, 8)) return false;

    // Axis arrangements are presentation-only QML data. They cover the new
    // native page without touching device discovery, persisted mappings, or
    // mapper input state.
    const auto axisFixture = [](int index, const QString &label, const QString &device,
                                const QString &target, double input, double output,
                                bool unipolar = false, bool disabled = false,
                                bool unavailable = false) {
        return QVariant::fromValue(QVariantMap{
            {QStringLiteral("index"), index}, {QStringLiteral("key"), QStringLiteral("axis-%1").arg(index)},
            {QStringLiteral("label"), label}, {QStringLiteral("hardwareLabel"), QStringLiteral("Axis %1").arg(index)},
            {QStringLiteral("detail"), QStringLiteral("DirectInput axis %1").arg(index)},
            {QStringLiteral("deviceName"), device}, {QStringLiteral("available"), true},
            {QStringLiteral("fixed"), false}, {QStringLiteral("activity"), QStringLiteral("active")},
            {QStringLiteral("calibrated"), input}, {QStringLiteral("transformed"), output},
            {QStringLiteral("virtualValue"), output}, {QStringLiteral("target"), disabled ? QStringLiteral("Disabled") : target},
            {QStringLiteral("virtualRouted"), !disabled && !unavailable},
            {QStringLiteral("virtualValid"), !disabled && !unavailable},
            {QStringLiteral("targetAvailable"), !unavailable}, {QStringLiteral("outputAlias"), QString{}},
            {QStringLiteral("rangeMode"), unipolar ? QStringLiteral("oneSided") : QStringLiteral("centered")},
            {QStringLiteral("rangeModeLabel"), unipolar ? QStringLiteral("One-Sided (0 to 100)") : QStringLiteral("Centered (-100 to +100)")},
            {QStringLiteral("unipolar"), unipolar}, {QStringLiteral("inverted"), index == 1},
            {QStringLiteral("deadzone"), index == 1 ? 0.08 : 0.02},
            {QStringLiteral("hysteresis"), 0.0}, {QStringLiteral("outputMinimum"), unipolar ? 0.0 : -1.0},
            {QStringLiteral("outputMaximum"), 1.0}, {QStringLiteral("curveSummary"), index == 2 ? QStringLiteral("S-Curve · 50%") : QStringLiteral("Linear · 0%")},
            {QStringLiteral("customName"), QString{}},
        });
    };
    const QVariantList fourAxisFixture{
        axisFixture(0, QStringLiteral("Roll"), QStringLiteral("VKB Gunfighter IV"), QStringLiteral("X"), -0.43, -0.39),
        axisFixture(1, QStringLiteral("Pitch Control With A Long Friendly Name"), QStringLiteral("VKB Gunfighter IV"), QStringLiteral("Y"), 0.24, -0.18),
        axisFixture(2, QStringLiteral("Throttle"), QStringLiteral("VKB STECS Throttle"), QStringLiteral("Z"), 0.72, 0.68, true),
        axisFixture(5, QStringLiteral("Yaw"), QStringLiteral("MFG Crosswind Pedals"), QStringLiteral("Rz"), 0.08, 0.08),
    };
    QVariantList eightAxisFixture = fourAxisFixture;
    eightAxisFixture.append(axisFixture(3, QStringLiteral("Brake Axis With A Deliberately Long Friendly Name"),
        QStringLiteral("VKB STECS Throttle · Long Device Identity"), QStringLiteral("Rx"), -0.12, -0.12));
    eightAxisFixture.append(axisFixture(4, QStringLiteral("Trim"), QStringLiteral("VKB Gunfighter IV"),
        QStringLiteral("Ry"), 0.31, 0.30));
    eightAxisFixture.append(axisFixture(6, QStringLiteral("Left Brake"), QStringLiteral("MFG Crosswind Pedals"),
        QStringLiteral("Slider 0"), 0.56, 0.56, true));
    eightAxisFixture.append(axisFixture(7, QStringLiteral("Right Brake"), QStringLiteral("MFG Crosswind Pedals"),
        QStringLiteral("Slider 1"), 0.48, 0.0, true, false, true));

    if (!selectPage(surface, 0)) return false;
    QObject *axes = pageItem(surface, 0);
    if (!axes || axes->objectName() != QStringLiteral("flightDeckAxes")
        || !axes->setProperty("axisPresentationOverride", fourAxisFixture)
        || !axes->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native Axes page did not accept its test-only fixture")
            .arg(appearance));
    }
    settlePresentation();
    const QStringList expectedAxisChoices{QStringLiteral("Disabled"), QStringLiteral("X"),
        QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("Rx"),
        QStringLiteral("Ry"), QStringLiteral("Rz"), QStringLiteral("Slider 0"),
        QStringLiteral("Slider 1")};
    const QStringList nativeAxisChoices = axes->property("outputChoices").toStringList();
    auto *axesItem = qobject_cast<QQuickItem *>(axes);
    const bool hasRollCard = findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckAxisCard_0"));
    const bool hasYawCard = findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckAxisCard_5"));
    if (nativeAxisChoices != expectedAxisChoices || !hasRollCard || !hasYawCard) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axes selector/cards incomplete: choices=%2 expected=%3 visible=%4 roll=%5 yaw=%6")
            .arg(appearance).arg(nativeAxisChoices.join(u"/"_qs)).arg(expectedAxisChoices.join(u"/"_qs))
            .arg(axes->property("hasVisibleAxes").toBool()).arg(hasRollCard).arg(hasYawCard));
    }
    if (!backend.setAdaptiveResponsePreset(QStringLiteral("profile"), 0, QStringLiteral("fast"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axes fixture could not apply the existing Fast Adaptive Response preset")
            .arg(appearance));
    }
    if (!captureShell(QStringLiteral("axes-four-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 collapsed four-axis fixture did not render")
            .arg(appearance));
    }
    QQmlExpression configureAxis(qmlContext(axes), axes, QStringLiteral("configureAxis(0)"));
    configureAxis.evaluate();
    settlePresentation();
    if (configureAxis.hasError() || backend.selectedAxisIndex() != 0
        || axes->property("expandedAxisIndex").toInt() != 0
        || !findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckMappingSelector_0"))
        || !findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckResponsePreview_0"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axes configure flow did not select the authoritative axis or materialize its static preview")
            .arg(appearance));
    }
    // Exercise the actual native mapping selector, then its command path with
    // three independent rows. Both must survive immediate model refresh.
    if (!backend.setMapping(0, QStringLiteral("X"), true)
        || !backend.setMapping(1, QStringLiteral("Y"), true)
        || !backend.setMapping(2, QStringLiteral("Rz"), true)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native mapping fixture could not establish independent routes")
            .arg(appearance));
    }
    QObject *mappingSelector = findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckMappingSelector_0"));
    const int sliderChoice = expectedAxisChoices.indexOf(QStringLiteral("Slider 0"));
    const bool selectorClicked = mappingSelector && sliderChoice >= 0
        && clickResponseComboRow(window, axes, mappingSelector, sliderChoice, false);
    const QString selectedRoute = targetForAxis(backend.axes(), 0);
    if (!mappingSelector || sliderChoice < 0 || !selectorClicked
        || selectedRoute != QStringLiteral("Slider 0")
        || !backend.setMapping(0, QStringLiteral("X"), true)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native mapping selector did not complete a pointer-selected route: selector=%2 choice=%3 clicked=%4 index=%5 route=%6")
            .arg(appearance).arg(mappingSelector != nullptr).arg(sliderChoice).arg(selectorClicked)
            .arg(mappingSelector ? mappingSelector->property("currentIndex").toInt() : -1).arg(selectedRoute));
    }
    QQmlExpression mapAxisB(qmlContext(axes), axes, QStringLiteral("requestMapping(1, 'Z', false)"));
    const bool mapAxisBResult = mapAxisB.evaluate().toBool();
    const QVariantList nativeMappedAxes = backend.axes();
    if (mapAxisB.hasError() || !mapAxisBResult
        || targetForAxis(nativeMappedAxes, 0) != QStringLiteral("X")
        || targetForAxis(nativeMappedAxes, 1) != QStringLiteral("Z")
        || targetForAxis(nativeMappedAxes, 2) != QStringLiteral("Rz")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native mapping selector changed an unrelated route or did not persist Axis B -> Z")
            .arg(appearance));
    }
    const QVariantMap beforeSettingsIsolation = targetForAxis(backend.axes(), 1).isEmpty()
        ? QVariantMap{} : backend.axes().at(1).toMap();
    backend.setAxisInverted(0, true);
    backend.setAxisDeadzone(0, 0.11);
    backend.setAxisOutputLimits(0, -0.72, 0.76);
    backend.setSelectedAxis(0);
    backend.setCurveFamily(QStringLiteral("S-Curve"));
    const QVariantMap afterSettingsIsolation = backend.axes().at(1).toMap();
    if (beforeSettingsIsolation.value(QStringLiteral("inverted")) != afterSettingsIsolation.value(QStringLiteral("inverted"))
        || beforeSettingsIsolation.value(QStringLiteral("deadzone")) != afterSettingsIsolation.value(QStringLiteral("deadzone"))
        || beforeSettingsIsolation.value(QStringLiteral("outputMinimum")) != afterSettingsIsolation.value(QStringLiteral("outputMinimum"))
        || beforeSettingsIsolation.value(QStringLiteral("outputMaximum")) != afterSettingsIsolation.value(QStringLiteral("outputMaximum"))
        || beforeSettingsIsolation.value(QStringLiteral("curveSummary")) != afterSettingsIsolation.value(QStringLiteral("curveSummary"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 axis response controls mutated a neighbouring axis")
            .arg(appearance));
    }
    if (!captureShell(QStringLiteral("axes-expanded-response"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 expanded response fixture did not render")
            .arg(appearance));
    }
    QQmlExpression closeAxis(qmlContext(axes), axes, QStringLiteral("configureAxis(0)"));
    closeAxis.evaluate();
    if (closeAxis.hasError()) return false;
    if (!axes->setProperty("axisPresentationOverride", eightAxisFixture)) return false;
    settlePresentation();
    if (!findVisualItemByObjectName(axesItem, QStringLiteral("flightDeckAxisCard_7"))
        || !captureShell(QStringLiteral("axes-eight-multidevice-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 eight-axis multi-device fixture did not render")
            .arg(appearance));
    }
    QQmlExpression configureUnavailable(qmlContext(axes), axes, QStringLiteral("configureAxis(7)"));
    configureUnavailable.evaluate();
    axes->setProperty("contentY", std::max<qreal>(0.0,
        axes->property("contentHeight").toReal() - axes->property("height").toReal()));
    settlePresentation();
    if (configureUnavailable.hasError() || !captureShell(QStringLiteral("axes-unavailable-output"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 unavailable-output fixture did not render")
            .arg(appearance));
    }
    QQmlExpression adaptiveDeepLink(qmlContext(axes), axes, QStringLiteral("openAdaptiveForAxis(0)"));
    adaptiveDeepLink.evaluate();
    settlePresentation();
    if (adaptiveDeepLink.hasError() || surface->property("currentPage").toInt() != 9
        || backend.selectedAxisIndex() != 0) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axes Adaptive Response deep link did not preserve axis context")
            .arg(appearance));
    }

    // This test-only fixture publishes the same bounded UI-facing button/POV
    // snapshot a connected controller would provide. It does not enumerate a
    // device, issue a report, or start virtual output; the selectors below
    // still invoke the real AppBackend configuration commands.
    backend.setButtonUiFixtureForTest(32, 32, 1, 1);
    if (!backend.setButtonMapping(1, 1, true)
        || !backend.setButtonMapping(2, 2, true)
        || !backend.setButtonMapping(3, 3, true)
        || !backend.setMappingControl(4, QStringLiteral("Toggle Mapping"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons fixture could not establish independent authoritative routes")
            .arg(appearance));
    }
    const QVariantList profileChoices = backend.profileTriggerChoices();
    const QString profileTargetId = profileChoices.size() > 1
        ? profileChoices.at(1).toMap().value(QStringLiteral("id")).toString() : QString{};
    if (!profileTargetId.isEmpty()
        && !backend.setProfileTrigger(3, profileTargetId, QStringLiteral("Toggle"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons fixture could not configure the existing profile control")
            .arg(appearance));
    }
    // An incomplete, disabled Automation draft is enough to exercise the
    // presentation-only relationship and deep-link path. It cannot enter the
    // runtime set or execute an output action.
    const QString automationTargetId = backend.createAutomation();
    if (automationTargetId.isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons fixture could not create an Automation navigation target")
            .arg(appearance));
    }
    if (!selectPage(surface, 1)) return false;
    QObject *buttons = pageItem(surface, 1);
    auto *buttonsItem = qobject_cast<QQuickItem *>(buttons);
    const auto interactiveButton = [](int index, int target) {
        return QVariant::fromValue(QVariantMap{
            {QStringLiteral("index"), index}, {QStringLiteral("label"),
                index == 1 ? QStringLiteral("Trigger") : QStringLiteral("Button %1").arg(index)},
            {QStringLiteral("hardwareLabel"), QStringLiteral("Button %1").arg(index)},
            {QStringLiteral("customName"), index == 1 ? QStringLiteral("Trigger") : QString{}},
            {QStringLiteral("pressed"), false}, {QStringLiteral("target"), target},
            {QStringLiteral("targetLabel"), QStringLiteral("vJoy Button %1").arg(target)},
            {QStringLiteral("virtualPressed"), false}, {QStringLiteral("profileControlEnabled"), false},
            {QStringLiteral("profileControlTargetId"), QString{}}, {QStringLiteral("profileControlTargetName"), QString{}},
            {QStringLiteral("profileControlTargetAvailable"), false}, {QStringLiteral("profileControlMode"), QString{}},
            {QStringLiteral("profileControlActive"), false}, {QStringLiteral("mappingControl"), QStringLiteral("None")},
            {QStringLiteral("mappingControlKey"), QStringLiteral("none")},
        });
    };
    const QVariantList interactiveButtons{interactiveButton(1, 1), interactiveButton(2, 2),
        interactiveButton(3, 3), interactiveButton(4, 4)};
    if (!buttons || !buttons->setProperty("buttonPresentationOverride", interactiveButtons)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons interactive presentation fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    if (!buttons || buttons->objectName() != QStringLiteral("flightDeckButtons") || !buttonsItem
        || !findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_1"))
        || !findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_3"))) {
        QQmlExpression visibleCount(qmlContext(buttons), buttons, QStringLiteral("visibleButtonCount()"));
        const int count = visibleCount.evaluate().toInt();
        QQmlExpression assignedCount(qmlContext(buttons), buttons, QStringLiteral("assignedButtonCount()"));
        const int assigned = assignedCount.evaluate().toInt();
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not load native scan-first Buttons cards (fixture=%2 visible=%3 assigned=%4 cards=%5/%6)")
            .arg(appearance).arg(buttons->property("buttonItems").toList().size()).arg(count)
            .arg(assigned).arg(findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_1")) != nullptr)
            .arg(findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_3")) != nullptr));
    }
    QQmlExpression configureButton(qmlContext(buttons), buttons, QStringLiteral("setExpandedButton(2)"));
    configureButton.evaluate();
    settlePresentation();
    // Re-arm the bounded test snapshot immediately before the pointer action.
    // The normal worker is allowed to continue publishing an empty physical
    // state on this no-device host, so the test does not retain a fake device.
    backend.setButtonUiFixtureForTest(32, 32, 1, 1);
    if (!backend.setButtonMapping(1, 1, true)
        || !backend.setButtonMapping(2, 2, true)
        || !backend.setButtonMapping(3, 3, true)) return false;
    settlePresentation();
    QObject *buttonSelector = findVisualItemByObjectName(buttonsItem,
        QStringLiteral("flightDeckButtonMappingSelector_2"));
    const bool buttonSelectorClicked = buttonSelector
        && clickResponseComboRow(window, buttons, buttonSelector, 7, false);
    if (configureButton.hasError() || !buttonSelector || !buttonSelectorClicked
        || targetForButton(backend.buttons(), 1) != 1
        || targetForButton(backend.buttons(), 2) != 7
        || targetForButton(backend.buttons(), 3) != 3) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 pointer-selected Button 2 -> vJoy 7 did not persist in isolation")
            .arg(appearance));
    }
    QQmlExpression clearButton(qmlContext(buttons), buttons, QStringLiteral("requestButtonMapping(2, 0, false)"));
    if (!clearButton.evaluate().toBool() || clearButton.hasError() || targetForButton(backend.buttons(), 2) != 0
        || targetForButton(backend.buttons(), 1) != 1 || targetForButton(backend.buttons(), 3) != 3) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 clear assignment changed an unrelated button route")
            .arg(appearance));
    }
    if (!profileTargetId.isEmpty()) {
        QQmlExpression openProfile(qmlContext(buttons), buttons,
            QStringLiteral("navigateToProfile('%1')").arg(profileTargetId));
        openProfile.evaluate();
        settlePresentation();
        QObject *profiles = pageItem(surface, 5);
        if (openProfile.hasError() || !profiles
            || profiles->property("selectedProfileId").toString() != profileTargetId) {
            return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 profile deep link did not select its referenced profile")
                .arg(appearance));
        }
        if (!selectPage(surface, 1)) return false;
        buttons = pageItem(surface, 1);
        buttonsItem = qobject_cast<QQuickItem *>(buttons);
        if (!buttons || !buttonsItem) return false;
    }
    const auto buttonFixture = [](int index, const QString &label, int target, bool pressed,
                                  bool profileControl = false, const QString &profileName = QString{},
                                  const QString &mappingControl = QStringLiteral("None")) {
        const QString hardwareLabel = QStringLiteral("Button %1").arg(index);
        return QVariant::fromValue(QVariantMap{
            {QStringLiteral("index"), index}, {QStringLiteral("label"), label},
            {QStringLiteral("hardwareLabel"), hardwareLabel},
            {QStringLiteral("customName"), label == hardwareLabel ? QString{} : label},
            {QStringLiteral("pressed"), pressed}, {QStringLiteral("target"), target},
            {QStringLiteral("targetLabel"), target > 0 ? QStringLiteral("vJoy Button %1").arg(target) : QStringLiteral("Disabled")},
            {QStringLiteral("virtualPressed"), pressed && target > 0},
            {QStringLiteral("profileControlEnabled"), profileControl},
            {QStringLiteral("profileControlTargetId"), QStringLiteral("fixture-profile")},
            {QStringLiteral("profileControlTargetName"), profileName},
            {QStringLiteral("profileControlTargetAvailable"), profileControl},
            {QStringLiteral("profileControlMode"), QStringLiteral("Toggle")},
            {QStringLiteral("profileControlActive"), false},
            {QStringLiteral("mappingControl"), mappingControl},
            {QStringLiteral("mappingControlKey"), mappingControl == QStringLiteral("None") ? QStringLiteral("none") : QStringLiteral("toggleMapping")},
        });
    };
    QVariantList mixedButtons{
        buttonFixture(1, QStringLiteral("Trigger"), 1, true),
        buttonFixture(2, QStringLiteral("Precision"), 2, false, true, QStringLiteral("BF6 / Helicopter Precision")),
        buttonFixture(3, QStringLiteral("Button 3"), 0, false),
        buttonFixture(4, QStringLiteral("Mapping switch"), 0, false, false, QString{}, QStringLiteral("Toggle Mapping")),
        buttonFixture(5, QStringLiteral("A deliberately long physical control label for a throttle panel"), 9, false),
        buttonFixture(6, QStringLiteral("Button 6"), 0, false),
    };
    QVariantList largeButtons = mixedButtons;
    for (int index = 7; index <= 32; ++index) {
        largeButtons.append(buttonFixture(index, QStringLiteral("Button %1").arg(index),
            index <= 12 ? index : 0, false));
    }
    const QVariantList automationFixture{
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), automationTargetId},
            {QStringLiteral("name"), QStringLiteral("Precision Mode Toggle")},
            {QStringLiteral("conditionSummary"), QStringLiteral("Button 2 is pressed")},
            {QStringLiteral("conditions"), QVariantList{QVariant::fromValue(QVariantMap{
                {QStringLiteral("type"), 11}, {QStringLiteral("button"), 2}})}}}),
    };
    const auto povFixture = []() {
        return QVariant::fromValue(QVariantMap{{QStringLiteral("index"), 1},
            {QStringLiteral("centered"), false}, {QStringLiteral("direction"), QStringLiteral("Up")},
            {QStringLiteral("angle"), 0}, {QStringLiteral("nativeEnabled"), false},
            {QStringLiteral("nativeTargetKey"), QString{}}, {QStringLiteral("nativeTargetLabel"), QStringLiteral("Off")},
            {QStringLiteral("nativeAvailable"), true}, {QStringLiteral("nativeStatus"), QStringLiteral("OFF")}});
    };
    const QStringList directionNames{QStringLiteral("Up"), QStringLiteral("Up-Right"),
        QStringLiteral("Right"), QStringLiteral("Down-Right"), QStringLiteral("Down"),
        QStringLiteral("Down-Left"), QStringLiteral("Left"), QStringLiteral("Up-Left")};
    QVariantList povInputFixture;
    for (int direction = 0; direction < directionNames.size(); ++direction) {
        povInputFixture.append(QVariant::fromValue(QVariantMap{{QStringLiteral("hat"), 1},
            {QStringLiteral("direction"), direction}, {QStringLiteral("label"), directionNames.at(direction)},
            {QStringLiteral("active"), direction == 0}, {QStringLiteral("target"), direction == 0 ? 10 : 0},
            {QStringLiteral("targetLabel"), direction == 0 ? QStringLiteral("vJoy Button 10") : QStringLiteral("Disabled")},
            {QStringLiteral("virtualPressed"), direction == 0}, {QStringLiteral("profileControlEnabled"), false},
            {QStringLiteral("profileControlTargetId"), QString{}}, {QStringLiteral("profileControlTargetName"), QString{}},
            {QStringLiteral("profileControlTargetAvailable"), false}, {QStringLiteral("profileControlMode"), QString{}},
            {QStringLiteral("profileControlActive"), false}}));
    }
    if (!buttons->setProperty("buttonPresentationOverride", mixedButtons)
        || !buttons->setProperty("povPresentationOverride", QVariantList{povFixture()})
        || !buttons->setProperty("povInputsPresentationOverride", povInputFixture)
        || !buttons->setProperty("automationPresentationOverride", automationFixture)
        || !buttons->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV · Long Device Name"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons visual fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    auto *automationButton = findVisualItemByObjectName(buttonsItem,
        QStringLiteral("flightDeckButtonCard_2"));
    if (!automationButton || automationButton->property("automations").toList().size() != 1) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons page did not expose its existing Automation relationship")
            .arg(appearance));
    }
    QQmlExpression openAutomation(qmlContext(buttons), buttons,
        QStringLiteral("navigateToAutomation('%1')").arg(automationTargetId));
    openAutomation.evaluate();
    settlePresentation();
    QObject *automation = pageItem(surface, 7);
    if (openAutomation.hasError() || !automation
        || automation->property("editingId").toString() != automationTargetId) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation deep link did not select its referenced rule")
            .arg(appearance));
    }
    if (!backend.deleteAutomation(automationTargetId) || !selectPage(surface, 1)) return false;
    buttons = pageItem(surface, 1);
    buttonsItem = qobject_cast<QQuickItem *>(buttons);
    if (!buttons || !buttonsItem
        || !buttons->setProperty("buttonPresentationOverride", mixedButtons)
        || !buttons->setProperty("povPresentationOverride", QVariantList{povFixture()})
        || !buttons->setProperty("povInputsPresentationOverride", povInputFixture)
        || !buttons->setProperty("automationPresentationOverride", automationFixture)
        || !buttons->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV · Long Device Name"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Buttons fixture could not be restored after Automation navigation")
            .arg(appearance));
    }
    settlePresentation();
    if (!findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_1"))
        || !findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckHatCard_1"))
        || !captureShell(QStringLiteral("buttons-mixed-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 mixed Buttons/Hat fixture did not render")
            .arg(appearance));
    }
    QQmlExpression configureHat(qmlContext(buttons), buttons, QStringLiteral("setExpandedPov(1, 0)"));
    configureHat.evaluate();
    settlePresentation();
    backend.setButtonUiFixtureForTest(32, 32, 1, 1);
    if (!backend.setPovMapping(1, 0, 10, true)
        || !backend.setPovMapping(1, 1, 11, true)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Hat fixture could not establish independent direction routes")
            .arg(appearance));
    }
    buttons->setProperty("contentY", std::max<qreal>(0.0,
        buttons->property("contentHeight").toReal() - buttons->property("height").toReal()));
    settlePresentation();
    QObject *povSelector = findVisualItemByObjectName(buttonsItem,
        QStringLiteral("flightDeckPovMappingSelector_1_0"));
    const bool povSelectorClicked = povSelector
        && clickResponseComboRow(window, buttons, povSelector, 5, false);
    if (configureHat.hasError() || !povSelector || !povSelectorClicked
        || targetForPovDirection(backend.povInputs(), 1, 0) != 5
        || targetForPovDirection(backend.povInputs(), 1, 1) != 11
        || !captureShell(QStringLiteral("buttons-hat-expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Hat direction selector did not preserve adjacent direction routes (selector=%2 up=%3 upRight=%4)")
            .arg(appearance).arg(povSelectorClicked)
            .arg(targetForPovDirection(backend.povInputs(), 1, 0))
            .arg(targetForPovDirection(backend.povInputs(), 1, 1)));
    }
    if (!buttons->setProperty("buttonPresentationOverride", largeButtons)
        || !captureShell(QStringLiteral("buttons-large-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 large button fixture did not render")
            .arg(appearance));
    }
    QQmlExpression filterUnassigned(qmlContext(buttons), buttons, QStringLiteral("filterMode = 'unassigned'; visibleButtonCount()"));
    if (filterUnassigned.evaluate().toInt() <= 0 || filterUnassigned.hasError()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 presentation-only button filtering did not retain unassigned controls")
            .arg(appearance));
    }
    if (!selectPage(surface, 2) || !captureShell(QStringLiteral("devices-empty-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Devices empty state did not render")
            .arg(appearance));
    }
    // These fixtures exercise the native Devices composition only. They are
    // assigned to presentation-only QML properties, never to AppBackend,
    // device discovery, the controller store, or mapper state.
    const QVariantList readyChecks{
        QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("PHYSICAL CONTROLLER")},
            {QStringLiteral("state"), QStringLiteral("READY")},
            {QStringLiteral("message"), QStringLiteral("Physical controller input is available.")},
            {QStringLiteral("severity"), QStringLiteral("ready")}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("VJOY OUTPUT")},
            {QStringLiteral("state"), QStringLiteral("READY")},
            {QStringLiteral("message"), QStringLiteral("Virtual output is online.")},
            {QStringLiteral("severity"), QStringLiteral("ready")}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("HIDHIDE ISOLATION")},
            {QStringLiteral("state"), QStringLiteral("READY")},
            {QStringLiteral("message"), QStringLiteral("Exact-device isolation is verified.")},
            {QStringLiteral("severity"), QStringLiteral("ready")}}),
    };
    const QVariantMap readyVisualState{
        {QStringLiteral("physicalConnected"), true},
        {QStringLiteral("connectedControllerCount"), 3},
        {QStringLiteral("deviceName"), QStringLiteral("Flight Stick")},
        {QStringLiteral("mappingActive"), true},
        {QStringLiteral("mappingRequested"), true},
        {QStringLiteral("mappingStatus"), QStringLiteral("MAPPING ACTIVE")},
        {QStringLiteral("vjoyReady"), true},
        {QStringLiteral("vjoyStatus"), QStringLiteral("vJoy 1 online")},
        {QStringLiteral("vjoyStatusSeverity"), QStringLiteral("ready")},
        {QStringLiteral("vjoyDeviceId"), QStringLiteral("1")},
        {QStringLiteral("vjoyButtonCount"), 32},
        {QStringLiteral("vjoyContinuousPovCount"), 1},
        {QStringLiteral("vjoyDiscretePovCount"), 0},
        {QStringLiteral("outputLayoutName"), QStringLiteral("BF6 Output")},
        {QStringLiteral("controllerReadinessState"), QStringLiteral("READY")},
        {QStringLiteral("controllerReadinessStatus"), QStringLiteral("Controller setup is ready for use.")},
        {QStringLiteral("controllerReadinessProposedChanges"), QVariantList{}},
        {QStringLiteral("controllerReconnectRequired"), false},
        {QStringLiteral("controllerDisconnectObserved"), false},
        {QStringLiteral("controllerSetupInProgress"), false},
        {QStringLiteral("controllerSetupCanApply"), false},
        {QStringLiteral("controllerSetupCanUndo"), false},
        {QStringLiteral("hidhideAvailable"), true},
        {QStringLiteral("hidhideCloakStateKnown"), true},
        {QStringLiteral("hidhideCloaked"), true},
        {QStringLiteral("hidhideMapperAllowed"), true},
        {QStringLiteral("checks"), readyChecks},
        {QStringLiteral("effectiveProfileDisplayName"), QStringLiteral("BF6 Helicopter")},
        {QStringLiteral("profileSourceLabel"), QStringLiteral("Automatic game profile")},
        {QStringLiteral("automaticGameDetection"), true},
        {QStringLiteral("activeCategoryName"), QStringLiteral("Battlefield 6")},
        {QStringLiteral("activeCategoryRules"), QStringList{QStringLiteral("bf6.exe")}},
        {QStringLiteral("runningApplications"), QVariantList{QVariant::fromValue(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("Battlefield 6")},
            {QStringLiteral("executable"), QStringLiteral("bf6.exe")},
        })}},
    };
    const QVariantList multiControllerFixture{
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("stick-record")},
            {QStringLiteral("directInputId"), QStringLiteral("stick-di")}, {QStringLiteral("name"), QStringLiteral("VKB Gunfighter IV")},
            {QStringLiteral("connected"), true}, {QStringLiteral("verified"), true},
            {QStringLiteral("selected"), true}, {QStringLiteral("ambiguous"), false}, {QStringLiteral("active"), true},
            {QStringLiteral("axisCount"), 6}, {QStringLiteral("buttonCount"), 32}, {QStringLiteral("povCount"), 1}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("throttle-record")},
            {QStringLiteral("directInputId"), QStringLiteral("throttle-di")}, {QStringLiteral("name"), QStringLiteral("VKB STECS Throttle")},
            {QStringLiteral("connected"), true}, {QStringLiteral("verified"), true},
            {QStringLiteral("selected"), false}, {QStringLiteral("ambiguous"), false}, {QStringLiteral("active"), false},
            {QStringLiteral("axisCount"), 5}, {QStringLiteral("buttonCount"), 29}, {QStringLiteral("povCount"), 0}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("")},
            {QStringLiteral("directInputId"), QStringLiteral("panel-di")},
            {QStringLiteral("name"), QString(88, u'X') + QStringLiteral(" Button Panel")},
            {QStringLiteral("connected"), true}, {QStringLiteral("verified"), false},
            {QStringLiteral("selected"), false}, {QStringLiteral("ambiguous"), false}, {QStringLiteral("active"), false},
            {QStringLiteral("axisCount"), 0}, {QStringLiteral("buttonCount"), 48}, {QStringLiteral("povCount"), 0}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("pedals-record")},
            {QStringLiteral("directInputId"), QStringLiteral("pedals-di")}, {QStringLiteral("name"), QStringLiteral("MFG Crosswind Pedals")},
            {QStringLiteral("connected"), false}, {QStringLiteral("verified"), true},
            {QStringLiteral("selected"), false}, {QStringLiteral("ambiguous"), false}, {QStringLiteral("active"), false},
            {QStringLiteral("axisCount"), 3}, {QStringLiteral("buttonCount"), 0}, {QStringLiteral("povCount"), 0}}),
    };
    const QVariantList unverifiedControllerFixture{multiControllerFixture.at(2)};
    const auto showDevicesFixture = [&](const QVariantMap &fixture, const QVariantList &controllers) {
        readinessModel->setProperty("presentationStateOverride", fixture);
        if (!selectPage(surface, 2)) return static_cast<QObject *>(nullptr);
        settlePresentation();
        QObject *fixtureDevices = pageItem(surface, 2);
        if (!fixtureDevices || !fixtureDevices->setProperty("controllerPresentationOverride", controllers))
            return static_cast<QObject *>(nullptr);
        settlePresentation();
        return fixtureDevices;
    };

    if (!selectPage(surface, 8)) return false;
    readinessModel->setProperty("presentationStateOverride", readyVisualState);
    settlePresentation();
    if (!captureShell(QStringLiteral("ready-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 ready state did not render")
            .arg(appearance));
    }

    devices = showDevicesFixture(readyVisualState, multiControllerFixture);
    const QVariantMap sharedReadiness = readinessModel->property("readiness").toMap();
    QObject *emptyState = devices ? devices->findChild<QObject *>(QStringLiteral("flightDeckNoControllers")) : nullptr;
    QObject *controllerRepeater = devices ? devices->findChild<QObject *>(QStringLiteral("flightDeckControllerRepeater")) : nullptr;
    const int controllerCount = devices ? deviceValue(QStringLiteral("controllerItems.length")).toInt() : -1;
    const QString firstAction = devices ? deviceValue(QStringLiteral("controllerActionLabel(controllerItems[0])")).toString() : QString{};
    const QString secondAction = devices ? deviceValue(QStringLiteral("controllerActionLabel(controllerItems[1])")).toString() : QString{};
    const QString thirdAction = devices ? deviceValue(QStringLiteral("controllerActionLabel(controllerItems[2])")).toString() : QString{};
    const QString fourthAction = devices ? deviceValue(QStringLiteral("controllerActionLabel(controllerItems[3])")).toString() : QString{};
    const bool multiCaptured = captureShell(QStringLiteral("devices-multi-normal"));
    if (!devices || devices->property("readiness").toMap() != sharedReadiness || controllerCount != 4
        || !emptyState || emptyState->property("visible").toBool() || !controllerRepeater
        || controllerRepeater->property("count").toInt() != 4
        || firstAction != QStringLiteral("ACTIVE") || secondAction != QStringLiteral("USE CONTROLLER")
        || thirdAction != QStringLiteral("VERIFY CONTROLLER") || fourthAction != QStringLiteral("RESCAN")
        || !multiCaptured) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 Devices multi-controller fixture was incomplete: readiness=%2 count=%3 empty=%4 repeater=%5 actions=%6/%7/%8/%9")
            .arg(appearance).arg(devices && devices->property("readiness").toMap() == sharedReadiness)
            .arg(controllerCount).arg(emptyState ? emptyState->property("visible").toBool() : true)
            .arg(controllerRepeater ? controllerRepeater->property("count").toInt() : -1)
            .arg(firstAction).arg(secondAction).arg(thirdAction).arg(fourthAction));
    }

    QVariantMap vjoyAttentionState = readyVisualState;
    vjoyAttentionState.insert(QStringLiteral("vjoyReady"), false);
    vjoyAttentionState.insert(QStringLiteral("vjoyStatus"), QStringLiteral("vJoy Device 1 is unavailable."));
    vjoyAttentionState.insert(QStringLiteral("vjoyStatusSeverity"), QStringLiteral("error"));
    QVariantList vjoyAttentionChecks = readyChecks;
    vjoyAttentionChecks[1] = QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("VJOY OUTPUT")},
        {QStringLiteral("state"), QStringLiteral("ACTION REQUIRED")},
        {QStringLiteral("message"), QStringLiteral("Virtual output is unavailable, so games cannot receive mapped input.")},
        {QStringLiteral("severity"), QStringLiteral("error")}});
    vjoyAttentionState.insert(QStringLiteral("checks"), vjoyAttentionChecks);
    devices = showDevicesFixture(vjoyAttentionState, multiControllerFixture);
    if (!devices || devices->property("vjoyReady").toBool()
        || !devices->findChild<QObject *>(QStringLiteral("flightDeckVirtualOutput"))
        || !captureShell(QStringLiteral("devices-vjoy-attention-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Virtual Output attention fixture did not render")
            .arg(appearance));
    }

    QVariantMap isolationAttentionState = readyVisualState;
    isolationAttentionState.insert(QStringLiteral("hidhideAvailable"), false);
    isolationAttentionState.insert(QStringLiteral("hidhideCloakStateKnown"), false);
    QVariantList isolationAttentionChecks = readyChecks;
    isolationAttentionChecks[2] = QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("HIDHIDE ISOLATION")},
        {QStringLiteral("state"), QStringLiteral("ACTION REQUIRED")},
        {QStringLiteral("message"), QStringLiteral("Physical input may also be visible to games." )},
        {QStringLiteral("severity"), QStringLiteral("error")}});
    isolationAttentionState.insert(QStringLiteral("checks"), isolationAttentionChecks);
    devices = showDevicesFixture(isolationAttentionState, multiControllerFixture);
    if (!devices || devices->property("hidhideAvailable").toBool()
        || !devices->findChild<QObject *>(QStringLiteral("flightDeckDeviceIsolation"))
        || !captureShell(QStringLiteral("devices-isolation-attention-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Device Isolation attention fixture did not render")
            .arg(appearance));
    }

    QVariantMap unverifiedState = readyVisualState;
    unverifiedState.insert(QStringLiteral("physicalConnected"), false);
    unverifiedState.insert(QStringLiteral("connectedControllerCount"), 1);
    unverifiedState.insert(QStringLiteral("mappingActive"), false);
    unverifiedState.insert(QStringLiteral("controllerReadinessState"), QStringLiteral("ACTION REQUIRED"));
    unverifiedState.insert(QStringLiteral("controllerReadinessStatus"), QStringLiteral("A newly connected controller needs explicit verification."));
    QVariantList unverifiedChecks = readyChecks;
    unverifiedChecks[0] = QVariant::fromValue(QVariantMap{{QStringLiteral("name"), QStringLiteral("PHYSICAL CONTROLLER")},
        {QStringLiteral("state"), QStringLiteral("ACTION REQUIRED")},
        {QStringLiteral("message"), QStringLiteral("New controller detected. Verify it before mapping.")},
        {QStringLiteral("severity"), QStringLiteral("warning")}});
    unverifiedState.insert(QStringLiteral("checks"), unverifiedChecks);
    devices = showDevicesFixture(unverifiedState, unverifiedControllerFixture);
    if (!devices || devices->property("verificationState").toString() != QStringLiteral("ACTION REQUIRED")
        || !captureShell(QStringLiteral("devices-unverified-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 unverified-controller fixture did not render")
            .arg(appearance));
    }

    QVariantMap repairConfirmationState = isolationAttentionState;
    repairConfirmationState.insert(QStringLiteral("controllerSetupCanApply"), true);
    repairConfirmationState.insert(QStringLiteral("controllerReadinessProposedChanges"), QVariantList{
        QVariant::fromValue(QVariantMap{{QStringLiteral("message"), QStringLiteral("Restore device isolation for the selected controller.")}}),
    });
    devices = showDevicesFixture(repairConfirmationState, multiControllerFixture);
    QObject *repairConfirmation = window->findChild<QObject *>(QStringLiteral("flightDeckRepairConfirmation"));
    if (!devices || !devices->property("canRepairSetup").toBool() || !repairConfirmation
        || !QMetaObject::invokeMethod(repairConfirmation, "open")
        || !repairConfirmation->property("visible").toBool() || !captureShell(QStringLiteral("devices-repair-confirmation"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 repair confirmation did not render")
            .arg(appearance));
    }
    QMetaObject::invokeMethod(repairConfirmation, "close");
    settlePresentation();

    if (!selectPage(surface, 8)) return false;
    window->resize(900, 650);
    window->requestUpdate();
    QTest::qWait(60);
    settlePresentation();
    if (!captureShell(QStringLiteral("minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not render at minimum size")
            .arg(appearance));
    }
    if (!selectPage(surface, 1)) return false;
    buttons = pageItem(surface, 1);
    buttonsItem = qobject_cast<QQuickItem *>(buttons);
    if (!buttons || !buttonsItem
        || !buttons->setProperty("buttonPresentationOverride", largeButtons)
        || !buttons->setProperty("povPresentationOverride", QVariantList{povFixture()})
        || !buttons->setProperty("povInputsPresentationOverride", povInputFixture)
        || !buttons->setProperty("automationPresentationOverride", automationFixture)
        || !buttons->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV"))
        || !buttons->setProperty("filterMode", QStringLiteral("all"))
        || !buttons->setProperty("expandedButtonIndex", -1)
        || !buttons->setProperty("expandedHatIndex", -1)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 minimum Buttons fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    if (!findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonCard_1"))
        || !captureShell(QStringLiteral("buttons-large-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 minimum Buttons fixture did not render")
            .arg(appearance));
    }
    if (!selectPage(surface, 0)) return false;
    QObject *minimumAxes = pageItem(surface, 0);
    if (!minimumAxes || !minimumAxes->setProperty("axisPresentationOverride", eightAxisFixture)
        || !minimumAxes->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV"))
        || !captureShell(QStringLiteral("axes-eight-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 constrained eight-axis fixture did not render")
            .arg(appearance));
    }
    devices = showDevicesFixture(readyVisualState, multiControllerFixture);
    if (!devices || !captureShell(QStringLiteral("devices-multi-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Devices multi-controller minimum state did not render")
            .arg(appearance));
    }
    if (!selectPage(surface, 8)) return false;
    auto *navigationViewport = surface->findChild<QQuickItem *>(
        QStringLiteral("flightDeckNavigationViewport"));
    auto *readiness = surface->findChild<QQuickItem *>(QStringLiteral("flightDeckReadiness"));
    if (!navigationViewport || !readiness
        || navigationViewport->property("contentHeight").toReal() <= navigationViewport->height()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 minimum rail did not expose its constrained-layout scroll path")
            .arg(appearance));
    }
    navigationViewport->setProperty("contentY", navigationViewport->property("contentHeight").toReal()
        - navigationViewport->height());
    settlePresentation();
    const qreal readinessTop = readiness->mapToScene(QPointF{}).y();
    const qreal railTop = navigationViewport->mapToScene(QPointF{}).y();
    if (readinessTop < railTop || readinessTop >= railTop + navigationViewport->height()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 minimum rail could not reach the readiness card")
            .arg(appearance));
    }
    window->resize(1600, 980);
    window->requestUpdate();
    QTest::qWait(60);
    settlePresentation();
    if (!captureShell(QStringLiteral("expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not render at expanded size")
            .arg(appearance));
    }
    if (!selectPage(surface, 0)) return false;
    QObject *expandedAxes = pageItem(surface, 0);
    if (!expandedAxes || !expandedAxes->setProperty("axisPresentationOverride", eightAxisFixture)
        || !expandedAxes->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV"))) return false;
    QQmlExpression expandWideAxis(qmlContext(expandedAxes), expandedAxes, QStringLiteral("configureAxis(0)"));
    expandWideAxis.evaluate();
    if (expandWideAxis.hasError() || !captureShell(QStringLiteral("axes-eight-expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 expanded eight-axis fixture did not render")
            .arg(appearance));
    }
    if (!selectPage(surface, 1)) return false;
    buttons = pageItem(surface, 1);
    buttonsItem = qobject_cast<QQuickItem *>(buttons);
    if (!buttons || !buttonsItem
        || !buttons->setProperty("buttonPresentationOverride", mixedButtons)
        || !buttons->setProperty("povPresentationOverride", QVariantList{povFixture()})
        || !buttons->setProperty("povInputsPresentationOverride", povInputFixture)
        || !buttons->setProperty("automationPresentationOverride", automationFixture)
        || !buttons->setProperty("inputDeviceNameOverride", QStringLiteral("VKB Gunfighter IV"))
        || !buttons->setProperty("filterMode", QStringLiteral("all"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 expanded Buttons fixture could not be installed")
            .arg(appearance));
    }
    QQmlExpression expandWideButton(qmlContext(buttons), buttons, QStringLiteral("setExpandedButton(1)"));
    expandWideButton.evaluate();
    settlePresentation();
    if (expandWideButton.hasError()
        || !findVisualItemByObjectName(buttonsItem, QStringLiteral("flightDeckButtonMappingSelector_1"))
        || !captureShell(QStringLiteral("buttons-expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 expanded Buttons fixture did not render")
            .arg(appearance));
    }
    devices = showDevicesFixture(readyVisualState, multiControllerFixture);
    if (!devices || !captureShell(QStringLiteral("devices-multi-expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Devices multi-controller expanded state did not render")
            .arg(appearance));
    }
    if (!selectPage(surface, 8)) return false;
    return true;
}

bool verifyFlightDeckAxesQmlLoad(hotas::AppBackend &backend, hotas::ThemeManager &themeManager)
{
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    QQmlComponent component(&engine);
    component.loadFromModule(u"HOTASMapperStartupTest"_qs, u"FlightDeckAxes"_qs);
    if (component.status() != QQmlComponent::Ready) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Axes component did not load: %1")
            .arg(component.errorString()));
    }
    QObject *axes = component.create();
    if (!axes) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Axes component did not create: %1")
            .arg(component.errorString()));
    }
    delete axes;
    return true;
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
    hotas::ThemeManager themeManager({}, true);
    // These are the three selectable themes in this Phase 1-derived snapshot,
    // not a product-wide theme-count assertion. Day Ops remains absent here
    // until its concurrent V2.4 implementation reaches this baseline.
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
            || !qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
            qCritical().noquote() << QStringLiteral("%1 theme root window did not load").arg(theme);
            return 1;
        }

        settlePresentation();
        if (!verifyPageLifecycle(backend,
                qobject_cast<QWindow *>(engine.rootObjects().constFirst()), theme)) return 1;
    }

    for (const QString &appearance : {QStringLiteral("Dark"), QStringLiteral("Light")}) {
        if (!verifyFlightDeckAxesQmlLoad(backend, themeManager)) return 1;
        if (!verifyFlightDeckShell(backend, themeManager, appearance)) return 1;
    }
    themeManager.setCurrentExperience(QStringLiteral("Existing"));

    if (!verifyAdaptiveResponseSimulator(backend)) return 1;
    if (!verifyAdaptiveResponsePreviewTruth(backend)) return 1;
    if (!verifyAutomationEditorInteraction(backend)) return 1;

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
