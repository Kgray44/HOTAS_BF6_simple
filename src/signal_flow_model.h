#pragma once

#include "mapping_types.h"

namespace hotas {

// Signal Flow is a projection of MapperConfiguration's established mapping
// fields. These helpers own only durable graph identity lifecycle and
// presentation metadata reconciliation; they never participate in the report
// path or create a second routing payload.
QString signalFlowDeterministicId(const QString &key, std::uint32_t generation);

QString signalFlowRouteIdentityKey(const ControllerProfile &profile,
                                   const QString &controllerRecordId,
                                   const QString &kind, int index,
                                   int subIndex = -1);
QString signalFlowAxisProcessorIdentityKey(const ControllerProfile &profile,
                                           const QString &controllerRecordId,
                                           int axis, const QString &kind);

const SignalFlowIdentityRecord *findSignalFlowIdentity(
    const std::vector<SignalFlowIdentityRecord> &identities, const QString &key);

// Reconciles durable IDs with the canonical profile/device mapping fields.
// A route mutation retains its source-slot identity. A route deleted in one
// persisted configuration and later recreated receives a new generation/ID,
// so stale selection, undo, and diagnostic references cannot attach to an
// unrelated replacement.
void reconcileSignalFlowState(MapperConfiguration *configuration);

} // namespace hotas
