#include "direct_input_axis.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace hotas {

int physicalAxisIndexForDirectInputOffset(DWORD offset)
{
    if (offset == DIJOFS_X) return static_cast<int>(PhysicalAxis::X);
    if (offset == DIJOFS_Y) return static_cast<int>(PhysicalAxis::Y);
    if (offset == DIJOFS_Z) return static_cast<int>(PhysicalAxis::Z);
    if (offset == DIJOFS_RX) return static_cast<int>(PhysicalAxis::Rx);
    if (offset == DIJOFS_RY) return static_cast<int>(PhysicalAxis::Ry);
    if (offset == DIJOFS_RZ) return static_cast<int>(PhysicalAxis::Rz);
    if (offset == DIJOFS_SLIDER(0)) return static_cast<int>(PhysicalAxis::Slider0);
    if (offset == DIJOFS_SLIDER(1)) return static_cast<int>(PhysicalAxis::Slider1);
    return -1;
}

int physicalAxisIndexForDirectInputSemanticGuid(const GUID &guid)
{
    if (IsEqualGUID(guid, GUID_XAxis)) return static_cast<int>(PhysicalAxis::X);
    if (IsEqualGUID(guid, GUID_YAxis)) return static_cast<int>(PhysicalAxis::Y);
    if (IsEqualGUID(guid, GUID_ZAxis)) return static_cast<int>(PhysicalAxis::Z);
    if (IsEqualGUID(guid, GUID_RxAxis)) return static_cast<int>(PhysicalAxis::Rx);
    if (IsEqualGUID(guid, GUID_RyAxis)) return static_cast<int>(PhysicalAxis::Ry);
    if (IsEqualGUID(guid, GUID_RzAxis)) return static_cast<int>(PhysicalAxis::Rz);
    // GUID_Slider intentionally has no authority here: DirectInput does not
    // distinguish Slider 0 from Slider 1 through that shared semantic GUID.
    // Their safe identity remains the explicit formatted/offset slot.
    return -1;
}

int resolveUniqueDirectInputAxisSlot(
    NativeAxisDescriptor *candidate,
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> *assigned,
    std::array<bool, kPhysicalAxisCount> *available)
{
    if (!candidate || !candidate->present || candidate->canonicalAxis < 0
        || candidate->canonicalAxis >= kPhysicalAxisCount) {
        return -1;
    }
    const int semanticSlot = candidate->canonicalAxis;
    if (!assigned || candidate->resolutionSource != AxisResolutionSource::StandardSemanticGuid) {
        return semanticSlot;
    }

    // Some controllers reuse one standard semantic GUID (notably GUID_ZAxis)
    // for several independently stored controls. Preserve the object whose
    // reported field actually occupies the semantic slot and relocate each
    // duplicate to its own fixed storage slot. This is a collision rule; it
    // does not make reported offsets redefine a unique canonical identity.
    NativeAxisDescriptor &incumbent = (*assigned)[static_cast<size_t>(semanticSlot)];
    if (!incumbent.present
        || incumbent.resolutionSource != AxisResolutionSource::StandardSemanticGuid) {
        return semanticSlot;
    }
    const int candidateOffsetSlot = physicalAxisIndexForDirectInputOffset(candidate->directInputOffset);
    if (candidateOffsetSlot == semanticSlot) {
        const int incumbentOffsetSlot = physicalAxisIndexForDirectInputOffset(incumbent.directInputOffset);
        if (incumbentOffsetSlot >= 0 && incumbentOffsetSlot < kPhysicalAxisCount
            && incumbentOffsetSlot != semanticSlot
            && !(*assigned)[static_cast<size_t>(incumbentOffsetSlot)].present) {
            NativeAxisDescriptor relocated = incumbent;
            relocated.canonicalAxis = incumbentOffsetSlot;
            relocated.formattedSource = incumbentOffsetSlot;
            relocated.resolutionSource = AxisResolutionSource::ReportedOffset;
            relocated.resolutionConfidence = AxisResolutionConfidence::Medium;
            relocated.metadataContradiction = true;
            relocated.formattedSourceEvidence = AxisFormattedSourceEvidence::ReportedOffsetFallback;
            relocated.formattedSourceVerified = true;
            (*assigned)[static_cast<size_t>(incumbentOffsetSlot)] = std::move(relocated);
            if (available) (*available)[static_cast<size_t>(incumbentOffsetSlot)] = true;
            // The caller will now store the current semantic-slot owner.
            // Clearing the incumbent prevents the generic semantic tie-break
            // from retaining the earlier duplicate in the canonical slot.
            incumbent = {};
        }
        return semanticSlot;
    }
    if (candidateOffsetSlot < 0 || candidateOffsetSlot >= kPhysicalAxisCount
        || (*assigned)[static_cast<size_t>(candidateOffsetSlot)].present) {
        return semanticSlot;
    }
    candidate->canonicalAxis = candidateOffsetSlot;
    candidate->formattedSource = candidateOffsetSlot;
    candidate->resolutionSource = AxisResolutionSource::ReportedOffset;
    candidate->resolutionConfidence = AxisResolutionConfidence::Medium;
    candidate->metadataContradiction = true;
    candidate->formattedSourceEvidence = AxisFormattedSourceEvidence::ReportedOffsetFallback;
    candidate->formattedSourceVerified = true;
    return candidateOffsetSlot;
}

LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis)
{
    switch (axis) {
    case PhysicalAxis::X: return state.lX;
    case PhysicalAxis::Y: return state.lY;
    case PhysicalAxis::Z: return state.lZ;
    case PhysicalAxis::Rx: return state.lRx;
    case PhysicalAxis::Ry: return state.lRy;
    case PhysicalAxis::Rz: return state.lRz;
    case PhysicalAxis::Slider0: return state.rglSlider[0];
    case PhysicalAxis::Slider1: return state.rglSlider[1];
    }
    return 0;
}

LONG directInputAxisValueAtOffset(const DIJOYSTATE2 &state, DWORD offset)
{
    const int index = physicalAxisIndexForDirectInputOffset(offset);
    return index < 0 ? 0 : directInputAxisValue(state, static_cast<PhysicalAxis>(index));
}

float normalizeDirectInputAxisValue(LONG value, const NativeAxisDescriptor &descriptor)
{
    const LONG minimum = descriptor.nativeMinimum;
    const LONG maximum = descriptor.nativeMaximum;
    if (maximum <= minimum) {
        return std::clamp(static_cast<float>(value) / 10000.0F, -1.0F, 1.0F);
    }
    const float position = std::clamp((static_cast<float>(value) - static_cast<float>(minimum))
        / (static_cast<float>(maximum) - static_cast<float>(minimum)), 0.0F, 1.0F);
    // Do not infer one-sided behavior from the control name. Mapping-level
    // range policy decides how a valid native span is interpreted; this is a
    // neutral normalized representation of the actual DirectInput range.
    return position * 2.0F - 1.0F;
}

float normalizeRuntimeAxisAcquisition(LONG value, const RuntimeAxisAcquisition &binding)
{
    if (!binding.valid || !std::isfinite(binding.scale) || !std::isfinite(binding.offset)) return 0.0F;
    float normalized = static_cast<float>(value) * binding.scale + binding.offset;
    const bool oneSided = (binding.flags & RuntimeAxisAcquisitionOneSided) != 0;
    normalized = std::clamp(normalized, oneSided ? 0.0F : -1.0F, 1.0F);
    if ((binding.flags & RuntimeAxisAcquisitionReversed) != 0) {
        normalized = oneSided ? 1.0F - normalized : -normalized;
    }
    return normalized;
}

int uniqueCorrelatedDirectInputStateField(LONG bufferedValue,
                                          const DIJOYSTATE2 &state,
                                          const RuntimeAxisAcquisition &binding)
{
    if (!binding.valid) return -1;
    const float normalizedBufferedValue = normalizeRuntimeAxisAcquisition(bufferedValue, binding);
    int matchedSource = -1;
    for (int source = 0; source < kPhysicalAxisCount; ++source) {
        const float normalizedStateValue = normalizeRuntimeAxisAcquisition(
            directInputAxisValue(state, static_cast<PhysicalAxis>(source)), binding);
        if (std::abs(normalizedBufferedValue - normalizedStateValue) > 0.002F) continue;
        // Multiple equal-value fields provide no source proof.  Wait for a
        // later event whose state is distinctive enough to be authoritative.
        if (matchedSource >= 0) return -1;
        matchedSource = source;
    }
    return matchedSource;
}

