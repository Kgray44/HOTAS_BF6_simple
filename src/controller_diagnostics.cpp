#include "controller_diagnostics.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>

#include <algorithm>

namespace hotas {

namespace {

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

} // namespace

bool isControllerDiagnosticsAvailable(ControllerReadinessState state)
{
    return state == ControllerReadinessState::Attention || state == ControllerReadinessState::Failed
        || state == ControllerReadinessState::NeedsChanges || state == ControllerReadinessState::Cancelled;
}

QString sanitizeControllerDiagnosticText(QString text, const QStringList &privatePaths)
{
    QStringList paths = privatePaths;
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    for (const QString &path : paths) {
        if (!path.trimmed().isEmpty()) text.replace(path, QStringLiteral("<LOCAL_PATH>"), Qt::CaseInsensitive);
    }
    // Redact the complete per-user path, not merely the username, from tool
    // output. The selected HID remains only in the marked technical section.
    text.replace(QRegularExpression(QStringLiteral("(?i)[A-Z]:\\\\Users\\\\[^\\r\\n]*")),
                 QStringLiteral("<USER_HOME>"));
    return text.trimmed();
}

QString buildControllerDiagnostics(const ControllerDiagnosticsSnapshot &snapshot)
{
    const int advertisedAxes = static_cast<int>(std::count(snapshot.physical.axes.cbegin(),
        snapshot.physical.axes.cend(), true));
    int activeAxes = 0;
    int fixedAxes = 0;
    for (const ControllerAxisDiagnostic &axis : snapshot.axes) {
        activeAxes += axis.activity == PhysicalAxisActivity::Active ? 1 : 0;
        fixedAxes += axis.activity == PhysicalAxisActivity::Fixed ? 1 : 0;
    }
    QStringList lines{
        QStringLiteral("HOTAS BF6 Diagnostics"),
        QStringLiteral("Version: v%1").arg(snapshot.version),
        QStringLiteral("Timestamp: %1").arg(snapshot.timestamp),
        QStringLiteral("Windows: %1").arg(sanitizeControllerDiagnosticText(
            snapshot.windowsVersion, snapshot.privatePaths)),
        QString{},
        QStringLiteral("PHYSICAL CONTROLLER"),
        QStringLiteral("Name: %1").arg(sanitizeControllerDiagnosticText(
            snapshot.physical.name, snapshot.privatePaths)),
        QStringLiteral("Connected: %1").arg(yesNo(snapshot.physical.connected)),
        QStringLiteral("Axes: %1 advertised  %2 active  %3 fixed  Buttons: %4  POVs: %5")
            .arg(advertisedAxes).arg(activeAxes).arg(fixedAxes)
            .arg(snapshot.physical.buttons).arg(snapshot.physical.povs),
        QStringLiteral("Input reports received: %1").arg(yesNo(snapshot.physical.inputReportsReceived)),
        QStringLiteral("Reacquisition attempted: %1").arg(yesNo(snapshot.repair.physicalReacquisitionAttempted)),
        QStringLiteral("Reacquisition result: %1").arg(snapshot.repair.physicalReacquisitionSucceeded
            ? QStringLiteral("success") : QStringLiteral("not confirmed")),
        QString{},
        QStringLiteral("VJOY"),
        QStringLiteral("Installed: %1  Device ID: %2  Driver ready: %3")
            .arg(yesNo(snapshot.vjoy.installed)).arg(snapshot.vjoy.deviceId)
            .arg(yesNo(snapshot.vjoy.driverReady)),
        QStringLiteral("Configured axes: %1  Buttons: %2  Continuous POV: %3  Discrete POV: %4")
            .arg(std::count(snapshot.vjoy.axes.cbegin(), snapshot.vjoy.axes.cend(), true))
            .arg(snapshot.vjoy.buttons).arg(snapshot.vjoy.continuousPovs).arg(snapshot.vjoy.discretePovs),
        QString{},
        QStringLiteral("ACTIVE PROFILE / OUTPUT"),
        QStringLiteral("Profile: %1").arg(snapshot.activeProfileName),
        QStringLiteral("HIDHIDE"),
        QStringLiteral("Installed: %1  Service ready: %2  Cloaking: %3")
            .arg(yesNo(snapshot.hidhide.installed)).arg(yesNo(snapshot.hidhide.serviceReady))
            .arg(snapshot.hidhide.cloaked ? QStringLiteral("on") : QStringLiteral("off")),
        QStringLiteral("HOTAS BF6 allowlisted: %1  Selected controller hidden: %2")
            .arg(yesNo(snapshot.hidhide.mapperAllowlisted))
            .arg(yesNo(snapshot.hidhide.selectedControllerHidden)),
        QStringLiteral("Post-repair controller reacquisition: %1").arg(snapshot.repair.physicalReacquisitionSucceeded
            ? QStringLiteral("confirmed") : QStringLiteral("not confirmed")),
        QString{},
        QStringLiteral("AUTOMATIC REPAIR"),
        QStringLiteral("Result: %1").arg(sanitizeControllerDiagnosticText(
            snapshot.repair.message, snapshot.privatePaths)),
    };
    for (const VirtualOutputDiagnostic &output : snapshot.virtualOutputs) {
        const QString visibility = !output.visibilityManaged ? QStringLiteral("not managed")
            : output.hidden ? QStringLiteral("hidden") : QStringLiteral("visible");
        lines.append(QStringLiteral("Output: %1  vJoy %2  %3  %4%5")
            .arg(output.name).arg(output.deviceId).arg(output.descriptor).arg(visibility)
            .arg(output.active ? QStringLiteral("  active") : QString{}));
    }
    for (const AutomaticRepairOperationResult &operation : snapshot.repair.operations) {
        lines.append(QStringLiteral("%1: %2 (exit code %3%4)")
            .arg(sanitizeControllerDiagnosticText(operation.operationName, snapshot.privatePaths),
                 operation.succeeded ? QStringLiteral("success") : QStringLiteral("failed"),
                 QString::number(operation.exitCode),
                 operation.rollback ? QStringLiteral(", rollback") : QString{}));
        const QString output = sanitizeControllerDiagnosticText(operation.output, snapshot.privatePaths).left(800);
        const QString error = sanitizeControllerDiagnosticText(operation.errorOutput.isEmpty()
            ? operation.message : operation.errorOutput, snapshot.privatePaths).left(800);
        if (!output.isEmpty()) lines.append(QStringLiteral("  stdout: %1").arg(output));
        if (!error.isEmpty()) lines.append(QStringLiteral("  stderr: %1").arg(error));
    }
    lines.append(QString{});
    lines.append(QStringLiteral("ROLLBACK"));
    lines.append(QStringLiteral("Attempted: %1  Result: %2  Physical reports after rollback: %3")
        .arg(yesNo(snapshot.repair.rollbackAttempted),
             snapshot.repair.rollbackSucceeded ? QStringLiteral("success") : QStringLiteral("not confirmed"),
             yesNo(snapshot.repair.physicalReportsReceivedAfterRollback)));
    lines.append(QString{});
    lines.append(QStringLiteral("CALIBRATION"));
    for (const ControllerAxisDiagnostic &axis : snapshot.axes) {
        lines.append(QStringLiteral("%1 RAW MIN: %2  RAW NEUTRAL: %3  RAW MAX: %4  CALIBRATED: %5  MAPPED: %6  ACTIVITY: %7")
            .arg(axis.label).arg(axis.rawMinimum, 0, 'f', 3).arg(axis.rawNeutral, 0, 'f', 3)
            .arg(axis.rawMaximum, 0, 'f', 3).arg(axis.calibratedInput, 0, 'f', 3)
            .arg(axis.mappedOutput, 0, 'f', 3).arg(physicalAxisActivityLabel(axis.activity)));
    }
    lines.append(QString{});
    lines.append(QStringLiteral("ADVANCED / TECHNICAL"));
    lines.append(QStringLiteral("Selected HID instance: %1").arg(snapshot.selectedHidInstance));
    return lines.join(u'\n');
}

bool copyControllerDiagnosticsToClipboard(const ControllerDiagnosticsSnapshot &snapshot)
{
    if (!QGuiApplication::instance() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(buildControllerDiagnostics(snapshot));
    return true;
}

} // namespace hotas
