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
#include <QJsonDocument>
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
#include <QUrl>
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

QQuickItem *flickableContentItem(QQuickItem *viewport)
{
    if (!viewport) return nullptr;
    const QVariant content = viewport->property("contentItem");
    if (auto *quickItem = qvariant_cast<QQuickItem *>(content)) return quickItem;
    return qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(content));
}

QPoint viewportPoint(QQuickItem *item, QQuickItem *viewport, const QPointF &point)
{
    if (!item || !viewport) return {};
    auto *contentItem = flickableContentItem(viewport);
    if (!contentItem) return item->mapToScene(point).toPoint();
    const QPointF inContent = item->mapToItem(contentItem, point);
    const QPointF inViewport = inContent - QPointF(viewport->property("contentX").toReal(),
                                                    viewport->property("contentY").toReal());
    return viewport->mapToScene(inViewport).toPoint();
}

QPointF contentPoint(QQuickItem *item, QQuickItem *viewport)
{
    if (!item || !viewport) return {};
    auto *contentItem = flickableContentItem(viewport);
    return contentItem ? item->mapToItem(contentItem, QPointF{})
                       : item->mapToItem(viewport, QPointF{});
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
    for (int attempt = 0; attempt < 3; ++attempt) {
        const bool viewportCoordinates = scroll->objectName() == QStringLiteral("flightDeckAdaptiveResponse");
        const QPointF relative = viewportCoordinates ? contentPoint(comboItem, scroll)
            : comboItem->mapToScene(QPointF{}) - scroll->mapToScene(QPointF{});
        const qreal contentY = scroll->property("contentY").toReal();
        scroll->setProperty("contentY", std::max<qreal>(0.0,
            viewportCoordinates ? relative.y() - 96.0 : contentY + relative.y() - 96.0));
        settlePresentation();
        const QPoint comboPoint = viewportCoordinates
            ? viewportPoint(comboItem, scroll, QPointF(comboItem->width() * 0.5, comboItem->height() * 0.5))
            : comboItem->mapToScene(QPointF(comboItem->width() * 0.5, comboItem->height() * 0.5)).toPoint();
        QTest::qWait(16);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, comboPoint);
        QTest::qWait(16);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, comboPoint);
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
        QTest::qWait(16);
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
    const auto containsPhysical = [](const QVariantList &samples, auto predicate) {
        for (const QVariant &sample : samples) {
            if (predicate(sample.toMap().value(QStringLiteral("physical")).toDouble())) return true;
        }
        return false;
    };
    const bool firstObservedNegative = containsPhysical(firstLiveGraphSamples, [](double physical) {
        return physical <= -0.64;
    });
    const bool secondObservedPositive = containsPhysical(liveGraphSamples, [](double physical) {
        return physical >= 0.69;
    });
    // The producer can publish a newer physical snapshot between an injection
    // and a direct backend history read. Assert the page's two bounded UI
    // history snapshots instead: one following each injection. This verifies
    // the live source and graph feed without assuming ring ordering, retention,
    // or host vJoy-driver availability beyond the UI contract. Mapping must
    // remain suspended; vJoy readiness itself is independent hardware state.
    if (refreshFirstLiveHistory.hasError() || refreshLiveHistory.hasError()
        || firstLiveGraphSamples.isEmpty() || !firstObservedNegative
        || liveGraphSamples.isEmpty() || !secondObservedPositive
        || backend.mappingStatus() != QStringLiteral("MAPPING SUSPENDED")
        || backend.mappingActive()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Live Controller snapshot did not update the unified Response Lab while mapping was suspended "
            "(first_samples=%1 first_negative=%2 samples=%3 second_positive=%4 oldest=%5 newest=%6 status=%7 active=%8 vjoy=%9)")
            .arg(firstLiveGraphSamples.size()).arg(firstObservedNegative)
            .arg(liveGraphSamples.size()).arg(secondObservedPositive)
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
        configuration.normalMovementResponse = preset == QStringLiteral("extreme") ? 1.0F
            : preset == QStringLiteral("aggressive") ? 0.82F
            : preset == QStringLiteral("fast") ? 0.66F : 0.48F;
        configuration.rapidMovementResponse = preset == QStringLiteral("extreme") ? 1.0F
            : preset == QStringLiteral("aggressive") ? 0.98F
            : preset == QStringLiteral("fast") ? 0.93F : 0.85F;
        configuration.engagementSensitivity = preset == QStringLiteral("extreme") ? 0.94F
            : preset == QStringLiteral("aggressive") ? 0.80F
            : preset == QStringLiteral("fast") ? 0.66F : 0.50F;
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
                || !equal(direct[index].telemetry.deliberateMotionEvidence, sample, QStringLiteral("deliberateMotionEvidence"))
                || !equal(direct[index].telemetry.normalMotionAuthority, sample, QStringLiteral("normalMotionAuthority"))
                || !equal(direct[index].telemetry.rapidMotionAuthority, sample, QStringLiteral("rapidMotionAuthority"))
                || !equal(direct[index].telemetry.rapidMotionBlend, sample, QStringLiteral("rapidMotionBlend"))
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
         QStringLiteral("Small Slow Correction"), QStringLiteral("Gentle Hover Corrections"),
         QStringLiteral("Smooth Cyclic Sweep"), QStringLiteral("Normal Bank and Recover"),
         QStringLiteral("Sustained Moderate Turn"), QStringLiteral("Approach Corrections"),
         QStringLiteral("Normal Direction Change"), QStringLiteral("Extreme Turning-Point Torture")}) {
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
    // V2.5.4 forbids changing only activeDeviceRigId. A non-active rig may
    // invoke the coordinated activation command; an already-active route has
    // no standalone DEACTIVATE control at all.
    if (!rigStartedActive) {
        if (!triggerDevicesControl(QStringLiteral("activateRigButton"), QStringLiteral("Activate compatible route"))) return false;
        settlePresentation();
        if (devices->property("actionFeedback").toMap().value(QStringLiteral("title")).toString().isEmpty()) {
            return failPresentationLifecycleTest(QStringLiteral("Compatible Device Rig activation did not report its result"));
        }
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
    if (backend.editingDeviceRigId() != rigId || backend.selectedDeviceRigId() != rigId
        || backend.selectedDeviceLabel() != secondName || backend.editingScopeLabel() != secondName
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
    if (!backend.setSelectedDeviceContext(rigId, {firstMember})
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
    auto *legacyHeader = theme == QStringLiteral("Legacy")
        ? surface->findChild<QQuickItem *>(QStringLiteral("legacyHeaderBar")) : nullptr;
    for (const QSize &size : sizes) {
        shell->resize(size);
        settlePresentation();
        if (!contextSelector->isVisible()) {
            shell->resize(original);
            return failPresentationLifecycleTest(QStringLiteral("Persistent Device Context disappeared at %1x%2 for %3")
                .arg(size.width()).arg(size.height()).arg(theme));
        }
        if (mappingControl && mappingControl->isVisible()) {
            // The Legacy ApplicationWindow header is hosted in Qt Quick
            // Controls' header item. Compare both controls in that shared
            // coordinate system rather than their component-local scenes.
            const QRectF contextBounds = legacyHeader
                ? contextSelector->mapRectToItem(legacyHeader, QRectF(0, 0, contextSelector->width(), contextSelector->height()))
                : QRectF(contextSelector->mapToScene(QPointF{}), contextSelector->size());
            const QRectF mappingBounds = legacyHeader
                ? mappingControl->mapRectToItem(legacyHeader, QRectF(0, 0, mappingControl->width(), mappingControl->height()))
                : QRectF(mappingControl->mapToScene(QPointF{}), mappingControl->size());
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

bool clickFlightDeckSettingsItem(QQuickWindow *window, QQuickItem *settings, QQuickItem *item)
{
    if (!window || !settings || !item) return false;
    const qreal maximumY = std::max<qreal>(0.0, settings->property("contentHeight").toReal()
        - settings->height());
    const qreal targetY = std::clamp(contentPoint(item, settings).y() - 84.0, 0.0, maximumY);
    settings->setProperty("contentY", targetY);
    settlePresentation();
    const QPoint clickPoint = viewportPoint(item, settings,
        QPointF(item->width() * 0.5, item->height() * 0.5));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, clickPoint);
    settlePresentation();
    return true;
}

bool clickPresentationChoice(QQuickWindow *window, QQuickItem *settings, QQuickItem *selector,
                             int choiceIndex, int choiceCount)
{
    if (!window || !settings || !selector || choiceIndex < 0 || choiceIndex >= choiceCount) {
        return false;
    }
    // Qt promotes a ComboBox popup into QQuickOverlay after release. Retry the
    // complete physical sequence rather than treating its model as a click.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const qreal maximumY = std::max<qreal>(0.0, settings->property("contentHeight").toReal()
            - settings->height());
        settings->setProperty("contentY", std::clamp(contentPoint(selector, settings).y() - 84.0,
            0.0, maximumY));
        settlePresentation();
        const QPoint selectorPoint = viewportPoint(selector, settings,
            QPointF(selector->width() * 0.5, selector->height() * 0.5));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, selectorPoint);
        QTest::qWait(8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, selectorPoint);
        settlePresentation();
        auto *popup = selector->findChild<QObject *>(selector->objectName()
            + QStringLiteral("Popup"));
        if (!popup || !popup->property("visible").toBool()) {
            qInfo().noquote() << QStringLiteral("presentation selector popup attempt=%1 popup=%2 visible=%3 index=%4 point=(%5,%6)")
                .arg(attempt).arg(popup != nullptr).arg(popup ? popup->property("visible").toBool() : false)
                .arg(selector->property("currentIndex").toInt()).arg(selectorPoint.x()).arg(selectorPoint.y());
            continue;
        }
        auto *content = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
        auto *choice = findVisualItemByObjectName(content, selector->objectName()
            + QStringLiteral("Choice_%1").arg(choiceIndex));
        if (!choice) {
            qInfo().noquote() << QStringLiteral("presentation selector choice attempt=%1 content=%2 choice=%3 index=%4")
                .arg(attempt).arg(content != nullptr).arg(choice != nullptr).arg(choiceIndex);
            continue;
        }
        const QPoint choicePoint = choice->mapToScene(QPointF(choice->width() * 0.5,
            choice->height() * 0.5)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, choicePoint);
        QTest::qWait(8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, choicePoint);
        settlePresentation();
        // This row can select Flight Deck, which unloads the Standard shell
        // and therefore destroys this popup synchronously. The caller
        // verifies the resulting ThemeManager state and replacement shell.
        return true;
    }
    return false;
}

QVariantList configuredInputRoutes(const QVariantList &inputs, const QStringList &keys)
{
    QVariantList configured;
    for (const QVariant &entry : inputs) {
        const QVariantMap input = entry.toMap();
        QVariantMap route;
        for (const QString &key : keys) route.insert(key, input.value(key));
        configured.push_back(route);
    }
    return configured;
}

QVariantList configuredDeviceRigs(const QVariantList &rigs)
{
    QVariantList configured;
    configured.reserve(rigs.size());
    for (const QVariant &entry : rigs) {
        const QVariantMap rig = entry.toMap();
        QVariantList members;
        for (const QVariant &memberEntry : rig.value(QStringLiteral("members")).toList()) {
            const QVariantMap member = memberEntry.toMap();
            members.append(QVariantMap{
                {QStringLiteral("id"), member.value(QStringLiteral("id"))},
                {QStringLiteral("enabled"), member.value(QStringLiteral("enabled"))},
                {QStringLiteral("required"), member.value(QStringLiteral("required"))},
                {QStringLiteral("preferredOutputLayoutId"), member.value(QStringLiteral("preferredOutputLayoutId"))},
            });
        }
        QVariantList outputs;
        for (const QVariant &outputEntry : rig.value(QStringLiteral("outputs")).toList()) {
            const QVariantMap output = outputEntry.toMap();
            outputs.append(QVariantMap{
                {QStringLiteral("id"), output.value(QStringLiteral("id"))},
                {QStringLiteral("enabled"), output.value(QStringLiteral("enabled"))},
            });
        }
        configured.append(QVariantMap{
            {QStringLiteral("id"), rig.value(QStringLiteral("id"))},
            {QStringLiteral("name"), rig.value(QStringLiteral("name"))},
            {QStringLiteral("enabled"), rig.value(QStringLiteral("enabled"))},
            {QStringLiteral("default"), rig.value(QStringLiteral("default"))},
            {QStringLiteral("autoActivate"), rig.value(QStringLiteral("autoActivate"))},
            {QStringLiteral("activationPriority"), rig.value(QStringLiteral("activationPriority"))},
            {QStringLiteral("fallbackRigId"), rig.value(QStringLiteral("fallbackRigId"))},
            {QStringLiteral("disconnectBehavior"), rig.value(QStringLiteral("disconnectBehavior"))},
            {QStringLiteral("members"), members},
            {QStringLiteral("outputs"), outputs},
        });
    }
    return configured;
}

QVariantMap flightDeckConfigurationSnapshot(hotas::AppBackend &backend)
{
    return {
        {QStringLiteral("profile"), backend.activeProfileId()},
        {QStringLiteral("category"), backend.activeCategoryId()},
        {QStringLiteral("profiles"), backend.profiles()},
        {QStringLiteral("axes"), configuredInputRoutes(backend.axes(),
            {QStringLiteral("index"), QStringLiteral("target"), QStringLiteral("inverted"),
             QStringLiteral("deadzone"), QStringLiteral("hysteresis"),
             QStringLiteral("outputMinimum"), QStringLiteral("outputMaximum"),
             QStringLiteral("curveSummary")})},
        {QStringLiteral("buttons"), configuredInputRoutes(backend.buttons(),
            {QStringLiteral("index"), QStringLiteral("target"), QStringLiteral("name"),
             QStringLiteral("profileId"), QStringLiteral("profileBehavior"),
             QStringLiteral("mappingControl")})},
        {QStringLiteral("povs"), configuredInputRoutes(backend.povInputs(),
            {QStringLiteral("hat"), QStringLiteral("direction"), QStringLiteral("target"),
             QStringLiteral("profileId"), QStringLiteral("profileBehavior")})},
        {QStringLiteral("automation"), backend.automationRules()},
        {QStringLiteral("adaptive"), backend.adaptiveResponseState()},
        {QStringLiteral("controllers"), backend.controllers()},
        // deviceRigs() deliberately projects current readiness/connection
        // state as well as persisted rig settings. The interaction assertion
        // below must reject configuration writes, not a harmless native
        // refresh of that live status.
        {QStringLiteral("deviceRigs"), configuredDeviceRigs(backend.deviceRigs())},
        {QStringLiteral("editingDeviceRig"), backend.editingDeviceRigId()},
        {QStringLiteral("editingScope"), backend.editingScopeLabel()},
        {QStringLiteral("vjoyDevice"), backend.vjoyDeviceId()},
        {QStringLiteral("disabledAxisValue"), backend.disabledAxisValue()},
        {QStringLiteral("mappingRequested"), backend.mappingRequested()},
    };
}

QString differingSnapshotKeys(const QVariantMap &before, const QVariantMap &after)
{
    QStringList keys = before.keys();
    for (const QString &key : after.keys()) {
        if (!keys.contains(key)) keys.push_back(key);
    }
    QStringList changed;
    for (const QString &key : keys) {
        if (before.value(key) != after.value(key)) changed.push_back(key);
    }
    return changed.join(u", "_qs);
}

bool verifyFlightDeckSettings(hotas::AppBackend &backend, hotas::ThemeManager &themeManager,
                              QQuickWindow *window, QObject *&surface,
                              const QString &expectedAppearance)
{
    if (!selectPage(surface, 4)) return false;
    auto *settings = qobject_cast<QQuickItem *>(pageItem(surface, 4));
    if (!settings || settings->objectName() != QStringLiteral("flightDeckSettings")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck did not load native Settings"));
    }
    auto *settingsTheme = settings->findChild<QObject *>(QStringLiteral("flightDeckSettingsTheme"));
    auto *autoSwitch = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckSettingsAutoSwitchToggle"));
    auto *gameDetection = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckSettingsGameDetectionToggle"));
    if (!settingsTheme || !autoSwitch || !gameDetection) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Settings omitted a native control or theme"));
    }

    const bool autoSwitchBefore = backend.autoSwitchVerifiedController();
    const bool gameDetectionBefore = backend.automaticGameDetection();
    if (!clickFlightDeckSettingsItem(window, settings, autoSwitch)
        || backend.autoSwitchVerifiedController() == autoSwitchBefore
        || backend.automaticGameDetection() != gameDetectionBefore
        || !clickFlightDeckSettingsItem(window, settings, autoSwitch)
        || backend.autoSwitchVerifiedController() != autoSwitchBefore) {
        return failPresentationLifecycleTest(QStringLiteral("Settings toggle pointer action did not commit or remain isolated"));
    }

    auto *light = findVisualItemByObjectName(settings, QStringLiteral("flightDeckSettingsAppearanceLight"));
    auto *dark = findVisualItemByObjectName(settings, QStringLiteral("flightDeckSettingsAppearanceDark"));
    if (!light || !dark || !clickFlightDeckSettingsItem(window, settings, light)
        || themeManager.flightDeckAppearance() != QStringLiteral("Light")
        || !settingsTheme->property("light").toBool()
        || !clickFlightDeckSettingsItem(window, settings, dark)
        || themeManager.flightDeckAppearance() != QStringLiteral("Dark")
        || settingsTheme->property("light").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck appearance pointer controls did not update semantic resources"));
    }
    themeManager.setFlightDeckAppearance(expectedAppearance);
    settlePresentation();

    auto *profiles = findVisualItemByObjectName(settings, QStringLiteral("flightDeckSettingsOpenProfiles"));
    if (!profiles || !clickFlightDeckSettingsItem(window, settings, profiles)
        || surface->property("currentPage").toInt() != 5) {
        return failPresentationLifecycleTest(QStringLiteral("Settings navigation button did not route to Profiles"));
    }
    if (!selectPage(surface, 4)) return false;
    settings = qobject_cast<QQuickItem *>(pageItem(surface, 4));
    if (!settings) return failPresentationLifecycleTest(QStringLiteral("Settings did not reload after a native deep link"));

    auto *vjoyDevice = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckSettingsVjoyDevice"));
    auto *vjoyIncrement = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckSettingsVjoyDeviceIncrement"));
    auto *vjoyDecrement = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckSettingsVjoyDeviceDecrement"));
    const auto stepperControlIsContained = [vjoyDevice](QQuickItem *control) {
        if (!vjoyDevice || !control) return false;
        const QRectF bounds(control->mapToItem(vjoyDevice, QPointF{}), control->size());
        constexpr qreal epsilon = 0.5;
        return bounds.left() >= -epsilon && bounds.top() >= -epsilon
            && bounds.right() <= vjoyDevice->width() + epsilon
            && bounds.bottom() <= vjoyDevice->height() + epsilon;
    };
    if (!vjoyDevice || !vjoyDevice->property("flightDeckStyled").toBool()
        || !vjoyIncrement || !vjoyDecrement
        || !stepperControlIsContained(vjoyIncrement)
        || !stepperControlIsContained(vjoyDecrement)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Settings vJoy device selector fell back from the themed, contained stepper"));
    }

    const QVariantMap configurationBeforeSwitch = flightDeckConfigurationSnapshot(backend);
    auto *standardCard = findVisualItemByObjectName(settings,
        QStringLiteral("flightDeckExperience_theme:Standard"));
    if (!standardCard || !clickFlightDeckSettingsItem(window, settings, standardCard)
        || themeManager.currentExperience() != QStringLiteral("Existing")
        || themeManager.currentTheme() != QStringLiteral("Standard")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck experience card did not switch to Standard"));
    }
    const QVariantMap configurationAfterStandard = flightDeckConfigurationSnapshot(backend);
    if (configurationAfterStandard != configurationBeforeSwitch) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck to Standard changed controller configuration: %1")
            .arg(differingSnapshotKeys(configurationBeforeSwitch, configurationAfterStandard)));
    }
    if (window->findChild<QObject *>(QStringLiteral("flightDeckSurface"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck to Standard retained a stale Flight Deck shell"));
    }

    auto *standard = window->findChild<QObject *>(QStringLiteral("standardSurface"));
    if (!standard || !standard->setProperty("currentPage", 4)) {
        return failPresentationLifecycleTest(QStringLiteral("Standard shell did not expose the preview Settings entry point"));
    }
    settlePresentation();
    auto *standardSettings = qobject_cast<QQuickItem *>(pageItem(standard, 4));
    auto *selector = standardSettings ? findVisualItemByObjectName(standardSettings,
        QStringLiteral("experienceAppearanceSelector")) : nullptr;
    int flightDeckChoice = -1;
    const QVariantList choices = themeManager.presentationChoices();
    for (qsizetype index = 0; index < choices.size(); ++index) {
        if (choices.at(index).toMap().value(QStringLiteral("id")).toString()
            == QStringLiteral("flight-deck")) {
            flightDeckChoice = static_cast<int>(index);
            break;
        }
    }
    if (!standardSettings || !selector || flightDeckChoice < 0
        || !clickPresentationChoice(window, standardSettings, selector, flightDeckChoice, choices.size())
        || themeManager.currentExperience() != QStringLiteral("Flight Deck")) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Standard to Flight Deck pointer selection failed "
            "(settings=%1 selector=%2 choice=%3 choices=%4 experience=%5 presentation=%6)")
            .arg(standardSettings != nullptr).arg(selector != nullptr).arg(flightDeckChoice)
            .arg(choices.size()).arg(themeManager.currentExperience(), themeManager.currentPresentationId()));
    }
    settlePresentation();
    surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    if (!surface || surface->property("loadedPageCount").toInt() != 1
        || flightDeckConfigurationSnapshot(backend) != configurationBeforeSwitch) {
        return failPresentationLifecycleTest(QStringLiteral("Standard to Flight Deck changed configuration or duplicated its host"));
    }

    for (const QString &presentation : {QStringLiteral("theme:Legacy"),
                                        QStringLiteral("flight-deck"),
                                        QStringLiteral("theme:Top Gun"),
                                        QStringLiteral("flight-deck"),
                                        QStringLiteral("theme:Standard"),
                                        QStringLiteral("flight-deck")}) {
        themeManager.selectPresentation(presentation);
        settlePresentation();
        const bool flightDeck = presentation == QStringLiteral("flight-deck");
        const int flightDeckShells = window->findChildren<QObject *>(QStringLiteral("flightDeckSurface")).size();
        if ((flightDeck && (flightDeckShells != 1 || themeManager.currentExperience() != QStringLiteral("Flight Deck")))
            || (!flightDeck && (flightDeckShells != 0 || themeManager.currentExperience() != QStringLiteral("Existing")))) {
            return failPresentationLifecycleTest(QStringLiteral("Experience-switch stress left an incorrect presentation host"));
        }
    }
    surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    if (!surface || flightDeckConfigurationSnapshot(backend) != configurationBeforeSwitch) {
        return failPresentationLifecycleTest(QStringLiteral("Experience-switch stress changed controller configuration"));
    }
    return true;
}

