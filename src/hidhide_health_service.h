#pragma once

#include "hidhide_core/hidhide_read_only_protocol.h"

#include <QDateTime>
#include <QList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>

namespace hotas {

enum class HidHideHealthState { Ready, Checking, Degraded, RepairAvailable, UserActionRequired, DoctorRecommended, RestartRequired, Unknown };
enum class HidHideHealthSeverity { Info, Warning, Error };
enum class HidHideRepairability { None, FixNow, GuidedRepair, UserActionRequired, DoctorRecommended, DoctorRequired };
enum class HidHideHealthScanDepth { Essential, Full };

// This is a control-plane projection of one managed Rig member.  The ID is
// durable; names are presentation only and are never used to identify a HidHide
// entry.
struct HidHidePhysicalDeviceHealth final {
    QString controllerRecordId;
    QString friendlyName;
    bool required = true;
    bool connected = false;
    QStringList exactCurrentHidInstances;
    bool identityResolved = false;
    bool hiddenStateKnown = false;
    bool expectedHidden = true;
    bool actualHidden = false;
    HidHideHealthState state = HidHideHealthState::Unknown;
    HidHideRepairability repairability = HidHideRepairability::None;
    QString technicalDetails;

    QVariantMap toVariantMap() const;
};

struct HidHideHealthContext final {
    quint64 sessionId = 0;
    QString contextKey;
    QString deviceRigId;
    QString selectedControllerId;
    QString mapperExecutable;
    bool installed = false;
    bool cliAvailable = false;
    bool serviceReady = false;
    bool cloakKnown = false;
    bool cloaked = false;
    bool inverseKnown = false;
    bool inverse = false;
    bool mapperAllowlistKnown = false;
    bool mapperAllowlisted = false;
    bool hiddenDeviceListKnown = false;
    bool selectedControllerResolved = false;
    bool selectedControllerHidden = false;
    bool inspectionComplete = false;
    bool inspectionTimedOut = false;
    bool pendingReadinessRecovery = false;
    bool managedVirtualOutputInspectionKnown = false;
    bool managedVirtualOutputHidden = false;
    // Full-check-only package evidence. Empty values deliberately mean
    // uninspected/unavailable, never "matching".
    bool packageEvidenceInspected = false;
    QString clientVersion;
    QString cliVersion;
    QString loadedDriverVersion;
    QString driverStorePackageVersion;
    bool pendingPackageRestartKnown = false;
    bool pendingPackageRestart = false;
    QList<HidHidePhysicalDeviceHealth> physicalDevices;
    QStringList expectedPhysicalInstances;
    QStringList hiddenDeviceInstances;
    QStringList managedVirtualOutputInstances;
};

struct HidHideHealthDimension final {
    QString id;
    QString title;
    HidHideHealthState state = HidHideHealthState::Unknown;
    HidHideHealthSeverity severity = HidHideHealthSeverity::Info;
    QString shortSummary;
    QString explanation;
    QString technicalDetails;
    QStringList checkIds;
    HidHideRepairability repairability = HidHideRepairability::None;

    QVariantMap toVariantMap() const;
};

struct HidHideHealthFinding final {
    QString id;
    QString code;
    QString dimensionId;
    HidHideHealthSeverity severity = HidHideHealthSeverity::Info;
    QString title;
    QString explanation;
    QString whyItMatters;
    HidHideRepairability repairability = HidHideRepairability::None;
    QString technicalDetails;
    QStringList affectedObjectIds;

    QVariantMap toVariantMap() const;
};

struct HidHideHealthActivity final {
    QDateTime timestamp;
    QString event;
    QString detail;

    QVariantMap toVariantMap() const;
};

struct HidHideHealthSnapshot final {
    quint64 sessionId = 0;
    QString contextKey;
    HidHideHealthScanDepth scanDepth = HidHideHealthScanDepth::Essential;
    HidHideHealthState overallState = HidHideHealthState::Unknown;
    QDateTime lastChecked;
    bool inProgress = false;
    bool cancelled = false;
    int checksCompleted = 0;
    int checksTotal = 0;
    int percentComplete = 0;
    QString currentStage;
    QString currentCheckId;
    QString currentCheckTitle;
    QList<HidHidePhysicalDeviceHealth> physicalDevices;
    QList<HidHideHealthDimension> dimensions;
    QList<HidHideReadObservation> checks;
    QList<HidHideHealthFinding> findings;
    QList<HidHideHealthActivity> activity;

    QVariantMap toVariantMap() const;
};

QString hidHideHealthStateLabel(HidHideHealthState state);
QString hidHideHealthSeverityLabel(HidHideHealthSeverity severity);
QString hidHideRepairabilityLabel(HidHideRepairability repairability);
QString hidHideHealthScanDepthLabel(HidHideHealthScanDepth depth);

// This service has no mutation API. The only injectable work is a narrow
// read-only direct-protocol provider so tests can deterministically model
// independent GET failures without real HidHide hardware or drivers.
class HidHideHealthService final {
public:
    using ReadOnlyProbe = std::function<QList<HidHideReadObservation>(
        std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback)>;
    using ProgressCallback = std::function<void(const HidHideHealthSnapshot &)>;

    explicit HidHideHealthService(ReadOnlyProbe probe = HidHideReadOnlyProtocol::inspect);

    HidHideHealthSnapshot inspect(const HidHideHealthContext &context, HidHideHealthScanDepth depth,
                                  std::atomic_bool *cancelled = nullptr,
                                  ProgressCallback progress = {}) const;
    static HidHideHealthSnapshot checkingSnapshot(const HidHideHealthContext &context,
                                                   HidHideHealthScanDepth depth);
    static QVariantList appIssues(const HidHideHealthSnapshot &snapshot);
    static QVariantMap sanitizedEvidence(const HidHideHealthSnapshot &snapshot);

private:
    ReadOnlyProbe m_probe;
};

} // namespace hotas