namespace {

QString guidString(const GUID &guid)
{
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return QString::fromWCharArray(text);
}

} // namespace

namespace {

int resolvedAxisForDirectInputObject(const DIDEVICEOBJECTINSTANCEW &instance)
{
    const int semanticAxis = physicalAxisIndexForDirectInputSemanticGuid(instance.guidType);
    return semanticAxis >= 0 ? semanticAxis : physicalAxisIndexForDirectInputOffset(instance.dwOfs);
}

RuntimeAxisAcquisition compileBinding(const NativeAxisDescriptor &descriptor, int source,
                                      const AxisAcquisitionOverride *override, bool manual)
{
    RuntimeAxisAcquisition binding;
    if (!descriptor.present || source < 0 || source >= kPhysicalAxisCount) return binding;
    if (override && override->interpretation == AxisRawInterpretation::Relative) {
        // Relative reports need a distinct accumulation contract. Do not
        // pretend a raw direct value is an absolute axis; fall back safely.
        return binding;
    }
    qint32 minimum = descriptor.nativeMinimum;
    qint32 maximum = descriptor.nativeMaximum;
    if (override && (override->rangePolicy == AxisRawRangePolicy::Manual
        || override->rangePolicy == AxisRawRangePolicy::Observed)) {
        minimum = override->manualMinimum;
        maximum = override->manualMaximum;
    }
    if (maximum <= minimum) return binding;
    const bool oneSided = override
        && override->interpretation == AxisRawInterpretation::OneSidedAbsolute;
    const float span = static_cast<float>(maximum) - static_cast<float>(minimum);
    binding.sourceIndex = static_cast<std::uint8_t>(source);
    binding.minimum = static_cast<float>(minimum);
    binding.maximum = static_cast<float>(maximum);
    binding.scale = oneSided ? 1.0F / span : 2.0F / span;
    binding.offset = oneSided ? -static_cast<float>(minimum) / span
                              : -2.0F * static_cast<float>(minimum) / span - 1.0F;
    if (!std::isfinite(binding.scale) || !std::isfinite(binding.offset)) return {};
    if (oneSided) binding.flags |= RuntimeAxisAcquisitionOneSided;
    if (override && override->polarity == AxisRawPolarity::Reversed) {
        binding.flags |= RuntimeAxisAcquisitionReversed;
    }
    if (!manual) binding.flags |= RuntimeAxisAcquisitionAllowBufferedEvidence;
    if (manual) binding.flags |= RuntimeAxisAcquisitionManual;
    binding.valid = true;
    return binding;
}

} // namespace

