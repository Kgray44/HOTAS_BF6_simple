#pragma once

#include "controller_readiness.h"

#include <QList>
#include <QStringList>

namespace hotas {

// This snapshot is assembled only by the UI/control plane. It owns no mapper
// handle and is never constructed from a DirectInput report.
struct ControllerAxisDiagnostic {
    QString label;
    float rawMinimum = -1.0F;
    float rawNeutral = 0.0F;
    float rawMaximum = 1.0F;
    float calibratedInput = 0.0F;
    float mappedOutput = 0.0F;
    PhysicalAxisActivity activity = PhysicalAxisActivity::Unknown;
};

struct VirtualOutputDiagnostic {
    QString name;
    QString descriptor;
    int deviceId = 0;
    bool active = false;
    bool visibilityManaged = false;
    bool hidden = false;
};

struct ControllerDiagnosticsSnapshot {
    QString version;
    QString timestamp;
    QString windowsVersion;
    PhysicalControllerCapabilities physical;
    VJoyCapabilities vjoy;
    HidHideCapabilities hidhide;
    AutomaticRepairResult repair;
    QList<ControllerAxisDiagnostic> axes;
    QList<VirtualOutputDiagnostic> virtualOutputs;
    QString activeProfileName;
    QString selectedHidInstance;
    // Known local paths are redacted before text reaches the clipboard.
    QStringList privatePaths;
};

bool isControllerDiagnosticsAvailable(ControllerReadinessState state);
QString sanitizeControllerDiagnosticText(QString text, const QStringList &privatePaths = {});
QString buildControllerDiagnostics(const ControllerDiagnosticsSnapshot &snapshot);
bool copyControllerDiagnosticsToClipboard(const ControllerDiagnosticsSnapshot &snapshot);

} // namespace hotas
