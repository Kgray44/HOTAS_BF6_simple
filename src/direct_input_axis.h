#pragma once

#include "mapping_types.h"

#include <dinput.h>

namespace hotas {

// The DirectInput state layout is a closed, fixed set.  Keep this mapping in
// one shared control-plane/runtime helper so enumeration order can never
// decide which DIJOYSTATE2 field backs a physical axis.
int physicalAxisIndexForDirectInputOffset(DWORD offset);
LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis);

// Capture object metadata before the mapper requests its normalized report
// range.  This data is durable device capability evidence, never a report-path
// lookup table.
NativeAxisDescriptor describeDirectInputAxisObject(LPDIRECTINPUTDEVICE8W device,
                                                   const DIDEVICEOBJECTINSTANCEW &instance);
void configureDirectInputAxisRange(LPDIRECTINPUTDEVICE8W device,
                                   const DIDEVICEOBJECTINSTANCEW &instance);

} // namespace hotas
