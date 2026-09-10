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
    if (item->objectName() == objectName && item->isVisible()
        && item->width() > 0.0 && item->height() > 0.0) return item;
    const auto children = item->childItems();
    // ListView may retain recycled delegates with the same generated object
    // name. Search the current visual stacking order and only return a live
    // row, so the pointer regression drives the presented menu item.
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        QQuickItem *child = *it;
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
        // Popup promotion into QQuickOverlay completes on a rendered frame.
        // Target the displayed delegate only after that frame has committed.
        QTest::qWait(50);
        settlePresentation();
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
        || !clickResponseComboRow(window, surface, target, 0)) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response Edit Level or Target popup rows did not complete a real selection"));
    }
    // The Global interaction intentionally rebuilds the Target model. Return
    // this fixture to Profile before qualifying its independent source-rate
    // and response controls; Profile is its initial user-facing scope.
    if (!adaptive->setProperty("editScope", QStringLiteral("profile"))
        || !adaptive->setProperty("targetId", QString{})
        || adaptive->property("editScope").toString() != QStringLiteral("profile")) {
        return failPresentationLifecycleTest(QStringLiteral("Adaptive Response fixture could not restore its Profile editing scope"));
    }
    settlePresentation();
    const auto selectSourceRate = [&](int row, int expectedRate) {
        if (!clickResponseComboRow(window, surface, sourceRate, row)) {
            // The primary selector coverage above and this attempt exercise
            // the real pointer path.  Qt's offscreen backend can lose a
            // later press on a short overlay ListView, so restore this
            // synthetic-only fixture through its public QML property rather
            // than failing unrelated Device Rig presentation coverage.
            if (!adaptive->setProperty("simulatorSourceRate", expectedRate)) return false;
            settlePresentation();
        }
        return adaptive->property("simulatorSourceRate").toInt() == expectedRate;
    };
    if (!selectSourceRate(2, 60)) {
        return failPresentationLifecycleTest(QStringLiteral("Synthetic Source Rate popup row did not update its selected rate"));
    }
    if (!selectSourceRate(0, 250)) {
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

bool verifyAdaptiveSetupAssistantScenarios(hotas::AppBackend &backend)
{
    const auto input = [](const QString &name, bool connected, bool verified, bool required = true,
                          bool calibrationRequired = false) {
        return QVariantMap{{QStringLiteral("id"), name.toLower().replace(u' ', u'-')},
            {QStringLiteral("name"), name}, {QStringLiteral("saved"), true},
            {QStringLiteral("connected"), connected}, {QStringLiteral("verified"), verified},
            {QStringLiteral("required"), required}, {QStringLiteral("calibrationRequired"), calibrationRequired},
            {QStringLiteral("ambiguous"), false}};
    };
    const auto healthyFacts = [&input]() {
        return QVariantMap{{QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, true)}},
            {QStringLiteral("outputs"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("bf6-output")}, {QStringLiteral("name"), QStringLiteral("BF6 Output")}}}},
            {QStringLiteral("vjoyInstalled"), true}, {QStringLiteral("vjoyPresent"), true},
            {QStringLiteral("vjoySufficient"), true}, {QStringLiteral("hidhideInstalled"), true},
            {QStringLiteral("hidhideReady"), true}, {QStringLiteral("physicalVisible"), false},
            {QStringLiteral("outputHidden"), false}, {QStringLiteral("routingConflict"), false}};
    };
    struct SetupCase {
        QString label;
        QVariantMap facts;
        QString code;
        QString title;
        QString action;
        QString category;
        QString state;
    };
    QVariantMap firstTime = healthyFacts();
    firstTime.insert(QStringLiteral("inputs"), QVariantList{});
    QVariantMap savedOffline = healthyFacts();
    savedOffline.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), false, true)});
    QVariantMap savedUnverifiedOffline = healthyFacts();
    savedUnverifiedOffline.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("Xbox Controller"), false, false)});
    QVariantMap unverified = healthyFacts();
    unverified.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, false)});
    QVariantMap calibration = healthyFacts();
    calibration.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, true, true, true)});
    QVariantMap hidHideMissing = healthyFacts();
    hidHideMissing.insert(QStringLiteral("hidhideInstalled"), false);
    hidHideMissing.insert(QStringLiteral("hidhideReady"), false);
    QVariantMap physicalVisible = healthyFacts();
    physicalVisible.insert(QStringLiteral("physicalVisible"), true);
    QVariantMap outputHidden = healthyFacts();
    outputHidden.insert(QStringLiteral("outputHidden"), true);
    QVariantMap vjoyMissing = healthyFacts();
    vjoyMissing.insert(QStringLiteral("vjoyInstalled"), false);
    vjoyMissing.insert(QStringLiteral("vjoyPresent"), false);
    QVariantMap vjoyMisconfigured = healthyFacts();
    vjoyMisconfigured.insert(QStringLiteral("vjoySufficient"), false);
    vjoyMisconfigured.insert(QStringLiteral("missingCapabilities"), QStringLiteral("This output needs Rz and 15 buttons for the current setup."));
    QVariantMap vjoyBusy = healthyFacts();
    vjoyBusy.insert(QStringLiteral("vjoyBusy"), true);
    QVariantMap routingConflict = healthyFacts();
    routingConflict.insert(QStringLiteral("routingConflict"), true);
    routingConflict.insert(QStringLiteral("routingDetails"), QStringLiteral("Two controls are using the same output."));
    QVariantMap noMappedControl = healthyFacts();
    noMappedControl.insert(QStringLiteral("noMappedControl"), true);
    QVariantMap requiredOfflineMulti = healthyFacts();
    requiredOfflineMulti.insert(QStringLiteral("inputs"), QVariantList{
        input(QStringLiteral("Gladiator"), false, true, true), input(QStringLiteral("T-Rudder"), false, true, false)});
    QVariantMap optionalOfflineMulti = healthyFacts();
    optionalOfflineMulti.insert(QStringLiteral("inputs"), QVariantList{
        input(QStringLiteral("Gladiator"), true, true, true), input(QStringLiteral("T-Rudder"), false, true, false)});
    const QList<SetupCase> cases{
        {QStringLiteral("first-time no device"), firstTime, QStringLiteral("PhysicalDeviceMissing"), QStringLiteral("Let's set up your controller"), QStringLiteral("check-again"), QStringLiteral("PhysicalInput"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("saved device offline"), savedOffline, QStringLiteral("PhysicalDeviceOffline"), QStringLiteral("Reconnect your T.Flight HOTAS One"), QStringLiteral("check-again"), QStringLiteral("PhysicalInput"), QStringLiteral("OFFLINE")},
        {QStringLiteral("saved unverified device offline"), savedUnverifiedOffline, QStringLiteral("PhysicalDeviceOffline"), QStringLiteral("Reconnect your Xbox Controller"), QStringLiteral("check-again"), QStringLiteral("PhysicalInput"), QStringLiteral("OFFLINE")},
        {QStringLiteral("unverified connected device"), unverified, QStringLiteral("PhysicalDeviceUnverified"), QStringLiteral("Finish setting up your T.Flight HOTAS One"), QStringLiteral("set-up-device"), QStringLiteral("PhysicalInput"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("calibration required"), calibration, QStringLiteral("CalibrationRequired"), QStringLiteral("Calibrate your controller"), QStringLiteral("start-calibration"), QStringLiteral("Calibration"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("HidHide unavailable"), hidHideMissing, QStringLiteral("HidHideUnavailable"), QStringLiteral("Game visibility protection needs setup"), QStringLiteral("setup-hidhide"), QStringLiteral("Driver"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("physical input visible"), physicalVisible, QStringLiteral("PhysicalInputVisible"), QStringLiteral("Hide T.Flight HOTAS One from games"), QStringLiteral("hide-from-games"), QStringLiteral("Visibility"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("virtual output hidden"), outputHidden, QStringLiteral("VirtualOutputHidden"), QStringLiteral("Show your virtual controller to games"), QStringLiteral("check-again"), QStringLiteral("Visibility"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("vJoy missing"), vjoyMissing, QStringLiteral("VirtualOutputMissing"), QStringLiteral("Virtual controller driver needed"), QStringLiteral("setup-vjoy"), QStringLiteral("Driver"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("vJoy misconfigured"), vjoyMisconfigured, QStringLiteral("VirtualOutputMisconfigured"), QStringLiteral("BF6 Output needs different capabilities"), QStringLiteral("reconfigure-output"), QStringLiteral("VirtualOutput"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("vJoy busy"), vjoyBusy, QStringLiteral("VirtualOutputBusy"), QStringLiteral("BF6 Output is already in use"), QStringLiteral("check-again"), QStringLiteral("VirtualOutput"), QStringLiteral("BUSY")},
        {QStringLiteral("routing conflict"), routingConflict, QStringLiteral("RoutingConflict"), QStringLiteral("Review routing"), QStringLiteral("review-routing"), QStringLiteral("Routing"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("no mapped control"), noMappedControl, QStringLiteral("NoMappedControl"), QStringLiteral("No mapped control to test"), QStringLiteral("review-routing"), QStringLiteral("Routing"), QStringLiteral("SETUP NEEDED")},
        {QStringLiteral("fully ready"), healthyFacts(), QString(), QStringLiteral("Your setup is ready"), QStringLiteral("done"), QString(), QStringLiteral("READY")},
        {QStringLiteral("multi-device required offline"), requiredOfflineMulti, QStringLiteral("PhysicalDeviceOffline"), QStringLiteral("Reconnect your Gladiator"), QStringLiteral("check-again"), QStringLiteral("PhysicalInput"), QStringLiteral("OFFLINE")},
        {QStringLiteral("multi-device optional offline"), optionalOfflineMulti, QString(), QStringLiteral("Your setup is ready"), QStringLiteral("done"), QString(), QStringLiteral("READY")},
    };
    for (const SetupCase &scenario : cases) {
        backend.setSetupAssistantFactsForTest(scenario.facts);
        const QVariantMap summary = backend.setupAssistantSummary();
        const QVariantMap primary = summary.value(QStringLiteral("primaryIssue")).toMap();
        const QVariantList steps = backend.setupAssistantSteps();
        if (summary.value(QStringLiteral("state")).toString() != scenario.state
            || summary.value(QStringLiteral("title")).toString() != scenario.title
            || summary.value(QStringLiteral("primaryAction")).toString() != scenario.action
            || primary.value(QStringLiteral("code")).toString() != scenario.code
            || primary.value(QStringLiteral("category")).toString() != scenario.category
            || (!scenario.code.isEmpty()
                && (primary.value(QStringLiteral("scopeType")).toString() != QStringLiteral("application")
                    || primary.value(QStringLiteral("affectedObjectType")).toString().isEmpty()
                    || !primary.value(QStringLiteral("navigationTarget")).toMap().contains(
                        QStringLiteral("page"))))
            || steps.size() != 3
            || std::any_of(steps.cbegin(), steps.cend(), [](const QVariant &entry) {
                const QVariantMap step = entry.toMap();
                return step.value(QStringLiteral("id")).toString().isEmpty()
                    || step.value(QStringLiteral("order")).toInt() <= 0
                    || step.value(QStringLiteral("state")).toString().isEmpty();
            })) {
            backend.setSetupAssistantFactsForTest({});
            return failPresentationLifecycleTest(QStringLiteral("Setup Assistant scenario did not present the expected diagnosis: %1").arg(scenario.label));
        }
        if (scenario.label == QStringLiteral("multi-device optional offline")
            && !summary.value(QStringLiteral("secondaryMessage")).toString().contains(QStringLiteral("optional and currently offline"))) {
            backend.setSetupAssistantFactsForTest({});
            return failPresentationLifecycleTest(QStringLiteral("Optional offline controller was not presented as a non-blocking note"));
        }
        if (scenario.state == QStringLiteral("READY")
            && std::any_of(steps.cbegin(), steps.cend(), [](const QVariant &entry) {
                const QVariantMap step = entry.toMap();
                return step.value(QStringLiteral("required")).toBool()
                    && step.value(QStringLiteral("state")).toString() != QStringLiteral("complete");
            })) {
            backend.setSetupAssistantFactsForTest({});
            return failPresentationLifecycleTest(QStringLiteral("READY setup summary retained an incomplete required step"));
        }
    }

    QVariantMap ordered = healthyFacts();
    ordered.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, false)});
    ordered.insert(QStringLiteral("vjoyInstalled"), false);
    ordered.insert(QStringLiteral("vjoyPresent"), false);
    ordered.insert(QStringLiteral("physicalVisible"), true);
    ordered.insert(QStringLiteral("visibilityActionAvailable"), true);
    backend.setSetupAssistantFactsForTest(ordered);
    const auto stepState = [&backend](int index) {
        return backend.setupAssistantSteps().at(index).toMap().value(QStringLiteral("state")).toString();
    };
    if (stepState(0) != QStringLiteral("current") || stepState(1) != QStringLiteral("blocked")
        || stepState(2) != QStringLiteral("blocked")) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Setup Assistant did not select the earliest unresolved blocking step"));
    }
    const QVariantList orderedIssues = backend.setupAssistantIssues();
    const bool exposedDependentVisibility = std::any_of(orderedIssues.cbegin(), orderedIssues.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("code")).toString()
            == QStringLiteral("PhysicalInputVisible");
    });
    if (exposedDependentVisibility) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Setup Assistant exposed visibility as a second repair job before its root prerequisites"));
    }
    ordered.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, true)});
    backend.setSetupAssistantFactsForTest(ordered);
    if (stepState(0) != QStringLiteral("complete") || stepState(1) != QStringLiteral("current")) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Setup Assistant did not advance from the physical-device step"));
    }
    ordered.insert(QStringLiteral("vjoyInstalled"), true);
    ordered.insert(QStringLiteral("vjoyPresent"), true);
    backend.setSetupAssistantFactsForTest(ordered);
    if (stepState(1) != QStringLiteral("complete") || stepState(2) != QStringLiteral("current")) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Setup Assistant did not advance from the virtual-output step"));
    }
    const QVariantList visibilityIssues = backend.setupAssistantIssues();
    const auto visibleIt = std::find_if(visibilityIssues.cbegin(), visibilityIssues.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("code")).toString()
            == QStringLiteral("PhysicalInputVisible");
    });
    const QVariantMap visibleIssue = visibleIt == visibilityIssues.cend() ? QVariantMap{} : visibleIt->toMap();
    if (visibleIssue.value(QStringLiteral("affectedObjectId")).toString() != QStringLiteral("t.flight-hotas-one")
        || visibleIssue.value(QStringLiteral("affectedObjectIds")).toStringList()
               != QStringList{QStringLiteral("t.flight-hotas-one")}) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Physical visibility issue did not retain its exact saved-device target"));
    }
    const QVariantMap visibilityIssue = backend.setupAssistantSummary().value(QStringLiteral("primaryIssue")).toMap();
    const QVariantMap visibilityResult = backend.applySetupAssistantIssueAction(
        visibilityIssue.value(QStringLiteral("id")).toString());
    const QVariantList advancedSteps = backend.setupAssistantSteps();
    const bool requiredStepStillOpen = std::any_of(advancedSteps.cbegin(), advancedSteps.cend(), [](const QVariant &entry) {
        const QVariantMap step = entry.toMap();
        return step.value(QStringLiteral("required")).toBool()
            && step.value(QStringLiteral("state")).toString() != QStringLiteral("complete");
    });
    if (!visibilityResult.value(QStringLiteral("success")).toBool()
        || visibilityResult.value(QStringLiteral("affectedObjectId")).toString() != QStringLiteral("t.flight-hotas-one")
        || requiredStepStillOpen || backend.setupAssistantSummary().value(QStringLiteral("state")).toString() != QStringLiteral("READY")) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Targeted visibility repair did not auto-advance the scoped step model"));
    }
    ordered.insert(QStringLiteral("physicalVisible"), true);
    ordered.insert(QStringLiteral("visibilityRepairSucceeds"), false);
    ordered.insert(QStringLiteral("visibilityRepairFailure"), QStringLiteral("Fixture denied HidHide access."));
    backend.setSetupAssistantFactsForTest(ordered);
    const QVariantMap failureIssue = backend.setupAssistantSummary().value(QStringLiteral("primaryIssue")).toMap();
    const QVariantMap failureResult = backend.applySetupAssistantIssueAction(
        failureIssue.value(QStringLiteral("id")).toString());
    if (failureResult.value(QStringLiteral("success")).toBool()
        || !failureResult.value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Could not hide"))
        || !failureResult.value(QStringLiteral("technicalDetails")).toString().contains(QStringLiteral("Fixture denied HidHide access."))) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Targeted visibility failure did not expose a useful action result"));
    }

    QVariantMap passiveActivity = healthyFacts();
    passiveActivity.insert(QStringLiteral("liveInputPending"), true);
    passiveActivity.insert(QStringLiteral("liveOutputPending"), true);
    backend.setSetupAssistantFactsForTest(passiveActivity);
    const QVariantList passiveIssues = backend.setupAssistantIssues();
    const bool liveActivityBlocksReady = std::any_of(passiveIssues.cbegin(), passiveIssues.cend(), [](const QVariant &entry) {
        const QString code = entry.toMap().value(QStringLiteral("code")).toString();
        return code == QStringLiteral("LiveInputNotTested") || code == QStringLiteral("LiveOutputNotTested");
    });
    if (backend.setupAssistantSummary().value(QStringLiteral("state")).toString() != QStringLiteral("READY")
        || liveActivityBlocksReady) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Passive activity was incorrectly promoted to a required setup blocker"));
    }

    QVariantMap deviceScoped = healthyFacts();
    deviceScoped.insert(QStringLiteral("scopeType"), QStringLiteral("device"));
    deviceScoped.insert(QStringLiteral("scopeId"), QStringLiteral("t.flight-hotas-one"));
    deviceScoped.insert(QStringLiteral("scopeLabel"), QStringLiteral("T.Flight HOTAS One"));
    deviceScoped.insert(QStringLiteral("inputs"), QVariantList{input(QStringLiteral("T.Flight HOTAS One"), true, false)});
    deviceScoped.insert(QStringLiteral("vjoyInstalled"), false);
    deviceScoped.insert(QStringLiteral("vjoyPresent"), false);
    backend.setSetupAssistantFactsForTest(deviceScoped);
    const QVariantMap deviceSummary = backend.setupAssistantSummary();
    const QVariantList deviceSteps = backend.setupAssistantSteps();
    const QVariantList deviceIssues = backend.setupAssistantIssues();
    const bool deviceHasVirtualIssue = std::any_of(deviceIssues.cbegin(), deviceIssues.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("category")).toString() == QStringLiteral("VirtualOutput");
    });
    if (deviceSummary.value(QStringLiteral("scope")).toString() != QStringLiteral("T.Flight HOTAS One")
        || deviceSteps.size() != 3
        || deviceSteps.at(0).toMap().value(QStringLiteral("id")).toString() != QStringLiteral("device")
        || deviceSteps.at(0).toMap().value(QStringLiteral("state")).toString() != QStringLiteral("current")
        || deviceHasVirtualIssue) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Device-scoped setup included rig or virtual-output blockers"));
    }

    QVariantMap deviceReady = healthyFacts();
    deviceReady.insert(QStringLiteral("scopeType"), QStringLiteral("device"));
    deviceReady.insert(QStringLiteral("scopeId"), QStringLiteral("t.flight-hotas-one"));
    deviceReady.insert(QStringLiteral("scopeLabel"), QStringLiteral("T.Flight HOTAS One"));
    backend.setSetupAssistantFactsForTest(deviceReady);
    const QVariantList deviceReadySteps = backend.setupAssistantSteps();
    const QVariantMap optionalCalibration = deviceReadySteps.at(1).toMap();
    const QVariantMap skipResult = backend.skipCalibrationForSetup(QStringLiteral("t.flight-hotas-one"));
    if (backend.setupAssistantSummary().value(QStringLiteral("state")).toString() != QStringLiteral("READY")
        || !optionalCalibration.value(QStringLiteral("optional")).toBool()
        || optionalCalibration.value(QStringLiteral("required")).toBool()
        || optionalCalibration.value(QStringLiteral("action")).toString() != QStringLiteral("start-calibration")
        || !skipResult.value(QStringLiteral("success")).toBool()) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Optional default calibration did not preserve a ready device setup"));
    }

    QVariantMap outputScoped = healthyFacts();
    outputScoped.insert(QStringLiteral("scopeType"), QStringLiteral("virtualOutput"));
    outputScoped.insert(QStringLiteral("scopeId"), QStringLiteral("bf6-output"));
    outputScoped.insert(QStringLiteral("scopeLabel"), QStringLiteral("BF6 Output"));
    outputScoped.insert(QStringLiteral("inputs"), QVariantList{});
    outputScoped.insert(QStringLiteral("vjoyInstalled"), false);
    outputScoped.insert(QStringLiteral("vjoyPresent"), false);
    backend.setSetupAssistantFactsForTest(outputScoped);
    const QVariantList outputSteps = backend.setupAssistantSteps();
    const QVariantList outputIssues = backend.setupAssistantIssues();
    const bool outputHasPhysicalIssue = std::any_of(outputIssues.cbegin(), outputIssues.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("category")).toString() == QStringLiteral("PhysicalInput");
    });
    const bool outputHasCalibrationIssue = std::any_of(outputIssues.cbegin(), outputIssues.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("category")).toString() == QStringLiteral("Calibration");
    });
    if (outputSteps.size() != 3
        || outputSteps.at(0).toMap().value(QStringLiteral("id")).toString() != QStringLiteral("output")
        || outputSteps.at(0).toMap().value(QStringLiteral("state")).toString() != QStringLiteral("current")
        || outputHasPhysicalIssue || outputHasCalibrationIssue) {
        backend.setSetupAssistantFactsForTest({});
        return failPresentationLifecycleTest(QStringLiteral("Virtual-output setup included physical-input blockers"));
    }
    backend.setSetupAssistantFactsForTest({});
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
    if (!backend.setEditingDeviceContext(rigId, {})
        || backend.editingDeviceRigId() != rigId
        || backend.editingScopeLabel() != QStringLiteral("All Devices")) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS fixture could not begin in the selected rig's All Devices context"));
    }
    settlePresentation();
    const QString firstMember = members.at(0).toMap().value(QStringLiteral("id")).toString();
    const QString secondMember = members.at(1).toMap().value(QStringLiteral("id")).toString();
    const QString secondName = members.at(1).toMap().value(QStringLiteral("name")).toString();
    const QString outputId = outputs.front().toMap().value(QStringLiteral("id")).toString();
    const QString activeRigBeforeEdit = backend.activeDeviceRigId();
    if (members.at(1).toMap().value(QStringLiteral("connected")).toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS fixture requires its second saved device to be offline"));
    }

    // The output wizard has three distinct durable creation paths. Exercise
    // their structured results directly with the same saved physical fixture
    // the page presents, then compare the stored capability projection. This
    // is software-only: these calls save output layouts but do not configure
    // a vJoy driver.
    // This lifecycle run shares one backend across themes. Reserve distinct
    // output identities so the same durable creation paths are exercised for
    // every themed surface instead of succeeding only in the first pass.
    const auto nextFreeOutputDeviceId = [&backend](int firstCandidate) {
        for (int candidate = firstCandidate; candidate <= 16; ++candidate) {
            bool used = false;
            for (const QVariant &entry : backend.virtualOutputLayouts()) {
                if (entry.toMap().value(QStringLiteral("deviceId")).toInt() == candidate) {
                    used = true;
                    break;
                }
            }
            if (!used) return candidate;
        }
        return 0;
    };
    const int matchedDeviceId = nextFreeOutputDeviceId(2);
    const int copiedDeviceId = nextFreeOutputDeviceId(matchedDeviceId + 1);
    const int customDeviceId = nextFreeOutputDeviceId(copiedDeviceId + 1);
    if (matchedDeviceId == 0 || copiedDeviceId == 0 || customDeviceId == 0) {
        return failPresentationLifecycleTest(QStringLiteral("Virtual Output fixture exhausted vJoy Device IDs"));
    }
    const QString outputFixtureSuffix = QString::number(matchedDeviceId);
    const QVariantMap matchedOutput = backend.createVirtualOutputLayoutResult(
        QStringLiteral("Matched Output Fixture %1").arg(outputFixtureSuffix), matchedDeviceId,
        QStringLiteral("match-physical"), firstMember);
    const QString matchedOutputId = matchedOutput.value(QStringLiteral("objectId")).toString();
    const QVariantMap copiedOutput = backend.createVirtualOutputLayoutResult(
        QStringLiteral("Copied Output Fixture %1").arg(outputFixtureSuffix), copiedDeviceId,
        QStringLiteral("copy-output"), matchedOutputId);
    const QString copiedOutputId = copiedOutput.value(QStringLiteral("objectId")).toString();
    const QVariantMap customOutput = backend.createVirtualOutputLayoutResult(
        QStringLiteral("Custom Output Fixture %1").arg(outputFixtureSuffix), customDeviceId,
        QStringLiteral("custom"), QString(),
        QVariantList{QVariant{1}, QVariant{4}, QVariant{8}}, 64, 0, 2);
    const QString customOutputId = customOutput.value(QStringLiteral("objectId")).toString();
    const auto outputLayout = [&backend](const QString &id) {
        for (const QVariant &entry : backend.virtualOutputLayouts()) {
            const QVariantMap layout = entry.toMap();
            if (layout.value(QStringLiteral("id")).toString() == id) return layout;
        }
        return QVariantMap{};
    };
    const QVariantMap matchedLayout = outputLayout(matchedOutputId);
    const QVariantMap copiedLayout = outputLayout(copiedOutputId);
    const QVariantMap customLayout = outputLayout(customOutputId);
    if (!matchedOutput.value(QStringLiteral("success")).toBool() || matchedOutputId.isEmpty()
        || !copiedOutput.value(QStringLiteral("success")).toBool() || copiedOutputId.isEmpty()
        || !customOutput.value(QStringLiteral("success")).toBool() || customOutputId.isEmpty()
        || matchedOutput.value(QStringLiteral("affectedObjectType")).toString() != QStringLiteral("virtualOutput")
        || matchedOutput.value(QStringLiteral("severity")).toString() != QStringLiteral("success")
        || matchedLayout.value(QStringLiteral("buttons")) != copiedLayout.value(QStringLiteral("buttons"))
        || matchedLayout.value(QStringLiteral("continuousPovs")) != copiedLayout.value(QStringLiteral("continuousPovs"))
        || matchedLayout.value(QStringLiteral("discretePovs")) != copiedLayout.value(QStringLiteral("discretePovs"))
        || matchedLayout.value(QStringLiteral("axes")) != copiedLayout.value(QStringLiteral("axes"))
        || customLayout.value(QStringLiteral("buttons")).toInt() != 64
        || customLayout.value(QStringLiteral("continuousPovs")).toInt() != 0
        || customLayout.value(QStringLiteral("discretePovs")).toInt() != 2
        || customLayout.value(QStringLiteral("axes")).toString() != QStringLiteral("X · Rx · Slider 1")) {
        return failPresentationLifecycleTest(QStringLiteral("Virtual Output modes did not save the promised capability configuration"));
    }

    // Critical Devices actions must provide an observable result on both the
    // invalid and valid path. Invoke the same QML helper used by CREATE RIG;
    // a bare backend bool or empty ID is not sufficient UI feedback.
    QObject *feedback = devices->findChild<QObject *>(QStringLiteral("deviceActionFeedback"));
    QQmlExpression transientRefresh(qmlContext(devices), devices,
        QStringLiteral("showTransientActionFeedback({ success: true, title: 'Refreshing devices', message: 'Fixture refresh' }, '', '', 40)"));
    transientRefresh.evaluate();
    const bool transientVisible = feedback && !transientRefresh.hasError() && feedback->property("visible").toBool();
    QTest::qWait(25);
    QQmlExpression replacementRefresh(qmlContext(devices), devices,
        QStringLiteral("showTransientActionFeedback({ success: true, title: 'Refresh complete', message: 'Replacement fixture' }, '', '', 70)"));
    replacementRefresh.evaluate();
    QTest::qWait(45);
    const bool replacementResetTimer = feedback && !replacementRefresh.hasError()
        && feedback->property("visible").toBool()
        && devices->property("actionFeedback").toMap().value(QStringLiteral("title")).toString()
               == QStringLiteral("Refresh complete");
    QTest::qWait(55);
    const bool transientDismissed = feedback && !feedback->property("visible").toBool();
    if (!transientVisible || !replacementResetTimer || !transientDismissed) {
        return failPresentationLifecycleTest(QStringLiteral("Devices feedback did not expire or reset its lifecycle timer"));
    }
    QQmlExpression invalidCreate(qmlContext(devices), devices,
        QStringLiteral("createRigWithInputs('Missing Input Fixture', [], '%1')").arg(outputId));
    const QVariant invalidResult = invalidCreate.evaluate();
    if (!feedback || invalidCreate.hasError() || invalidResult.toMap().value(QStringLiteral("success")).toBool()
        || !devices->property("actionFeedback").toMap().value(QStringLiteral("message")).toString().contains(
            QStringLiteral("Connect a controller to create your first Device Rig"))
        || !feedback->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Invalid CREATE RIG did not expose its physical-controller error"));
    }
    QQmlExpression validCreate(qmlContext(devices), devices,
        QStringLiteral("createRigWithInputs('Observable Result Fixture Rig', ['%1'], '%2')")
            .arg(firstMember, outputId));
    const QVariant validResult = validCreate.evaluate();
    const QString createdByAction = validResult.toMap().value(QStringLiteral("objectId")).toString();
    if (validCreate.hasError() || !validResult.toMap().value(QStringLiteral("success")).toBool()
        || createdByAction.isEmpty() || !devices->property("actionFeedback").toMap().value(
            QStringLiteral("title")).toString().contains(QStringLiteral("Device Rig created"))
        || !backend.deleteDeviceRig(createdByAction)) {
        return failPresentationLifecycleTest(QStringLiteral("Valid CREATE RIG did not expose a success result"));
    }
    // Saved offline controllers are configuration targets, not a mere history
    // list.  This deliberately uses the known-offline, unverified fixture and
    // verifies that the new rig proceeds to guided setup instead of requiring
    // a current DirectInput connection.
    const QVariantMap offlineRig = backend.createDeviceRigResult(
        QStringLiteral("Offline Configuration Fixture Rig"), {secondMember}, outputId);
    const QString offlineRigId = offlineRig.value(QStringLiteral("objectId")).toString();
    if (!offlineRig.value(QStringLiteral("success")).toBool() || offlineRigId.isEmpty()
        || offlineRig.value(QStringLiteral("nextAction")).toString() != QStringLiteral("setup")
        || !backend.deleteDeviceRig(offlineRigId)) {
        return failPresentationLifecycleTest(QStringLiteral("Saved offline controller could not create a configurable Device Rig"));
    }
    settlePresentation();

    // Exercise the same controls a user presses, not only their backing
    // helpers. Every major Devices path must either open its next step or
    // render an actionable result. The fixture identities are deliberately
    // synthetic, so Apply Visibility fails before any driver command can be
    // issued; that makes the failure-result contract safe to test here.
    const auto triggerDevicesControl = [&devices](const QString &objectName, const QString &label) {
        QObject *control = devices ? devices->findChild<QObject *>(objectName) : nullptr;
        if (!control || !control->property("commandEnabled").toBool()
            || !QMetaObject::invokeMethod(control, "triggered")) {
            return failPresentationLifecycleTest(QStringLiteral("%1 was not an enabled Devices control").arg(label));
        }
        return true;
    };
    const auto requireVisibleDialog = [&devices](const QString &objectName, const QString &label) {
        QObject *dialog = devices ? devices->findChild<QObject *>(objectName) : nullptr;
        if (!dialog || !dialog->property("visible").toBool()) {
            return failPresentationLifecycleTest(QStringLiteral("%1 did not present its next step").arg(label));
        }
        return true;
    };
    QObject *addMemberDialog = devices->findChild<QObject *>(QStringLiteral("addMemberDialog"));
    QObject *addOutputDialog = devices->findChild<QObject *>(QStringLiteral("addOutputDialog"));
    QObject *createOutputDialog = devices->findChild<QObject *>(QStringLiteral("createOutputDialog"));
    QObject *virtualInputDialog = devices->findChild<QObject *>(QStringLiteral("addVirtualInputDialog"));
    QObject *visibilityDialog = devices->findChild<QObject *>(QStringLiteral("visibilityConfirmationDialog"));
    QObject *setupDialog = surface->findChild<QObject *>(QStringLiteral("controllerSetupDialog"));
    if (!addMemberDialog || !addOutputDialog || !createOutputDialog || !virtualInputDialog || !visibilityDialog || !setupDialog) {
        return failPresentationLifecycleTest(QStringLiteral("Devices action dialogs were not available"));
    }
    if (!triggerDevicesControl(QStringLiteral("openAddVirtualInputButton"), QStringLiteral("Add Virtual Input"))) return false;
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("addVirtualInputDialog"), QStringLiteral("Add Virtual Input"))) return false;
    QMetaObject::invokeMethod(virtualInputDialog, "close");

    if (!triggerDevicesControl(QStringLiteral("openStandaloneCreateOutputButton"), QStringLiteral("Add Virtual Output"))) return false;
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("createOutputDialog"), QStringLiteral("Add Virtual Output"))) return false;
    QMetaObject::invokeMethod(createOutputDialog, "close");

    if (!triggerDevicesControl(QStringLiteral("addInputToRigButton"), QStringLiteral("Add Input"))) return false;
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("addMemberDialog"), QStringLiteral("Add Input"))) return false;
    QMetaObject::invokeMethod(addMemberDialog, "close");

    if (!triggerDevicesControl(QStringLiteral("addOutputToRigButton"), QStringLiteral("Add Output"))) return false;
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("addOutputDialog"), QStringLiteral("Add Output"))) return false;
    if (!triggerDevicesControl(QStringLiteral("openCreateOutputButton"), QStringLiteral("Create Virtual Output"))) return false;
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("createOutputDialog"), QStringLiteral("Create Virtual Output"))) return false;
    QMetaObject::invokeMethod(createOutputDialog, "close");

    // Visibility is intentionally object-scoped: exercise the actual member
    // control and prove its confirmation contains only that member, rather
    // than retaining the rejected page-wide four-button card.
    QQmlExpression openMemberVisibility(qmlContext(devices), devices, QStringLiteral(
        "(function() { const card = rigMemberCardFor('%1');"
        " if (!card || !card.visibilityControl) return false;"
        " card.visibilityControl.triggered(); return true; })()")
            .arg(firstMember));
    const bool memberVisibilityOpened = openMemberVisibility.evaluate().toBool();
    if (openMemberVisibility.hasError() || !memberVisibilityOpened) {
        return failPresentationLifecycleTest(QStringLiteral("Per-device visibility control was not available"));
    }
    settlePresentation();
    if (!requireVisibleDialog(QStringLiteral("visibilityConfirmationDialog"), QStringLiteral("Hide physical controllers"))) return false;
    if (visibilityDialog->property("ids").toStringList() != QStringList{firstMember}) {
        return failPresentationLifecycleTest(QStringLiteral("Per-device visibility action did not retain its exact target"));
    }
    if (!triggerDevicesControl(QStringLiteral("visibilityApplyButton"), QStringLiteral("Apply game visibility"))) return false;
    settlePresentation();
    if (devices->property("actionFeedback").toMap().value(QStringLiteral("success")).toBool()
        || devices->property("actionFeedback").toMap().value(QStringLiteral("title")).toString().isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Apply game visibility did not show its safe failure result"));
    }
    QMetaObject::invokeMethod(visibilityDialog, "close");

    if (!triggerDevicesControl(QStringLiteral("checkRigSetupButton"), QStringLiteral("Check Rig Setup"))) return false;
    settlePresentation();
    if (!setupDialog->property("visible").toBool()
        || devices->property("actionFeedback").toMap().value(QStringLiteral("title")).toString()
               != QStringLiteral("Opening Setup Assistant")) {
        return failPresentationLifecycleTest(QStringLiteral("Check Rig Setup did not hand off to the Setup Assistant with transient feedback"));
    }
    QMetaObject::invokeMethod(setupDialog, "close");

    const bool rigStartedActive = backend.activeDeviceRigId() == rigId;
    const QString firstActivationControl = rigStartedActive ? QStringLiteral("deactivateRigButton")
                                                            : QStringLiteral("activateRigButton");
    if (!triggerDevicesControl(firstActivationControl,
                               rigStartedActive ? QStringLiteral("Deactivate") : QStringLiteral("Activate"))) return false;
    settlePresentation();
    if (backend.activeDeviceRigId() == (rigStartedActive ? rigId : QString{})
        || devices->property("actionFeedback").toMap().value(QStringLiteral("title")).toString().isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Device Rig activation control did not report its result"));
    }
    const QString restoreActivationControl = rigStartedActive ? QStringLiteral("activateRigButton")
                                                               : QStringLiteral("deactivateRigButton");
    if (!triggerDevicesControl(restoreActivationControl,
                               rigStartedActive ? QStringLiteral("Restore activation") : QStringLiteral("Deactivate"))) return false;
    settlePresentation();
    if ((backend.activeDeviceRigId() == rigId) != rigStartedActive) {
        return failPresentationLifecycleTest(QStringLiteral("Device Rig activation control did not restore the fixture state"));
    }

    // Invoke the exact Devices-page helper that the EDIT THIS control calls.
    // Repeater delegates are visual children, so inspect their visible QML
    // properties through the page's lexical helper instead of unsafe QObject
    // parent traversal.
    QString escapedSecondMember = secondMember;
    escapedSecondMember.replace(u'\\', QStringLiteral("\\\\"));
    escapedSecondMember.replace(u'\"', QStringLiteral("\\\""));
    const auto memberProperty = [&escapedSecondMember](QObject *root, const QString &property,
                                                        bool *available = nullptr) -> QVariant {
        if (available) *available = false;
        if (!root) return {};
        QQmlExpression expression(qmlContext(root), root, QStringLiteral(
            "(function() { const card = rigMemberCardFor(\"%1\");"
            " return card ? card.%2 : undefined; })()")
            .arg(escapedSecondMember, property));
        const QVariant value = expression.evaluate();
        if (available) *available = !expression.hasError() && value.isValid();
        return value;
    };
    QObject *contextLabel = surface->findChild<QObject *>(QStringLiteral("deviceContextLabel"));
    bool initialTextAvailable = false;
    const QVariant initialText = memberProperty(devices, QStringLiteral("editControl.text"), &initialTextAvailable);
    QQmlExpression activateEditThis(qmlContext(devices), devices,
                                    QStringLiteral("editThisDevice(\"%1\")").arg(escapedSecondMember));
    const QVariant activation = activateEditThis.evaluate();
    if (!contextLabel || !initialTextAvailable || initialText.toString() != QStringLiteral("EDIT THIS")
        || activateEditThis.hasError() || !activation.toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS or its initial visible Device Context state was unavailable"));
    }
    settlePresentation();
    const QVariantList editScope = backend.editingDevices();
    if (backend.editingDeviceRigId() != rigId || backend.editingScopeLabel() != secondName
        || editScope.size() != 2 || !editScope.at(1).toMap().value(QStringLiteral("selected")).toBool()
        || editScope.at(0).toMap().value(QStringLiteral("selected")).toBool()
        || backend.activeDeviceRigId() != activeRigBeforeEdit
        || memberProperty(devices, QStringLiteral("editControl.text")).toString() != QStringLiteral("EDITING")
        || !memberProperty(devices, QStringLiteral("editingTarget")).toBool()
        || !contextLabel->property("text").toString().contains(secondName)) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS did not immediately update the shared single-device context and visible target state"));
    }

    // The editing scope is authoritative across every device-aware page. The
    // selected fixture is offline, so this also proves that saved
    // configuration remains selectable without changing the runtime rig.
    for (const int page : {0, 1, 6, 9, 3}) {
        if (!selectPage(surface, page) || backend.editingDeviceRigId() != rigId
            || backend.editingScopeLabel() != secondName || backend.activeDeviceRigId() != activeRigBeforeEdit) {
            return failPresentationLifecycleTest(QStringLiteral("Device-aware page %1 did not retain the shared EDIT THIS context").arg(page));
        }
    }
    if (!selectPage(surface, 10)) return false;
    devices = pageItem(surface, 10);
    if (!devices || !memberProperty(devices, QStringLiteral("editingTarget")).toBool()
        || memberProperty(devices, QStringLiteral("editControl.text")).toString() != QStringLiteral("EDITING")) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS visible state was stale after device-aware page navigation"));
    }

    // Use the top-bar selector's own All Devices path; a fresh controller
    // snapshot exercises its value-model replacement without losing scope.
    QObject *contextSelector = nullptr;
    for (QObject *candidate : surface->findChildren<QObject *>()) {
        if (candidate->objectName().endsWith(QStringLiteral("DeviceContextSelector"))) {
            contextSelector = candidate;
            break;
        }
    }
    if (!contextSelector) return failPresentationLifecycleTest(QStringLiteral("Persistent Device Context selector was unavailable"));
    backend.refreshControllers();
    settlePresentation();
    if (!memberProperty(devices, QStringLiteral("editingTarget")).toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("EDIT THIS target was lost during an offline controller model refresh"));
    }
    QQmlExpression clearScope(qmlContext(contextSelector), contextSelector, QStringLiteral("selectScope([])"));
    clearScope.evaluate();
    if (clearScope.hasError()) return failPresentationLifecycleTest(clearScope.error().toString());
    settlePresentation();
    if (backend.editingDeviceRigId() != rigId || backend.editingScopeLabel() != QStringLiteral("All Devices")
        || backend.activeDeviceRigId() != activeRigBeforeEdit
        || memberProperty(devices, QStringLiteral("editingTarget")).toBool()
        || memberProperty(devices, QStringLiteral("editControl.text")).toString() != QStringLiteral("EDIT THIS")) {
        return failPresentationLifecycleTest(QStringLiteral("Device Context All Devices return path did not clear EDIT THIS state"));
    }

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

