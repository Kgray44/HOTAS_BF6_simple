#include "device_rig.h"

#include "automation_engine.h"
#include "controller_manager.h"

#include <algorithm>

namespace hotas {
namespace {

const DeviceRigStatus *statusFor(const QList<DeviceRigStatus> &statuses, const QString &rigId)
{
    const auto found = std::find_if(statuses.cbegin(), statuses.cend(), [&rigId](const DeviceRigStatus &status) {
        return status.rigId == rigId;
    });
    return found == statuses.cend() ? nullptr : &*found;
}

const DeviceRig *rigFor(const MapperConfiguration &configuration, const QString &rigId)
{
    const auto found = std::find_if(configuration.deviceRigs.cbegin(), configuration.deviceRigs.cend(),
                                    [&rigId](const DeviceRig &rig) { return rig.id == rigId; });
    return found == configuration.deviceRigs.cend() ? nullptr : &*found;
}

bool isEligible(const DeviceRig &rig, const DeviceRigStatus &status)
{
    // This legacy selector is retained for explicit setup/manual contexts.
    // Category/Profile automatic activation has its own visible ordering, but
    // it must share the same required-vs-optional readiness rule.
    const bool ambiguousRequired = !status.ambiguousRequiredMemberIds.isEmpty()
        || (!status.ambiguousMemberIds.isEmpty()
            && status.ambiguousOptionalMemberIds.isEmpty());
    const bool unverifiedRequired = !status.needsVerificationRequiredMemberIds.isEmpty()
        || (!status.needsVerificationMemberIds.isEmpty()
            && status.needsVerificationOptionalMemberIds.isEmpty());
    return rig.enabled && rig.autoActivate && status.complete
        && !ambiguousRequired && !unverifiedRequired;
}

const SavedControllerRecord *recordFor(const MapperConfiguration &configuration, const QString &id)
{
    const auto found = std::find_if(configuration.savedControllers.cbegin(),
        configuration.savedControllers.cend(), [&id](const SavedControllerRecord &record) {
            return record.id == id;
        });
    return found == configuration.savedControllers.cend() ? nullptr : &*found;
}

} // namespace

QString deviceRigHealthKey(DeviceRigHealth health)
{
    switch (health) {
    case DeviceRigHealth::Ready: return u"ready"_qs;
    case DeviceRigHealth::Partial: return u"partial"_qs;
    case DeviceRigHealth::Offline: return u"offline"_qs;
    case DeviceRigHealth::Disabled: return u"disabled"_qs;
    case DeviceRigHealth::Conflict: return u"conflict"_qs;
    case DeviceRigHealth::NeedsAttention: return u"needs-attention"_qs;
    }
    return u"needs-attention"_qs;
}

QString deviceRigHealthLabel(DeviceRigHealth health)
{
    switch (health) {
    case DeviceRigHealth::Ready: return u"Ready"_qs;
    case DeviceRigHealth::Partial: return u"Partial"_qs;
    case DeviceRigHealth::Offline: return u"Offline"_qs;
    case DeviceRigHealth::Disabled: return u"Disabled"_qs;
    case DeviceRigHealth::Conflict: return u"Conflict"_qs;
    case DeviceRigHealth::NeedsAttention: return u"Needs Attention"_qs;
    }
    return u"Needs Attention"_qs;
}

DeviceRigStatus evaluateDeviceRig(const DeviceRig &rig,
                                  const std::vector<SavedControllerRecord> &records,
                                  const QList<DiscoveredController> &inventory)
{
    DeviceRigStatus result;
    result.rigId = rig.id;
    if (!rig.enabled) {
        result.health = DeviceRigHealth::Disabled;
        return result;
    }
    if (rig.members.empty() || rig.members.size() > kMaximumDeviceRigMembers
        || rig.outputs.empty() || rig.outputs.size() > kMaximumDeviceRigOutputs) {
        result.health = DeviceRigHealth::NeedsAttention;
        return result;
    }

    const int enabledOutputCount = static_cast<int>(std::count_if(rig.outputs.cbegin(), rig.outputs.cend(),
        [](const DeviceRigOutputTarget &output) { return output.enabled; }));
    const int enabledMemberCount = static_cast<int>(std::count_if(rig.members.cbegin(), rig.members.cend(),
        [](const DeviceRigMember &member) { return member.enabled; }));
    if (enabledMemberCount == 0 || enabledOutputCount == 0) {
        // A saved rig may be temporarily edited, but an empty effective
        // topology must never look Ready or become an auto-activation target.
        result.health = DeviceRigHealth::NeedsAttention;
        return result;
    }

    bool anyConnected = false;
    for (const DeviceRigMember &member : rig.members) {
        if (!member.enabled) continue;
        const auto record = std::find_if(records.cbegin(), records.cend(), [&member](const SavedControllerRecord &item) {
            return item.id == member.controllerRecordId;
        });
        if (record == records.cend()) {
            if (member.required) result.missingRequiredMemberIds.append(member.controllerRecordId);
            else result.missingOptionalMemberIds.append(member.controllerRecordId);
            continue;
        }
        bool connected = false;
        bool ambiguous = false;
        for (const DiscoveredController &candidate : inventory) {
            if (!candidate.connected || candidate.virtualDevice) continue;
            const ControllerMatch match = ControllerManager::match(candidate, records);
            if (match.recordId == member.controllerRecordId) {
                if (match.ambiguous) ambiguous = true;
                else connected = true;
            }
        }
        if (ambiguous) {
            result.ambiguousMemberIds.append(member.controllerRecordId);
            if (member.required) result.ambiguousRequiredMemberIds.append(member.controllerRecordId);
            else result.ambiguousOptionalMemberIds.append(member.controllerRecordId);
        } else if (connected) {
            result.connectedMemberIds.append(member.controllerRecordId);
            anyConnected = true;
            if (record->lastVerified.isEmpty()) {
                result.needsVerificationMemberIds.append(member.controllerRecordId);
                if (member.required) result.needsVerificationRequiredMemberIds.append(member.controllerRecordId);
                else result.needsVerificationOptionalMemberIds.append(member.controllerRecordId);
            }
        } else if (member.required) {
            result.missingRequiredMemberIds.append(member.controllerRecordId);
        } else {
            result.missingOptionalMemberIds.append(member.controllerRecordId);
        }
    }

    result.complete = result.missingRequiredMemberIds.isEmpty()
        && result.ambiguousRequiredMemberIds.isEmpty()
        && result.needsVerificationRequiredMemberIds.isEmpty();
    if (!result.ambiguousRequiredMemberIds.isEmpty()) result.health = DeviceRigHealth::Conflict;
    else if (!result.needsVerificationRequiredMemberIds.isEmpty()) {
        result.health = DeviceRigHealth::NeedsAttention;
    }
    else if (result.complete) result.health = result.missingOptionalMemberIds.isEmpty()
        && result.ambiguousOptionalMemberIds.isEmpty()
        && result.needsVerificationOptionalMemberIds.isEmpty()
        ? DeviceRigHealth::Ready : DeviceRigHealth::Partial;
    else result.health = anyConnected ? DeviceRigHealth::Partial : DeviceRigHealth::Offline;
    return result;
}

QList<DeviceRigStatus> evaluateDeviceRigs(const MapperConfiguration &configuration,
                                          const QList<DiscoveredController> &inventory)
{
    QList<DeviceRigStatus> result;
    result.reserve(static_cast<qsizetype>(configuration.deviceRigs.size()));
    for (const DeviceRig &rig : configuration.deviceRigs) {
        result.append(evaluateDeviceRig(rig, configuration.savedControllers, inventory));
    }
    return result;
}

DeviceRigActivationDecision chooseDeviceRigActivation(const MapperConfiguration &configuration,
                                                      const QList<DeviceRigStatus> &statuses)
{
    DeviceRigActivationDecision decision;
    if (const DeviceRig *active = rigFor(configuration, configuration.activeDeviceRigId)) {
        if (const DeviceRigStatus *status = statusFor(statuses, active->id); status && status->complete
            && active->enabled) {
            decision.rigId = active->id;
            decision.retainedActiveRig = true;
            decision.reason = u"The active Device Rig remains ready."_qs;
            return decision;
        }
        if (active->disconnectBehavior == DeviceRigDisconnectBehavior::UseFallback) {
            if (const DeviceRig *fallback = rigFor(configuration, active->fallbackRigId)) {
                if (const DeviceRigStatus *status = statusFor(statuses, fallback->id);
                    status && isEligible(*fallback, *status)) {
                    decision.rigId = fallback->id;
                    decision.reason = u"The active rig is unavailable; its configured fallback is ready."_qs;
                    return decision;
                }
            }
        }
    }

    struct Candidate { const DeviceRig *rig = nullptr; const DeviceRigStatus *status = nullptr; };
    std::vector<Candidate> candidates;
    for (const DeviceRig &rig : configuration.deviceRigs) {
        const DeviceRigStatus *status = statusFor(statuses, rig.id);
        if (status && isEligible(rig, *status)) candidates.push_back({&rig, status});
    }
    if (candidates.empty()) {
        decision.reason = u"No complete auto-activatable Device Rig is available."_qs;
        return decision;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
        if (left.rig->isDefault != right.rig->isDefault) return left.rig->isDefault;
        if (left.rig->activationPriority != right.rig->activationPriority) {
            return left.rig->activationPriority > right.rig->activationPriority;
        }
        return left.rig->presentationOrder < right.rig->presentationOrder;
    });
    const Candidate winner = candidates.front();
    if (candidates.size() > 1 && candidates[1].rig->isDefault == winner.rig->isDefault
        && candidates[1].rig->activationPriority == winner.rig->activationPriority) {
        decision.ambiguous = true;
        decision.reason = u"Multiple complete Device Rigs have the same default and priority. Choose one in Devices."_qs;
        return decision;
    }
    decision.rigId = winner.rig->id;
    decision.reason = u"A complete Device Rig was selected by the saved default and priority policy."_qs;
    return decision;
}