NativeAxisDescriptor describeDirectInputAxisObject(LPDIRECTINPUTDEVICE8W device,
                                                   const DIDEVICEOBJECTINSTANCEW &instance)
{
    NativeAxisDescriptor descriptor;
    const int semanticAxis = physicalAxisIndexForDirectInputSemanticGuid(instance.guidType);
    const int offsetAxis = physicalAxisIndexForDirectInputOffset(instance.dwOfs);
    const int index = resolvedAxisForDirectInputObject(instance);
    if (index < 0) return descriptor;

    descriptor.present = true;
    descriptor.nativeName = QString::fromWCharArray(instance.tszName).trimmed();
    descriptor.directInputGuid = guidString(instance.guidType);
    descriptor.directInputType = instance.dwType;
    descriptor.directInputOffset = instance.dwOfs;
    descriptor.directInputInstance = DIDFT_GETINSTANCE(instance.dwType);
    descriptor.relative = (instance.dwType & DIDFT_RELAXIS) != 0;
    descriptor.canonicalAxis = index;
    // A semantic GUID is strong proof of native identity, but it is not
    // universal proof of DIJOYSTATE2 storage.  When metadata conflicts, keep
    // the two facts independent: the reported field is an unverified
    // candidate, never an inference from the identically named semantic GUID.
    const bool metadataContradiction = semanticAxis >= 0 && offsetAxis >= 0
        && semanticAxis != offsetAxis;
    descriptor.formattedSource = metadataContradiction ? offsetAxis
        : semanticAxis >= 0 ? semanticAxis : offsetAxis;
    descriptor.resolutionSource = semanticAxis >= 0
        ? AxisResolutionSource::StandardSemanticGuid : AxisResolutionSource::ReportedOffset;
    descriptor.metadataContradiction = metadataContradiction;
    // Canonical identity remains GUID-first even when the candidate storage
    // field is contradictory. Buffered native-object evidence decides which
    // candidate becomes a verified source.
    descriptor.resolutionConfidence = semanticAxis >= 0
        ? AxisResolutionConfidence::High : AxisResolutionConfidence::Medium;
    descriptor.acquisitionSourceResolved = descriptor.formattedSource >= 0;
    descriptor.formattedSourceEvidence = semanticAxis >= 0 && offsetAxis >= 0
            && semanticAxis == offsetAxis
        ? AxisFormattedSourceEvidence::MetadataAgreement
        : metadataContradiction
            ? AxisFormattedSourceEvidence::ReportedOffsetCandidate
        : semanticAxis >= 0
            ? AxisFormattedSourceEvidence::SemanticFallback
            : AxisFormattedSourceEvidence::ReportedOffsetFallback;
    descriptor.formattedSourceVerified = descriptor.formattedSourceEvidence
        == AxisFormattedSourceEvidence::MetadataAgreement
        || descriptor.formattedSourceEvidence == AxisFormattedSourceEvidence::ReportedOffsetFallback;
    if (!device) return descriptor;

    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    const HRESULT read = device->GetProperty(DIPROP_RANGE, &range.diph);
    descriptor.rangeReadResult = static_cast<qint32>(read);
    if (SUCCEEDED(read)) {
        descriptor.nativeMinimum = range.lMin;
        descriptor.nativeMaximum = range.lMax;
    }
    return descriptor;
}

void configureDirectInputAxisRange(LPDIRECTINPUTDEVICE8W device,
                                   const DIDEVICEOBJECTINSTANCEW &instance,
                                   NativeAxisDescriptor *descriptor)
{
    if (!device || resolvedAxisForDirectInputObject(instance) < 0) return;
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    range.lMin = -10000;
    range.lMax = 10000;
    const HRESULT setResult = device->SetProperty(DIPROP_RANGE, &range.diph);
    if (descriptor) {
        descriptor->requestedMinimum = range.lMin;
        descriptor->requestedMaximum = range.lMax;
        descriptor->rangeSetAttempted = true;
        descriptor->rangeSetResult = static_cast<qint32>(setResult);
        // Read after the request even if SetProperty says success. A number
        // of drivers accept a range request but retain a native data range.
        DIPROPRANGE actual{};
        actual.diph.dwSize = sizeof(actual);
        actual.diph.dwHeaderSize = sizeof(actual.diph);
        actual.diph.dwHow = DIPH_BYID;
        actual.diph.dwObj = instance.dwType;
        const HRESULT readResult = device->GetProperty(DIPROP_RANGE, &actual.diph);
        descriptor->rangeReadResult = static_cast<qint32>(readResult);
        if (SUCCEEDED(readResult)) {
            descriptor->nativeMinimum = actual.lMin;
            descriptor->nativeMaximum = actual.lMax;
        }
        descriptor->acquisitionSourceResolved = SUCCEEDED(readResult)
            && descriptor->formattedSource >= 0;
    }
}

HRESULT configureDirectInputBufferedEvents(LPDIRECTINPUTDEVICE8W device, DWORD capacity)
{
    if (!device || capacity == 0) return E_INVALIDARG;
    DIPROPDWORD property{};
    property.diph.dwSize = sizeof(property);
    property.diph.dwHeaderSize = sizeof(property.diph);
    property.diph.dwHow = DIPH_DEVICE;
    property.diph.dwObj = 0;
    property.dwData = capacity;
    return device->SetProperty(DIPROP_BUFFERSIZE, &property.diph);
}

