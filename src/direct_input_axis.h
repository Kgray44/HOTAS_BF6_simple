#pragma once

#include "mapping_types.h"

#include <dinput.h>

namespace hotas {

// The DirectInput state layout is a closed, fixed set.  Keep this mapping in
// one shared control-plane/runtime helper so enumeration order can never
// decide which DIJOYSTATE2 field backs a physical axis.
int physicalAxisIndexForDirectInputOffset(DWORD offset);
// Some legacy DirectInput drivers expose a valid axis GUID but report a
// DIJOYSTATE2 offset for another slot. Prefer the object identity when it is
// one of the fixed standard axes; retain the raw offset as native evidence.
int physicalAxisIndexForDirectInputObject(const DIDEVICEOBJECTINSTANCEW &instance);
LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis);
LONG directInputAxisValueAtOffset(const DIJOYSTATE2 &state, DWORD offset);
float normalizeDirectInputAxisValue(LONG value, const NativeAxisDescriptor &descriptor);

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

} // namespace hotas