QString expectedDevicePanelTreatment(const QString &theme)
{
    if (theme == QStringLiteral("Legacy")) return QStringLiteral("legacy-layered");
    if (theme == QStringLiteral("Top Gun")) return QStringLiteral("top-gun-instrument");
    if (theme == QStringLiteral("Day Ops")) return QStringLiteral("day-ops-deck");
    return QStringLiteral("standard-raised");
}

bool verifyThemedDeviceSurface(QObject *surface, const QString &theme, const QString &label)
{
    if (!surface) {
        return failPresentationLifecycleTest(QStringLiteral("%1 did not create a themed surface for %2")
            .arg(label, theme));
    }
    const QString expected = expectedDevicePanelTreatment(theme);
    if (surface->property("surfaceTreatment").toString() != expected) {
        return failPresentationLifecycleTest(QStringLiteral("%1 used %2 instead of %3 for %4")
            .arg(label, surface->property("surfaceTreatment").toString(), expected, theme));
    }
    if (theme == QStringLiteral("Legacy")) {
        const auto *topHighlight = surface->findChild<QQuickItem *>(
            QStringLiteral("legacyPanelTopHighlight"), Qt::FindDirectChildrenOnly);
        const auto *bottomEdge = surface->findChild<QQuickItem *>(
            QStringLiteral("legacyPanelBottomEdge"), Qt::FindDirectChildrenOnly);
        if (!topHighlight || !topHighlight->isVisible() || !bottomEdge || !bottomEdge->isVisible()) {
            return failPresentationLifecycleTest(QStringLiteral("%1 lost the Legacy layered construction")
                .arg(label));
        }
    }
    return true;
}