bool axisAcquisitionOverrideMatchesNativeObject(const AxisAcquisitionOverride &override,
                                                const NativeAxisDescriptor &descriptor)
{
    return override.mode == AxisAcquisitionMode::NativeDirectInputObject
        && descriptor.present
        && !override.nativeSemanticGuid.isEmpty()
        && override.nativeSemanticGuid.compare(descriptor.directInputGuid, Qt::CaseInsensitive) == 0
        && override.nativeDirectInputType == descriptor.directInputType
        && override.nativeDirectInputOffset == descriptor.directInputOffset;
}

bool directInputAxisDescriptorSignatureMatches(const NativeAxisDescriptor &current,
                                               const NativeAxisDescriptor &persisted)
{
    return current.present && persisted.present
        && current.canonicalAxis >= 0
        && current.canonicalAxis == persisted.canonicalAxis
        && !current.directInputGuid.isEmpty()
        && current.directInputGuid.compare(persisted.directInputGuid, Qt::CaseInsensitive) == 0
        && current.directInputType == persisted.directInputType
        && current.directInputOffset == persisted.directInputOffset
        && current.directInputInstance == persisted.directInputInstance
        && current.relative == persisted.relative;
}

bool reuseVerifiedFormattedSource(NativeAxisDescriptor *current,
                                  const NativeAxisDescriptor &persisted)
{
    if (!current || !persisted.formattedSourceVerified
        || persisted.formattedSource < 0 || persisted.formattedSource >= kPhysicalAxisCount
        || !directInputAxisDescriptorSignatureMatches(*current, persisted)) {
        return false;
    }
    current->formattedSource = persisted.formattedSource;
    current->formattedSourceEvidence = persisted.formattedSourceEvidence;
    current->formattedSourceVerified = true;
    current->acquisitionSourceResolved = true;
    return true;
}

