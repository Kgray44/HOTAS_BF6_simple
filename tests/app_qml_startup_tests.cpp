#include "app_backend.h"
#include "axis_transform.h"
#include "config_store.h"
#include "response_curve.h"
#include "theme_manager.h"

#include <QApplication>
#include <QColor>
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

bool verifyAxisRouteTransactionAndPresentation(hotas::AppBackend &backend, QObject *surface)
{
    // The first theme starts in the legacy profile context. Later themes
    // deliberately inherit the interaction test's multi-device context, so
    // select one physical source before changing routes. That is the same
    // user-facing guard the product applies: per-device edits are valid, but
    // browsing/editing an inactive rig must not silently alter live mapping.
    const QVariantList runtimeBefore = backend.runtimeAxisRoutesForTest();
    const bool editingDeviceOverride = !backend.editingDeviceRigId().isEmpty();
    if (editingDeviceOverride) {
        QVariantMap editingRig;
        for (const QVariant &entry : backend.deviceRigs()) {
            const QVariantMap candidate = entry.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == backend.editingDeviceRigId()) {
                editingRig = candidate;
                break;
            }
        }
        const QVariantList members = editingRig.value(QStringLiteral("members")).toList();
        const QString source = members.isEmpty() ? QString{}
            : members.front().toMap().value(QStringLiteral("id")).toString();
        if (source.isEmpty() || !backend.setEditingDeviceContext(backend.editingDeviceRigId(), {source})) {
            return failPresentationLifecycleTest(QStringLiteral("Axis route fixture could not select one rig input"));
        }
    }
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
        const QString runtimeTarget = targetForAxis(backend.runtimeAxisRoutesForTest(), axis);
        if (!editingDeviceOverride && runtimeTarget != expected[index]) {
            return failPresentationLifecycleTest(QStringLiteral("Compiled runtime route did not match stored axis mapping for axis %1").arg(axis));
        }
        if (editingDeviceOverride && runtimeTarget != targetForAxis(runtimeBefore, axis)) {
            return failPresentationLifecycleTest(QStringLiteral("Editing an inactive rig changed the live runtime route for axis %1").arg(axis));
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

bool verifyDevicesInteractionStress(hotas::AppBackend &backend, QObject *surface)
{
    if (!selectPage(surface, 10)) return false;
    QObject *devices = pageItem(surface, 10);
    if (!devices) return failPresentationLifecycleTest(QStringLiteral("Devices page was not available for interaction stress"));
    const QVariantList initialRigs = backend.deviceRigs();
    if (initialRigs.size() != 1) {
        return failPresentationLifecycleTest(QStringLiteral("Devices interaction fixture did not expose exactly one rig"));
    }
    const QVariantMap initialRig = initialRigs.front().toMap();
    const QString rigId = initialRig.value(QStringLiteral("id")).toString();
    const QVariantList members = initialRig.value(QStringLiteral("members")).toList();
    const QVariantList outputs = initialRig.value(QStringLiteral("outputs")).toList();
    if (rigId.isEmpty() || members.size() != 2) {
        return failPresentationLifecycleTest(QStringLiteral("Devices interaction fixture has no usable rig members"));
    }
    if (outputs.isEmpty() || !outputs.front().toMap().contains(QStringLiteral("ready"))
        || !outputs.front().toMap().contains(QStringLiteral("status"))
        || !outputs.front().toMap().contains(QStringLiteral("routeCount"))) {
        return failPresentationLifecycleTest(QStringLiteral("Devices output card projection is missing readiness or route summary"));
    }
    const QString firstMember = members.at(0).toMap().value(QStringLiteral("id")).toString();
    const QString secondMember = members.at(1).toMap().value(QStringLiteral("id")).toString();
    QObject *contextPopup = surface->findChild<QObject *>(QStringLiteral("deviceContextPopup"));
    QObject *physicalDialog = devices->findChild<QObject *>(QStringLiteral("physicalDeviceDialog"));
    QObject *outputDialog = devices->findChild<QObject *>(QStringLiteral("outputDetailDialog"));
    QObject *createDialog = devices->findChild<QObject *>(QStringLiteral("createRigDialog"));
    if (!contextPopup || !physicalDialog || !outputDialog || !createDialog) {
        return failPresentationLifecycleTest(QStringLiteral("Devices context popup or detail dialog was not created"));
    }
    // This is the concrete regression boundary: leave the custom context
    // popup and detail dialogs live while the backend replaces device-rig and
    // controller value models. The old native Menu/Instantiator path could
    // retain stale QML objects here during ordinary clicking.
    QMetaObject::invokeMethod(contextPopup, "open");
    devices->setProperty("selectedDeviceId", firstMember);
    devices->setProperty("selectedOutputId", QStringLiteral("bf6-output"));
    QMetaObject::invokeMethod(physicalDialog, "open");
    QMetaObject::invokeMethod(outputDialog, "open");
    QMetaObject::invokeMethod(createDialog, "open");
    settlePresentation();
    if (!backend.setEditingDeviceContext(rigId, {firstMember})
        || !backend.removeDeviceRigMember(rigId, secondMember)
        || !backend.addDeviceRigMember(rigId, secondMember, false)) {
        return failPresentationLifecycleTest(QStringLiteral("Devices interaction fixture could not replace member model"));
    }
    backend.refreshControllers();
    settlePresentation();
    const QString transientRig = backend.createDeviceRig(QStringLiteral("Transient Fixture Rig"), {firstMember});
    if (transientRig.isEmpty() || !backend.setEditingDeviceContext(transientRig, {firstMember})
        || !backend.deleteDeviceRig(transientRig)) {
        return failPresentationLifecycleTest(QStringLiteral("Devices interaction fixture could not delete a live rig model"));
    }
    settlePresentation();
    if (devices->property("selectedRigId").toString() != rigId) {
        return failPresentationLifecycleTest(QStringLiteral("Devices model replacement retained a stale deleted rig"));
    }
    if (!backend.setEditingDeviceContext(rigId, {firstMember, secondMember})) {
        return failPresentationLifecycleTest(QStringLiteral("Devices interaction fixture could not restore a multi-device context"));
    }
    QMetaObject::invokeMethod(contextPopup, "open");
    settlePresentation();
    if (!contextPopup->property("visible").toBool()
        || backend.editingScopeLabel() != QStringLiteral("2 Devices")) {
        return failPresentationLifecycleTest(QStringLiteral("Devices custom context popup did not survive multi-device refresh"));
    }
    QMetaObject::invokeMethod(contextPopup, "close");
    QMetaObject::invokeMethod(createDialog, "close");
    QMetaObject::invokeMethod(physicalDialog, "close");
    QMetaObject::invokeMethod(outputDialog, "close");
    return selectPage(surface, 8);
}

bool verifyDevicesResponsiveLayout(QObject *surface, QWindow *shell, const QString &theme)
{
    if (!shell || !selectPage(surface, 10)) {
        return failPresentationLifecycleTest(QStringLiteral("Devices responsive layout could not enter its page"));
    }
    QObject *devicesObject = pageItem(surface, 10);
    auto *devices = qobject_cast<QQuickItem *>(devicesObject);
    if (!devices) return failPresentationLifecycleTest(QStringLiteral("Devices responsive layout did not create a visual root"));
    const QString contextName = theme == QStringLiteral("Legacy")
        ? QStringLiteral("legacyDeviceContextSelector")
        : theme == QStringLiteral("Top Gun") ? QStringLiteral("topGunDeviceContextSelector")
                                           : QStringLiteral("standardDeviceContextSelector");
    auto *contextSelector = surface->findChild<QQuickItem *>(contextName);
    auto *mappingControl = surface->findChild<QQuickItem *>(QStringLiteral("globalMappingControl"));
    if (!contextSelector) {
        return failPresentationLifecycleTest(QStringLiteral("Persistent Device Context was not available for %1").arg(theme));
    }
    const QSize original = shell->size();
    const QList<QSize> sizes{{900, 650}, {1280, 720}, {1440, 900}, {1920, 1080}};
    const QStringList panels{QStringLiteral("activeRigPanel"), QStringLiteral("deviceRigListPanel"),
        QStringLiteral("rigDetailsPanel"), QStringLiteral("automaticBehaviorPanel"),
        QStringLiteral("knownDevicesPanel")};
    for (const QSize &size : sizes) {
        shell->resize(size);
        settlePresentation();
        if (!contextSelector->isVisible()) {
            shell->resize(original);
            return failPresentationLifecycleTest(QStringLiteral("Persistent Device Context disappeared at %1x%2 for %3")
                .arg(size.width()).arg(size.height()).arg(theme));
        }
        if (mappingControl && mappingControl->isVisible()) {
            const QRectF contextBounds(contextSelector->mapToScene(QPointF{}), contextSelector->size());
            const QRectF mappingBounds(mappingControl->mapToScene(QPointF{}), mappingControl->size());
            if (contextBounds.intersects(mappingBounds)) {
                shell->resize(original);
                return failPresentationLifecycleTest(QStringLiteral("Device Context overlaps mapping status at %1x%2 for %3")
                    .arg(size.width()).arg(size.height()).arg(theme));
            }
        }
        QList<QQuickItem *> resolved;
        for (const QString &name : panels) {
            auto *panel = devices->findChild<QQuickItem *>(name);
            if (!panel || panel->width() <= 0 || panel->height() <= 0
                || panel->width() > devices->width() + 1.0) {
                const auto *scroll = devices->findChild<QQuickItem *>(QStringLiteral("devicesScroll"));
                const auto *content = devices->findChild<QQuickItem *>(QStringLiteral("devicesContent"));
                shell->resize(original);
                return failPresentationLifecycleTest(QStringLiteral("Devices panel %1 did not fit at %2x%3 (panel %4x%5, page %6x%7, scroll %8x%9, content %10x%11)")
                    .arg(name).arg(size.width()).arg(size.height())
                    .arg(panel ? panel->width() : -1).arg(panel ? panel->height() : -1)
                    .arg(devices->width()).arg(devices->height())
                    .arg(scroll ? scroll->width() : -1).arg(scroll ? scroll->height() : -1)
                    .arg(content ? content->width() : -1).arg(content ? content->height() : -1));
            }
            if (theme == QStringLiteral("Legacy")) {
                const QColor expectedLegacySurface(QStringLiteral("#e9161d23"));
                if (panel->property("color").value<QColor>() != expectedLegacySurface) {
                    shell->resize(original);
                    return failPresentationLifecycleTest(QStringLiteral("Legacy Devices panel %1 lost its established layered surface")
                        .arg(name));
                }
            }
            resolved.append(panel);
        }
        for (int index = 1; index < resolved.size(); ++index) {
            const QPointF previous = resolved.at(index - 1)->mapToItem(devices, QPointF{});
            const QPointF current = resolved.at(index)->mapToItem(devices, QPointF{});
            if (previous.y() + resolved.at(index - 1)->height() > current.y() + 0.5) {
                shell->resize(original);
                return failPresentationLifecycleTest(QStringLiteral("Devices panels overlapped at %1x%2")
                    .arg(size.width()).arg(size.height()));
            }
        }
    }
    shell->resize(original);
    return selectPage(surface, 8);
}

bool verifyPageLifecycle(hotas::AppBackend &backend, QWindow *shell, const QString &theme)
{
    QObject *presentation = shell->findChild<QObject *>(QStringLiteral("presentationLoader"));
    if (!presentation) return failPresentationLifecycleTest(QStringLiteral("presentation Loader was not found"));
    QObject *surface = qvariant_cast<QObject *>(presentation->property("item"));
    if (!surface) return failPresentationLifecycleTest(QStringLiteral("theme surface was not loaded"));

    // Device Rigs are a first-class V2.4 page, not an optional dialog. Keep
    // its loader in the same four-theme lifecycle qualification as the
    // established workspaces. Qt Quick may defer one-time control-template
    // construction until the event loop has completed a number of page
    // changes, so use a complete stress traversal as a burn-in before taking
    // the exact steady-state baseline below.
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int page = 0; page <= 10; ++page) {
            if (!selectPage(surface, page)) return false;
        }
    }
    if (!selectPage(surface, 8)) return false;
    const ProcessMemoryFootprint fresh = currentProcessMemoryFootprint();
    const int freshObjectCount = surface->findChildren<QObject *>().size();
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int page = 0; page <= 10; ++page) {
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
    if (!verifyDevicesInteractionStress(backend, surface)) return false;
    if (!verifyDevicesResponsiveLayout(surface, shell, theme)) return false;
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

bool seedDeviceRigFixture()
{
    hotas::MapperConfiguration configuration = hotas::defaultConfiguration();
    const auto saved = [&configuration](const QString &id, const QString &name, bool verified) {
        hotas::SavedControllerRecord record;
        record.id = id;
        record.displayName = name;
        record.lastDirectInputId = QStringLiteral("{fixture-%1}").arg(id);
        record.productGuid = QStringLiteral("{fixture-product-%1}").arg(id);
        record.hidInstanceId = QStringLiteral("HID\\FIXTURE\\%1").arg(id);
        record.axes[0] = true;
        record.axes[1] = true;
        record.axes[2] = true;
        record.axisCount = 3;
        record.buttonCount = 16;
        record.povCount = 1;
        record.vjoyRequirements = configuration.outputLayouts.front().requirements;
        record.lastVerified = verified ? QStringLiteral("2026-09-07T00:00:00Z") : QString();
        return record;
    };
    const hotas::SavedControllerRecord stick = saved(QStringLiteral("fixture-stick"),
        QStringLiteral("Fixture Gladiator"), true);
    const hotas::SavedControllerRecord throttle = saved(QStringLiteral("fixture-throttle"),
        QStringLiteral("Fixture STECS"), false);
    configuration.savedControllers = {stick, throttle};
    hotas::DeviceRig rig;
    rig.id = QStringLiteral("fixture-rig");
    rig.name = QStringLiteral("Fixture Flight Rig");
    rig.isDefault = true;
    rig.members = {{stick.id, true, true, hotas::defaultOutputLayoutId()},
                   {throttle.id, true, false, hotas::defaultOutputLayoutId()}};
    rig.outputs = {{hotas::defaultOutputLayoutId(), true}};
    configuration.deviceRigs = {rig};
    // Begin with the legacy profile editor context. The route-parity section
    // below validates the worker's base-profile cache; the Devices stress
    // section then explicitly enters single and multi-device rig scopes.
    // Preselecting a rig here would correctly write a device override while
    // incorrectly comparing it to the unrelated base-profile test seam.
    configuration.editingDeviceRigId.clear();
    configuration.editingDeviceRecordIds.clear();
    return hotas::ConfigStore::save(configuration);
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
    if (!seedDeviceRigFixture()) return 1;

    hotas::AppBackend backend;
    hotas::ThemeManager themeManager;
    const QStringList themes{
        QStringLiteral("Legacy"),
        QStringLiteral("Standard"),
        QStringLiteral("Top Gun"),
        QStringLiteral("Day Ops"),
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

    // The Devices stress deliberately leaves the user-facing editing context
    // in a multi-device scope. Exercise the production deletion transition to
    // return subsequent legacy profile tests to their real no-rig state;
    // otherwise those tests would try to write a device-qualified override
    // while asserting the independent base-profile runtime cache.
    if (!backend.deleteDeviceRig(QStringLiteral("fixture-rig"))
        || !backend.editingDeviceRigId().isEmpty()) {
        failPresentationLifecycleTest(QStringLiteral("fixture Device Rig context was not safely cleared"));
        return 1;
    }

    if (!verifyAdaptiveResponseSimulator(backend)) return 1;
    if (!verifyAdaptiveResponsePreviewTruth(backend)) return 1;
    if (!verifyAutomationEditorInteraction(backend)) return 1;

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