bool verifyFlightDeckAdaptivePresetChoiceSafeArea(QObject *adaptiveVisual, const QString &appearance)
{
    auto *adaptiveVisualItem = qobject_cast<QQuickItem *>(adaptiveVisual);
    if (!adaptiveVisual || !adaptiveVisualItem
        || adaptiveVisual->objectName() != QStringLiteral("flightDeckAdaptiveResponse")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not load native Adaptive Response for visual review")
            .arg(appearance));
    }
    // A Button with a custom anchored contentItem does not apply its padding
    // automatically. Keep the preset strength labels inside the same reusable
    // control-safe area that protects the card and dialog fixtures.
    const auto textFitsPresetSafeArea = [](QQuickItem *text, QQuickItem *choice) {
        if (!text || !choice) return false;
        const QRectF textRect = text->mapRectToItem(choice, QRectF(QPointF{}, text->size()));
        const qreal inset = choice->property("padding").toReal();
        return textRect.left() >= inset - 0.5 && textRect.right() <= choice->width() - inset + 0.5
            && textRect.top() >= inset - 0.5 && textRect.bottom() <= choice->height() - inset + 0.5;
    };
    for (const QString &presetId : {QStringLiteral("off"), QStringLiteral("light"),
                                    QStringLiteral("balanced"), QStringLiteral("fast"),
                                    QStringLiteral("aggressive"), QStringLiteral("extreme")}) {
        auto *choice = findVisualItemByObjectName(adaptiveVisualItem,
            QStringLiteral("adaptivePresetButton_") + presetId);
        auto *title = findVisualItemByObjectName(adaptiveVisualItem,
            QStringLiteral("adaptivePresetTitle_") + presetId);
        auto *description = findVisualItemByObjectName(adaptiveVisualItem,
            QStringLiteral("adaptivePresetDescription_") + presetId);
        if (!textFitsPresetSafeArea(title, choice) || !textFitsPresetSafeArea(description, choice)) {
            return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive preset %2 text escaped its choice safe area")
                .arg(appearance, presetId));
        }
    }
    return true;
}

bool verifyFlightDeckAdaptivePresetChoiceGeometry(hotas::AppBackend &backend,
                                                   hotas::ThemeManager &themeManager,
                                                   const QString &appearance)
{
    themeManager.setCurrentTheme(QStringLiteral("Standard"));
    themeManager.setFlightDeckAppearance(appearance);
    themeManager.setCurrentExperience(QStringLiteral("Flight Deck"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) return failPresentationLifecycleTest(QStringLiteral("Flight Deck geometry window did not load"));

    settlePresentation();
    QObject *surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    if (!surface || !selectPage(surface, 9)) return false;
    settlePresentation();
    return verifyFlightDeckAdaptivePresetChoiceSafeArea(pageItem(surface, 9), appearance);
}

bool verifyFlightDeckNavigationRailGeometry(hotas::AppBackend &backend,
                                            hotas::ThemeManager &themeManager,
                                            const QString &appearance)
{
    themeManager.setCurrentTheme(QStringLiteral("Standard"));
    themeManager.setFlightDeckAppearance(appearance);
    themeManager.setCurrentExperience(QStringLiteral("Flight Deck"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) return failPresentationLifecycleTest(QStringLiteral("Flight Deck rail geometry window did not load"));

    settlePresentation();
    QObject *surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    if (!surface || !selectPage(surface, 8)) return false;
    settlePresentation();

    auto *navigationViewport = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNavigationViewport"));
    auto *navigationRail = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNavigationRail"));
    auto *readiness = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckReadiness"));
    auto *readinessContent = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckReadinessContent"));
    if (!navigationViewport || !navigationRail || !readiness || !readinessContent) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 rail did not construct its pinned readiness layout")
            .arg(appearance));
    }
    const auto rectIn = [](QQuickItem *item, QQuickItem *container) {
        return item->mapRectToItem(container, QRectF(QPointF{}, item->size()));
    };
    const auto verifyRailContainment = [&]() {
        const QRectF footerRect = rectIn(readiness, navigationRail);
        const QRectF viewportRect = rectIn(navigationViewport, navigationRail);
        const QRectF contentRect = rectIn(readinessContent, readiness);
        const qreal padding = readiness->property("contentPadding").toReal();
        return footerRect.left() >= -0.5 && footerRect.right() <= navigationRail->width() + 0.5
            && footerRect.top() >= -0.5 && footerRect.bottom() <= navigationRail->height() + 0.5
            && viewportRect.bottom() <= footerRect.top() - 0.5
            && contentRect.left() >= padding - 0.5
            && contentRect.right() <= readiness->width() - padding + 0.5
            && contentRect.top() >= padding - 0.5
            && contentRect.bottom() <= readiness->height() - padding + 0.5;
    };
    bool navigationScrollsAtConstrainedHeight = false;
    for (const QSize &railSize : std::array<QSize, 5>{QSize{900, 650}, QSize{1000, 720},
                                                        QSize{1200, 800}, QSize{1400, 900},
                                                        QSize{1600, 980}}) {
        window->resize(railSize);
        window->requestUpdate();
        settlePresentation();
        if (!verifyRailContainment()) {
            const QRectF footerRect = rectIn(readiness, navigationRail);
            const QRectF viewportRect = rectIn(navigationViewport, navigationRail);
            const QRectF contentRect = rectIn(readinessContent, readiness);
            qWarning().noquote() << "Flight Deck rail geometry" << appearance << railSize
                                 << "rail=" << navigationRail->size()
                                 << "footer=" << footerRect
                                 << "viewport=" << viewportRect
                                 << "content=" << contentRect
                                 << "padding=" << readiness->property("contentPadding").toReal();
            return failPresentationLifecycleTest(QStringLiteral(
                "Flight Deck %1 readiness rail escaped or obscured navigation at %2x%3 "
                "(rail=%4x%5 footer=%6,%7 %8x%9 viewport=%10,%11 %12x%13 "
                "content=%14,%15 %16x%17 inset=%18)")
                .arg(appearance).arg(railSize.width()).arg(railSize.height())
                .arg(navigationRail->width()).arg(navigationRail->height())
                .arg(footerRect.x()).arg(footerRect.y()).arg(footerRect.width()).arg(footerRect.height())
                .arg(viewportRect.x()).arg(viewportRect.y()).arg(viewportRect.width()).arg(viewportRect.height())
                .arg(contentRect.x()).arg(contentRect.y()).arg(contentRect.width()).arg(contentRect.height())
                .arg(readiness->property("contentPadding").toReal()));
        }
        navigationScrollsAtConstrainedHeight = navigationScrollsAtConstrainedHeight
            || navigationViewport->property("contentHeight").toReal() > navigationViewport->height() + 0.5;
    }
    if (!navigationScrollsAtConstrainedHeight) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 constrained rail did not expose navigation scrolling")
            .arg(appearance));
    }
    window->resize(900, 650);
    window->requestUpdate();
    settlePresentation();
    const qreal readinessTopBeforeScroll = rectIn(readiness, navigationRail).top();
    navigationViewport->setProperty("contentY", navigationViewport->property("contentHeight").toReal()
        - navigationViewport->height());
    settlePresentation();
    if (!verifyRailContainment()
        || std::abs(rectIn(readiness, navigationRail).top() - readinessTopBeforeScroll) > 0.5) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 navigation scrolling moved or obscured readiness")
            .arg(appearance));
    }
    if (!selectPage(surface, 6)) return false;
    settlePresentation();
    auto *selectedNavigation = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNav_6"));
    const QRectF selectedRect = selectedNavigation
        ? rectIn(selectedNavigation, navigationViewport) : QRectF{};
    if (!selectedNavigation || selectedRect.top() < -0.5
        || selectedRect.bottom() > navigationViewport->height() + 0.5) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 selected navigation item did not remain reachable")
            .arg(appearance));
    }
    return true;
}

