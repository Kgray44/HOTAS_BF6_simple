#pragma once

#include "button_mapping.h"

#include <array>
#include <cstdint>

namespace hotas {

// A small, allocation-free model of the physical controller. It deliberately
// knows nothing about vJoy or Mapping Active: the worker updates this as long
// as DirectInput can supply reports, and the UI projects the latest snapshot.
struct PhysicalInputReport {
    std::array<float, kPhysicalAxisCount> axes{};
    PhysicalButtonStates buttons{};
    PhysicalPovValues povs{[] { PhysicalPovValues values{}; values.fill(-1); return values; }()};
};

struct PhysicalInputSnapshot {
    std::array<float, kPhysicalAxisCount> axes{};
    PhysicalButtonStates buttons{};
    PhysicalPovValues povs{[] { PhysicalPovValues values{}; values.fill(-1); return values; }()};
    int lastChangedButton = 0; // One-based source button, including release.
    std::uint64_t reportCount = 0;
};

class PhysicalInputMonitor final {
public:
    void configure(const std::array<bool, kPhysicalAxisCount> &axes,
                   const std::array<bool, kMaximumPhysicalButtons> &buttons,
                   int povCount);
    void accept(const PhysicalInputReport &report);
    void disconnect();

    const std::array<bool, kPhysicalAxisCount> &availableAxes() const { return m_availableAxes; }
    const std::array<bool, kMaximumPhysicalButtons> &availableButtons() const { return m_availableButtons; }
    int povCount() const { return m_povCount; }
    const PhysicalInputSnapshot &snapshot() const { return m_snapshot; }

private:
    std::array<bool, kPhysicalAxisCount> m_availableAxes{};
    std::array<bool, kMaximumPhysicalButtons> m_availableButtons{};
    int m_povCount = 0;
    PhysicalInputSnapshot m_snapshot;
};

} // namespace hotas
