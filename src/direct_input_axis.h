#pragma once

#include "mapping_types.h"

#include <dinput.h>

namespace hotas {

// The DirectInput state layout is a closed, fixed set.  Keep this mapping in
// one shared control-plane/runtime helper so enumeration order can never
// decide which DIJOYSTATE2 field backs a physical axis.
int physicalAxisIndexForDirectInputOffset(DWORD offset);
int physicalAxisIndexForDirectInputSemanticGuid(const GUID &guid);
// Preserve each separate physical object when a driver reuses one generic
// semantic GUID for multiple DIJOYSTATE2 fields.  The result is deterministic
// even when DirectInput enumerates the objects in a different order.
int resolveUniqueDirectInputAxisSlot(
    NativeAxisDescriptor *candidate,
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> *assigned,
    std::array<bool, kPhysicalAxisCount> *available = nullptr);
LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis);
LONG directInputAxisValueAtOffset(const DIJOYSTATE2 &state, DWORD offset);
float normalizeDirectInputAxisValue(LONG value, const NativeAxisDescriptor &descriptor);
float normalizeRuntimeAxisAcquisition(LONG value, const RuntimeAxisAcquisition &binding);

// A buffered event identifies one native DirectInput object; DIJOYSTATE2 is a
// separate fixed storage layout.  Resolve that storage only when exactly one
// normalized state member matches the fresh event.  A coincident or ambiguous
// sample intentionally returns -1 rather than guessing.
int uniqueCorrelatedDirectInputStateField(LONG bufferedValue,
                                          const DIJOYSTATE2 &state,
                                          const RuntimeAxisAcquisition &binding);

// Capture object metadata before the mapper requests its normalized report
// range.  This data is durable device capability evidence, never a report-path
// lookup table.
NativeAxisDescriptor describeDirectInputAxisObject(LPDIRECTINPUTDEVICE8W device,
                                                   const DIDEVICEOBJECTINSTANCEW &instance);
void configureDirectInputAxisRange(LPDIRECTINPUTDEVICE8W device,
                                   const DIDEVICEOBJECTINSTANCEW &instance,
                                   NativeAxisDescriptor *descriptor = nullptr);

// Buffered object data is deliberately enabled at acquisition time, not in
// the report path. It is a bounded evidence/fallback channel keyed by the
// enumerated object offset; failure leaves standard DIJOYSTATE2 sampling
// intact.
HRESULT configureDirectInputBufferedEvents(LPDIRECTINPUTDEVICE8W device,
                                           DWORD capacity = 32);

// Compiles every automatic/manual decision into a fixed primitive table. A
// false entry means no safe source was available; callers retain the normal
// disconnected/unavailable behavior for that axis rather than guessing.
std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> compileRuntimeAxisAcquisitions(
    const std::array<NativeAxisDescriptor, kPhysicalAxisCount> &descriptors,
    const std::array<AxisAcquisitionOverride, kPhysicalAxisCount> &overrides,
    std::array<bool, kPhysicalAxisCount> *manualApplied = nullptr);

bool axisAcquisitionOverrideMatchesNativeObject(const AxisAcquisitionOverride &override,
                                                const NativeAxisDescriptor &descriptor);

// Reuse a saved evidence-resolved source only when the freshly enumerated
// native object has the same stable DirectInput signature. A changed driver
// layout deliberately falls back to fresh automatic evidence instead.
bool directInputAxisDescriptorSignatureMatches(const NativeAxisDescriptor &current,
                                               const NativeAxisDescriptor &persisted);
bool reuseVerifiedFormattedSource(NativeAxisDescriptor *current,
                                  const NativeAxisDescriptor &persisted);

} // namespace hotas