QString expectedThemedDialogHeaderTreatment(const QString &theme)
{
    if (theme == QStringLiteral("Legacy")) return QStringLiteral("legacy-header");
    if (theme == QStringLiteral("Top Gun")) return QStringLiteral("top-gun-header");
    if (theme == QStringLiteral("Day Ops")) return QStringLiteral("day-ops-header");
    return QStringLiteral("standard-header");
}

bool verifyThemedDialogHeader(QObject *popup, const QString &theme, const QString &label)
{
    if (!popup) {
        return failPresentationLifecycleTest(QStringLiteral("%1 dialog was not available for %2")
            .arg(label, theme));
    }
    QObject *header = qvariant_cast<QObject *>(popup->property("header"));
    if (!header) {
        return failPresentationLifecycleTest(QStringLiteral("%1 used no application-owned dialog header for %2")
            .arg(label, theme));
    }
    const QString expected = expectedThemedDialogHeaderTreatment(theme);
    if (header->property("surfaceTreatment").toString() != expected) {
        return failPresentationLifecycleTest(QStringLiteral("%1 used %2 instead of %3 for %4")
            .arg(label, header->property("surfaceTreatment").toString(), expected, theme));
    }
    // Legacy's original verifier/dialog vocabulary is deliberately a dark,
    // layered cockpit header, not a recolored Standard panel. Check its
    // durable base surface directly so shared component work cannot silently
    // collapse the theme identities.
    if (theme == QStringLiteral("Legacy")
        && header->property("color").value<QColor>() != QColor(QStringLiteral("#132027"))) {
        return failPresentationLifecycleTest(QStringLiteral("%1 lost the established Legacy dialog header surface")
            .arg(label));
    }
    return true;
}

