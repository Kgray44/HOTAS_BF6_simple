#pragma once

#include "verification_harness.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace hotas::verification {

// This is deliberately a physical-input-only evidence format. It contains no
// virtual-output state and is consumed by the offline replay executable.
struct PhysicalAxisTraceSample {
    std::uint64_t timestampUs = 0;
    float rawNormalized = 0.0F;
    float calibratedNormalized = 0.0F;
    bool sourceReported = true;
    bool sourceChanged = false;
    std::uint64_t elapsedUs = 0;
    double effectiveCadenceHz = 0.0;
    // The unmodified value returned by DirectInput. This preserves driver
    // range evidence when a device does not honor the requested mapper range.
    std::int32_t rawDirectInput = 0;
};

struct PhysicalAxisTrace {
    QString captureLabel;
    int selectedAxis = -1;
    QString selectedAxisLabel;
    bool calibrationEnabled = false;
    std::int32_t directInputRangeMinimum = -10000;
    std::int32_t directInputRangeMaximum = 10000;
    // The range used to transform rawDirectInput into rawNormalized. It is
    // explicit because some HID drivers retain a native unsigned range even
    // after accepting a DirectInput DIPROP_RANGE request.
    std::int32_t rawNormalizationMinimum = -10000;
    std::int32_t rawNormalizationMaximum = 10000;
    std::vector<PhysicalAxisTraceSample> samples;
};

bool writePhysicalAxisTraceCsv(const QString &path, const PhysicalAxisTrace &trace,
                               QString *error = nullptr);
bool readPhysicalAxisTraceCsv(const QString &path, PhysicalAxisTrace *trace,
                              QString *error = nullptr);

// Converts every captured DirectInput report into one replay sample. The
// timestamp and source-report cadence are retained; no synthetic mapper or
// source resampling is introduced.
std::vector<TraceSample> traceSamplesFromPhysicalAxisTrace(const PhysicalAxisTrace &trace);

} // namespace hotas::verification
