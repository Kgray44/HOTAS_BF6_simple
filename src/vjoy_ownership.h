#pragma once

#include <QString>
#include <QtGlobal>

namespace hotas {

// vJoy's public VjdStat values.  Keeping the raw integer alongside the
// classified state is intentional: control-plane diagnostics must show the
// driver evidence before any HOTAS interpretation is applied.
constexpr int kVJoyStatusOwn = 0;
constexpr int kVJoyStatusFree = 1;
constexpr int kVJoyStatusBusy = 2;
constexpr int kVJoyStatusMissing = 3;
constexpr int kVJoyStatusUnknown = 4;

enum class VJoyOwnershipState {
    OwnedByCurrentProcess,
    Free,
    BusyOtherProcess,
    StaleOwnership,
    Missing,
    Unknown,
};

enum class VJoyAcquireAction {
    AlreadyOwned,
    Acquire,
    ExternalBusy,
    StaleOwnership,
    Unavailable,
};

struct VJoyOwnerProcessEvidence {
    bool queryAttempted = false;
    bool livenessKnown = false;
    bool live = false;
    QString name;
    QString path;
    QString diagnostic;
};

struct VJoyOwnershipEvidence {
    int deviceId = 1;
    int rawStatus = kVJoyStatusUnknown;
    bool ownerPidAvailable = false;
    quint64 ownerPid = 0;
    quint64 hotasProcessId = 0;
    VJoyOwnerProcessEvidence ownerProcess;
    VJoyOwnershipState state = VJoyOwnershipState::Unknown;
    QString diagnostic;
};

QString vjoyRawStatusName(int rawStatus);
QString vjoyOwnershipStateName(VJoyOwnershipState state);
QString vjoyOwnershipOwnerLabel(const VJoyOwnershipEvidence &evidence);

VJoyOwnerProcessEvidence inspectVJoyOwnerProcess(quint64 pid);
VJoyOwnershipEvidence classifyVJoyOwnership(int deviceId, int rawStatus, bool ownerPidAvailable,
                                             quint64 ownerPid, quint64 hotasProcessId,
                                             const VJoyOwnerProcessEvidence &ownerProcess = {});
VJoyOwnershipEvidence queryVJoyOwnership(int deviceId, quint64 hotasProcessId = 0);
VJoyAcquireAction vjoyAcquireActionFor(const VJoyOwnershipEvidence &evidence);

} // namespace hotas