CompiledDeviceRigRuntime compileDeviceRigRuntime(const MapperConfiguration &configuration,
                                                 const QString &rigId,
                                                 const QString &profileId)
{
    CompiledDeviceRigRuntime runtime;
    const DeviceRig *rig = findDeviceRig(configuration, rigId);
    if (!rig || !rig->enabled) {
        runtime.issue = u"No enabled Device Rig is selected."_qs;
        return runtime;
    }
    if (rig->members.empty() || rig->members.size() > kMaximumDeviceRigMembers
        || rig->outputs.empty() || rig->outputs.size() > kMaximumDeviceRigOutputs) {
        runtime.issue = u"The Device Rig has an invalid number of members or outputs."_qs;
        return runtime;
    }
    const ControllerProfile *profile = findProfile(configuration,
        profileId.isEmpty() ? configuration.activeProfileId : profileId);
    if (!profile) {
        runtime.issue = u"The active profile is unavailable."_qs;
        return runtime;
    }
    if (!profile->deviceRigId.isEmpty() && profile->deviceRigId != rig->id) {
        runtime.issue = u"The active profile belongs to another Device Rig."_qs;
        return runtime;
    }
    const RuntimeProfileCache profileCache = compileRuntimeProfileCache(configuration);

    for (int index = 0; index < static_cast<int>(rig->outputs.size()); ++index) {
        const DeviceRigOutputTarget &configured = rig->outputs[static_cast<size_t>(index)];
        if (!configured.enabled) continue;
        const VirtualOutputLayout *layout = findOutputLayout(configuration, configured.outputLayoutId);
        if (!layout || runtime.outputCount >= kMaximumDeviceRigOutputs) {
            runtime.issue = u"A Device Rig output layout is unavailable."_qs;
            return runtime;
        }
        CompiledDeviceRigOutput &output = runtime.outputs[static_cast<size_t>(runtime.outputCount++)];
        output.vjoyDeviceId = layout->requirements.deviceId;
        output.requiredAxes = layout->requirements.axes;
        output.requiredButtons = layout->requirements.buttons;
        output.requiredContinuousPovs = layout->requirements.continuousPovs;
        output.requiredDiscretePovs = layout->requirements.discretePovs;
    }
    if (runtime.outputCount == 0) {
        runtime.issue = u"The Device Rig has no enabled output."_qs;
        return runtime;
    }

    // Runtime array indexes describe enabled outputs, while configuration
    // indexes can include disabled rows. Resolve by layout id so a disabled
    // first output can never shift a physical member onto the wrong vJoy.
    const auto compiledOutputFor = [&runtime, rig](const QString &layoutId) {
        int compiledIndex = 0;
        for (const DeviceRigOutputTarget &candidate : rig->outputs) {
            if (!candidate.enabled) continue;
            if (candidate.outputLayoutId == layoutId) return compiledIndex;
            ++compiledIndex;
        }
        return -1;
    };

    for (const DeviceRigMember &configured : rig->members) {
        if (!configured.enabled) continue;
        if (runtime.memberCount >= kMaximumDeviceRigMembers) {
            runtime.issue = u"The Device Rig has too many enabled inputs."_qs;
            return runtime;
        }
        const SavedControllerRecord *record = recordFor(configuration, configured.controllerRecordId);
        if (!record) {
            runtime.issue = u"A Device Rig member is no longer saved."_qs;
            return runtime;
        }
        if (record->lastVerified.isEmpty()) {
            if (configured.required) {
                runtime.issue = u"A required Device Rig input needs setup verification."_qs;
                return runtime;
            }
            // An optional, known-but-unverified device is retained durably but
            // cannot emit routes until it has completed the same setup path.
            continue;
        }
        const DeviceProfileMapping *deviceMapping = findDeviceProfileMapping(*profile, record->id);
        if (!deviceMapping || !deviceMapping->enabled) {
            runtime.issue = u"The active profile has no enabled mapping for a Device Rig member."_qs;
            return runtime;
        }
        QString outputLayoutId = configured.preferredOutputLayoutId;
        if (outputLayoutId.isEmpty()) outputLayoutId = profile->outputLayoutId;
        if (compiledOutputFor(outputLayoutId) < 0) {
            // A one-output rig makes the safe/simple relationship implicit;
            // a multi-output rig requires an explicit member destination.
            if (runtime.outputCount == 1) {
                for (const DeviceRigOutputTarget &candidate : rig->outputs) {
                    if (candidate.enabled) {
                        outputLayoutId = candidate.outputLayoutId;
                        break;
                    }
                }
            } else {
                runtime.issue = u"A Device Rig member has no enabled assigned output."_qs;
                return runtime;
            }
        }
        CompiledDeviceRigMember &member = runtime.members[static_cast<size_t>(runtime.memberCount++)];
        member.controllerRecordId = record->id;
        member.directInputId = record->lastDirectInputId;
        member.displayName = record->displayName;
        member.outputLayoutId = outputLayoutId;
        member.required = configured.required;
        member.outputIndex = compiledOutputFor(outputLayoutId);
        member.mapping = compileDeviceProfileMapping(configuration, *profile, *deviceMapping, record);
        member.nativePovBindings = deviceMapping->nativePovBindings;
        QString automationIssue;
        member.automation = compileDeviceAutomationSet(configuration, profileCache, record->id,
            outputLayoutId, rig->members.size() > 1, &automationIssue);
        if (!automationIssue.isEmpty()) {
            runtime.issue = automationIssue;
            return runtime;
        }
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            member.fixedAxes[static_cast<size_t>(axis)] = record->axisActivity[static_cast<size_t>(axis)]
                == PhysicalAxisActivity::Fixed;
        }
    }
    if (runtime.memberCount == 0) {
        runtime.issue = u"The Device Rig has no enabled physical inputs."_qs;
        return runtime;
    }

    // A shared output between physical members would otherwise depend on poll
    // order. Each member may still contain its own explicit Signal Flow
    // fan-out/mixer topology; only a cross-member claim is rejected here.
    std::array<std::array<bool, kVirtualAxisSlotCount>, kMaximumDeviceRigOutputs> claimedAxes{};
    std::array<std::array<bool, kMaximumPhysicalPovs + 1>, kMaximumDeviceRigOutputs>
        claimedContinuousPovs{};
    std::array<std::array<bool, kMaximumPhysicalPovs + 1>, kMaximumDeviceRigOutputs>
        claimedDiscretePovs{};
    for (int memberIndex = 0; memberIndex < runtime.memberCount; ++memberIndex) {
        const CompiledDeviceRigMember &member = runtime.members[static_cast<size_t>(memberIndex)];
        std::array<bool, kVirtualAxisSlotCount> memberAxisClaims{};
        if (member.mapping.signalFlowTopologyCompiled) {
            const int routeCount = std::clamp(member.mapping.signalFlowAxisRouteCount, 0,
                                              kMaximumRuntimeSignalFlowAxisRoutes);
            for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
                const int target = member.mapping.signalFlowAxisRoutes[static_cast<size_t>(routeIndex)]
                    .destinationAxis;
                if (target > 0 && target < kVirtualAxisSlotCount) {
                    memberAxisClaims[static_cast<size_t>(target)] = true;
                }
            }
        } else {
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const int target = static_cast<int>(member.mapping.axes[static_cast<size_t>(axis)].profile.target);
                if (target > 0 && target < kVirtualAxisSlotCount) {
                    memberAxisClaims[static_cast<size_t>(target)] = true;
                }
            }
        }
        for (int target = 1; target < kVirtualAxisSlotCount; ++target) {
            if (!memberAxisClaims[static_cast<size_t>(target)]) continue;
            std::array<bool, kVirtualAxisSlotCount> &claims = claimedAxes[static_cast<size_t>(member.outputIndex)];
            if (claims[static_cast<size_t>(target)]) {
                runtime.issue = u"Two physical axis routes target the same virtual axis."_qs;
                return runtime;
            }
            claims[static_cast<size_t>(target)] = true;
        }
        const auto claimNativePov = [&runtime, &member, &claimedContinuousPovs, &claimedDiscretePovs]
            (NativePovTargetType type, int target) {
                if (target < 1 || target > kMaximumPhysicalPovs
                    || (type != NativePovTargetType::Continuous && type != NativePovTargetType::Discrete)) {
                    runtime.issue = u"A native POV route has an unsupported target index."_qs;
                    return false;
                }
                auto &claims = type == NativePovTargetType::Continuous
                    ? claimedContinuousPovs[static_cast<size_t>(member.outputIndex)]
                    : claimedDiscretePovs[static_cast<size_t>(member.outputIndex)];
                if (claims[static_cast<size_t>(target)]) {
                    runtime.issue = u"Two physical POV routes target the same virtual POV."_qs;
                    return false;
                }
                claims[static_cast<size_t>(target)] = true;
                return true;
            };
        if (member.mapping.signalFlowTopologyCompiled) {
            const int routeCount = std::clamp(member.mapping.signalFlowNativePovRouteCount, 0,
                                              kMaximumRuntimeSignalFlowNativePovRoutes);
            for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
                const RuntimeSignalFlowNativePovRoute &route = member.mapping.signalFlowNativePovRoutes[
                    static_cast<size_t>(routeIndex)];
                if (!claimNativePov(static_cast<NativePovTargetType>(route.destinationType),
                                    route.destinationIndex)) {
                    return runtime;
                }
            }
        } else {
            for (const NativePovBinding &binding : member.nativePovBindings) {
                if (!binding.enabled || binding.targetType == NativePovTargetType::Disabled) continue;
                if (!claimNativePov(binding.targetType, binding.targetIndex)) return runtime;
            }
        }
    }
    runtime.valid = true;
    return runtime;
}

DeviceRigRuntimeAvailability evaluateDeviceRigRuntimeAvailability(
    const CompiledDeviceRigRuntime &runtime,
    const std::array<DeviceRigInputSessionState, kMaximumDeviceRigMembers> &inputStates,
    bool mappingRequested, bool outputsReady, DeviceRigDisconnectBehavior disconnectBehavior)
{
    DeviceRigRuntimeAvailability availability;
    for (int index = 0; index < runtime.memberCount; ++index) {
        const bool connected = inputStates[static_cast<size_t>(index)]
            == DeviceRigInputSessionState::Connected;
        if (connected) {
            ++availability.connectedMemberCount;
            availability.anyConnected = true;
        } else if (runtime.members[static_cast<size_t>(index)].required) {
            availability.allRequiredConnected = false;
        }
    }
    availability.allMembersLost = runtime.memberCount > 0 && !availability.anyConnected;
    const bool deactivateForRequiredLoss = disconnectBehavior
        == DeviceRigDisconnectBehavior::DeactivateRig;
    availability.mappingAllowed = mappingRequested && availability.anyConnected && outputsReady
        && (!deactivateForRequiredLoss || availability.allRequiredConnected);
    return availability;
}

} // namespace hotas
