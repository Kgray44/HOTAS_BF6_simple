#pragma once

#include <array>
#include <cstdint>

namespace hotas {

// A deliberately small adapter boundary.  `sourceReportObserved` is factual:
// callers must not turn a mapper poll into a report.  The X3 offline runner
// uses ExplicitSourceReport; the current DirectInput path can only create the
// ValueChange form.  This class is shadow-only in X3 and is not used by the
// mapper or vJoy output path.
enum class SourceObservationMode : std::uint8_t {
    ExplicitSourceReport,
    ValueChange,
};

struct SourceObservation final {
    double mapperPhysical = 0.0;
    double sourcePosition = 0.0;
    // Monotonic seconds retain the exact qualified source-time arithmetic in
    // offline replay.  A future DirectInput adapter may expose a coarser
    // timestamp; that is a recorded semantics gap, not an implicit rounding.
    double mapperTimeSeconds = 0.0;
    double sourceTimeSeconds = 0.0;
    std::uint64_t sourceSequence = 0U;
    bool sourceReportObserved = false;
    bool sourceValueChanged = false;
    SourceObservationMode mode = SourceObservationMode::ExplicitSourceReport;
    double domainMinimum = -1.0;
    double domainMaximum = 1.0;
};

struct SourceNativeAdaptiveResponseTelemetry final {
    double q3Position = 0.0;
    double q3Velocity = 0.0;
    double q3Acceleration = 0.0;
    double q5Position = 0.0;
    double innovation = 0.0;
    double disagreement = 0.0;
    double curvature = 0.0;
    double sourceTravel = 0.003;
    double sourceAgeSeconds = 0.0;
    double sourceIntervalSeconds = 0.0;
    double risk = 0.0;
    double calibrationBound = 0.0;
    double qualityBudget = 0.0;
    double horizonSeconds = 0.0;
    double rawForecast = 0.0;
    double rawLead = 0.0;
    double boundedLead = 0.0;
    double endpointLead = 0.0;
    double finalShadowOutput = 0.0;
    std::uint64_t acceptedSourceSequence = 0U;
    std::uint8_t riskBin = 0U;
    bool sourceReportAccepted = false;
    bool ordinarySafetyVeto = false;
    bool terminalOwnerVeto = false;
};

class SourceNativeAdaptiveResponseProcessor final {
public:
    static constexpr std::size_t kHorizonCount = 14U;
    static constexpr std::array<double, kHorizonCount> kHorizonsMs{
        0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 15.0, 18.0, 20.0, 24.0, 30.0, 35.0, 40.0};

    void reset();
    SourceNativeAdaptiveResponseTelemetry process(const SourceObservation &observation);

private:
    struct Point { double time = 0.0; double value = 0.0; };
    struct Ownership {
        double current = 0.0;
        double old = 0.0;
        double pending = 0.0;
        double previous = 0.0;
        std::uint32_t reports = 0U;
        std::uint32_t confirmations = 0U;
        bool credibleOpposite = false;
    };

    void acceptSource(double time, double value, double interval, std::uint64_t sequence);
    void solveQ3();
    void solveQ5();
    double q3Predict(double time) const;
    double q5Predict(double time) const;
    double q3Velocity(double time) const;
    double q3Acceleration(double time) const;
    double qualityBudget() const;
    std::uint8_t riskBin(double mapperTime, double horizonSeconds, double *risk) const;
    void acceptOwnership(double time, double value);

    std::array<Point, 3> m_q3Points{};
    std::array<double, 3> m_q3Coefficients{};
    std::uint8_t m_q3Count = 0U;
    bool m_q3Valid = false;
    std::array<Point, 5> m_q5Points{};
    std::array<double, 3> m_q5Coefficients{};
    std::uint8_t m_q5Count = 0U;
    bool m_q5Valid = false;
    Ownership m_ownership{};
    double m_innovation = 0.0;
    double m_curvature = 0.0;
    double m_travel = 0.003;
    double m_period = 0.015484;
    double m_cadence = 0.0;
    double m_priorValue = 0.0;
    double m_priorAcceleration = 0.0;
    double m_lastReportTime = 0.0;
    double m_direction = 0.0;
    std::uint32_t m_reports = 0U;
    std::uint32_t m_quiet = 0U;
    std::uint32_t m_stop = 0U;
    std::uint32_t m_reversalGuard = 0U;
    std::uint64_t m_lastSequence = 0U;
};

static_assert(sizeof(SourceNativeAdaptiveResponseProcessor) < 1024U,
              "X3 source-native state must remain bounded and small");

} // namespace hotas
