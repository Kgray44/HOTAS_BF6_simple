#pragma once

#include "mapping_types.h"

namespace hotas {

// Signal Flow owns the canonical route topology. Focused axis/button/POV
// fields remain the authoritative settings surface for their existing
// transforms, and are projected to/from topology at configuration boundaries.
// These helpers are control-plane only; the worker consumes a bounded compiled
// table and never traverses this object graph per report.
QString signalFlowDeterministicId(const QString &key, std::uint32_t generation);

QString signalFlowRouteIdentityKey(const ControllerProfile &profile,
                                   const QString &controllerRecordId,
                                   const QString &kind, int index,
                                   int subIndex = -1);
QString signalFlowAxisProcessorIdentityKey(const ControllerProfile &profile,
                                            const QString &controllerRecordId,
                                            int axis, const QString &kind);
QString signalFlowFanoutRouteIdentityKey(const ControllerProfile &profile,
                                         const QString &controllerRecordId,
                                         const QString &kind, int sourceIndex, int sourceSubIndex,
                                         SignalFlowPortKind destinationKind, int destinationIndex,
                                         int destinationSubIndex = -1);
QString signalFlowMixerIdentityKey(const ControllerProfile &profile,
                                   const QString &controllerRecordId, int destinationAxis);
QString signalFlowSharedProcessorIdentityKey(const ControllerProfile &profile,
                                             const QString &controllerRecordId,
                                             const QString &kind, int ownerAxis);

const SignalFlowIdentityRecord *findSignalFlowIdentity(
    const std::vector<SignalFlowIdentityRecord> &identities, const QString &key);
const SignalFlowRoute *findSignalFlowRouteById(const SignalFlowState &state, const QString &id);
SignalFlowRoute *findSignalFlowRouteById(SignalFlowState *state, const QString &id);
bool signalFlowRouteMatchesScope(const SignalFlowRoute &route, const ControllerProfile &profile,
                                 const QString &controllerRecordId);

// A focused editor may edit the owner of a shared processor.  Propagate that
// one canonical setting at the commit boundary so every linked channel stays
// in lockstep.  A non-owner edit deliberately returns false; reconciliation
// then represents that divergence by separating the changed channel.
bool signalFlowPropagateSharedProcessorSettings(MapperConfiguration *configuration,
                                                const QString &profileId,
                                                const QString &controllerRecordId,
                                                const QString &kind, int ownerAxis);

// Reconciles canonical topology, durable IDs, processor references, and
// presentation metadata. Schema-26/older configurations are deterministically
// imported from compatibility fields exactly once without changing semantics.
void reconcileSignalFlowState(MapperConfiguration *configuration);

// Focused editors still mutate their established compatibility fields. Import
// those deliberate changes into topology at the next normal configuration
// commit, then rebuild a one-target focused-editor projection. A focused
// source rewrite intentionally replaces every route from that source because
// legacy editors cannot express fan-out; graph-originated edits call the
// projection function directly and preserve native fan-out.
void synchronizeSignalFlowTopologyFromFocusedEditors(MapperConfiguration *configuration);
void projectSignalFlowTopologyToFocusedEditors(MapperConfiguration *configuration);

} // namespace hotas