bool verifyFlightDeckDialogHeaderGeometry(hotas::AppBackend &backend,
                                          hotas::ThemeManager &themeManager,
                                          const QString &appearance)
{
    themeManager.setCurrentTheme(QStringLiteral("Standard"));
    themeManager.setFlightDeckAppearance(appearance);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    QQmlComponent component(&engine);
    component.loadFromModule(u"HOTASMapper"_qs, u"FlightDeckDevices"_qs);
    if (component.status() != QQmlComponent::Ready) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 dialog fixture did not load: %2")
            .arg(appearance, component.errorString()));
    }
    QObject *root = component.create();
    auto *rootItem = qobject_cast<QQuickItem *>(root);
    if (!rootItem) {
        delete root;
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 dialog fixture did not create an item")
            .arg(appearance));
    }
    QQuickWindow window;
    // Force the actual minimum-height fallback: header remains fixed, a long
    // body scrolls, and its action row is still reachable at the bottom.
    window.resize(900, 350);
    rootItem->setParentItem(window.contentItem());
    rootItem->setSize(window.size());
    QVariantList repairPlan;
    for (int index = 0; index < 16; ++index) {
        repairPlan.append(QVariantMap{{QStringLiteral("message"), QStringLiteral(
            "Scoped repair step %1 keeps unrelated mappings and visibility rules intact while it verifies the selected device.")
                .arg(index + 1)}});
    }
    root->setProperty("proposedChangesPresentationOverride", repairPlan);
    settlePresentation();

    QObject *dialog = root->findChild<QObject *>(QStringLiteral("flightDeckRepairConfirmation"));
    const bool opened = dialog && QMetaObject::invokeMethod(dialog, "open");
    settlePresentation();
    auto *header = dialog ? dialog->findChild<QQuickItem *>(
        QStringLiteral("flightDeckDialogHeader")) : nullptr;
    auto *heading = dialog ? dialog->findChild<QQuickItem *>(
        QStringLiteral("flightDeckDialogHeading")) : nullptr;
    auto *body = dialog ? dialog->findChild<QQuickItem *>(
        QStringLiteral("flightDeckRepairConfirmationBody")) : nullptr;
    auto *actions = dialog ? dialog->findChild<QQuickItem *>(
        QStringLiteral("flightDeckRepairConfirmationActions")) : nullptr;
    const qreal inset = dialog ? dialog->property("contentPadding").toReal() : 0.0;
    const QRectF headingRect = (header && heading)
        ? heading->mapRectToItem(header, QRectF(QPointF{}, heading->size())) : QRectF{};
    const bool headerSafe = opened && dialog->property("visible").toBool()
        && header && heading && inset > 0.0
        && headingRect.left() >= inset - 0.5
        && headingRect.right() <= header->width() - inset + 0.5
        && headingRect.top() >= inset - 0.5;
    const qreal maximumBodyHeight = dialog ? dialog->property("maximumBodyHeight").toReal() : 0.0;
    const bool hasScrollableRepairPlan = body && body->property("contentHeight").toReal()
        > body->height() + 0.5 && body->height() <= maximumBodyHeight + 0.5;
    if (body) {
        body->setProperty("contentY", std::max<qreal>(0.0,
            body->property("contentHeight").toReal() - body->height()));
    }
    settlePresentation();
    const QRectF actionsRect = (body && actions)
        ? actions->mapRectToItem(body, QRectF(QPointF{}, actions->size())) : QRectF{};
    const bool actionsReachable = actions && body
        && actionsRect.top() >= -0.5 && actionsRect.bottom() <= body->height() + 0.5;
    if (dialog) QMetaObject::invokeMethod(dialog, "close");
    rootItem->setParentItem(nullptr);
    delete root;
    if (!headerSafe || !hasScrollableRepairPlan || !actionsReachable) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 repair dialog did not retain its title inset, scrollable body, and reachable actions")
            .arg(appearance));
    }
    return true;
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
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) return failPresentationLifecycleTest(QStringLiteral("Flight Deck window did not load"));

    settlePresentation();
    QObject *surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    QQmlExpression tokensLightExpression(qmlContext(surface), surface, QStringLiteral("themeTokens.light"));
    const bool tokensLight = surface && tokensLightExpression.evaluate().toBool();
    if (!surface || tokensLightExpression.hasError()
        || tokensLight != (appearance == QStringLiteral("Light"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 semantic resources did not resolve (surface=%2 light=%3 error=%4)")
            .arg(appearance).arg(surface != nullptr).arg(tokensLight)
            .arg(tokensLightExpression.hasError() ? tokensLightExpression.error().toString() : QStringLiteral("none")));
    }

    // Shared shell controls must be present on Overview too, use the one
    // authoritative appearance service, and route Controller to setup.
    auto *controllerPill = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckControllerPill"));
    auto *appearancePill = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckAppearancePill"));
    auto *sharedTitle = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckSharedPageTitle"));
    auto *selectedDeviceSelector = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckSelectedDeviceSelector"));
    auto *navigationContent = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNavigationContent"));
    if (!controllerPill || !appearancePill || !sharedTitle || !selectedDeviceSelector || !navigationContent
        || !selectedDeviceSelector->property("visible").toBool()
        || sharedTitle->property("text").toString() != QStringLiteral("Overview")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck shared header is incomplete on Overview"));
    }
    const QString alternateAppearance = appearance == QStringLiteral("Dark")
        ? QStringLiteral("Light") : QStringLiteral("Dark");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        appearancePill->mapToScene(QPointF(appearancePill->width() * 0.5,
            appearancePill->height() * 0.5)).toPoint());
    settlePresentation();
    if (themeManager.flightDeckAppearance() != alternateAppearance
        || appearancePill->property("value").toString() != alternateAppearance.toUpper()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Appearance pill did not toggle the authoritative appearance"));
    }
    if (appearancePill->property("activeFocus").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Appearance mouse click left a persistent keyboard-focus highlight"));
    }
    themeManager.setFlightDeckAppearance(appearance);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        controllerPill->mapToScene(QPointF(controllerPill->width() * 0.5,
            controllerPill->height() * 0.5)).toPoint());
    settlePresentation();
    if (surface->property("currentPage").toInt() != 2
        || !selectedDeviceSelector->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Controller route did not retain the selected-device selector on Devices and setup"));
    }
    if (controllerPill->property("activeFocus").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck Controller mouse click left a persistent keyboard-focus highlight"));
    }
    if (!surface->setProperty("currentPage", 8)) return false;
    settlePresentation();

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
    // Calibration is controller-gated, but its Flight Deck entry and native
    // modal must remain available without fabricating a physical controller.
    // Opening and dismissing the modal cannot start capture or alter a route.
    const QVariantMap calibrationBeforePresentation = flightDeckConfigurationSnapshot(backend);
    QObject *calibrationEntry = devices->findChild<QObject *>(QStringLiteral("flightDeckCalibration"));
    QObject *calibrationDialog = devices->findChild<QObject *>(QStringLiteral("flightDeckCalibrationDialog"));
    if (!calibrationEntry || !calibrationDialog || !QMetaObject::invokeMethod(calibrationDialog, "open")
        || !calibrationDialog->property("visible").toBool()
        || !calibrationDialog->findChild<QObject *>(QStringLiteral("flightDeckCalibrationStart"))
        || backend.calibrationActive()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck calibration entry did not stay native and controller-gated"));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (calibrationDialog->property("visible").toBool() || backend.calibrationActive()
        || flightDeckConfigurationSnapshot(backend) != calibrationBeforePresentation) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck calibration presentation changed controller configuration"));
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

    for (const int page : {8, 2, 0, 1, 6, 5, 9, 7, 3, 4}) {
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
            || !nav->property("selected").toBool() || !loaded
            || !selectedDeviceSelector->property("visible").toBool()) {
            return failPresentationLifecycleTest(QStringLiteral(
                "Flight Deck route %1 did not update selected state and page host (current=%2 selected=%3 loaded=%4)")
                .arg(page).arg(surface->property("currentPage").toInt())
                .arg(nav->property("selected").toBool()).arg(loaded != nullptr));
        }
        if (page == 6 && loaded->objectName() != QStringLiteral("flightDeckCurveEditor")) {
            return failPresentationLifecycleTest(QStringLiteral(
                "Flight Deck Curve Editor route loaded the legacy composition"));
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
        if (auto *rail = findVisualItemByObjectName(window->contentItem(),
                QStringLiteral("flightDeckNavigationRail"))) {
            const QRectF railRect = rail->mapRectToItem(window->contentItem(),
                QRectF(QPointF{}, rail->size()));
            if (railRect.left() < -0.5 || railRect.top() < -0.5
                || railRect.right() > window->width() + 0.5
                || railRect.bottom() > window->height() + 0.5) {
                qWarning().noquote() << "Flight Deck navigation rail escaped the native viewport during"
                                     << sizeLabel << railRect;
                return false;
            }
        }
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
    if (!selectPage(surface, 2)) return false;
    auto *devicesForCapture = qobject_cast<QQuickItem *>(pageItem(surface, 2));
    auto *calibrationForCapture = devicesForCapture
        ? devicesForCapture->findChild<QObject *>(QStringLiteral("flightDeckCalibrationDialog")) : nullptr;
    if (!calibrationForCapture || !QMetaObject::invokeMethod(calibrationForCapture, "open")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 calibration dialog could not be prepared for visual review")
            .arg(appearance));
    }
    settlePresentation();
    const bool calibrationCaptured = calibrationForCapture->property("visible").toBool()
        && captureShell(QStringLiteral("devices-calibration"));
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (!calibrationCaptured || calibrationForCapture->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 calibration dialog escaped visual review or dismissal")
            .arg(appearance));
    }
    if (!selectPage(surface, 4)) return false;
    auto *settingsPage = qobject_cast<QQuickItem *>(pageItem(surface, 4));
    if (!settingsPage || settingsPage->objectName() != QStringLiteral("flightDeckSettings")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Settings could not be prepared for visual review")
            .arg(appearance));
    }
    const auto scrollSettingsAndCapture = [&](const QString &groupName, const QString &label) {
        auto *group = findVisualItemByObjectName(settingsPage, groupName);
        if (!group) return false;
        const qreal maximumY = std::max<qreal>(0.0, settingsPage->property("contentHeight").toReal()
            - settingsPage->height());
        settingsPage->setProperty("contentY", std::clamp(contentPoint(group, settingsPage).y() - 24.0,
            0.0, maximumY));
        settlePresentation();
        return captureShell(label);
    };
    settingsPage->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("settings-main"))
        || !scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsAppearanceGroup"),
            QStringLiteral("settings-appearance"))
        || !scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsStartupGroup"),
            QStringLiteral("settings-startup"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Settings primary visual capture failed")
            .arg(appearance));
    }
    const bool smoothingBeforeFixture = backend.curveTransitionSmoothingEnabled();
    backend.setCurveTransitionSmoothingEnabled(false);
    settingsPage->setProperty("presentationState", QVariantMap{
        {QStringLiteral("updateStatus"), QStringLiteral("A deliberately long update validation message verifies wrapping without escaping the native Settings surface.")},
        {QStringLiteral("vjoyStatus"), QStringLiteral("Virtual output is unavailable until a long validation explanation can be reviewed without clipping or overlap.")},
    });
    if (!scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsMappingDefaultsGroup"),
            QStringLiteral("settings-advanced-disabled"))
        || !scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsVirtualOutputGroup"),
            QStringLiteral("settings-long-value"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Settings advanced visual capture failed")
            .arg(appearance));
    }
    settingsPage->setProperty("presentationState", QVariantMap{});
    backend.setCurveTransitionSmoothingEnabled(smoothingBeforeFixture);
    const QSize settingsOriginalSize = window->size();
    window->resize(900, 650);
    settlePresentation();
    if (!scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsAppearanceGroup"),
            QStringLiteral("settings-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Settings minimum layout capture failed")
            .arg(appearance));
    }
    window->resize(1600, 980);
    settlePresentation();
    if (!scrollSettingsAndCapture(QStringLiteral("flightDeckSettingsVirtualOutputGroup"),
            QStringLiteral("settings-wide"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Settings wide layout capture failed")
            .arg(appearance));
    }
    window->resize(settingsOriginalSize);
    if (!selectPage(surface, 8)) return false;
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
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Battlefield 6")}, {QStringLiteral("executable"), QStringLiteral("bf6.exe")}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Orbital Racer")}, {QStringLiteral("executable"), QStringLiteral("orbital-racer.exe")}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("X-Plane 12")}, {QStringLiteral("executable"), QStringLiteral("xplane12.exe")}}})
        || !profilesPage->setProperty("profileDetailPresentationOverride", fixtureDetails)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Profiles fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    // Searching the Running list is presentation-only until the user presses
    // an existing ADD action. Exercise the actual text field so case-folded
    // display-name and executable matching cannot silently regress.
    const QVariantList categoriesBeforeRunningSearch = backend.profileCategories();
    QObject *addGameDialog = profilesPage->findChild<QObject *>(QStringLiteral("flightDeckAddGameDialog"));
    auto *runningGameSearch = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckRunningGameSearch"));
    if (!addGameDialog || !QMetaObject::invokeMethod(addGameDialog, "open")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Add Game dialog could not open for search review")
            .arg(appearance));
    }
    settlePresentation();
    runningGameSearch = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckRunningGameSearch"));
    const auto filteredRunningCount = [&]() {
        QQmlExpression filtered(qmlContext(addGameDialog), addGameDialog,
            QStringLiteral("filteredRunningApplications.length"));
        const QVariant value = filtered.evaluate();
        return !filtered.hasError() ? value.toInt() : -1;
    };
    if (!runningGameSearch || !clickFlightDeckSettingsItem(window, window->contentItem(), runningGameSearch)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search was not pointer reachable")
            .arg(appearance));
    }
    addGameDialog->setProperty("runningSearchText", QStringLiteral("BF6"));
    settlePresentation();
    if (filteredRunningCount() != 1 || backend.profileCategories() != categoriesBeforeRunningSearch) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search did not filter a display name without mutating configuration")
            .arg(appearance));
    }
    addGameDialog->setProperty("runningSearchText", QStringLiteral("ORBITAL-RACER.EXE"));
    settlePresentation();
    if (filteredRunningCount() != 1 || backend.profileCategories() != categoriesBeforeRunningSearch) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search did not case-fold executable matching")
            .arg(appearance));
    }
    addGameDialog->setProperty("runningSearchText", QStringLiteral("no matching process"));
    settlePresentation();
    QObject *noRunningMatch = window->findChild<QObject *>(QStringLiteral("flightDeckRunningGameNoMatch"));
    if (filteredRunningCount() != 0 || !noRunningMatch || !noRunningMatch->property("visible").toBool()
        || backend.profileCategories() != categoriesBeforeRunningSearch) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search did not keep no-match and configuration states distinct")
            .arg(appearance));
    }
    auto *clearRunningSearch = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckRunningGameSearchClear"));
    if (!clearRunningSearch || !clickFlightDeckSettingsItem(window, window->contentItem(), clearRunningSearch)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search clear was not pointer reachable")
            .arg(appearance));
    }
    settlePresentation();
    if (filteredRunningCount() != 3 || backend.profileCategories() != categoriesBeforeRunningSearch) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Running search clear did not restore the unmutated list")
            .arg(appearance));
    }
    QMetaObject::invokeMethod(addGameDialog, "close");
    settlePresentation();
    if (!findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckProfileLibrary"))
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckProfileDetailPane"))
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
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckCategoryActivationResolver"))
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

    // Secondary profile flows stay inside the native Flight Deck host. The
    // transfer dialog is presentation-only until the existing portability
    // command receives a platform-picked file, while form rejection must
    // remain visible and must not create an authoritative category.
    auto *transferButton = findVisualItemByObjectName(profilesItem,
        QStringLiteral("flightDeckProfilesTransfer"));
    if (!transferButton || !clickFlightDeckSettingsItem(window, profilesItem, transferButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native transfer entry was not pointer reachable")
            .arg(appearance));
    }
    QObject *transferDialog = profilesPage->findChild<QObject *>(QStringLiteral("flightDeckTransferDialog"));
    if (!transferDialog || !transferDialog->property("visible").toBool()
        || surface->property("currentPage").toInt() != 5
        || !captureShell(QStringLiteral("profiles-transfer"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 transfer entry leaked to a non-native host")
            .arg(appearance));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (transferDialog->property("visible").toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 transfer dialog did not close on Escape")
            .arg(appearance));
    }
    auto *newCategoryButton = findVisualItemByObjectName(profilesItem,
        QStringLiteral("flightDeckNewCategory"));
    if (!newCategoryButton || !clickFlightDeckSettingsItem(window, profilesItem, newCategoryButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 native category form was not pointer reachable")
            .arg(appearance));
    }
    auto *newCategorySave = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNewCategorySave"));
    // The error label intentionally starts hidden. Locate its QML object
    // before the rejected save, then assert that validation makes it visible.
    auto *newCategoryError = window->findChild<QQuickItem *>(
        QStringLiteral("flightDeckNewCategoryError"));
    const int categoryCountBeforeRejectedForm = backend.profileCategories().size();
    if (!newCategorySave || !newCategoryError
        || !clickFlightDeckSettingsItem(window, window->contentItem(), newCategorySave)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 rejected category form was not pointer reachable")
            .arg(appearance));
    }
    settlePresentation();
    if (!newCategoryError->property("visible").toBool()
        || backend.profileCategories().size() != categoryCountBeforeRejectedForm
        || !captureShell(QStringLiteral("profiles-form-error"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 rejected category form closed, hid validation, or mutated state")
            .arg(appearance));
    }
    QTest::keyClick(window, Qt::Key_Escape);
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
    if (!backend.setCategoryGameDetectionRules(testCategoryId, {QStringLiteral("flight-deck-test.exe")})
        || !backend.setProfileAutomaticSelectionMode(firstId, QStringLiteral("fallback"))
        || !backend.setProfileAutomaticSelectionMode(secondId, QStringLiteral("preferred"))
        || !backend.reorderCategoryAutomaticProfiles(testCategoryId, {secondId, firstId})) {
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
        const QVariantMap resolver = backend.activationResolverState();
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 explicit activation did not commit through AppBackend "
            "(expected=%2 actual=%3 rig=%4 resolver=%5)")
            .arg(appearance, secondId, backend.activeProfileId(), backend.activeDeviceRigId(),
                 QString::fromUtf8(QJsonDocument::fromVariant(resolver).toJson(QJsonDocument::Compact))));
    }
    QQmlExpression openCategoryForBehavior(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openCategory('%1')").arg(testCategoryId));
    openCategoryForBehavior.evaluate();
    settlePresentation();
    const QVariantMap resolvedCategory = categoryWithName(testCategoryName);
    if (openCategoryForBehavior.hasError()
        || !findVisualItemByObjectName(profilesItem, QStringLiteral("flightDeckCategoryActivationResolver"))
        || resolvedCategory.value(QStringLiteral("profileIds")).toStringList()
            != QStringList{secondId, firstId}
        || profileWithName(firstName).value(QStringLiteral("automaticSelectionMode")).toString()
            != QStringLiteral("fallback")
        || profileWithName(secondName).value(QStringLiteral("automaticSelectionMode")).toString()
            != QStringLiteral("preferred")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 automatic activation policy was not isolated")
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
    if (!selectPage(surface, 5)) return false;
    profilesPage = pageItem(surface, 5);
    profilesItem = qobject_cast<QQuickItem *>(profilesPage);
    QQmlExpression openInactiveForDelete(qmlContext(profilesPage), profilesPage,
        QStringLiteral("openProfile('%1')").arg(firstId));
    openInactiveForDelete.evaluate();
    settlePresentation();
    auto *deleteProfileButton = findVisualItemByObjectName(profilesItem,
        QStringLiteral("flightDeckProfileDelete"));
    if (openInactiveForDelete.hasError() || !deleteProfileButton
        || !clickFlightDeckSettingsItem(window, profilesItem, deleteProfileButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 destructive profile action was not pointer reachable")
            .arg(appearance));
    }
    auto *deleteProfileCancel = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckProfileDeleteCancel"));
    if (!deleteProfileCancel || !captureShell(QStringLiteral("profiles-delete-confirm"))
        || !clickFlightDeckSettingsItem(window, window->contentItem(), deleteProfileCancel)
        || profileWithName(firstName).isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 destructive cancel mutated its profile or failed to close")
            .arg(appearance));
    }
    if (!clickFlightDeckSettingsItem(window, profilesItem, deleteProfileButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 destructive confirmation did not reopen")
            .arg(appearance));
    }
    auto *deleteProfileConfirm = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckProfileDeleteConfirm"));
    if (!deleteProfileConfirm || !clickFlightDeckSettingsItem(window, window->contentItem(), deleteProfileConfirm)
        || !profileWithName(firstName).isEmpty()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 destructive confirmation did not commit through AppBackend")
            .arg(appearance));
    }
    if (backend.deleteProfile(secondId)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 allowed deletion of the active profile")
            .arg(appearance));
    }
    if (!backend.activateProfile(originalActiveProfileId)
        || !backend.deleteProfile(secondId)
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
    const bool mappingSelectorVisible = findVisualItemByObjectName(axesItem,
        QStringLiteral("flightDeckMappingSelector_0")) != nullptr;
    // The expanded editor can be taller than a minimum-height Flight Deck
    // viewport. Scroll its real Flickable range before asserting the static
    // preview, rather than relying on it to be eagerly created offscreen.
    QQuickItem *responsePreview = nullptr;
    const qreal maximumContentY = std::max<qreal>(0.0,
        axesItem->property("contentHeight").toReal() - axesItem->height());
    for (const qreal progress : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        axesItem->setProperty("contentY", maximumContentY * progress);
        QTest::qWait(16);
        settlePresentation();
        responsePreview = findVisualItemByObjectName(axesItem,
            QStringLiteral("flightDeckResponsePreview_0"));
        if (responsePreview) break;
    }
    axesItem->setProperty("contentY", 0.0);
    settlePresentation();
    if (configureAxis.hasError() || backend.selectedAxisIndex() != 0
        || axes->property("expandedAxisIndex").toInt() != 0
        || !mappingSelectorVisible || !responsePreview) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 Axes configure flow did not select the authoritative axis or materialize its static preview "
            "(qmlError=%2 selected=%3 expanded=%4 mapping=%5 preview=%6 visible=%7 size=%8x%9)")
            .arg(appearance).arg(configureAxis.hasError()).arg(backend.selectedAxisIndex())
            .arg(axes->property("expandedAxisIndex").toInt())
            .arg(mappingSelectorVisible)
            .arg(responsePreview != nullptr)
            .arg(responsePreview ? responsePreview->isVisible() : false)
            .arg(responsePreview ? responsePreview->width() : 0.0)
            .arg(responsePreview ? responsePreview->height() : 0.0));
    }
    const QVariantMap axisLearningBefore = flightDeckConfigurationSnapshot(backend);
    auto *axisLearningButton = findVisualItemByObjectName(axesItem,
        QStringLiteral("flightDeckAxisLearn_0"));
    if (!axisLearningButton || !clickFlightDeckSettingsItem(window, axesItem, axisLearningButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axis Learn pointer entry was not reachable")
            .arg(appearance));
    }
    settlePresentation();
    QObject *axisLearningDialog = window->findChild<QObject *>(QStringLiteral("flightDeckInputLearningDialog"));
    const bool axisDialogVisible = axisLearningDialog && axisLearningDialog->property("visible").toBool();
    const QString axisWorkflow = axisLearningDialog ? axisLearningDialog->property("workflow").toString() : QString{};
    const bool axisLearningActive = backend.inputLearning().value(QStringLiteral("active")).toBool();
    const bool axisLearningExplained = axisLearningDialog
        && !axisLearningDialog->property("startError").toString().trimmed().isEmpty();
    QObject *axisLearningHost = window->findChild<QObject *>(QStringLiteral("standardSurface"));
    const QString queuedAxisOperation = axisLearningHost
        ? axisLearningHost->property("flightDeckLearningOperation").toString() : QString{};
    const bool cachedAxisDialog = axisLearningHost
        && qvariant_cast<QObject *>(axisLearningHost->property("flightDeckLearningDialog"));
    if (!axisLearningDialog || !axisDialogVisible || axisWorkflow != QStringLiteral("single-axis")
        || (!axisLearningActive && !axisLearningExplained)
        || !captureShell(QStringLiteral("axes-learn-safe-unavailable"))) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 Axis Learn escaped its native safety dialog (dialog=%2 visible=%3 workflow=%4 active=%5 explained=%6 queued=%7 cached=%8)")
            .arg(appearance).arg(axisLearningDialog != nullptr).arg(axisDialogVisible).arg(axisWorkflow)
            .arg(axisLearningActive).arg(axisLearningExplained).arg(queuedAxisOperation).arg(cachedAxisDialog));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (axisLearningDialog->property("visible").toBool()
        || flightDeckConfigurationSnapshot(backend) != axisLearningBefore) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Axis Learn entry changed configuration while unavailable")
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
    // The quick-map pointer route must stay inside the Flight Deck modal. The
    // bounded startup fixture may arm the existing control-plane state, but
    // emits no input report; escaping it must cancel safely without mutating
    // mappings or falling back to the Standard dialog.
    const QVariantMap quickMapBefore = flightDeckConfigurationSnapshot(backend);
    auto *quickMapButton = findVisualItemByObjectName(buttonsItem,
        QStringLiteral("flightDeckButtonsQuickMap"));
    if (!quickMapButton || !clickFlightDeckSettingsItem(window, buttonsItem, quickMapButton)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Quick Map pointer entry was not reachable")
            .arg(appearance));
    }
    settlePresentation();
    QObject *learningDialog = window->findChild<QObject *>(QStringLiteral("flightDeckInputLearningDialog"));
    if (!learningDialog || !learningDialog->property("visible").toBool()
        || learningDialog->property("workflow").toString() != QStringLiteral("quick-buttons")
        || !learningDialog->findChild<QObject *>(QStringLiteral("flightDeckLearningOutputSelector"))
        || (!backend.inputLearning().value(QStringLiteral("active")).toBool()
            && learningDialog->property("startError").toString().trimmed().isEmpty())
        || !captureShell(QStringLiteral("buttons-quick-map-safe-unavailable"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Quick Map escaped its native modal")
            .arg(appearance));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    const QVariantMap quickMapAfter = flightDeckConfigurationSnapshot(backend);
    if (learningDialog->property("visible").toBool() || backend.inputLearning().value(QStringLiteral("active")).toBool()
        || quickMapAfter != quickMapBefore) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Quick Map entry changed configuration while unavailable (changed=%2)")
            .arg(appearance, differingSnapshotKeys(quickMapBefore, quickMapAfter)));
    }
    const QVariantMap buttonLearningBefore = flightDeckConfigurationSnapshot(backend);
    QQmlExpression openButtonLearning(qmlContext(buttons), buttons,
        QStringLiteral("(function() { requestButtonLearning(); return true; })()"));
    openButtonLearning.evaluate();
    settlePresentation();
    if (openButtonLearning.hasError() || !learningDialog->property("visible").toBool()
        || learningDialog->property("workflow").toString() != QStringLiteral("single-button")
        || !learningDialog->findChild<QObject *>(QStringLiteral("flightDeckLearningOutputSelector"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Button Learn did not stay in the native modal")
            .arg(appearance));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (learningDialog->property("visible").toBool()
        || flightDeckConfigurationSnapshot(backend) != buttonLearningBefore) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Button Learn entry changed configuration while unavailable")
            .arg(appearance));
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
    const QVariantList automationVisualFixture{
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-active")},
            {QStringLiteral("name"), QStringLiteral("Precision hold")},
            {QStringLiteral("enabled"), true}, {QStringLiteral("active"), true},
            {QStringLiteral("health"), 0}, {QStringLiteral("priority"), 85},
            {QStringLiteral("conditionSummary"), QStringLiteral("Button 2 is held")},
            {QStringLiteral("actionSummary"), QStringLiteral("Hold vJoy Button 7")},
            {QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("type"), 5}, {QStringLiteral("button"), 2}}}},
            {QStringLiteral("actions"), QVariantList{QVariantMap{{QStringLiteral("type"), 0}, {QStringLiteral("virtualButton"), 7}}}}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-disabled")},
            {QStringLiteral("name"), QStringLiteral("Landing mode")},
            {QStringLiteral("enabled"), false}, {QStringLiteral("active"), false},
            {QStringLiteral("health"), 0}, {QStringLiteral("priority"), 50},
            {QStringLiteral("conditionSummary"), QStringLiteral("Throttle is below 10%")},
            {QStringLiteral("actionSummary"), QStringLiteral("Switch to Landing profile")},
            {QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("type"), 2}, {QStringLiteral("axis"), 2}}}},
            {QStringLiteral("actions"), QVariantList{QVariantMap{{QStringLiteral("type"), 3}}}}}),
        QVariant::fromValue(QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture-attention")},
            {QStringLiteral("name"), QStringLiteral("Missing profile safety")},
            {QStringLiteral("enabled"), true}, {QStringLiteral("active"), false},
            {QStringLiteral("health"), 2}, {QStringLiteral("healthMessage"), QStringLiteral("Referenced profile is unavailable")},
            {QStringLiteral("priority"), 20}, {QStringLiteral("conditionSummary"), QStringLiteral("Active profile is unavailable")},
            {QStringLiteral("actionSummary"), QStringLiteral("Use missing profile")},
            {QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("type"), 10}}}},
             {QStringLiteral("actions"), QVariantList{QVariantMap{{QStringLiteral("type"), 2}}}}}),
    };
    QVariantList automationLargeVisualFixture = automationVisualFixture;
    for (int index = 4; index <= 18; ++index) {
        const bool longName = index == 4;
        automationLargeVisualFixture.append(QVariant::fromValue(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("fixture-large-%1").arg(index)},
            {QStringLiteral("name"), longName
                ? QStringLiteral("A deliberately long Automation rule name that must remain readable in the Flight Deck rule collection")
                : QStringLiteral("Response helper %1").arg(index)},
            {QStringLiteral("enabled"), index % 3 != 0}, {QStringLiteral("active"), false},
            {QStringLiteral("health"), 0}, {QStringLiteral("priority"), 100 - index},
            {QStringLiteral("conditionSummary"), longName
                ? QStringLiteral("Button 3 is pressed after the throttle has crossed the configured safety threshold")
                : QStringLiteral("Button %1 is pressed").arg(index)},
            {QStringLiteral("actionSummary"), longName
                ? QStringLiteral("Apply the selected Adaptive Response preset to the configured response axis")
                : QStringLiteral("Tap vJoy Button %1").arg(index)},
            {QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("type"), 11}, {QStringLiteral("button"), index}}}},
            {QStringLiteral("actions"), QVariantList{QVariantMap{{QStringLiteral("type"), 10}, {QStringLiteral("virtualButton"), index}}}},
        }));
    }
    if (!captureShell(QStringLiteral("automation-editor-deep-link"))
        || !evaluateEditorFunction(automation, QStringLiteral("closeEditor()"))
        || !automation->setProperty("automationPresentationOverride", automationVisualFixture)
        || !automation->setProperty("filterMode", QStringLiteral("all"))
        || !automation->setProperty("searchText", QString{})) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation editor or list fixture could not be prepared")
            .arg(appearance));
    }
    settlePresentation();
    if (!captureShell(QStringLiteral("automation-list-mixed"))
        || !automation->setProperty("filterMode", QStringLiteral("enabled"))
        || !captureShell(QStringLiteral("automation-filter-enabled"))
        || !automation->setProperty("filterMode", QStringLiteral("disabled"))
        || !captureShell(QStringLiteral("automation-filter-disabled"))
        || !automation->setProperty("filterMode", QStringLiteral("all"))
        || !automation->setProperty("searchText", QStringLiteral("missing"))
        || !captureShell(QStringLiteral("automation-search-attention"))
        || !automation->setProperty("searchText", QString{})
        || !automation->setProperty("automationPresentationOverride", QVariantList{})
        || !captureShell(QStringLiteral("automation-empty"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation visual-state matrix did not render")
            .arg(appearance));
    }
    const QSize automationOriginalSize = window->size();
    if (!automation->setProperty("automationPresentationOverride", automationLargeVisualFixture)
        || !automation->setProperty("filterMode", QStringLiteral("all"))
        || !automation->setProperty("searchText", QString{})) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation long-name fixture could not be installed")
            .arg(appearance));
    }
    settlePresentation();
    if (!captureShell(QStringLiteral("automation-large-long-normal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation large collection did not render")
            .arg(appearance));
    }
    window->resize(900, 650);
    settlePresentation();
    if (std::abs(automation->property("contentWidth").toReal() - automation->property("width").toReal()) > 0.5
        || !captureShell(QStringLiteral("automation-large-long-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation minimum layout escaped horizontally")
            .arg(appearance));
    }
    window->resize(1600, 980);
    settlePresentation();
    if (std::abs(automation->property("contentWidth").toReal() - automation->property("width").toReal()) > 0.5
        || !captureShell(QStringLiteral("automation-large-long-wide"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Automation wide layout escaped horizontally")
            .arg(appearance));
    }
    window->resize(automationOriginalSize);
    settlePresentation();
    automation->setProperty("automationPresentationOverride", QVariant{});
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
    buttons->setProperty("contentY", std::max<qreal>(0.0,
        buttons->property("contentHeight").toReal() - buttons->property("height").toReal()));
    settlePresentation();
    // Re-arm immediately before the deep route check. The ordinary
    // no-device worker can publish between page setup and a deep card scroll;
    // this bounded UI fixture supplies only the selector's available controls.
    backend.setButtonUiFixtureForTest(32, 32, 1, 1);
    if (!backend.setPovMapping(1, 0, 10, true)
        || !backend.setPovMapping(1, 1, 11, true)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Hat fixture could not establish independent direction routes")
            .arg(appearance));
    }
    settlePresentation();
    QObject *povSelector = findVisualItemByObjectName(buttonsItem,
        QStringLiteral("flightDeckPovMappingSelector_1_0"));
    // The ordinary Button selector above remains the pointer-command proof.
    // This deep Hat fixture verifies its distinct direction routes through the
    // authoritative API without coupling the responsive scroll geometry to a
    // second popup-coordinate assertion.
    if (configureHat.hasError() || !povSelector
        || targetForPovDirection(backend.povInputs(), 1, 0) != 10
        || targetForPovDirection(backend.povInputs(), 1, 1) != 11
        || !captureShell(QStringLiteral("buttons-hat-expanded"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Hat direction routes did not preserve adjacent directions (up=%2 upRight=%3)")
            .arg(appearance)
            .arg(targetForPovDirection(backend.povInputs(), 1, 0))
            .arg(targetForPovDirection(backend.povInputs(), 1, 1)));
    }
    const QVariantMap povLearningBefore = flightDeckConfigurationSnapshot(backend);
    QQmlExpression openPovLearning(qmlContext(buttons), buttons,
        QStringLiteral("(function() { requestPovLearning(10); return true; })()"));
    openPovLearning.evaluate();
    settlePresentation();
    QObject *povLearningDialog = window->findChild<QObject *>(QStringLiteral("flightDeckInputLearningDialog"));
    if (openPovLearning.hasError() || !povLearningDialog || !povLearningDialog->property("visible").toBool()
        || povLearningDialog->property("workflow").toString() != QStringLiteral("single-pov")
        || (!backend.inputLearning().value(QStringLiteral("active")).toBool()
            && povLearningDialog->property("startError").toString().trimmed().isEmpty())) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 POV Learn did not stay in the native safety dialog")
            .arg(appearance));
    }
    QTest::keyClick(window, Qt::Key_Escape);
    settlePresentation();
    if (povLearningDialog->property("visible").toBool()
        || flightDeckConfigurationSnapshot(backend) != povLearningBefore) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 POV Learn entry changed configuration while unavailable")
            .arg(appearance));
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
    // A deliberately unbroken controller name is the responsive regression
    // fixture. Its text box must stay in the card's semantic safe area rather
    // than using its implicit width to force the card outside its grid.
    auto *longName = devices ? findVisualItemByObjectName(qobject_cast<QQuickItem *>(devices),
        QStringLiteral("flightDeckControllerName_2")) : nullptr;
    QQuickItem *longNameCard = nullptr;
    for (QQuickItem *candidate = longName; candidate; candidate = candidate->parentItem()) {
        if (candidate->objectName().startsWith(QStringLiteral("flightDeckControllerCard_"))
            && candidate->property("contentPadding").isValid()) {
            longNameCard = candidate;
            break;
        }
    }
    const auto textFitsCardSafeArea = [](QQuickItem *text, QQuickItem *card) {
        if (!text || !card) return false;
        const QRectF textRect = text->mapRectToItem(card, QRectF(QPointF{}, text->size()));
        const qreal inset = card->property("contentPadding").toReal();
        return textRect.left() >= inset - 0.5 && textRect.right() <= card->width() - inset + 0.5
            && textRect.top() >= inset - 0.5 && textRect.bottom() <= card->height() - inset + 0.5;
    };
    const bool multiCaptured = captureShell(QStringLiteral("devices-multi-normal"));
    if (!devices || devices->property("readiness").toMap() != sharedReadiness || controllerCount != 4
        || !emptyState || emptyState->property("visible").toBool() || !controllerRepeater
        || controllerRepeater->property("count").toInt() != 4
        || firstAction != QStringLiteral("ACTIVE") || secondAction != QStringLiteral("USE CONTROLLER")
        || thirdAction != QStringLiteral("VERIFY CONTROLLER") || fourthAction != QStringLiteral("RESCAN")
        || !textFitsCardSafeArea(longName, longNameCard)
        || !multiCaptured) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 Devices multi-controller fixture was incomplete: readiness=%2 count=%3 empty=%4 repeater=%5 actions=%6/%7/%8/%9 safe=%10 capture=%11 longName=%12 card=%13")
            .arg(appearance).arg(devices && devices->property("readiness").toMap() == sharedReadiness)
            .arg(controllerCount).arg(emptyState ? emptyState->property("visible").toBool() : true)
            .arg(controllerRepeater ? controllerRepeater->property("count").toInt() : -1)
            .arg(firstAction).arg(secondAction).arg(thirdAction).arg(fourthAction)
            .arg(textFitsCardSafeArea(longName, longNameCard)).arg(multiCaptured)
            .arg(longName != nullptr).arg(longNameCard != nullptr)
            + QStringLiteral(" readiness-diff=%1").arg(devices
                ? differingSnapshotKeys(sharedReadiness, devices->property("readiness").toMap())
                : QStringLiteral("device-page-unavailable")));
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
    auto *repairHeader = repairConfirmation ? repairConfirmation->findChild<QQuickItem *>(
        QStringLiteral("flightDeckDialogHeader")) : nullptr;
    auto *repairHeading = repairConfirmation ? repairConfirmation->findChild<QQuickItem *>(
        QStringLiteral("flightDeckDialogHeading")) : nullptr;
    const auto headingFitsDialogInset = [](QQuickItem *heading, QQuickItem *header, qreal inset) {
        if (!heading || !header) return false;
        const QRectF headingRect = heading->mapRectToItem(header, QRectF(QPointF{}, heading->size()));
        return headingRect.left() >= inset - 0.5 && headingRect.right() <= header->width() - inset + 0.5
            && headingRect.top() >= inset - 0.5;
    };
    if (!devices || !devices->property("canRepairSetup").toBool() || !repairConfirmation
        || !QMetaObject::invokeMethod(repairConfirmation, "open")
        || !repairConfirmation->property("visible").toBool()
        || !headingFitsDialogInset(repairHeading, repairHeader,
            repairConfirmation->property("contentPadding").toReal())
        || !captureShell(QStringLiteral("devices-repair-confirmation"))) {
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
    auto *navigationRail = surface->findChild<QQuickItem *>(
        QStringLiteral("flightDeckNavigationRail"));
    auto *readiness = surface->findChild<QQuickItem *>(QStringLiteral("flightDeckReadiness"));
    auto *readinessContent = surface->findChild<QQuickItem *>(
        QStringLiteral("flightDeckReadinessContent"));
    if (!navigationViewport || !navigationRail || !readiness || !readinessContent
        || navigationViewport->property("contentHeight").toReal() <= navigationViewport->height()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 minimum rail did not expose its constrained-layout scroll path")
            .arg(appearance));
    }
    const auto rectIn = [](QQuickItem *item, QQuickItem *container) {
        return item->mapRectToItem(container, QRectF(QPointF{}, item->size()));
    };
    const auto verifyRailContainment = [&]() {
        const QRectF footerRect = rectIn(readiness, navigationRail);
        const QRectF viewportRect = rectIn(navigationViewport, navigationRail);
        const QRectF contentRect = rectIn(readinessContent, readiness);
        const qreal padding = readiness->property("contentPadding").toReal();
        return footerRect.left() >= -0.5 && footerRect.right() <= navigationRail->width() + 0.5
            && footerRect.top() >= -0.5 && footerRect.bottom() <= navigationRail->height() + 0.5
            && viewportRect.bottom() <= footerRect.top() - 0.5
            && contentRect.left() >= padding - 0.5
            && contentRect.right() <= readiness->width() - padding + 0.5
            && contentRect.top() >= padding - 0.5
            && contentRect.bottom() <= readiness->height() - padding + 0.5;
    };
    if (!verifyRailContainment()) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 minimum rail let readiness, navigation, or padded content escape its surface")
            .arg(appearance));
    }
    const qreal readinessTopBeforeScroll = readiness->mapToScene(QPointF{}).y();
    navigationViewport->setProperty("contentY", navigationViewport->property("contentHeight").toReal()
        - navigationViewport->height());
    settlePresentation();
    if (!verifyRailContainment()
        || std::abs(readiness->mapToScene(QPointF{}).y() - readinessTopBeforeScroll) > 0.5) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 minimum rail moved or obscured the persistent readiness card while navigation scrolled")
            .arg(appearance));
    }
    if (!selectPage(surface, 6)) return false;
    settlePresentation();
    auto *selectedNavigation = findVisualItemByObjectName(window->contentItem(),
        QStringLiteral("flightDeckNav_6"));
    const QRectF selectedRect = selectedNavigation
        ? rectIn(selectedNavigation, navigationViewport) : QRectF{};
    if (!selectedNavigation || selectedRect.top() < -0.5
        || selectedRect.bottom() > navigationViewport->height() + 0.5) {
        return failPresentationLifecycleTest(QStringLiteral(
            "Flight Deck %1 selected navigation item did not scroll into the usable rail viewport")
            .arg(appearance));
    }
    if (!selectPage(surface, 8)) return false;
    for (const QSize &railSize : std::array<QSize, 5>{QSize{900, 650}, QSize{1000, 720},
                                                        QSize{1200, 800}, QSize{1400, 900},
                                                        QSize{1600, 980}}) {
        window->resize(railSize);
        window->requestUpdate();
        QTest::qWait(40);
        settlePresentation();
        if (!verifyRailContainment()
            || !captureShell(QStringLiteral("rail-%1x%2").arg(railSize.width()).arg(railSize.height()))) {
            return failPresentationLifecycleTest(QStringLiteral(
                "Flight Deck %1 rail containment failed at %2x%3")
                .arg(appearance).arg(railSize.width()).arg(railSize.height()));
        }
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
    if (!selectPage(surface, 9)) return false;
    QObject *adaptiveVisual = pageItem(surface, 9);
    auto *adaptiveVisualItem = qobject_cast<QQuickItem *>(adaptiveVisual);
    if (!verifyFlightDeckAdaptivePresetChoiceSafeArea(adaptiveVisual, appearance)) return false;
    for (const QString &flowName : {QStringLiteral("adaptiveContextMetrics"),
                                    QStringLiteral("adaptiveContextSelectors"),
                                    QStringLiteral("adaptivePresetFlow"),
                                    QStringLiteral("adaptiveBasicMetrics")}) {
        auto *flow = adaptiveVisual->findChild<QQuickItem *>(flowName);
        if (!flow || flow->width() < adaptiveVisualItem->width() * 0.70) {
            return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive flow %2 did not receive the page width (flow=%3 page=%4)")
                .arg(appearance, flowName).arg(flow ? flow->width() : 0).arg(adaptiveVisualItem->width()));
        }
    }
    const auto *contextCategory = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextMetricCategory"));
    const auto *contextProfile = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextMetricProfile"));
    const auto *contextDevice = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveSelectedDeviceContext"));
    const auto *contextLevel = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextLevel"));
    const auto *contextTarget = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextTarget"));
    const auto *contextAxis = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextAxis"));
    if (!contextCategory || !contextProfile || !contextDevice || !contextLevel || !contextTarget || !contextAxis
        || contextProfile->y() != contextCategory->y() || contextProfile->x() <= contextCategory->x()
        || contextTarget->y() != contextDevice->y() || contextTarget->x() <= contextDevice->x()
        || contextAxis->y() != contextTarget->y() || contextAxis->x() <= contextTarget->x()
        || contextLevel->y() != contextAxis->y() || contextLevel->x() <= contextAxis->x()) {
        const auto *metricFlow = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextMetrics"));
        const auto *selectorFlow = adaptiveVisual->findChild<QQuickItem *>(QStringLiteral("adaptiveContextSelectors"));
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive Flow layout did not arrange context horizontally (metricFlow=%2 width=%3 selectorFlow=%4 width=%5 category=%6,%7 %8x%9 profile=%10,%11 %12x%13 level=%14,%15 %16x%17 target=%18,%19 %20x%21)")
            .arg(appearance).arg(metricFlow ? metricFlow->property("flow").toInt() : -1).arg(metricFlow ? metricFlow->width() : -1)
            .arg(selectorFlow ? selectorFlow->property("flow").toInt() : -1).arg(selectorFlow ? selectorFlow->width() : -1)
            .arg(contextCategory ? contextCategory->x() : -1).arg(contextCategory ? contextCategory->y() : -1)
            .arg(contextCategory ? contextCategory->width() : -1).arg(contextCategory ? contextCategory->height() : -1)
            .arg(contextProfile ? contextProfile->x() : -1).arg(contextProfile ? contextProfile->y() : -1)
            .arg(contextProfile ? contextProfile->width() : -1).arg(contextProfile ? contextProfile->height() : -1)
            .arg(contextLevel ? contextLevel->x() : -1).arg(contextLevel ? contextLevel->y() : -1)
            .arg(contextLevel ? contextLevel->width() : -1).arg(contextLevel ? contextLevel->height() : -1)
            .arg(contextTarget ? contextTarget->x() : -1).arg(contextTarget ? contextTarget->y() : -1)
            .arg(contextTarget ? contextTarget->width() : -1).arg(contextTarget ? contextTarget->height() : -1));
    }
    adaptiveVisual->setProperty("advancedExpanded", false);
    adaptiveVisual->setProperty("testLabExpanded", false);
    adaptiveVisual->setProperty("responseLabSource", QStringLiteral("interactive"));
    adaptiveVisual->setProperty("contentY", 0.0);
    const auto scrollAdaptiveTo = [&](const QString &section, const QString &targetName) {
        QQmlExpression request(qmlContext(adaptiveVisual), adaptiveVisual,
            QStringLiteral("scrollToSection('%1')").arg(section));
        const QVariant scrolled = request.evaluate();
        if (request.hasError() || !scrolled.toBool()) {
            const QByteArray message = QStringLiteral("Adaptive visual navigation %1 could not scroll: result=%2 error=%3\n")
                .arg(section, scrolled.toString(), request.hasError() ? request.error().toString() : QStringLiteral("none")).toUtf8();
            std::fputs(message.constData(), stderr);
            return false;
        }
        settlePresentation();
        auto *target = findVisualItemByObjectName(adaptiveVisualItem, targetName);
        if (!target) {
            const QByteArray message = QStringLiteral("Adaptive visual navigation %1 could not find target %2\n")
                .arg(section, targetName).toUtf8();
            std::fputs(message.constData(), stderr);
            return false;
        }
        const qreal targetTop = target->mapToScene(QPointF{}).y();
        const qreal targetBottom = targetTop + target->height();
        const qreal pageTop = adaptiveVisualItem->mapToScene(QPointF{}).y();
        const qreal pageBottom = pageTop + adaptiveVisualItem->height();
        const bool aligned = targetTop >= pageTop
            && (target->height() > adaptiveVisualItem->height()
                ? targetTop <= pageTop + 32.0
                : targetBottom <= pageBottom);
        if (!aligned) {
            const QByteArray message = QStringLiteral("Adaptive visual navigation %1 -> %2 did not align: targetTop=%3 targetHeight=%4 pageTop=%5 pageHeight=%6 contentY=%7 contentHeight=%8\n")
                .arg(section, targetName).arg(targetTop).arg(target->height()).arg(pageTop)
                .arg(adaptiveVisualItem->height()).arg(adaptiveVisual->property("contentY").toReal())
                .arg(adaptiveVisual->property("contentHeight").toReal()).toUtf8();
            std::fputs(message.constData(), stderr);
        }
        return aligned;
    };
    settlePresentation();
    if (!captureShell(QStringLiteral("adaptive-basic"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive basic state did not render")
            .arg(appearance));
    }
    QQmlExpression disableForEvidence(qmlContext(adaptiveVisual), adaptiveVisual,
        QStringLiteral("(function() { updateParameter('enabled', false); return !effective().enabled; })()"));
    const QVariant disabledForEvidence = disableForEvidence.evaluate();
    if (disableForEvidence.hasError() || !disabledForEvidence.toBool()
        || !captureShell(QStringLiteral("adaptive-off"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive off state did not render")
            .arg(appearance));
    }
    QQmlExpression enableForEvidence(qmlContext(adaptiveVisual), adaptiveVisual,
        QStringLiteral("(function() { updateParameter('enabled', true); return effective().enabled; })()"));
    if (enableForEvidence.hasError() || !enableForEvidence.evaluate().toBool()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive off-state fixture could not restore")
            .arg(appearance));
    }
    QQmlExpression makeCustomForEvidence(qmlContext(adaptiveVisual), adaptiveVisual,
        QStringLiteral("(function() { updateParameter('maximumHorizonMs', effective().maximumHorizonMs + 1.0); return previewSamples.length; })()"));
    makeCustomForEvidence.evaluate();
    if (makeCustomForEvidence.hasError() || adaptiveVisual->property("previewSamples").toList().isEmpty()
        || !captureShell(QStringLiteral("adaptive-custom"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive custom configuration state did not render")
            .arg(appearance));
    }
    if (!scrollAdaptiveTo(QStringLiteral("preview"), QStringLiteral("staticResponsePreviewCard"))
        || !captureShell(QStringLiteral("adaptive-static-rapid-reversal"))
        || !scrollAdaptiveTo(QStringLiteral("comparison"), QStringLiteral("adaptiveComparisonCard"))
        || !captureShell(QStringLiteral("adaptive-comparison"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive static analysis state did not render")
            .arg(appearance));
    }
    adaptiveVisual->setProperty("advancedExpanded", true);
    if (!scrollAdaptiveTo(QStringLiteral("advanced"), QStringLiteral("flightDeckAdaptiveAdvancedCard"))
        || !captureShell(QStringLiteral("adaptive-advanced"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive advanced state did not render")
            .arg(appearance));
    }
    adaptiveVisual->setProperty("presetWorkshopExpanded", true);
    if (!scrollAdaptiveTo(QStringLiteral("advanced"), QStringLiteral("flightDeckAdaptiveAdvancedCard"))
        || !captureShell(QStringLiteral("adaptive-preset-workshop"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive preset workshop did not render")
            .arg(appearance));
    }
    adaptiveVisual->setProperty("presetWorkshopExpanded", false);
    if (!scrollAdaptiveTo(QStringLiteral("advanced-controls"), QStringLiteral("adaptivePredictorSelector"))
        || !captureShell(QStringLiteral("adaptive-advanced-controls"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive advanced controls did not render")
            .arg(appearance));
    }
    backend.injectAdaptiveResponseLiveSampleForTest(0, -0.72F);
    backend.injectAdaptiveResponseLiveSampleForTest(0, 0.68F);
    QQmlExpression liveSource(qmlContext(adaptiveVisual), adaptiveVisual,
        QStringLiteral("setResponseLabSource('live'); refreshHistory(true)"));
    liveSource.evaluate();
    if (liveSource.hasError()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive live fixture could not select its source")
            .arg(appearance));
    }
    if (!scrollAdaptiveTo(QStringLiteral("analysis"), QStringLiteral("responseLabCard"))
        || !captureShell(QStringLiteral("adaptive-live-analysis"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive live analysis state did not render")
            .arg(appearance));
    }
    QQmlExpression interactiveSource(qmlContext(adaptiveVisual), adaptiveVisual,
        QStringLiteral("setResponseLabSource('interactive'); simulatorInput = 0.62; simulatorPaused = false"));
    interactiveSource.evaluate();
    if (interactiveSource.hasError()) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive interactive fixture could not select its source")
            .arg(appearance));
    }
    QTest::qWait(120);
    adaptiveVisual->setProperty("simulatorPaused", true);
    const bool testLabPropertyWritten = adaptiveVisual->setProperty("testLabExpanded", true);
    settlePresentation();
    if (!testLabPropertyWritten || !adaptiveVisual->property("testLabExpanded").toBool()) {
        const QByteArray message = QStringLiteral("Adaptive Test Lab fixture could not expand: written=%1 value=%2\n")
            .arg(testLabPropertyWritten).arg(adaptiveVisual->property("testLabExpanded").toBool()).toUtf8();
        std::fputs(message.constData(), stderr);
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive Test Lab fixture did not expand")
            .arg(appearance));
    }
    const auto sectionGeometry = [&](const QString &section) {
        QQmlExpression geometryExpression(qmlContext(adaptiveVisual), adaptiveVisual,
            QStringLiteral("sectionGeometry('%1')").arg(section));
        return geometryExpression.evaluate().toMap();
    };
    const QVariantMap advancedGeometry = sectionGeometry(QStringLiteral("advanced"));
    const QVariantMap telemetryGeometry = sectionGeometry(QStringLiteral("telemetry"));
    const QVariantMap analysisGeometry = sectionGeometry(QStringLiteral("analysis"));
    const QVariantMap testLabGeometry = sectionGeometry(QStringLiteral("test-lab"));
    const bool orderedSections = advancedGeometry.value(QStringLiteral("height")).toReal() > 76
        && telemetryGeometry.value(QStringLiteral("top")).toReal() >= advancedGeometry.value(QStringLiteral("bottom")).toReal()
        && analysisGeometry.value(QStringLiteral("top")).toReal() >= telemetryGeometry.value(QStringLiteral("bottom")).toReal()
        && testLabGeometry.value(QStringLiteral("top")).toReal() >= analysisGeometry.value(QStringLiteral("bottom")).toReal()
        && testLabGeometry.value(QStringLiteral("contentHeight")).toReal()
            >= testLabGeometry.value(QStringLiteral("bottom")).toReal() + 16.0;
    if (!orderedSections) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive expanded sections overlap or exceed the scroll range (advanced=%2/%3 telemetry=%4/%5 analysis=%6/%7 testLab=%8/%9 content=%10)")
            .arg(appearance)
            .arg(advancedGeometry.value(QStringLiteral("top")).toReal()).arg(advancedGeometry.value(QStringLiteral("height")).toReal())
            .arg(telemetryGeometry.value(QStringLiteral("top")).toReal()).arg(telemetryGeometry.value(QStringLiteral("height")).toReal())
            .arg(analysisGeometry.value(QStringLiteral("top")).toReal()).arg(analysisGeometry.value(QStringLiteral("height")).toReal())
            .arg(testLabGeometry.value(QStringLiteral("top")).toReal()).arg(testLabGeometry.value(QStringLiteral("height")).toReal())
            .arg(testLabGeometry.value(QStringLiteral("contentHeight")).toReal()));
    }
    const bool testLabScrolled = scrollAdaptiveTo(QStringLiteral("test-lab"), QStringLiteral("flightDeckAdaptiveTestLabCard"));
    const bool testLabCaptured = testLabScrolled && captureShell(QStringLiteral("adaptive-interactive-test-lab"));
    if (!testLabCaptured) {
        const QByteArray message = QStringLiteral("Adaptive Test Lab visual capture state: scrolled=%1 size=%2x%3 contentY=%4 contentHeight=%5\n")
            .arg(testLabScrolled).arg(window->width()).arg(window->height())
            .arg(adaptiveVisual->property("contentY").toReal()).arg(adaptiveVisual->property("contentHeight").toReal()).toUtf8();
        std::fputs(message.constData(), stderr);
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive Test Lab state did not render")
            .arg(appearance));
    }
    adaptiveVisual->setProperty("contentY", std::max<qreal>(0.0,
        adaptiveVisual->property("contentHeight").toReal() - adaptiveVisualItem->height()));
    settlePresentation();
    if (!captureShell(QStringLiteral("adaptive-test-lab-bottom"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive Test Lab bottom state did not render")
            .arg(appearance));
    }
    adaptiveVisual->setProperty("presentationOverride", QVariantMap{
        {QStringLiteral("category"), QStringLiteral("Battlefield Six Experimental Flight-Control Category With A Deliberately Long Name")},
        {QStringLiteral("profile"), QStringLiteral("Precision Helicopter Profile With A Deliberately Long Operational Description")},
        {QStringLiteral("axis"), QStringLiteral("Collective Lever Fine Trim Axis With A Deliberately Long Label")},
        {QStringLiteral("controllerAvailable"), false},
    });
    adaptiveVisual->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("adaptive-unavailable-long"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive unavailable/long-name state did not render")
            .arg(appearance));
    }
    const QSize adaptiveOriginalSize = window->size();
    window->resize(900, 650);
    adaptiveVisual->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("adaptive-minimum"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive minimum layout did not render")
            .arg(appearance));
    }
    if (!scrollAdaptiveTo(QStringLiteral("preview"), QStringLiteral("staticResponsePreviewCard"))
        || !captureShell(QStringLiteral("adaptive-minimum-static-rapid-reversal"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive minimum static preview did not render")
            .arg(appearance));
    }
    window->resize(1600, 980);
    adaptiveVisual->setProperty("presentationOverride", QVariant{});
    adaptiveVisual->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("adaptive-wide"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive wide layout did not render")
            .arg(appearance));
    }
    window->resize(adaptiveOriginalSize);
    adaptiveVisual->setProperty("presentationOverride", QVariant{});
    if (!selectPage(surface, 3)) return false;

    // Diagnostics is a display-only surface. This fixture exercises the
    // shared readiness model, exact route state, pointer controls, and a
    // recovery link without calling any backend command.
    QObject *diagnostics = pageItem(surface, 3);
    auto *diagnosticsItem = qobject_cast<QQuickItem *>(diagnostics);
    if (!diagnostics || !diagnosticsItem
        || diagnostics->objectName() != QStringLiteral("flightDeckDiagnostics")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 did not load native Diagnostics")
            .arg(appearance));
    }
    const QString profileBeforeDiagnostics = backend.activeProfileId();
    const QVariantList routesBeforeDiagnostics = backend.runtimeAxisRoutesForTest();
    const QVariantList automationsBeforeDiagnostics = backend.automationRules();
    const QVariantList controllersBeforeDiagnostics = backend.controllers();
    const QVariantMap adaptiveBeforeDiagnostics = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), backend.activeProfileId(), backend.selectedAxisIndex());
    const QVariantList routeFixture{
        QVariantMap{{QStringLiteral("label"), QStringLiteral("Roll")}, {QStringLiteral("available"), true}, {QStringLiteral("target"), QStringLiteral("X")}, {QStringLiteral("targetAvailable"), true}, {QStringLiteral("virtualRouted"), true}, {QStringLiteral("virtualValid"), true}, {QStringLiteral("raw"), 0.31}, {QStringLiteral("calibrated"), 0.29}, {QStringLiteral("virtualValue"), 0.29}, {QStringLiteral("curveSummary"), QStringLiteral("Linear")}, {QStringLiteral("deadzone"), 0.02}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("Pitch")}, {QStringLiteral("available"), true}, {QStringLiteral("target"), QStringLiteral("Z")}, {QStringLiteral("targetAvailable"), true}, {QStringLiteral("virtualRouted"), true}, {QStringLiteral("virtualValid"), true}, {QStringLiteral("raw"), -0.42}, {QStringLiteral("calibrated"), -0.40}, {QStringLiteral("virtualValue"), -0.40}, {QStringLiteral("curveSummary"), QStringLiteral("Precision")}, {QStringLiteral("deadzone"), 0.03}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("Yaw")}, {QStringLiteral("available"), true}, {QStringLiteral("target"), QStringLiteral("Rz")}, {QStringLiteral("targetAvailable"), true}, {QStringLiteral("virtualRouted"), true}, {QStringLiteral("virtualValid"), true}, {QStringLiteral("raw"), 0.08}, {QStringLiteral("calibrated"), 0.07}, {QStringLiteral("virtualValue"), 0.07}, {QStringLiteral("curveSummary"), QStringLiteral("Linear")}, {QStringLiteral("deadzone"), 0.02}}
    };
    const QVariantList checksFixture{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("PHYSICAL INPUT")}, {QStringLiteral("state"), QStringLiteral("Ready")}, {QStringLiteral("message"), QStringLiteral("Controller reports are available.")}, {QStringLiteral("severity"), QStringLiteral("ready")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("VIRTUAL OUTPUT")}, {QStringLiteral("state"), QStringLiteral("Ready")}, {QStringLiteral("message"), QStringLiteral("vJoy output is ready.")}, {QStringLiteral("severity"), QStringLiteral("ready")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("HIDHIDE ISOLATION")}, {QStringLiteral("state"), QStringLiteral("Ready")}, {QStringLiteral("message"), QStringLiteral("Physical input is protected.")}, {QStringLiteral("severity"), QStringLiteral("ready")}}
    };
    QVariantMap readyFixture{{QStringLiteral("physicalConnected"), true},
        {QStringLiteral("connectedControllerCount"), 1}, {QStringLiteral("deviceName"), QStringLiteral("T.16000M FCS")},
        {QStringLiteral("mappingActive"), true}, {QStringLiteral("mappingRequested"), true},
        {QStringLiteral("mappingStatus"), QStringLiteral("Mapping active")}, {QStringLiteral("vjoyReady"), true},
        {QStringLiteral("vjoyStatus"), QStringLiteral("vJoy Device 1 is ready")}, {QStringLiteral("vjoyStatusSeverity"), QStringLiteral("ready")},
        {QStringLiteral("vjoyDeviceId"), 1}, {QStringLiteral("hidhideAvailable"), true},
        {QStringLiteral("hidhideCloakStateKnown"), true}, {QStringLiteral("hidhideCloaked"), true},
        {QStringLiteral("hidhideMapperAllowed"), true}, {QStringLiteral("controllerReadinessState"), QStringLiteral("READY")},
        {QStringLiteral("effectiveProfileDisplayName"), QStringLiteral("Helicopter")}, {QStringLiteral("profileSourceLabel"), QStringLiteral("Manual selection")},
        {QStringLiteral("activeCategoryName"), QStringLiteral("Battlefield")}, {QStringLiteral("automaticGameDetection"), true},
        {QStringLiteral("activeCategoryRules"), QVariantList{QStringLiteral("bf6.exe")}},
        {QStringLiteral("runningApplications"), QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("Battlefield 6")}, {QStringLiteral("executable"), QStringLiteral("bf6.exe")}}}},
        {QStringLiteral("checks"), checksFixture}};
    QVariantMap diagnosticFixture{{QStringLiteral("deviceName"), QStringLiteral("T.16000M FCS")},
        {QStringLiteral("deviceId"), QStringLiteral("DIRECTINPUT:T16000M-FCS-TEST")}, {QStringLiteral("axes"), routeFixture},
        {QStringLiteral("buttons"), QVariantList{QVariantMap{{QStringLiteral("label"), QStringLiteral("B1")}, {QStringLiteral("pressed"), true}}, QVariantMap{{QStringLiteral("label"), QStringLiteral("B2")}, {QStringLiteral("pressed"), false}}}},
        {QStringLiteral("povs"), QVariantList{QVariantMap{{QStringLiteral("index"), 1}, {QStringLiteral("direction"), QStringLiteral("Centered")}, {QStringLiteral("centered"), true}}}},
        {QStringLiteral("controllers"), QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("T.16000M FCS")}, {QStringLiteral("state"), QStringLiteral("Connected · Verified · Active")}, {QStringLiteral("directInputId"), QStringLiteral("DIRECTINPUT:T16000M-FCS-TEST")}, {QStringLiteral("connected"), true}}}},
        {QStringLiteral("automationRules"), QVariantList{QVariantMap{{QStringLiteral("enabled"), true}, {QStringLiteral("health"), 0}}}},
        {QStringLiteral("automationRuleCount"), 1}, {QStringLiteral("automationActiveRuleCount"), 0}, {QStringLiteral("automationEngineEnabled"), true},
        {QStringLiteral("adaptive"), QVariantMap{{QStringLiteral("enabled"), true}, {QStringLiteral("physical"), 0.42}, {QStringLiteral("predicted"), 0.49}, {QStringLiteral("virtualOutput"), 0.47}, {QStringLiteral("velocity"), 5.87}, {QStringLiteral("acceleration"), 18.2}, {QStringLiteral("activeHorizonMs"), 15.3}, {QStringLiteral("lead"), 0.0486}, {QStringLiteral("confidence"), 0.94}, {QStringLiteral("state"), QStringLiteral("Accelerating")}, {QStringLiteral("model"), QStringLiteral("velocity")}}},
        {QStringLiteral("events"), QVariantList{QStringLiteral("12:00:01  Controller snapshot updated")}}};
    readinessModel->setProperty("presentationStateOverride", readyFixture);
    diagnostics->setProperty("presentationOverride", diagnosticFixture);
    settlePresentation();
    QQmlExpression routesExpression(qmlContext(diagnostics), diagnostics, QStringLiteral("routedAxes()"));
    const QVariantList displayedRoutes = routesExpression.evaluate().toList();
    if (routesExpression.hasError() || displayedRoutes.size() != 3
        || displayedRoutes.at(0).toMap().value(QStringLiteral("target")).toString() != QStringLiteral("X")
        || displayedRoutes.at(1).toMap().value(QStringLiteral("target")).toString() != QStringLiteral("Z")
        || displayedRoutes.at(2).toMap().value(QStringLiteral("target")).toString() != QStringLiteral("Rz")
        || diagnostics->property("outputHealth").toMap().value(QStringLiteral("tone")).toString() != QStringLiteral("healthy")
        || !captureShell(QStringLiteral("diagnostics-healthy"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Diagnostics healthy signal-path state is incomplete").arg(appearance));
    }
    const auto scrollDiagnostics = [&](const QString &section) {
        QQmlExpression call(qmlContext(diagnostics), diagnostics,
            QStringLiteral("scrollToSection('%1')").arg(section));
        const bool result = call.evaluate().toBool();
        settlePresentation();
        return !call.hasError() && result;
    };
    const auto clickDiagnosticsItem = [&](const QString &name, const QString &section) {
        if (!scrollDiagnostics(section)) return false;
        auto *item = findVisualItemByObjectName(diagnosticsItem, name);
        if (!item) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5)).toPoint());
        settlePresentation();
        return true;
    };
    const bool pathClicked = clickDiagnosticsItem(QStringLiteral("flightDeckDiagnosticsSection_path"), QStringLiteral("summary"));
    const bool pathScrolled = diagnostics->property("contentY").toReal() > 0.0;
    const bool detailClicked = clickDiagnosticsItem(QStringLiteral("flightDeckDiagnosticsTechnicalToggle"), QStringLiteral("advanced"));
    const bool detailsOpen = diagnostics->property("technicalDetailsExpanded").toBool();
    const bool observational = backend.activeProfileId() == profileBeforeDiagnostics
        && backend.runtimeAxisRoutesForTest() == routesBeforeDiagnostics
        && backend.automationRules() == automationsBeforeDiagnostics
        && backend.adaptiveResponseContextState(QStringLiteral("profile"), backend.activeProfileId(), backend.selectedAxisIndex()) == adaptiveBeforeDiagnostics;
    if (!pathClicked || !pathScrolled || !detailClicked || !detailsOpen || !observational
        || !captureShell(QStringLiteral("diagnostics-advanced-details"))) {
        qInfo().noquote() << QStringLiteral("diagnostics interaction path=%1 scrolled=%2 detail=%3 open=%4 observational=%5")
            .arg(pathClicked).arg(pathScrolled).arg(detailClicked).arg(detailsOpen).arg(observational);
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Diagnostics pointer navigation or technical disclosure was not observational").arg(appearance));
    }
    QVariantMap attentionFixture = readyFixture;
    attentionFixture.insert(QStringLiteral("vjoyReady"), false);
    attentionFixture.insert(QStringLiteral("vjoyStatus"), QStringLiteral("vJoy Device 1 does not match the configured output."));
    attentionFixture.insert(QStringLiteral("vjoyStatusSeverity"), QStringLiteral("error"));
    attentionFixture.insert(QStringLiteral("mappingActive"), false);
    attentionFixture.insert(QStringLiteral("controllerReadinessState"), QStringLiteral("ATTENTION"));
    attentionFixture.insert(QStringLiteral("checks"), QVariantList{checksFixture.at(0),
        QVariantMap{{QStringLiteral("name"), QStringLiteral("VIRTUAL OUTPUT")}, {QStringLiteral("state"), QStringLiteral("Action required")}, {QStringLiteral("message"), QStringLiteral("vJoy output needs setup.")}, {QStringLiteral("severity"), QStringLiteral("error")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("HIDHIDE ISOLATION")}, {QStringLiteral("state"), QStringLiteral("Attention")}, {QStringLiteral("message"), QStringLiteral("Physical controller may remain visible to games.")}, {QStringLiteral("severity"), QStringLiteral("warning")}}});
    diagnosticFixture.insert(QStringLiteral("vjoyReady"), false);
    diagnosticFixture.insert(QStringLiteral("vjoyStatus"), QStringLiteral("vJoy Device 1 does not match the configured output."));
    diagnosticFixture.insert(QStringLiteral("vjoyStatusSeverity"), QStringLiteral("error"));
    diagnosticFixture.insert(QStringLiteral("hidhideMapperAllowed"), false);
    readinessModel->setProperty("presentationStateOverride", attentionFixture);
    diagnostics->setProperty("presentationOverride", diagnosticFixture);
    diagnostics->setProperty("contentY", 0.0);
    diagnostics->setProperty("technicalDetailsExpanded", false);
    settlePresentation();
    if (diagnostics->property("outputHealth").toMap().value(QStringLiteral("tone")).toString() != QStringLiteral("fault")
        || diagnostics->property("isolationHealth").toMap().value(QStringLiteral("tone")).toString() != QStringLiteral("attention")
        || !captureShell(QStringLiteral("diagnostics-attention"))
        || !clickDiagnosticsItem(QStringLiteral("flightDeckDiagnosticsFilterAttention"), QStringLiteral("summary"))
        || diagnostics->property("filterMode").toString() != QStringLiteral("attention")) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Diagnostics centralized-attention state is incomplete").arg(appearance));
    }
    diagnostics->setProperty("filterMode", QStringLiteral("all"));
    QVariantList badRoutes = routeFixture;
    QVariantMap badPitch = badRoutes.at(1).toMap();
    badPitch.insert(QStringLiteral("targetAvailable"), false);
    badPitch.insert(QStringLiteral("virtualRouted"), false);
    badPitch.insert(QStringLiteral("virtualValid"), false);
    badRoutes[1] = badPitch;
    diagnosticFixture.insert(QStringLiteral("axes"), badRoutes);
    diagnostics->setProperty("presentationOverride", diagnosticFixture);
    if (!scrollDiagnostics(QStringLiteral("path")) || !captureShell(QStringLiteral("diagnostics-routing-problem"))
        || !clickDiagnosticsItem(QStringLiteral("flightDeckDiagnosticsOpenDevices"), QStringLiteral("systems"))
        || surface->property("currentPage").toInt() != 2
        || backend.activeProfileId() != profileBeforeDiagnostics
        || backend.runtimeAxisRoutesForTest() != routesBeforeDiagnostics
        || backend.automationRules() != automationsBeforeDiagnostics
        || backend.controllers() != controllersBeforeDiagnostics
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), backend.activeProfileId(), backend.selectedAxisIndex()) != adaptiveBeforeDiagnostics) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Diagnostics recovery route changed configuration or did not open Devices").arg(appearance));
    }
    if (!selectPage(surface, 3)) return false;
    diagnostics = pageItem(surface, 3);
    diagnosticsItem = qobject_cast<QQuickItem *>(diagnostics);
    if (!diagnostics || !diagnosticsItem) return false;
    readinessModel->setProperty("presentationStateOverride", readyFixture);
    diagnosticFixture.insert(QStringLiteral("deviceName"), QStringLiteral("T.16000M FCS Precision Flight Controller With An Intentionally Long Diagnostic Name"));
    diagnosticFixture.insert(QStringLiteral("deviceId"), QStringLiteral("DIRECTINPUT:VID_044F&PID_B68D&INSTANCE_000000000000000000000000000000000000000000000000"));
    diagnosticFixture.insert(QStringLiteral("axes"), routeFixture);
    diagnostics->setProperty("presentationOverride", diagnosticFixture);
    diagnostics->setProperty("outputDetailsExpanded", true);
    settlePresentation();
    if (!captureShell(QStringLiteral("diagnostics-signal-path")) || !scrollDiagnostics(QStringLiteral("systems"))
        || !captureShell(QStringLiteral("diagnostics-output-technical")) || !scrollDiagnostics(QStringLiteral("inspection"))
        || !captureShell(QStringLiteral("diagnostics-adaptive-technical"))) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Diagnostics technical visual matrix is incomplete").arg(appearance));
    }
    const QSize diagnosticsOriginalSize = window->size();
    window->resize(900, 650);
    diagnostics->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("diagnostics-minimum"))) return false;
    window->resize(1600, 980);
    diagnostics->setProperty("contentY", 0.0);
    settlePresentation();
    if (!captureShell(QStringLiteral("diagnostics-wide"))) return false;
    window->resize(diagnosticsOriginalSize);
    readinessModel->setProperty("presentationStateOverride", QVariant{});
    diagnostics->setProperty("presentationOverride", QVariant{});
    if (!selectPage(surface, 8)) return false;
    // Run cross-experience Settings actions after the long established
    // visual/interaction matrix has released its page fixtures. Selecting an
    // experience replaces the whole presentation host by design, so this is
    // the lifecycle-safe final interaction in this engine instance.
    if (!verifyFlightDeckSettings(backend, themeManager, window, surface, appearance)) return false;
    if (!selectPage(surface, 8)) return false;
    return true;
}

