#pragma once

#include "mapping_types.h"

#include <dinput.h>

namespace hotas {

// The DirectInput state layout is a closed, fixed set.  Keep this mapping in
// one shared control-plane/runtime helper so enumeration order can never
// decide which DIJOYSTATE2 field backs a physical axis.
int physicalAxisIndexForDirectInputOffset(DWORD offset);
int physicalAxisIndexForDirectInputSemanticGuid(const GUID &guid);
LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis);
LONG directInputAxisValueAtOffset(const DIJOYSTATE2 &state, DWORD offset);
float normalizeDirectInputAxisValue(LONG value, const NativeAxisDescriptor &descriptor);
float normalizeRuntimeAxisAcquisition(LONG value, const RuntimeAxisAcquisition &binding);

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

} // namespace hotas