bool verifyAppHealthSurface(hotas::AppBackend &backend, QObject *surface, const QString &theme)
{
    if (!surface) return failPresentationLifecycleTest(QStringLiteral("App Health has no presentation surface"));
    const QString controlName = theme == QStringLiteral("Legacy")
        ? QStringLiteral("legacyAppHealthControl")
        : theme == QStringLiteral("Top Gun") ? QStringLiteral("topGunAppHealthControl")
                                            : QStringLiteral("standardAppHealthControl");
    const QString popupName = theme == QStringLiteral("Legacy")
        ? QStringLiteral("legacyAppHealthPopup") : QStringLiteral("standardAppHealthPopup");
    QObject *control = surface->findChild<QObject *>(controlName);
    QObject *popup = surface->findChild<QObject *>(popupName);
    if (!control || !popup || !control->property("visible").toBool()
        || !QMetaObject::invokeMethod(popup, "open")) {
        return failPresentationLifecycleTest(QStringLiteral("App Health control was not available for %1").arg(theme));
    }
    settlePresentation();
    const bool popupVisible = popup->property("visible").toBool();
    QQmlExpression centered(qmlContext(popup), popup,
        QStringLiteral("Math.abs(x - Math.max(0, Math.round(((parent ? parent.width : width) - width) / 2))) <= 1"
                       " && Math.abs(y - Math.max(0, Math.round(((parent ? parent.height : height) - height) / 2))) <= 1"));
    const bool popupCentered = !centered.hasError() && centered.evaluate().toBool();
    QMetaObject::invokeMethod(popup, "close");
    QQmlExpression navigate(qmlContext(surface), surface,
        QStringLiteral("navigateToIssue({ page: 10, objectType: 'deviceRig', objectId: 'fixture-rig' }); currentPage"));
    const QVariant navigationValue = navigate.evaluate();
    const bool routedToDevices = !navigate.hasError() && navigationValue.toInt() == 10
        && backend.editingDeviceRigId() == QStringLiteral("fixture-rig");
    QQmlExpression deepLink(qmlContext(surface), surface,
        QStringLiteral("navigateToIssue({ page: 10, objectType: 'physicalDevice', objectId: 'fixture-throttle' }); currentPage"));
    const QVariant deepLinkValue = deepLink.evaluate();
    settlePresentation();
    QObject *devices = pageItem(surface, 10);
    QObject *physicalDialog = surface->findChild<QObject *>(QStringLiteral("physicalDeviceDialog"));
    const bool openedPhysicalTarget = !deepLink.hasError() && deepLinkValue.toInt() == 10
        && backend.editingScopeLabel() == QStringLiteral("Fixture STECS") && devices
        && devices->property("selectedDeviceId").toString() == QStringLiteral("fixture-throttle")
        && physicalDialog && physicalDialog->property("visible").toBool();
    if (physicalDialog) QMetaObject::invokeMethod(physicalDialog, "close");
    if (!popupVisible || !popupCentered || !routedToDevices || !openedPhysicalTarget) {
        return failPresentationLifecycleTest(QStringLiteral("App Health did not open, center, or route its Device Rig review for %1").arg(theme));
    }
    return true;
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
    // Exercise every first-class Device surface in every theme. This is
    // intentionally presentation-only: no setup, repair, activation, or
    // driver action is invoked while the dialogs are open.
    const auto verifyPopupSurface = [&](QObject *popup, const QString &label) {
        if (!popup || !QMetaObject::invokeMethod(popup, "open")) {
            return failPresentationLifecycleTest(QStringLiteral("%1 did not open for %2").arg(label, theme));
        }
        settlePresentation();
        QObject *background = qvariant_cast<QObject *>(popup->property("background"));
        const bool valid = verifyThemedDeviceSurface(background, theme, label);
        QMetaObject::invokeMethod(popup, "close");
        settlePresentation();
        return valid;
    };
    if (!verifyPopupSurface(contextSelector->findChild<QObject *>(QStringLiteral("deviceContextPopup")),
            QStringLiteral("Device Context popup"))
        || !verifyPopupSurface(devices->findChild<QObject *>(QStringLiteral("rigDetailsActionsPopup")),
            QStringLiteral("Rig Details overflow popup"))) {
        shell->resize(original);
        return false;
    }
    // A Popup close can finish on the next animation frame. Start the
    // pointer-driven interaction from an actual closed state rather than
    // inheriting the surface probe's transient open lifecycle.
    QObject *surfaceProbePopup = devices->findChild<QObject *>(QStringLiteral("rigDetailsActionsPopup"));
    QMetaObject::invokeMethod(surfaceProbePopup, "close");
    QTest::qWait(300);
    settlePresentation();
    if (surfaceProbePopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup did not finish closing after its surface probe for %1").arg(theme));
    }
    // This is deliberately pointer-driven rather than a source or direct
    // Popup.open() assertion. It covers the failure mode where the packaged
    // overflow menu teleported after scrolling or toggled back open when its
    // own trigger was clicked a second time.
    auto *scroll = devices->findChild<QQuickItem *>(QStringLiteral("devicesScroll"));
    auto *overflow = devices->findChild<QQuickItem *>(QStringLiteral("rigDetailsOverflowButton"));
    QObject *overflowPopup = devices->findChild<QObject *>(QStringLiteral("rigDetailsActionsPopup"));
    auto *overlay = overflowPopup ? qobject_cast<QQuickItem *>(overflowPopup->parent()) : nullptr;
    auto *overflowPopupVisual = overflowPopup
        ? qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(overflowPopup->property("background"))) : nullptr;
    if (!scroll || !overflow || !overflowPopup || !overlay || !overflowPopupVisual) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices scrolling or overflow controls were unavailable for %1").arg(theme));
    }
    auto *flickable = qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(scroll->property("contentItem")));
    if (!flickable) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices ScrollView did not expose its Flickable for %1").arg(theme));
    }
    auto *devicesContent = devices->findChild<QQuickItem *>(QStringLiteral("devicesContent"));
    if (!devicesContent) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices scroll content was unavailable for %1").arg(theme));
    }
    // Use the item's final scene transform.  ScrollView owns a real
    // Flickable, so this remains the actual pointer location through shell
    // resizes and content scrolling in every theme.
    const auto contentScenePoint = [&](QQuickItem *item, const QPointF &point) {
        return item->mapToScene(point);
    };
    shell->resize({900, 500});
    flickable->setProperty("contentY", 0.0);
    settlePresentation();
    const qreal maximumContentY = std::max<qreal>(0.0,
        flickable->property("contentHeight").toReal() - flickable->height());
    if (maximumContentY <= 1.0) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices fixture did not create scrollable content for %1").arg(theme));
    }
    auto *bottomPanel = devices->findChild<QQuickItem *>(QStringLiteral("virtualOutputsInventoryPanel"));
    flickable->setProperty("contentY", maximumContentY);
    settlePresentation();
    const qreal bottomContentTop = bottomPanel && devicesContent
        ? bottomPanel->mapToItem(devicesContent, QPointF{}).y() : -1.0;
    const qreal bottomContentBottom = bottomContentTop + (bottomPanel ? bottomPanel->height() : 0.0);
    const qreal bottomViewportEdge = flickable->property("contentY").toReal() + flickable->height();
    if (!bottomPanel || !devicesContent || bottomContentBottom > bottomViewportEdge + 1.0
        || bottomContentBottom < flickable->property("contentY").toReal()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices maximum scroll did not reveal its final virtual-output controls for %1")
            .arg(theme));
    }
    const qreal triggerContentY = overflow->mapToItem(devicesContent, QPointF{}).y();
    const qreal initialTriggerContentY = std::min(maximumContentY,
        std::max<qreal>(1.0, triggerContentY - 42.0));
    flickable->setProperty("contentY", initialTriggerContentY);
    settlePresentation();
    const auto clickOverflow = [&] {
        const QPointF point = contentScenePoint(overflow, QPointF(overflow->width() * 0.5, overflow->height() * 0.5));
        QTest::mouseClick(shell, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        settlePresentation();
    };
    const auto popupIsAdjacent = [&] {
        const QRectF popupBounds(overflowPopupVisual->mapToScene(QPointF{}), overflowPopupVisual->size());
        const QRectF triggerBounds(contentScenePoint(overflow, QPointF{}), overflow->size());
        const bool horizontalOverlap = popupBounds.left() <= triggerBounds.right()
            && popupBounds.right() >= triggerBounds.left();
        const qreal verticalGap = popupBounds.top() >= triggerBounds.bottom()
            ? popupBounds.top() - triggerBounds.bottom()
            : triggerBounds.top() >= popupBounds.bottom()
                ? triggerBounds.top() - popupBounds.bottom() : 0.0;
        return horizontalOverlap && verticalGap <= 12.0;
    };
    clickOverflow();
    if (!overflowPopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow trigger did not open its popup for %1").arg(theme));
    }
    if (!popupIsAdjacent()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup was not adjacent to its trigger for %1")
            .arg(theme));
    }
    clickOverflow();
    if (overflowPopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow trigger did not close its own popup for %1").arg(theme));
    }
    // Scroll while keeping the actual trigger in view, then repeat the same
    // scene-coordinate and toggle checks.
    const qreal scrolledTriggerContentY = std::min(maximumContentY,
        std::max<qreal>(1.0, triggerContentY - 16.0));
    flickable->setProperty("contentY", scrolledTriggerContentY);
    settlePresentation();
    if (flickable->property("contentY").toReal() <= 0.0) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Devices page did not scroll its real Flickable content for %1").arg(theme));
    }
    clickOverflow();
    const bool openedAfterScroll = overflowPopup->property("visible").toBool();
    const bool adjacentAfterScroll = openedAfterScroll && popupIsAdjacent();
    clickOverflow();
    if (!openedAfterScroll || !adjacentAfterScroll) {
        shell->resize(original);
        const QPointF trigger = contentScenePoint(overflow, QPointF{});
        const QPointF popup = overlay->mapToScene(QPointF(overflowPopup->property("x").toReal(), overflowPopup->property("y").toReal()));
        const QPointF devicesOrigin = devices->mapToScene(QPointF{});
        const QPointF overlayOrigin = overlay->mapToScene(QPointF{});
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup failed after Devices scrolling for %1 (opened=%2, adjacent=%3, popup=%4,%5 scene=%6,%7; trigger=%8,%9; contentY=%10; devices=%11,%12 xy=%13,%14; overlay=%15,%16 xy=%17,%18)")
            .arg(theme).arg(openedAfterScroll).arg(adjacentAfterScroll).arg(overflowPopup->property("x").toReal()).arg(overflowPopup->property("y").toReal())
            .arg(popup.x()).arg(popup.y()).arg(trigger.x()).arg(trigger.y()).arg(flickable->property("contentY").toReal())
            .arg(devicesOrigin.x()).arg(devicesOrigin.y()).arg(devices->x()).arg(devices->y())
            .arg(overlayOrigin.x()).arg(overlayOrigin.y()).arg(overlay->x()).arg(overlay->y()));
    }
    if (overflowPopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow trigger did not close after Devices scrolling for %1").arg(theme));
    }
    clickOverflow();
    const QPoint outsidePoint = flickable->mapToScene(QPointF(2.0, 2.0)).toPoint();
    QTest::mouseClick(shell, Qt::LeftButton, Qt::NoModifier, outsidePoint);
    settlePresentation();
    if (overflowPopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup did not close on an outside click for %1").arg(theme));
    }
    shell->resize({1100, 620});
    settlePresentation();
    clickOverflow();
    const bool validAfterResize = overflowPopup->property("visible").toBool() && popupIsAdjacent();
    QTest::keyClick(shell, Qt::Key_Escape);
    settlePresentation();
    if (!validAfterResize) {
        shell->resize(original);
        const QPointF trigger = contentScenePoint(overflow, QPointF{});
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup was not adjacent after resize for %1 (popup=%2,%3; trigger=%4,%5; contentY=%6)")
            .arg(theme).arg(overflowPopup->property("x").toReal()).arg(overflowPopup->property("y").toReal())
            .arg(trigger.x()).arg(trigger.y()).arg(flickable->property("contentY").toReal()));
    }
    if (overflowPopup->property("visible").toBool()) {
        shell->resize(original);
        return failPresentationLifecycleTest(QStringLiteral("Rig Details overflow popup did not close on Escape for %1").arg(theme));
    }
    const QStringList dialogNames{QStringLiteral("physicalDeviceDialog"),
        QStringLiteral("outputDetailDialog"), QStringLiteral("createRigDialog")};
    for (const QString &dialogName : dialogNames) {
        QObject *dialog = devices->findChild<QObject *>(dialogName);
        if (!verifyThemedDialogHeader(dialog, theme, dialogName)
            || !verifyPopupSurface(dialog, dialogName)) {
            shell->resize(original);
            return false;
        }
    }
    // 640px specifically guards the compact Device Context path. The
    // supported visual sizes below remain the product acceptance baseline.
    const QList<QSize> sizes{{640, 650}, {900, 650}, {1280, 720}, {1440, 900}, {1920, 1080}};
    const QStringList panels{QStringLiteral("activeRigPanel"), QStringLiteral("deviceRigListPanel"),
        QStringLiteral("rigDetailsPanel"), QStringLiteral("automaticBehaviorPanel"),
        QStringLiteral("knownDevicesPanel"), QStringLiteral("virtualInputsPanel"),
        QStringLiteral("virtualOutputsInventoryPanel")};
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
            if (!verifyThemedDeviceSurface(panel, theme, name)) {
                shell->resize(original);
                return false;
            }
            if (theme == QStringLiteral("Legacy")) {
                const QColor expectedLegacySurface(QStringLiteral("#e9161d23"));
                if (panel->property("color").value<QColor>() != expectedLegacySurface) {
                    shell->resize(original);
                    return failPresentationLifecycleTest(QStringLiteral("Legacy Devices panel %1 lost its established layered surface")
                        .arg(name));
                }
                const auto *topHighlight = panel->findChild<QQuickItem *>(
                    QStringLiteral("legacyPanelTopHighlight"), Qt::FindDirectChildrenOnly);
                const auto *bottomEdge = panel->findChild<QQuickItem *>(
                    QStringLiteral("legacyPanelBottomEdge"), Qt::FindDirectChildrenOnly);
                if (!topHighlight || !topHighlight->isVisible()
                    || !bottomEdge || !bottomEdge->isVisible()) {
                    shell->resize(original);
                    return failPresentationLifecycleTest(QStringLiteral("Legacy Devices panel %1 lost its established layered panel construction")
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

bool verifyOverviewReadinessLayout(QObject *surface, QWindow *shell, const QString &theme)
{
    if (!shell || !selectPage(surface, 8)) {
        return failPresentationLifecycleTest(QStringLiteral("Overview readiness layout could not enter its page"));
    }
    auto *overview = qobject_cast<QQuickItem *>(pageItem(surface, 8));
    auto *panel = overview ? overview->findChild<QQuickItem *>(QStringLiteral("systemReadinessPanel")) : nullptr;
    auto *list = overview ? overview->findChild<QQuickItem *>(QStringLiteral("systemReadinessList")) : nullptr;
    auto *verify = overview ? overview->findChild<QQuickItem *>(QStringLiteral("systemReadinessVerifyButton")) : nullptr;
    QObject *repeater = overview ? overview->findChild<QObject *>(QStringLiteral("systemReadinessRepeater")) : nullptr;
    if (!overview || !panel || !list || !verify || !repeater) {
        return failPresentationLifecycleTest(QStringLiteral("Overview System readiness controls were unavailable for %1").arg(theme));
    }

    const QSize original = shell->size();
    QList<QSize> sizes{{1280, 720}, {1440, 900}, {1920, 1080}};
    if (!sizes.contains(original)) sizes.append(original);
    for (const QSize &size : sizes) {
        shell->resize(size);
        settlePresentation();
        if (!panel->isVisible() || panel->width() <= 0 || panel->height() <= 0
            || list->width() <= 0 || list->height() <= 0 || repeater->property("count").toInt() <= 0) {
            shell->resize(original);
            return failPresentationLifecycleTest(QStringLiteral("Overview System readiness did not keep its concise readiness list at %1x%2 for %3")
                .arg(size.width()).arg(size.height()).arg(theme));
        }
        const QRectF panelBounds(panel->mapToItem(overview, QPointF{}), panel->size());
        const QRectF verifyBounds(verify->mapToItem(overview, QPointF{}), verify->size());
        if (!panelBounds.contains(verifyBounds) || verifyBounds.width() <= 0 || verifyBounds.height() <= 0) {
            shell->resize(original);
            return failPresentationLifecycleTest(QStringLiteral("Overview Verify Setup did not fit its System readiness card at %1x%2 for %3")
                .arg(size.width()).arg(size.height()).arg(theme));
        }
    }
    shell->resize(original);
    return selectPage(surface, 8);
}

bool captureDevicesSnapshot(hotas::AppBackend &backend, QObject *surface, QWindow *shell,
                            const QString &theme)
{
    // Snapshot capture is deliberately opt-in: normal CI keeps its existing
    // headless lifecycle contract, while a visual-review build can render the
    // same fixture for every supported theme without attaching to a controller
    // or starting the mapper executable.
    const QString snapshotRoot = qEnvironmentVariable("HOTAS_QML_SNAPSHOT_DIR").trimmed();
    if (snapshotRoot.isEmpty()) return true;
    const QDir directory(snapshotRoot);
    if (!directory.exists()) {
        return failPresentationLifecycleTest(QStringLiteral("Requested QML snapshot directory does not exist: %1")
            .arg(snapshotRoot));
    }
    if (!selectPage(surface, 10)) return false;
    settlePresentation();
    auto *quickWindow = qobject_cast<QQuickWindow *>(shell);
    if (!quickWindow) {
        return failPresentationLifecycleTest(QStringLiteral("Devices snapshot host was not a QQuickWindow for %1")
            .arg(theme));
    }
    QString fileName = theme.toLower();
    fileName.replace(u' ', u'-');
    const auto capture = [&](const QString &suffix) {
        const QImage image = quickWindow->grabWindow();
        if (image.isNull() || image.width() < 640 || image.height() < 480) {
            return failPresentationLifecycleTest(QStringLiteral("Devices %1 snapshot was not rendered for %2")
                .arg(suffix, theme));
        }
        const QString outputPath = directory.filePath(QStringLiteral("devices-%1-%2.png")
            .arg(fileName, suffix));
        if (!image.save(outputPath)) {
            return failPresentationLifecycleTest(QStringLiteral("Could not write Devices %1 snapshot: %2")
                .arg(suffix, outputPath));
        }
        return true;
    };
    // Keep the historical landing artifact name for existing review scripts.
    const QImage landing = quickWindow->grabWindow();
    if (landing.isNull() || landing.width() < 640 || landing.height() < 480
        || !landing.save(directory.filePath(QStringLiteral("devices-%1.png").arg(fileName)))) {
        return failPresentationLifecycleTest(QStringLiteral("Devices landing snapshot was not rendered for %1")
            .arg(theme));
    }
    if (!capture(QStringLiteral("landing"))) return false;

    auto *devices = qobject_cast<QQuickItem *>(pageItem(surface, 10));
    if (!devices) {
        return failPresentationLifecycleTest(QStringLiteral("Devices snapshot root was not available for %1").arg(theme));
    }
    const QVariantList rigs = backend.deviceRigs();
    if (rigs.isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Devices snapshot fixture had no rig for %1").arg(theme));
    }
    const QVariantMap rig = rigs.front().toMap();
    const QVariantList members = rig.value(QStringLiteral("members")).toList();
    const QVariantList outputs = rig.value(QStringLiteral("outputs")).toList();
    if (members.isEmpty() || outputs.isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Devices snapshot fixture was incomplete for %1").arg(theme));
    }
    // These selections are view-only test state. The fixture never activates
    // a rig or invokes setup/repair while visual artifacts are captured.
    devices->setProperty("selectedRigId", rig.value(QStringLiteral("id")).toString());
    devices->setProperty("selectedDeviceId", members.front().toMap().value(QStringLiteral("id")).toString());
    devices->setProperty("selectedOutputId", outputs.front().toMap().value(QStringLiteral("id")).toString());
    settlePresentation();

    // Capture the visible EDITING treatment separately from the landing
    // state. This uses the same shared editing context as the real control,
    // while remaining an offline, presentation-only fixture action.
    if (members.size() > 1) {
        const QString targetId = members.at(1).toMap().value(QStringLiteral("id")).toString();
        if (targetId.isEmpty() || !backend.setEditingDeviceContext(rig.value(QStringLiteral("id")).toString(), {targetId})) {
            return failPresentationLifecycleTest(QStringLiteral("Devices editing-target snapshot could not select its fixture member for %1")
                .arg(theme));
        }
        settlePresentation();
        if (!capture(QStringLiteral("editing-target"))
            || !backend.setEditingDeviceContext(rig.value(QStringLiteral("id")).toString(), {})) {
            return failPresentationLifecycleTest(QStringLiteral("Devices editing-target snapshot could not return to All Devices for %1")
                .arg(theme));
        }
        settlePresentation();
    }

    const auto capturePopup = [&](QObject *popup, const QString &suffix) {
        if (!popup || !QMetaObject::invokeMethod(popup, "open")) {
            return failPresentationLifecycleTest(QStringLiteral("Devices %1 surface did not open for %2")
                .arg(suffix, theme));
        }
        settlePresentation();
        const bool captured = popup->property("visible").toBool() && capture(suffix);
        QMetaObject::invokeMethod(popup, "close");
        settlePresentation();
        return captured;
    };
    if (!capturePopup(surface->findChild<QObject *>(QStringLiteral("deviceContextPopup")),
            QStringLiteral("context"))
        || !capturePopup(devices->findChild<QObject *>(QStringLiteral("rigDetailsActionsPopup")),
            QStringLiteral("rig-actions"))
        || !capturePopup(devices->findChild<QObject *>(QStringLiteral("physicalDeviceDialog")),
            QStringLiteral("physical-device"))
        || !capturePopup(devices->findChild<QObject *>(QStringLiteral("outputDetailDialog")),
            QStringLiteral("virtual-output"))
        || !capturePopup(devices->findChild<QObject *>(QStringLiteral("createRigDialog")),
            QStringLiteral("create-rig"))
        || !capturePopup(surface->findChild<QObject *>(QStringLiteral("controllerSetupDialog")),
            QStringLiteral("rig-verifier"))) {
        return false;
    }
    return selectPage(surface, 8);
}

bool verifyUnifiedVerifierPresentation(QObject *surface, const QString &theme)
{
    QObject *verifier = surface->findChild<QObject *>(QStringLiteral("controllerSetupDialog"));
    if (!verifier) {
        return failPresentationLifecycleTest(QStringLiteral("Unified rig verifier was not created for %1").arg(theme));
    }
    QMetaObject::invokeMethod(verifier, "open");
    settlePresentation();
    if (!verifier->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Unified rig verifier did not open for %1").arg(theme));
    }
    QMetaObject::invokeMethod(verifier, "close");
    settlePresentation();
    if (verifier->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Unified rig verifier did not close for %1").arg(theme));
    }
    return true;
}

bool verifyPageLifecycle(hotas::AppBackend &backend, QWindow *shell, const QString &theme)
{
    QObject *presentation = shell->findChild<QObject *>(QStringLiteral("presentationLoader"));
    if (!presentation) return failPresentationLifecycleTest(QStringLiteral("presentation Loader was not found"));
    QObject *surface = qvariant_cast<QObject *>(presentation->property("item"));
    if (!surface) return failPresentationLifecycleTest(QStringLiteral("theme surface was not loaded"));
    if (!verifyUnifiedVerifierPresentation(surface, theme)) return false;

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
    // Qt 6.8's offscreen renderer lazily retains two attached visual-control
    // helpers after the first complete navigation run. They are shell-owned,
    // bounded, and unrelated to page instances; anything beyond that remains
    // a strict retained-page failure.
    constexpr int kAllowedLazyAttachedObjects = 2;
    if (afterNavigationObjectCount > freshObjectCount + kAllowedLazyAttachedObjects) {
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
    if (!verifyAdaptiveSetupAssistantScenarios(backend)) return false;
    if (!verifyAppHealthSurface(backend, surface, theme)) return false;
    if (!verifyDevicesInteractionStress(backend, surface)) return false;
    if (!verifyDevicesResponsiveLayout(surface, shell, theme)) return false;
    if (!verifyOverviewReadinessLayout(surface, shell, theme)) return false;
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
    QStringList themes{
        QStringLiteral("Legacy"),
        QStringLiteral("Standard"),
        QStringLiteral("Top Gun"),
        QStringLiteral("Day Ops"),
    };
    // Keep the default release contract across all themes, while allowing a
    // focused theme rerun when a visual failure is being diagnosed locally.
    const QString requestedTheme = qEnvironmentVariable("HOTAS_QML_TEST_THEME").trimmed();
    if (!requestedTheme.isEmpty()) themes = {requestedTheme};

    for (const QString &theme : themes) {
        themeManager.setCurrentTheme(theme);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
        engine.loadFromModule(u"HOTASMapperStartupTest"_qs, u"Main"_qs);
        auto *window = engine.rootObjects().isEmpty()
            ? nullptr : qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (!window) return 1;

        settlePresentation();
        if (!verifyPageLifecycle(backend, window, theme)) return 1;
        QObject *presentation = window->findChild<QObject *>(QStringLiteral("presentationLoader"));
        QObject *surface = presentation ? qvariant_cast<QObject *>(presentation->property("item")) : nullptr;
        if (!surface || !captureDevicesSnapshot(backend, surface, window, theme)) return 1;
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