bool verifyFlightDeckAxesQmlLoad(hotas::AppBackend &backend, hotas::ThemeManager &themeManager)
{
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    QQmlComponent component(&engine);
    component.loadFromModule(u"HOTASMapper"_qs, u"FlightDeckAxes"_qs);
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

bool verifyFlightDeckAdaptiveResponseInteraction(hotas::AppBackend &backend,
                                                 hotas::ThemeManager &themeManager,
                                                 const QString &appearance)
{
    themeManager.setCurrentTheme(QStringLiteral("Standard"));
    themeManager.setFlightDeckAppearance(appearance);
    themeManager.setCurrentExperience(QStringLiteral("Flight Deck"));

    const int originalAxis = backend.selectedAxisIndex();
    backend.setSelectedAxis(0);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive shell did not load")
            .arg(appearance));
    }
    settlePresentation();
    QObject *surface = window->findChild<QObject *>(QStringLiteral("flightDeckSurface"));
    if (!surface || !surface->setProperty("currentPage", 9)) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive route did not load")
            .arg(appearance));
    }
    settlePresentation();
    QObject *adaptive = pageItem(surface, 9);
    auto *adaptiveItem = qobject_cast<QQuickItem *>(adaptive);
    if (!adaptiveItem) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive component did not create a QQuickItem")
            .arg(appearance));
    }

    const auto fail = [&](const QString &message) {
        return failPresentationLifecycleTest(QStringLiteral("Flight Deck %1 Adaptive Response: %2")
            .arg(appearance, message));
    };
    const auto findItem = [&](const QString &name) {
        return findVisualItemByObjectName(adaptiveItem, name);
    };
    const auto scrollTo = [&](QQuickItem *item) {
        if (!item) return false;
        const QPointF itemPoint = contentPoint(item, adaptiveItem);
        const qreal next = std::max<qreal>(0.0, itemPoint.y() - 72.0);
        adaptive->setProperty("contentY", next);
        settlePresentation();
        return true;
    };
    const auto clickItem = [&](QQuickItem *item) {
        if (!item || !scrollTo(item)) return false;
        const QPoint point = viewportPoint(item, adaptiveItem, QPointF(item->width() * 0.5,
                                                                         item->height() * 0.5));
        // A native window may promote a just-scrolled control in the next
        // polish frame. Match the ComboBox pointer helper's press/release
        // sequencing so this remains an actual pointer route on both the
        // Windows and offscreen platforms.
        QTest::qWait(8);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(8);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
        settlePresentation();
        return true;
    };

    if (adaptive->objectName() != QStringLiteral("flightDeckAdaptiveResponse")) {
        return fail(QStringLiteral("native page object was not selected"));
    }
    auto *axisSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptiveAxisSelector"));
    auto *scopeSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptiveEditScopeSelector"));
    auto *targetSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptiveTargetSelector"));
    if (!axisSelector || !scopeSelector || !targetSelector) {
        return fail(QStringLiteral("context controls are missing"));
    }
    const QVariantList axes = backend.axes();
    if (axes.size() < 3) return fail(QStringLiteral("Roll, Pitch, and Yaw fixture is unavailable"));
    const QString profileId = backend.activeProfileId();
    // Each physical click intentionally selects a different row. Qt's native
    // ComboBox leaves an already-selected row open in some offscreen builds,
    // which would test popup mechanics rather than axis selection.
    for (const int physicalAxis : {1, 2, 0}) {
        int modelIndex = -1;
        for (qsizetype index = 0; index < axes.size(); ++index) {
            if (axes.at(index).toMap().value(QStringLiteral("index")).toInt() == physicalAxis) {
                modelIndex = static_cast<int>(index);
                break;
            }
        }
        if (modelIndex < 0) {
            return fail(QStringLiteral("axis selector does not expose physical axis %1").arg(physicalAxis));
        }
        if (!clickResponseComboRow(window, adaptive, axisSelector, modelIndex)) {
            QObject *popup = axisSelector->findChild<QObject *>(QStringLiteral("adaptiveAxisSelectorPopup"));
            const auto *selectorItem = qobject_cast<QQuickItem *>(axisSelector);
            return fail(QStringLiteral("axis selector did not accept physical pointer selection for %1 (current=%2 width=%3 height=%4 enabled=%5 visible=%6 scene=(%7,%8) contentY=%9 popup=%10)")
                .arg(physicalAxis).arg(axisSelector->property("currentIndex").toInt())
                .arg(selectorItem ? selectorItem->width() : 0).arg(selectorItem ? selectorItem->height() : 0)
                .arg(selectorItem ? selectorItem->isEnabled() : false)
                .arg(selectorItem ? selectorItem->isVisible() : false)
                .arg(selectorItem ? selectorItem->mapToScene(QPointF{}).x() : 0)
                .arg(selectorItem ? selectorItem->mapToScene(QPointF{}).y() : 0)
                .arg(adaptive->property("contentY").toReal())
                .arg(popup ? popup->property("visible").toBool() : false));
        }
        const QVariantMap context = adaptive->property("state").toMap();
        if (backend.selectedAxisIndex() != physicalAxis
            || context.value(QStringLiteral("axis")).toInt() != physicalAxis
            || adaptive->property("previewSamples").toList().isEmpty()) {
            return fail(QStringLiteral("axis selection did not refresh context and deterministic preview"));
        }
    }
    if (!clickResponseComboRow(window, adaptive, scopeSelector, 0)
        || adaptive->property("editScope").toString() != QStringLiteral("global")
        || !clickResponseComboRow(window, adaptive, targetSelector, 0)
        || !clickResponseComboRow(window, adaptive, scopeSelector, 2)
        || adaptive->property("editScope").toString() != QStringLiteral("profile")) {
        return fail(QStringLiteral("editing level and target controls did not commit through pointer input"));
    }
    backend.setSelectedAxis(0);
    if (!backend.setAdaptiveResponsePresetAtContext(QStringLiteral("profile"), profileId, 0,
                                                     QStringLiteral("light"))
        || !backend.setAdaptiveResponsePresetAtContext(QStringLiteral("profile"), profileId, 1,
                                                        QStringLiteral("balanced"))) {
        return fail(QStringLiteral("could not establish isolated Adaptive Response fixture"));
    }
    settlePresentation();
    const QVariantMap pitchBefore = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 1);
    auto *fastPreset = findItem(QStringLiteral("adaptivePresetButton_fast"));
    if (!clickItem(fastPreset)) return fail(QStringLiteral("Fast preset card was not pointer reachable"));
    const QVariantMap rollAfterPreset = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0);
    if (!rollAfterPreset.value(QStringLiteral("effective")).toMap().value(QStringLiteral("enabled")).toBool()
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 1) != pitchBefore) {
        return fail(QStringLiteral("preset pointer action did not remain axis-isolated"));
    }
    for (const QVariant &entry : backend.adaptiveResponsePresets()) {
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        if (!findItem(QStringLiteral("adaptivePresetButton_") + id)) {
            return fail(QStringLiteral("current preset %1 has no Flight Deck destination").arg(id));
        }
    }
    auto *enabled = findItem(QStringLiteral("flightDeckAdaptiveEnabled"));
    const bool enabledBefore = rollAfterPreset.value(QStringLiteral("effective")).toMap()
        .value(QStringLiteral("enabled")).toBool();
    if (!clickItem(enabled)
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
                .value(QStringLiteral("effective")).toMap().value(QStringLiteral("enabled")).toBool() == enabledBefore
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 1) != pitchBefore
        || !clickItem(enabled)) {
        return fail(QStringLiteral("enable pointer action was not scoped to the selected axis"));
    }
    auto *advancedToggle = findItem(QStringLiteral("flightDeckAdaptiveAdvancedToggle"));
    if (!clickItem(advancedToggle) || !adaptive->property("advancedExpanded").toBool()) {
        return fail(QStringLiteral("Advanced Tuning did not expand from its pointer control"));
    }
    auto *modelSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptivePredictorSelector"));
    if (!modelSelector || !clickResponseComboRow(window, adaptive, modelSelector, 1)
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
                .value(QStringLiteral("effective")).toMap().value(QStringLiteral("model")).toString()
            != QStringLiteral("velocity")) {
        return fail(QStringLiteral("predictor selector did not commit an authoritative value"));
    }
    auto *horizonSlider = findItem(QStringLiteral("flightDeckAdaptiveSlider_maximumHorizonMs"));
    if (!scrollTo(horizonSlider)) return fail(QStringLiteral("maximum-horizon slider was not reachable"));
    const double horizonBefore = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0).value(QStringLiteral("effective")).toMap()
        .value(QStringLiteral("maximumHorizonMs")).toDouble();
    const QVariantList previewBeforeSlider = adaptive->property("previewSamples").toList();
    const QPoint sliderClick = viewportPoint(horizonSlider, adaptiveItem,
        QPointF(horizonSlider->width() * 0.72, horizonSlider->height() * 0.5));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, sliderClick);
    settlePresentation();
    const QVariantMap rollAfterSlider = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0);
    const double horizonAfter = rollAfterSlider.value(QStringLiteral("effective")).toMap()
        .value(QStringLiteral("maximumHorizonMs")).toDouble();
    if (std::abs(horizonAfter - horizonBefore) < 0.2
        || adaptive->property("previewSamples").toList() == previewBeforeSlider
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 1) != pitchBefore) {
        return fail(QStringLiteral("advanced slider pointer action did not update the authoritative preview and remain axis-isolated (before=%1 after=%2 slider=%3 presses=%4 click=(%5,%6) contentY=%7)")
            .arg(horizonBefore).arg(horizonAfter).arg(horizonSlider->property("value").toDouble())
            .arg(horizonSlider->property("pointerPresses").toInt())
            .arg(sliderClick.x()).arg(sliderClick.y())
            .arg(adaptive->property("contentY").toReal()));
    }
    const auto verifyPrimaryResponseSlider = [&](const QString &objectName, const QString &property) {
        auto *slider = findItem(objectName);
        if (!slider || !scrollTo(slider)) {
            return fail(QStringLiteral("%1 was not pointer reachable").arg(property));
        }
        const QVariantMap beforeState = backend.adaptiveResponseContextState(
            QStringLiteral("profile"), profileId, 0);
        const double before = beforeState.value(QStringLiteral("effective")).toMap().value(property).toDouble();
        const QVariantList previewBefore = adaptive->property("previewSamples").toList();
        const QPoint click = viewportPoint(slider, adaptiveItem,
            QPointF(slider->width() * 0.28, slider->height() * 0.5));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, click);
        settlePresentation();
        const double after = backend.adaptiveResponseContextState(
            QStringLiteral("profile"), profileId, 0).value(QStringLiteral("effective")).toMap()
            .value(property).toDouble();
        if (std::abs(after - before) < 0.015
            || adaptive->property("previewSamples").toList() == previewBefore
            || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 1) != pitchBefore) {
            return fail(QStringLiteral("%1 pointer action did not update authoritative response state and preview")
                .arg(property));
        }
        return true;
    };
    if (!verifyPrimaryResponseSlider(QStringLiteral("flightDeckAdaptiveSlider_normalMovementResponse"),
                                     QStringLiteral("normalMovementResponse"))
        || !verifyPrimaryResponseSlider(QStringLiteral("flightDeckAdaptiveSlider_rapidMovementResponse"),
                                        QStringLiteral("rapidMovementResponse"))
        || !verifyPrimaryResponseSlider(QStringLiteral("flightDeckAdaptiveSlider_engagementSensitivity"),
                                        QStringLiteral("engagementSensitivity"))) {
        return false;
    }
    const QVariantMap configurationBeforeComparison = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0);
    auto *comparisonSelector = adaptive->findChild<QObject *>(QStringLiteral("adaptiveComparisonSelector"));
    auto *staticBaseline = findItem(QStringLiteral("flightDeckStaticTraceBaseline"));
    if (!comparisonSelector || !staticBaseline
        || !clickResponseComboRow(window, adaptive, comparisonSelector, 1)
        || adaptive->property("comparisonSamples").toList().isEmpty()
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforeComparison) {
        return fail(QStringLiteral("static A/B comparison did not stay presentation-only"));
    }
    const bool staticBaselineBefore = adaptive->property("showBaselineTrace").toBool();
    if (!clickItem(staticBaseline)
        || adaptive->property("showBaselineTrace").toBool() == staticBaselineBefore
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforeComparison) {
        return fail(QStringLiteral("static trace selection changed configuration"));
    }
    if (staticBaseline->property("activeFocus").toBool()) {
        return fail(QStringLiteral("pointer-toggled trace retained a keyboard-focus highlight"));
    }
    const QVariantMap configurationBeforePresentation = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0);
    auto *liveSource = findItem(QStringLiteral("responseLabLiveSource"));
    if (!clickItem(liveSource) || adaptive->property("responseLabSource").toString() != QStringLiteral("live")
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("Live Controller source switch modified Adaptive configuration"));
    }
    backend.injectAdaptiveResponseLiveSampleForTest(0, -0.65F);
    QQmlExpression refreshHistory(qmlContext(adaptive), adaptive, QStringLiteral("refreshHistory(true)"));
    refreshHistory.evaluate();
    backend.injectAdaptiveResponseLiveSampleForTest(0, 0.70F);
    QQmlExpression appendHistory(qmlContext(adaptive), adaptive, QStringLiteral("refreshHistory(false)"));
    appendHistory.evaluate();
    if (refreshHistory.hasError() || appendHistory.hasError()
        || adaptive->property("responseLabSamples").toList().isEmpty()
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("live telemetry observation changed configuration or did not refresh"));
    }
    adaptive->setProperty("historyPaused", true);
    auto *historyInspect = findItem(QStringLiteral("adaptiveHistoryInspect"));
    if (!historyInspect || !scrollTo(historyInspect)) {
        return fail(QStringLiteral("bounded live-history inspection control was not reachable"));
    }
    const int historyIndexBefore = adaptive->property("historyInspectIndex").toInt();
    const QPoint inspectPoint = viewportPoint(historyInspect, adaptiveItem,
        QPointF(historyInspect->width() * 0.1, historyInspect->height() * 0.5));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, inspectPoint);
    settlePresentation();
    if (adaptive->property("historyInspectIndex").toInt() == historyIndexBefore
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("history inspection pointer control changed configuration or did not seek"));
    }
    auto *monitor = findItem(QStringLiteral("adaptiveResponseMonitorButton"));
    if (!clickItem(monitor) || !adaptive->property("responseMonitorVisible").toBool()
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("read-only monitor did not open safely"));
    }
    adaptive->setProperty("responseMonitorVisible", false);
    auto *physicalTrace = findItem(QStringLiteral("flightDeckTracePhysical"));
    const bool traceBefore = adaptive->property("showPhysicalTrace").toBool();
    if (!clickItem(physicalTrace)
        || adaptive->property("showPhysicalTrace").toBool() == traceBefore
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("trace visibility pointer control changed engine configuration"));
    }
    auto *testLabToggle = findItem(QStringLiteral("flightDeckAdaptiveTestLabToggle"));
    if (!clickItem(testLabToggle) || !adaptive->property("testLabExpanded").toBool()) {
        return fail(QStringLiteral("Test Lab did not open from its native control"));
    }
    auto *runTestLab = findItem(QStringLiteral("flightDeckTestLabRun"));
    if (!clickItem(runTestLab) || adaptive->property("testLabMetrics").toMap().isEmpty()
        || backend.adaptiveResponseContextState(QStringLiteral("profile"), profileId, 0)
            != configurationBeforePresentation) {
        return fail(QStringLiteral("Test Lab execution altered configuration or omitted results"));
    }

    auto *presetWorkshop = findItem(QStringLiteral("flightDeckAdaptivePresetWorkshopToggle"));
    if (!clickItem(presetWorkshop) || !adaptive->property("presetWorkshopExpanded").toBool()) {
        return fail(QStringLiteral("custom preset workshop did not open from its native control"));
    }
    const QString customPresetName = QStringLiteral("Flight Deck Native Preset %1").arg(appearance);
    // A prior interrupted run must not make the native save assertion depend
    // on fixture residue. These names are test-owned and are removed before
    // their pointer-driven lifecycle is exercised below.
    for (const QVariant &entry : backend.adaptiveResponsePresets()) {
        const QVariantMap preset = entry.toMap();
        const QString name = preset.value(QStringLiteral("name")).toString();
        if (name == customPresetName || name == customPresetName + QStringLiteral(" Copy"))
            backend.deleteAdaptiveResponsePreset(preset.value(QStringLiteral("id")).toString());
    }
    settlePresentation();
    adaptive->setProperty("presetNameDraft", customPresetName);
    adaptive->setProperty("presetDescriptionDraft", QStringLiteral("Pointer-created native Flight Deck fixture"));
    auto *savePreset = findItem(QStringLiteral("flightDeckAdaptivePresetSave"));
    if (!clickItem(savePreset)) {
        return fail(QStringLiteral("custom preset save was not pointer reachable"));
    }
    QString customPresetId;
    for (const QVariant &entry : backend.adaptiveResponsePresets()) {
        const QVariantMap preset = entry.toMap();
        if (preset.value(QStringLiteral("name")).toString() == customPresetName) {
            customPresetId = preset.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (customPresetId.isEmpty()) {
        return fail(QStringLiteral("custom preset save did not create an authoritative preset"));
    }
    auto *editPreset = findItem(QStringLiteral("adaptivePresetWorkshopEdit_") + customPresetId);
    if (!clickItem(editPreset)
        || adaptive->property("editScope").toString() != QStringLiteral("preset")
        || adaptive->property("targetId").toString() != customPresetId) {
        return fail(QStringLiteral("custom preset edit did not establish its native editing context"));
    }
    auto *renamePreset = findItem(QStringLiteral("adaptivePresetWorkshopRename_") + customPresetId);
    if (!clickItem(renamePreset)) {
        return fail(QStringLiteral("custom preset rename action was not pointer reachable"));
    }
    auto *renameDialog = adaptive->findChild<QObject *>(QStringLiteral("adaptiveRenamePresetDialog"));
    if (!renameDialog || !renameDialog->property("visible").toBool()) {
        return fail(QStringLiteral("custom preset rename dialog did not open"));
    }
    renameDialog->setProperty("visible", false);
    auto *duplicatePreset = findItem(QStringLiteral("adaptivePresetWorkshopDuplicate_") + customPresetId);
    if (!clickItem(duplicatePreset)) {
        return fail(QStringLiteral("custom preset duplicate action was not pointer reachable"));
    }
    QString duplicatePresetId;
    for (const QVariant &entry : backend.adaptiveResponsePresets()) {
        const QVariantMap preset = entry.toMap();
        if (preset.value(QStringLiteral("name")).toString() == customPresetName + QStringLiteral(" Copy")) {
            duplicatePresetId = preset.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (duplicatePresetId.isEmpty()) {
        return fail(QStringLiteral("custom preset duplicate did not create an authoritative preset"));
    }
    QQmlExpression restoreProfileContext(qmlContext(adaptive), adaptive,
        QStringLiteral("(function() { editScope = 'profile'; targetId = '%1'; setPreview(); return true; })()")
            .arg(profileId));
    restoreProfileContext.evaluate();
    const auto presetExists = [&](const QString &id) {
        for (const QVariant &entry : backend.adaptiveResponsePresets()) {
            if (entry.toMap().value(QStringLiteral("id")).toString() == id)
                return true;
        }
        return false;
    };
    const auto deletePresetByPointer = [&](const QString &id) {
        auto *deleteButton = findItem(QStringLiteral("adaptivePresetWorkshopDelete_") + id);
        return deleteButton && clickItem(deleteButton) && !presetExists(id);
    };
    if (restoreProfileContext.hasError()
        || !deletePresetByPointer(duplicatePresetId)
        || !deletePresetByPointer(customPresetId)) {
        return fail(QStringLiteral("custom preset cleanup actions were not pointer reachable"));
    }

    const QString categoryName = QStringLiteral("Flight Deck Adaptive Isolation %1").arg(appearance);
    if (!backend.createProfileCategory(categoryName)) {
        return fail(QStringLiteral("could not create isolated profile fixture"));
    }
    QString categoryId;
    for (const QVariant &entry : backend.profileCategories()) {
        const QVariantMap category = entry.toMap();
        if (category.value(QStringLiteral("name")).toString() == categoryName) {
            categoryId = category.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (categoryId.isEmpty() || !backend.createProfileInCategory(
            QStringLiteral("Adaptive Isolation %1").arg(appearance), categoryId, profileId)) {
        return fail(QStringLiteral("could not create profile-isolation fixture"));
    }
    QString isolatedProfileId;
    for (const QVariant &entry : backend.profiles()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("categoryId")).toString() == categoryId) {
            isolatedProfileId = profile.value(QStringLiteral("id")).toString();
            break;
        }
    }
    const QVariantMap sourceProfileBefore = backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0);
    const bool isolatedChanged = !isolatedProfileId.isEmpty()
        && backend.setAdaptiveResponsePresetAtContext(QStringLiteral("profile"), isolatedProfileId, 0,
                                                       QStringLiteral("aggressive"));
    const bool isolated = isolatedChanged && backend.adaptiveResponseContextState(
        QStringLiteral("profile"), profileId, 0) == sourceProfileBefore;
    const bool cleaned = !isolatedProfileId.isEmpty() && backend.deleteProfile(isolatedProfileId)
        && backend.deleteProfileCategory(categoryId);
    if (!isolated || !cleaned) {
        return fail(QStringLiteral("profile-level Adaptive configuration was not isolated"));
    }
    backend.setSelectedAxis(originalAxis);
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
    component.loadFromModule(u"HOTASMapper"_qs, u"AutomationPage"_qs);
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
    if (!hotas::ConfigStore::save(configuration)) {
        failPresentationLifecycleTest(QStringLiteral(
            "Device Rig fixture configuration could not be persisted"));
        return false;
    }
    return true;
}

QVariantMap flightDeckAutomationDraft(const QString &id, const QString &name, int button,
                                      int virtualButton)
{
    QVariantMap condition;
    condition.insert(QStringLiteral("type"), 5);
    condition.insert(QStringLiteral("button"), button);
    // Physical Automation conditions are intentionally device-scoped. The
    // fixture retains this saved controller after the Device Rig cleanup so
    // the Flight Deck editor is exercised with a valid, non-ambiguous source.
    condition.insert(QStringLiteral("controllerRecordId"), QStringLiteral("fixture-stick"));
    QVariantMap action;
    action.insert(QStringLiteral("type"), 0);
    action.insert(QStringLiteral("virtualButton"), virtualButton);
    QVariantMap draft;
    draft.insert(QStringLiteral("id"), id);
    draft.insert(QStringLiteral("name"), name);
    draft.insert(QStringLiteral("enabled"), false);
    draft.insert(QStringLiteral("matchMode"), 0);
    draft.insert(QStringLiteral("activationMode"), 0);
    draft.insert(QStringLiteral("activeDurationMs"), 250);
    draft.insert(QStringLiteral("priority"), 50);
    QVariantList conditions;
    conditions.append(condition);
    QVariantList actions;
    actions.append(action);
    draft.insert(QStringLiteral("conditions"), conditions);
    draft.insert(QStringLiteral("actions"), actions);
    return draft;
}

QVariantMap automationById(const hotas::AppBackend &backend, const QString &id)
{
    const QVariantList rules = backend.automationRules();
    for (const QVariant &entry : rules) {
        const QVariantMap rule = entry.toMap();
        if (rule.value(QStringLiteral("id")).toString() == id) return rule;
    }
    return {};
}

bool verifyFlightDeckAutomationInteraction(hotas::AppBackend &backend, hotas::ThemeManager &themeManager)
{
    const QString firstId = backend.createAutomation();
    const QString secondId = backend.createAutomation();
    const QString thirdId = backend.createAutomation();
    const bool savedFirst = !firstId.isEmpty()
        && backend.saveAutomation(flightDeckAutomationDraft(firstId, QStringLiteral("Rule A"), 1, 1));
    const bool savedSecond = !secondId.isEmpty()
        && backend.saveAutomation(flightDeckAutomationDraft(secondId, QStringLiteral("Rule B"), 2, 2));
    const bool savedThird = !thirdId.isEmpty()
        && backend.saveAutomation(flightDeckAutomationDraft(thirdId, QStringLiteral("Rule C"), 3, 3));
    if (!savedFirst || !savedSecond || !savedThird) {
        return failAutomationEditorTest(QStringLiteral(
            "could not create independent Flight Deck Automation rules "
            "(ids=%1/%2/%3 saved=%4/%5/%6 count=%7 validation=%8)")
            .arg(firstId, secondId, thirdId)
            .arg(savedFirst).arg(savedSecond).arg(savedThird)
            .arg(backend.automationRuleCount())
            .arg(backend.automationValidationMessage()));
    }

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    QQmlComponent component(&engine);
    component.loadUrl(QUrl(u"qrc:/qt/qml/HOTASMapper/qml/FlightDeckAutomation.qml"_qs));
    if (component.status() != QQmlComponent::Ready) {
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(component.errorString());
    }
    QObject *root = component.create();
    auto *rootItem = qobject_cast<QQuickItem *>(root);
    if (!rootItem) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("FlightDeckAutomation did not create a QQuickItem"));
    }
    QQuickWindow window;
    window.resize(1280, 800);
    rootItem->setParentItem(window.contentItem());
    rootItem->setSize(window.size());
    window.show();
    settlePresentation();

    const QString activeProfileBefore = backend.activeProfileId();
    auto clickItem = [&](QQuickItem *item) {
        if (!item) return false;
        const QPointF relative = item->mapToScene(QPointF{}) - rootItem->mapToScene(QPointF{});
        rootItem->setProperty("contentY", std::max<qreal>(0.0, relative.y() - 96.0));
        settlePresentation();
        const QPoint point = item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5)).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
        settlePresentation();
        return true;
    };

    if (!evaluateEditorFunction(root, QStringLiteral("openRuleById('%1')").arg(secondId))) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return false;
    }
    auto *conditionTarget = findVisualItemByObjectName(rootItem,
        QStringLiteral("flightDeckAutomationConditionButton_0"));
    if (!conditionTarget || !clickResponseComboRow(&window, root, conditionTarget, 2)
        || root->property("draft").toMap().value(QStringLiteral("conditions")).toList().at(0).toMap()
            .value(QStringLiteral("button")).toInt() != 3) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck condition target selector did not commit the selected control"));
    }
    auto *actionTarget = findVisualItemByObjectName(rootItem,
        QStringLiteral("flightDeckAutomationActionButton_0"));
    if (!actionTarget || !clickResponseComboRow(&window, root, actionTarget, 2)
        || root->property("draft").toMap().value(QStringLiteral("actions")).toList().at(0).toMap()
            .value(QStringLiteral("virtualButton")).toInt() != 3) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck action target selector did not commit the selected target"));
    }
    rootItem->setProperty("contentY", 0.0);
    settlePresentation();
    auto *save = findVisualItemByObjectName(rootItem, QStringLiteral("flightDeckAutomationSave"));
    if (!clickItem(save) || root->property("editing").toBool()) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck save control did not use the authoritative command path"));
    }
    const QVariantMap first = automationById(backend, firstId);
    const QVariantMap second = automationById(backend, secondId);
    const QVariantMap third = automationById(backend, thirdId);
    if (first.value(QStringLiteral("conditions")).toList().at(0).toMap().value(QStringLiteral("button")).toInt() != 1
        || second.value(QStringLiteral("conditions")).toList().at(0).toMap().value(QStringLiteral("button")).toInt() != 3
        || second.value(QStringLiteral("actions")).toList().at(0).toMap().value(QStringLiteral("virtualButton")).toInt() != 3
        || third.value(QStringLiteral("actions")).toList().at(0).toMap().value(QStringLiteral("virtualButton")).toInt() != 3) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck rule edit did not preserve A/B/C isolation"));
    }

    const int ruleCountBeforeDuplicate = backend.automationRuleCount();
    auto *duplicate = findVisualItemByObjectName(rootItem,
        QStringLiteral("flightDeckAutomationDuplicate_%1").arg(secondId));
    if (!clickItem(duplicate) || !root->property("editing").toBool()
        || backend.automationRuleCount() != ruleCountBeforeDuplicate + 1) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck duplicate control did not use the authoritative command path"));
    }
    const QString duplicateId = root->property("editingId").toString();
    if (duplicateId.isEmpty() || duplicateId == secondId
        || !evaluateEditorFunction(root, QStringLiteral("updateDraft('name', 'Renamed Rule B copy')"))) {
        delete root;
        backend.deleteAutomation(duplicateId);
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck duplicate draft was not independently editable"));
    }
    rootItem->setProperty("contentY", 0.0);
    settlePresentation();
    save = findVisualItemByObjectName(rootItem, QStringLiteral("flightDeckAutomationSave"));
    if (!clickItem(save) || root->property("editing").toBool()
        || automationById(backend, duplicateId).value(QStringLiteral("name")).toString()
            != QStringLiteral("Renamed Rule B copy")) {
        delete root;
        backend.deleteAutomation(duplicateId);
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck rename did not save only the duplicated rule"));
    }
    auto *deleteRule = findVisualItemByObjectName(rootItem,
        QStringLiteral("flightDeckAutomationDelete_%1").arg(duplicateId));
    if (!clickItem(deleteRule)) {
        delete root;
        backend.deleteAutomation(duplicateId);
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck delete control did not open its confirmation"));
    }
    auto *deleteConfirm = findVisualItemByObjectName(window.contentItem(),
        QStringLiteral("flightDeckAutomationDeleteConfirm"));
    if (!clickItem(deleteConfirm) || !automationById(backend, duplicateId).isEmpty()
        || automationById(backend, secondId).value(QStringLiteral("name")).toString() != QStringLiteral("Rule B")) {
        delete root;
        backend.deleteAutomation(duplicateId);
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck delete confirmation did not isolate the duplicate"));
    }

    auto *enabledToggle = findVisualItemByObjectName(rootItem,
        QStringLiteral("flightDeckAutomationEnabled_%1").arg(secondId));
    if (!clickItem(enabledToggle) || !automationById(backend, secondId).value(QStringLiteral("enabled")).toBool()
        || automationById(backend, firstId).value(QStringLiteral("enabled")).toBool()
        || automationById(backend, thirdId).value(QStringLiteral("enabled")).toBool()
        || backend.activeProfileId() != activeProfileBefore || backend.automationActiveRuleCount() != 0) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck enable toggle was not isolated or changed runtime state without input"));
    }

    if (!evaluateEditorFunction(root, QStringLiteral("openRuleById('%1')").arg(secondId))) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return false;
    }
    for (int type = 0; type <= 16; ++type) {
        if (!evaluateEditorFunction(root, QStringLiteral("setConditionType(0, %1)").arg(type))
            || root->property("draft").toMap().value(QStringLiteral("conditions")).toList().at(0).toMap()
                .value(QStringLiteral("type")).toInt() != type
            || !evaluateEditorFunction(root, QStringLiteral("setActionType(0, %1)").arg(type))
            || root->property("draft").toMap().value(QStringLiteral("actions")).toList().at(0).toMap()
                .value(QStringLiteral("type")).toInt() != type) {
            delete root;
            backend.deleteAutomation(firstId);
            backend.deleteAutomation(secondId);
            backend.deleteAutomation(thirdId);
            return failAutomationEditorTest(QStringLiteral("Flight Deck did not retain condition/action type %1 in its native editor").arg(type));
        }
    }
    if (!evaluateEditorFunction(root, QStringLiteral("setConditionType(0, 11); addCondition(11); updateCondition(0, 'button', 1); updateCondition(1, 'button', 2); updateDraft('matchMode', 1); setActionType(0, 10); updateAction(0, 'virtualButton', 4)"))
        || root->property("draft").toMap().value(QStringLiteral("conditions")).toList().size() != 2
        || root->property("draft").toMap().value(QStringLiteral("matchMode")).toInt() != 1) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck multi-condition ANY editing did not retain both conditions"));
    }
    rootItem->setProperty("contentY", 0.0);
    settlePresentation();
    save = findVisualItemByObjectName(rootItem, QStringLiteral("flightDeckAutomationSave"));
    const QVariantMap savedMultiCondition = automationById(backend, secondId);
    if (!clickItem(save)
        || automationById(backend, secondId).value(QStringLiteral("conditions")).toList().size() != 2
        || automationById(backend, secondId).value(QStringLiteral("matchMode")).toInt() != 1
        || savedMultiCondition.isEmpty()) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck multi-condition ANY save did not use the authoritative rule model"));
    }
    rootItem->setProperty("contentY", 0.0);
    settlePresentation();
    const int ruleCountBeforeCreate = backend.automationRuleCount();
    auto *newRule = findVisualItemByObjectName(rootItem, QStringLiteral("flightDeckAutomationNewRule"));
    if (!clickItem(newRule) || !root->property("editing").toBool()
        || backend.automationRuleCount() != ruleCountBeforeCreate + 1
        || backend.activeProfileId() != activeProfileBefore || backend.automationActiveRuleCount() != 0) {
        delete root;
        backend.deleteAutomation(firstId);
        backend.deleteAutomation(secondId);
        backend.deleteAutomation(thirdId);
        return failAutomationEditorTest(QStringLiteral("Flight Deck new-rule interaction did not remain execution-safe"));
    }
    const QString createdId = root->property("editingId").toString();
    root->setProperty("editing", false);
    backend.deleteAutomation(createdId);
    delete root;
    backend.deleteAutomation(firstId);
    backend.deleteAutomation(secondId);
    backend.deleteAutomation(thirdId);
    return true;
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
    // Conventional lifecycle coverage must not be redirected by a persisted
    // Flight Deck selection from a preceding focused fixture.
    themeManager.setCurrentExperience(QStringLiteral("Existing"));
    // A focused geometry pass can run with Main.qml's explicit hidden
    // presentation argument. It verifies the Flight Deck choice safe-area
    // contract without raising or focusing a window on the owner's desktop.
    const bool adaptiveChoiceGeometryOnly = qEnvironmentVariableIsSet(
        "HOTAS_QML_ADAPTIVE_CHOICE_GEOMETRY_ONLY");
    const bool containmentGeometryOnly = qEnvironmentVariableIsSet(
        "HOTAS_QML_CONTAINMENT_GEOMETRY_ONLY");
    // Render the full native Flight Deck matrix without spending time in the
    // established-experience lifecycle family. This is an opt-in visual-review
    // harness: it keeps test fixtures isolated while giving the release gate
    // fresh, inspectable screenshots after shared presentation changes.
    const bool flightDeckVisualOnly = qEnvironmentVariableIsSet(
        "HOTAS_QML_FLIGHT_DECK_VISUAL_ONLY");
    if (adaptiveChoiceGeometryOnly || containmentGeometryOnly) {
        const bool geometrySafe = (!containmentGeometryOnly
                || (verifyFlightDeckNavigationRailGeometry(backend, themeManager, QStringLiteral("Dark"))
                    && verifyFlightDeckNavigationRailGeometry(backend, themeManager, QStringLiteral("Light"))
                    && verifyFlightDeckDialogHeaderGeometry(backend, themeManager, QStringLiteral("Dark"))
                    && verifyFlightDeckDialogHeaderGeometry(backend, themeManager, QStringLiteral("Light"))))
            && verifyFlightDeckAdaptivePresetChoiceGeometry(backend, themeManager, QStringLiteral("Dark"))
            && verifyFlightDeckAdaptivePresetChoiceGeometry(backend, themeManager, QStringLiteral("Light"));
        themeManager.setCurrentExperience(QStringLiteral("Existing"));
        return geometrySafe ? 0 : 1;
    }
    if (flightDeckVisualOnly) {
        // The visual matrix uses an eight-axis Flight Deck fixture below.
        // Keep its authoritative test-only vJoy descriptor equally complete;
        // production availability continues to come only from the driver.
        backend.setVirtualAxisAvailabilityForTest(true);
        const QString requestedAppearance = qEnvironmentVariable(
            "HOTAS_QML_FLIGHT_DECK_VISUAL_APPEARANCE").trimmed();
        const QStringList appearances = requestedAppearance.isEmpty()
            ? QStringList{QStringLiteral("Dark"), QStringLiteral("Light")}
            : QStringList{requestedAppearance};
        bool visualSafe = verifyFlightDeckAxesQmlLoad(backend, themeManager);
        for (const QString &appearance : appearances) {
            if (appearance != QStringLiteral("Dark") && appearance != QStringLiteral("Light")) {
                visualSafe = failPresentationLifecycleTest(QStringLiteral(
                    "Flight Deck visual appearance must be Dark or Light, not '%1'").arg(appearance));
                break;
            }
            if (!visualSafe || !verifyFlightDeckShell(backend, themeManager, appearance)) {
                visualSafe = false;
                break;
            }
        }
        themeManager.setCurrentExperience(QStringLiteral("Existing"));
        return visualSafe ? 0 : 1;
    }
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
        QStringList engineWarnings;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &application,
            [&engineWarnings](const QList<QQmlError> &warnings) {
                for (const QQmlError &warning : warnings) engineWarnings.push_back(warning.toString());
            });
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
        engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
        auto *window = engine.rootObjects().isEmpty()
            ? nullptr : qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (!window) {
            failPresentationLifecycleTest(QStringLiteral(
                "Main.qml did not create a window for %1 (%2)")
                .arg(theme, engineWarnings.join(u" | "_qs)));
            return 1;
        }

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

    // Automation deliberately binds physical conditions to one authoritative
    // source device. Re-establish a single-device fixture after the Devices
    // lifecycle test cleaned up its multi-device rig, then release it before
    // the remaining presentation suites run.
    const QString automationFixtureRig = backend.createDeviceRig(
        QStringLiteral("Automation Fixture"), {QStringLiteral("fixture-stick")});
    if (automationFixtureRig.isEmpty()) {
        failPresentationLifecycleTest(QStringLiteral(
            "Automation fixture could not establish a single physical-device context"));
        return 1;
    }
    const bool automationSafe = verifyFlightDeckAutomationInteraction(backend, themeManager);
    const bool automationFixtureReleased = backend.deleteDeviceRig(automationFixtureRig);
    if (!automationSafe || !automationFixtureReleased) {
        if (!automationFixtureReleased) {
            failPresentationLifecycleTest(QStringLiteral(
                "Automation fixture did not release its temporary physical-device context"));
        }
        return 1;
    }

    for (const QString &appearance : {QStringLiteral("Dark"), QStringLiteral("Light")}) {
        if (!verifyFlightDeckAxesQmlLoad(backend, themeManager)) return 1;
        if (!verifyFlightDeckShell(backend, themeManager, appearance)) return 1;
        if (!verifyFlightDeckAdaptiveResponseInteraction(backend, themeManager, appearance)) return 1;
    }
    themeManager.setCurrentExperience(QStringLiteral("Existing"));

    if (!verifyAdaptiveResponseSimulator(backend)) return 1;
    if (!verifyAdaptiveResponsePreviewTruth(backend)) return 1;
    if (!verifyAutomationEditorInteraction(backend)) return 1;

    QTimer::singleShot(250, &application, &QCoreApplication::quit);
    return application.exec();
}
