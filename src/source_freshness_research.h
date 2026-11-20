#pragma once

// Compile-gated V2.4-X3B research only.  This handoff is intentionally not
// included by MappingWorker or any production target.  It models the smallest
// fixed-state producer/consumer boundary a future source-report augmentation
// would need; descriptor parsing and OS I/O stay on the producer side.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hotas::research {

enum class SourceFreshnessHealth : std::uint32_t {
    Healthy = 0U,
    ChangedValueFallback = 1U,
    OverflowOrLoss = 2U,
    Reconnecting = 3U,
    Disconnected = 4U,
};

struct SourceFreshnessSample final {
    std::uint64_t reportSequence = 0U;
    std::int64_t arrivalMicroseconds = 0;
    float decodedAxisValue = 0.0F;
    std::uint32_t reportId = 0U;
    SourceFreshnessHealth health = SourceFreshnessHealth::Disconnected;
};

// Every member is a lock-free scalar on the supported 64-bit Windows target.
// `epoch` is a seqlock-style publication guard: odd while the producer writes,
// even when a coherent snapshot is available to the mapping-side consumer.
struct SourceFreshnessSlot final {
    std::atomic<std::uint64_t> epoch{0U};
    std::atomic<std::uint64_t> reportSequence{0U};
    std::atomic<std::int64_t> arrivalMicroseconds{0};
    std::atomic<std::uint32_t> decodedAxisBits{0U};
    std::atomic<std::uint32_t> reportId{0U};
    std::atomic<std::uint32_t> health{static_cast<std::uint32_t>(SourceFreshnessHealth::Disconnected)};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "X3B handoff requires lock-free 64-bit atomics");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "X3B handoff requires lock-free 32-bit atomics");

class SourceFreshnessHandoff final {
public:
    static constexpr std::size_t kMaximumAxes = 8U;

    // Called only for an axis whose HID report descriptor says that report ID
    // actually contains that axis.  A button-only or another-axis report must
    // not manufacture freshness for this slot.
    void publish(std::size_t axis, const SourceFreshnessSample &sample) noexcept {
        if (axis >= m_axes.size()) return;
        SourceFreshnessSlot &slot = m_axes[axis];
        slot.epoch.fetch_add(1U, std::memory_order_acq_rel); // odd: writing
        slot.reportSequence.store(sample.reportSequence, std::memory_order_relaxed);
        slot.arrivalMicroseconds.store(sample.arrivalMicroseconds, std::memory_order_relaxed);
        slot.decodedAxisBits.store(floatBits(sample.decodedAxisValue), std::memory_order_relaxed);
        slot.reportId.store(sample.reportId, std::memory_order_relaxed);
        slot.health.store(static_cast<std::uint32_t>(sample.health), std::memory_order_relaxed);
        slot.epoch.fetch_add(1U, std::memory_order_release); // even: coherent
    }

    // The 250 Hz consumer retries at most once.  Failure to obtain a coherent
    // snapshot is a health signal, never a reason to synthesize a report.
    bool consume(std::size_t axis, SourceFreshnessSample *sample) const noexcept {
        if (!sample || axis >= m_axes.size()) return false;
        const SourceFreshnessSlot &slot = m_axes[axis];
        for (int attempt = 0; attempt < 2; ++attempt) {
            const std::uint64_t before = slot.epoch.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) continue;
            SourceFreshnessSample observed;
            observed.reportSequence = slot.reportSequence.load(std::memory_order_relaxed);
            observed.arrivalMicroseconds = slot.arrivalMicroseconds.load(std::memory_order_relaxed);
            observed.decodedAxisValue = floatFromBits(slot.decodedAxisBits.load(std::memory_order_relaxed));
            observed.reportId = slot.reportId.load(std::memory_order_relaxed);
            observed.health = static_cast<SourceFreshnessHealth>(slot.health.load(std::memory_order_relaxed));
            const std::uint64_t after = slot.epoch.load(std::memory_order_acquire);
            if (before == after && (after & 1U) == 0U) {
                *sample = observed;
                return true;
            }
        }
        return false;
    }

private:
    static std::uint32_t floatBits(float value) noexcept {
        std::uint32_t bits = 0U;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
    static float floatFromBits(std::uint32_t bits) noexcept {
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::array<SourceFreshnessSlot, kMaximumAxes> m_axes{};
};

} // namespace hotas::research
