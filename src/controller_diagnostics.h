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
    // Raw-acquisition evidence is assembled by AppBackend on demand. These
    // values are never read, formatted, or allocated by MappingWorker.
    QString nativeName;
    QString semanticGuid;
    int reportedOffset = -1;
    QString resolutionSource;
    QString resolutionConfidence;
    bool metadataContradiction = false;
    bool manualOverride = false;
    QString manualOverrideMode;
    int runtimeSource = -1;
    int nativeMinimum = 0;
    int nativeMaximum = 0;
    int observedMinimum = 0;
    int observedMaximum = 0;
    int rawValue = 0;
    bool movementObserved = false;
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
    QString vjoyAcquireAttempt;
    QString vjoyLastStatusTransition;
    // An on-demand technical record of the last exact verification durability
    // check. Ordinary device cards intentionally do not display these values.
    QString verificationDurabilityReadback;
    // Known local paths are redacted before text reaches the clipboard.
    QStringList privatePaths;
};

bool isControllerDiagnosticsAvailable(ControllerReadinessState state);
QString sanitizeControllerDiagnosticText(QString text, const QStringList &privatePaths = {});
QString buildControllerDiagnostics(const ControllerDiagnosticsSnapshot &snapshot);
bool copyControllerDiagnosticsToClipboard(const ControllerDiagnosticsSnapshot &snapshot);

} // namespace hotas
