#include "physical_input_monitor.h"

#include <algorithm>

namespace hotas {

void PhysicalInputMonitor::configure(const std::array<bool, kPhysicalAxisCount> &axes,
                                     const std::array<bool, kMaximumPhysicalButtons> &buttons,
                                     int povCount)
{
    m_availableAxes = axes;
    m_availableButtons = buttons;
    m_povCount = std::max(0, povCount);
    m_snapshot = {};
    m_snapshot.pov = -1;
}

void PhysicalInputMonitor::accept(const PhysicalInputReport &report)
{
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (m_availableAxes[index]) {
            m_snapshot.axes[index] = report.axes[index];
        }
    }
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        if (!m_availableButtons[index]) continue;
        if (m_snapshot.buttons[index] != report.buttons[index]) {
            m_snapshot.lastChangedButton = index + 1;
        }
        m_snapshot.buttons[index] = report.buttons[index];
    }
    m_snapshot.pov = m_povCount > 0 ? report.pov : -1;
    ++m_snapshot.reportCount;
}

void PhysicalInputMonitor::disconnect()
{
    m_availableAxes.fill(false);
    m_availableButtons.fill(false);
    m_povCount = 0;
    m_snapshot = {};
    m_snapshot.pov = -1;
}

} // namespace hotas