std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> compileRuntimeAxisAcquisitions(
    const std::array<NativeAxisDescriptor, kPhysicalAxisCount> &descriptors,
    const std::array<AxisAcquisitionOverride, kPhysicalAxisCount> &overrides,
    std::array<bool, kPhysicalAxisCount> *manualApplied)
{
    std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> bindings{};
    std::array<bool, kPhysicalAxisCount> applied{};
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const NativeAxisDescriptor &descriptor = descriptors[static_cast<size_t>(axis)];
        const int target = descriptor.canonicalAxis >= 0 ? descriptor.canonicalAxis : axis;
        if (target < 0 || target >= kPhysicalAxisCount) continue;
        const int source = descriptor.formattedSource >= 0 ? descriptor.formattedSource : axis;
        bindings[static_cast<size_t>(target)] = compileBinding(descriptor, source, nullptr, false);
    }

    std::array<int, kPhysicalAxisCount> targetClaims{};
    for (const AxisAcquisitionOverride &override : overrides) {
        if (!override.enabled || override.mode == AxisAcquisitionMode::Automatic) continue;
        const int target = static_cast<int>(override.target);
        if (target >= 0 && target < kPhysicalAxisCount) ++targetClaims[static_cast<size_t>(target)];
    }
    std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> manualBindings{};
    std::array<int, kPhysicalAxisCount> manualSourceAutomaticTargets{};
    manualSourceAutomaticTargets.fill(-1);
    std::array<int, kPhysicalAxisCount> sourceClaims{};

    for (const AxisAcquisitionOverride &override : overrides) {
        if (!override.enabled || override.mode == AxisAcquisitionMode::Automatic
            || override.mode == AxisAcquisitionMode::RawHidValue) continue;
        const int target = static_cast<int>(override.target);
        if (target < 0 || target >= kPhysicalAxisCount || targetClaims[static_cast<size_t>(target)] != 1) {
            continue;
        }
        const NativeAxisDescriptor *sourceDescriptor = nullptr;
        int source = override.formattedSource;
        if (override.mode == AxisAcquisitionMode::DirectInputFormattedSlot) {
            if (source < 0) {
                // Manual normalization can retain an automatic source. Resolve
                // that source only while compiling the fixed binding and only
                // when exactly one native object owns the requested canonical
                // axis. An ambiguous reconnect falls back to automatic.
                for (int descriptorIndex = 0; descriptorIndex < kPhysicalAxisCount;
                     ++descriptorIndex) {
                    const NativeAxisDescriptor &candidate = descriptors[
                        static_cast<size_t>(descriptorIndex)];
                    const int candidateTarget = candidate.canonicalAxis >= 0
                        ? candidate.canonicalAxis : descriptorIndex;
                    if (!candidate.present || candidate.formattedSource < 0
                        || candidateTarget != target) {
                        continue;
                    }
                    if (sourceDescriptor) {
                        sourceDescriptor = nullptr;
                        break;
                    }
                    sourceDescriptor = &candidate;
                }
                if (!sourceDescriptor) continue;
                source = sourceDescriptor->formattedSource;
            } else {
                if (source >= kPhysicalAxisCount) continue;
                const auto found = std::find_if(descriptors.cbegin(), descriptors.cend(),
                    [source](const NativeAxisDescriptor &candidate) {
                        return candidate.present && candidate.formattedSource == source;
                    });
                if (found == descriptors.cend()) continue;
                sourceDescriptor = &*found;
            }
        } else if (override.mode == AxisAcquisitionMode::NativeDirectInputObject) {
            const NativeAxisDescriptor *match = nullptr;
            for (const NativeAxisDescriptor &candidate : descriptors) {
                if (!axisAcquisitionOverrideMatchesNativeObject(override, candidate)) continue;
                if (match) {
                    match = nullptr; // Ambiguous re-match: intentionally reject.
                    break;
                }
                match = &candidate;
            }
            if (!match || match->formattedSource < 0) continue;
            sourceDescriptor = match;
            source = match->formattedSource;
        }
        if (!sourceDescriptor) continue;
        RuntimeAxisAcquisition candidate = compileBinding(*sourceDescriptor, source, &override, true);
        if (!candidate.valid) continue;

        // A manual claim replaces the selected object's automatic identity;
        // it must never leave one report field feeding two canonical axes.
        const int automaticTarget = sourceDescriptor->canonicalAxis >= 0
            ? sourceDescriptor->canonicalAxis : source;
        if (automaticTarget < 0 || automaticTarget >= kPhysicalAxisCount) continue;
        if (++sourceClaims[static_cast<size_t>(automaticTarget)] != 1) continue;
        manualBindings[static_cast<size_t>(target)] = candidate;
        manualSourceAutomaticTargets[static_cast<size_t>(target)] = automaticTarget;
    }

    for (int target = 0; target < kPhysicalAxisCount; ++target) {
        const RuntimeAxisAcquisition &candidate = manualBindings[static_cast<size_t>(target)];
        if (!candidate.valid) continue;
        const int automaticTarget = manualSourceAutomaticTargets[static_cast<size_t>(target)];
        if (automaticTarget < 0 || automaticTarget >= kPhysicalAxisCount
            || sourceClaims[static_cast<size_t>(automaticTarget)] != 1) continue;
        if (automaticTarget >= 0 && automaticTarget < kPhysicalAxisCount) {
            bindings[static_cast<size_t>(automaticTarget)] = {};
        }
    }
    for (int target = 0; target < kPhysicalAxisCount; ++target) {
        const RuntimeAxisAcquisition &candidate = manualBindings[static_cast<size_t>(target)];
        if (!candidate.valid) continue;
        const int automaticTarget = manualSourceAutomaticTargets[static_cast<size_t>(target)];
        if (automaticTarget < 0 || automaticTarget >= kPhysicalAxisCount
            || sourceClaims[static_cast<size_t>(automaticTarget)] != 1) continue;
        bindings[static_cast<size_t>(target)] = candidate;
        applied[static_cast<size_t>(target)] = true;
    }
    if (manualApplied) *manualApplied = applied;
    return bindings;
}

} // namespace hotas
