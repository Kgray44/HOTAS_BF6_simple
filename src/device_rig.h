#pragma once

#include "mapping_types.h"

#include <array>
#include <QList>

namespace hotas {

enum class DeviceRigHealth {
    Ready,
    Partial,
    Offline,
    Disabled,
    Conflict,
    NeedsAttention,
};

struct DeviceRigStatus {
    QString rigId;
    DeviceRigHealth health = DeviceRigHealth::Offline;
    QStringList connectedMemberIds;
    QStringList missingRequiredMemberIds;
    QStringList missingOptionalMemberIds;
    // Required and optional identity/setup failures deliberately remain
    // separate.  Optional hardware is a capability reduction, not a reason
    // to reject an otherwise-safe automatic configuration.
    QStringList ambiguousRequiredMemberIds;
    QStringList ambiguousOptionalMemberIds;
    QStringList needsVerificationRequiredMemberIds;
    QStringList needsVerificationOptionalMemberIds;
    // Compatibility summaries for older presentation consumers.  Resolver
    // eligibility must use the required-only fields above.
    QStringList ambiguousMemberIds;
    // A durable member can be safely part of a rig before it has completed
    // setup.  Keep that recoverable readiness state separate from identity
    // ambiguity or an offline device.
    QStringList needsVerificationMemberIds;
    bool complete = false;
};

struct DeviceRigActivationDecision {
    QString rigId;
    bool ambiguous = false;
    bool retainedActiveRig = false;
    QString reason;
};

// The runtime uses these compact, pre-resolved records.  QString identity is
// retained only for acquisition/reconnect boundaries; a report traverses its
// already-selected session and output index without any registry lookup.
struct CompiledDeviceRigOutput {
    int vjoyDeviceId = 0;
    std::array<bool, kVirtualAxisSlotCount> requiredAxes{};
    int requiredButtons = 0;
    int requiredContinuousPovs = 0;
    int requiredDiscretePovs = 0;
};

struct CompiledDeviceRigMember {
    QString controllerRecordId;
    QString directInputId;
    QString displayName;
    QString outputLayoutId;
    bool required = true;
    int outputIndex = -1;
    std::array<bool, kPhysicalAxisCount> fixedAxes{};
    RuntimeMappingConfiguration mapping;
    NativePovBindings nativePovBindings;
    std::shared_ptr<const struct CompiledAutomationSet> automation;
};

struct CompiledDeviceRigRuntime {
    std::array<CompiledDeviceRigMember, kMaximumDeviceRigMembers> members{};
    std::array<CompiledDeviceRigOutput, kMaximumDeviceRigOutputs> outputs{};
    int memberCount = 0;
    int outputCount = 0;
    bool valid = false;
    // A structural error is built at a configuration boundary and surfaced by
    // Devices/verification.  It is intentionally never synthesized from a
    // report callback.
    QString issue;
};

// This is the fixed, worker-facing outcome of one DirectInput session after a
// poll/read boundary.  Keeping DIERR_INPUTLOST and DIERR_NOTACQUIRED distinct
// makes disconnect behaviour deterministic in tests while both safely remove
// that member from the current output composition.
enum class DeviceRigInputSessionState {
    Connected,
    InputLost,
    NotAcquired,
    Disconnected,
};

struct DeviceRigRuntimeAvailability {
    int connectedMemberCount = 0;
    bool anyConnected = false;
    bool allRequiredConnected = true;
    bool allMembersLost = false;
    bool mappingAllowed = false;
};

QString deviceRigHealthKey(DeviceRigHealth health);
QString deviceRigHealthLabel(DeviceRigHealth health);

// Low-frequency inventory policy.  This is deliberately separate from
// MappingWorker: it may compare durable device identities and allocate result
// strings, but no report ever calls it.
DeviceRigStatus evaluateDeviceRig(const DeviceRig &rig,
                                  const std::vector<SavedControllerRecord> &records,
                                  const QList<DiscoveredController> &inventory);
QList<DeviceRigStatus> evaluateDeviceRigs(const MapperConfiguration &configuration,
                                          const QList<DiscoveredController> &inventory);
DeviceRigActivationDecision chooseDeviceRigActivation(const MapperConfiguration &configuration,
                                                      const QList<DeviceRigStatus> &statuses);
// An enabled active Device Rig is an explicit runtime topology even when it
// has only one physical member and one virtual output. That simple shape
// still owns controller-qualified mappings and canonical Signal Flow routes.
bool hasActiveDeviceRigRuntime(const MapperConfiguration &configuration);
CompiledDeviceRigRuntime compileDeviceRigRuntime(const MapperConfiguration &configuration,
                                                 const QString &rigId,
                                                 const QString &profileId = {});
// Worker policy seam: evaluates a post-poll set of member states without
// DirectInput, vJoy, GUI work, or allocations.  MappingWorker uses this exact
// gate after every bounded input pass; unit tests can therefore reproduce
// input-loss, reconnect, required/optional, and all-members-lost outcomes.
DeviceRigRuntimeAvailability evaluateDeviceRigRuntimeAvailability(
    const CompiledDeviceRigRuntime &runtime,
    const std::array<DeviceRigInputSessionState, kMaximumDeviceRigMembers> &inputStates,
    bool mappingRequested, bool outputsReady, DeviceRigDisconnectBehavior disconnectBehavior);

} // namespace hotas
