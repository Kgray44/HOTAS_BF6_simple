#include "source_native_adaptive_response.h"

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {
constexpr double kDirectionEpsilon = 1e-7;
constexpr double kMaximumLead = .18;
constexpr std::array<std::array<double, 5>, SourceNativeAdaptiveResponseProcessor::kHorizonCount> kCal95{{
    {{.00926142,.03862992,.05961804,.07437892,.38984397}},
    {{.01101280,.04580113,.07007855,.08554146,.45426508}},
    {{.01354688,.05829964,.08317694,.10302700,.55658550}},
    {{.01635523,.06218585,.09618494,.11938269,.70026461}},
    {{.01872800,.07298424,.11644204,.13607898,.80969357}},
    {{.02164805,.08032116,.13291703,.15438123,.96595877}},
    {{.02538179,.08907348,.14952359,.17320432,1.07229095}},
    {{.02991121,.09837658,.17254015,.20368922,1.25708479}},
    {{.03566533,.11114649,.19768130,.23727227,1.37060452}},
    {{.03864851,.11549792,.21432638,.25476209,1.51332816}},
    {{.05117812,.11847538,.23851637,.28563074,1.67699169}},
    {{.06562944,.13926015,.25848680,.32863852,1.87827193}},
    {{.08251078,.17061325,.25848680,.32863852,1.87827193}},
    {{.09984042,.20323797,.25848680,.32863852,2.33859267}}
}};
constexpr std::array<std::array<double, 5>, SourceNativeAdaptiveResponseProcessor::kHorizonCount> kCal99{{
    {{.02350258,.07034297,.09828779,.12847162,.78658718}},
    {{.02575649,.08396021,.11653327,.13825724,.94540180}},
    {{.02858742,.10056069,.12680312,.15655637,1.19488317}},
    {{.03234829,.11431651,.14676544,.17208531,1.44505432}},
    {{.03887434,.12822443,.17138796,.19483132,1.65372853}},
    {{.03937648,.14630034,.19645133,.21569778,1.93604499}},
    {{.04557313,.17822366,.22843183,.24455587,2.35480720}},
    {{.05806910,.20867505,.27998972,.30124829,2.85562445}},
    {{.06716860,.23676070,.32935037,.36097659,3.08232663}},
    {{.07343148,.25208050,.36877223,.38905366,3.43038117}},
    {{.09377967,.30387311,.43046792,.47040876,4.06471415}},
    {{.12206632,.42628912,.51927259,.57287187,4.97525247}},
    {{.15384144,.52520234,.62455799,.67182561,5.99396273}},
    {{.17800620,.63993970,.70683927,.78333064,7.31047784}}
}};

template <typename Points>
void solveQuadratic(const Points &points, std::array<double, 3> &coefficients, bool &valid) {
    const double newest = points.back().time;
    double a[3][4]{};
    for (const auto &point : points) {
        const double t = point.time - newest;
        const std::array<double, 3> basis{1.0, t, t * t};
        for (int row = 0; row < 3; ++row) {
            a[row][3] += basis[static_cast<std::size_t>(row)] * point.value;
            for (int col = 0; col < 3; ++col) a[row][col] += basis[static_cast<std::size_t>(row)] * basis[static_cast<std::size_t>(col)];
        }
    }
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 3; ++row)
            if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) best = row;
        if (std::abs(a[best][pivot]) < 1e-12) { valid = false; return; }
        for (int col = pivot; col < 4; ++col) std::swap(a[pivot][col], a[best][col]);
        const double divisor = a[pivot][pivot];
        for (int col = pivot; col < 4; ++col) a[pivot][col] /= divisor;
        for (int row = 0; row < 3; ++row) if (row != pivot) {
            const double factor = a[row][pivot];
            for (int col = pivot; col < 4; ++col) a[row][col] -= factor * a[pivot][col];
        }
    }
    coefficients = {a[0][3], a[1][3], a[2][3]};
    valid = true;
}
}

void SourceNativeAdaptiveResponseProcessor::reset() { *this = {}; }

void SourceNativeAdaptiveResponseProcessor::solveQ3() { solveQuadratic(m_q3Points, m_q3Coefficients, m_q3Valid); }
void SourceNativeAdaptiveResponseProcessor::solveQ5() { solveQuadratic(m_q5Points, m_q5Coefficients, m_q5Valid); }

double SourceNativeAdaptiveResponseProcessor::q3Predict(double time) const {
    if (m_q3Count == 0U) return 0.0;
    if (!m_q3Valid) return m_q3Points[m_q3Count - 1U].value;
    const double dt = time - m_q3Points[m_q3Count - 1U].time;
    return m_q3Coefficients[0] + m_q3Coefficients[1] * dt + m_q3Coefficients[2] * dt * dt;
}
double SourceNativeAdaptiveResponseProcessor::q5Predict(double time) const {
    if (m_q5Count == 0U) return 0.0;
    if (!m_q5Valid) return m_q5Points[m_q5Count - 1U].value;
    const double dt = time - m_q5Points[m_q5Count - 1U].time;
    return m_q5Coefficients[0] + m_q5Coefficients[1] * dt + m_q5Coefficients[2] * dt * dt;
}
double SourceNativeAdaptiveResponseProcessor::q3Velocity(double time) const {
    constexpr double e = .00025;
    return (q3Predict(time + e) - q3Predict(time - e)) / (2.0 * e);
}
double SourceNativeAdaptiveResponseProcessor::q3Acceleration(double time) const {
    constexpr double e = .0005;
    return (q3Predict(time + e) - 2.0 * q3Predict(time) + q3Predict(time - e)) / (e * e);
}

void SourceNativeAdaptiveResponseProcessor::acceptOwnership(double time, double value) {
    const double delta = m_ownership.reports == 0U ? 0.0 : value - m_ownership.previous;
    m_ownership.previous = value;
    ++m_ownership.reports;
    if (std::abs(delta) <= .002) return;
    const double direction = delta > 0.0 ? 1.0 : -1.0;
    if (m_ownership.current == 0.0) { m_ownership.current = direction; return; }
    if (!m_ownership.credibleOpposite) {
        if (direction != m_ownership.current) {
            m_ownership.old = m_ownership.current;
            m_ownership.pending = direction;
            m_ownership.confirmations = 1U;
            m_ownership.credibleOpposite = true;
        }
        return;
    }
    if (direction == m_ownership.pending) {
        ++m_ownership.confirmations;
        if (m_ownership.confirmations >= 2U) {
            m_ownership.current = m_ownership.pending;
            m_ownership.credibleOpposite = false;
        }
    } else if (direction == m_ownership.old) {
        m_ownership.pending = 0.0;
        m_ownership.confirmations = 0U;
        m_ownership.credibleOpposite = false;
    }
    (void)time;
}

void SourceNativeAdaptiveResponseProcessor::acceptSource(double time, double value, double interval, std::uint64_t sequence) {
    const double priorPrediction = m_reports >= 3U ? q3Predict(time) : value;
    m_innovation = value - priorPrediction;
    const double priorAcceleration = m_reports >= 3U ? q3Acceleration(time) : 0.0;
    const double delta = m_reports ? value - m_priorValue : 0.0;
    if (m_reports && std::abs(delta) < .0015) ++m_quiet; else m_quiet = 0U;
    if (m_reports && std::abs(delta) < .001 && std::abs(m_direction) > 0.0) ++m_stop; else m_stop = 0U;
    const double newDirection = std::abs(delta) > .0015 ? (delta > 0.0 ? 1.0 : -1.0) : m_direction;
    if (m_direction != 0.0 && newDirection != 0.0 && newDirection != m_direction) m_reversalGuard = 2U;
    if (std::abs(delta) > .0015) m_direction = newDirection;
    if (interval > 0.0) { const double priorPeriod = m_period; m_period = .85 * m_period + .15 * interval; m_cadence = .85 * m_cadence + .15 * std::abs(interval - priorPeriod); }
    m_travel = .85 * m_travel + .15 * std::abs(delta);
    if (m_q3Count < m_q3Points.size()) m_q3Points[m_q3Count++] = {time, value};
    else { m_q3Points[0] = m_q3Points[1]; m_q3Points[1] = m_q3Points[2]; m_q3Points[2] = {time, value}; }
    if (m_q3Count == m_q3Points.size()) solveQ3();
    if (m_q5Count < m_q5Points.size()) m_q5Points[m_q5Count++] = {time, value};
    else { for (std::size_t i = 1; i < m_q5Points.size(); ++i) m_q5Points[i - 1U] = m_q5Points[i]; m_q5Points.back() = {time, value}; }
    if (m_q5Count == m_q5Points.size()) solveQ5();
    const double newAcceleration = m_reports >= 2U ? q3Acceleration(time) : 0.0;
    m_curvature = interval > .0005 ? std::abs(newAcceleration - priorAcceleration) / interval : 0.0;
    m_priorAcceleration = newAcceleration;
    m_priorValue = value;
    m_lastReportTime = time;
    ++m_reports;
    m_lastSequence = sequence;
    acceptOwnership(time, value);
}

double SourceNativeAdaptiveResponseProcessor::qualityBudget() const {
    return std::min(.060, .020 + .25 * std::min(.16, m_travel));
}

std::uint8_t SourceNativeAdaptiveResponseProcessor::riskBin(double mapperTime, double horizon, double *risk) const {
    const double innovation = std::clamp(std::abs(m_innovation) / .09331237, 0.0, 4.0);
    const double disagreement = std::clamp(std::abs(q3Predict(mapperTime + horizon) - q5Predict(mapperTime + horizon)) / .12932549, 0.0, 4.0);
    const double curvature = std::clamp(std::abs(m_curvature) / 16883.858866, 0.0, 4.0);
    const double value = std::min(1.0, std::max({innovation, disagreement, curvature}));
    if (risk) *risk = value;
    return static_cast<std::uint8_t>(std::min<std::size_t>(4U, static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, .999999) * 5.0))));
}

SourceNativeAdaptiveResponseTelemetry SourceNativeAdaptiveResponseProcessor::process(const SourceObservation &observation) {
    SourceNativeAdaptiveResponseTelemetry result;
    const double mapperTime = observation.mapperTimeSeconds;
    const double reportedTime = observation.sourceTimeSeconds;
    const double domainMinimum = std::min(observation.domainMinimum, observation.domainMaximum);
    const double domainMaximum = std::max(observation.domainMinimum, observation.domainMaximum);
    const bool accept = observation.sourceReportObserved
        && (m_reports == 0U || observation.sourceSequence != m_lastSequence);
    if (accept) {
        const double interval = m_reports == 0U ? 0.0 : std::max(0.0, reportedTime - m_lastReportTime);
        acceptSource(reportedTime, std::clamp(observation.sourcePosition, domainMinimum, domainMaximum), interval, observation.sourceSequence);
        result.sourceReportAccepted = true;
    }
    std::size_t selected = 0U;
    double selectedRisk = 0.0;
    std::uint8_t selectedBin = 0U;
    const double budget = qualityBudget();
    for (std::size_t h = 0U; h < kHorizonsMs.size(); ++h) {
        double risk = 0.0;
        const std::uint8_t bin = riskBin(mapperTime, kHorizonsMs[h] / 1000.0, &risk);
        if (kCal95[h][bin] <= budget && std::max(kCal95[h][bin], kCal99[h][bin]) <= .060) {
            selected = h;
            selectedRisk = risk;
            selectedBin = bin;
        }
    }
    const double horizon = kHorizonsMs[selected] / 1000.0;
    if (selected == 0U) selectedBin = riskBin(mapperTime, horizon, &selectedRisk);
    const double predicted = q3Predict(mapperTime + horizon);
    const double physical = std::clamp(observation.mapperPhysical, domainMinimum, domainMaximum);
    const double rawLead = predicted - physical;
    const bool ordinaryVeto = (m_reversalGuard > 0U && rawLead * m_direction < 0.0) || m_quiet >= 2U || m_stop >= 2U;
    const double boundedLead = ordinaryVeto ? 0.0 : std::clamp(rawLead, -kMaximumLead, kMaximumLead);
    const double endpoint = std::clamp(physical + boundedLead, domainMinimum, domainMaximum);
    const double endpointLead = endpoint - physical;
    const bool terminalVeto = m_ownership.credibleOpposite && endpointLead * m_ownership.old > kDirectionEpsilon;
    result.q3Position = q3Predict(mapperTime);
    result.q3Velocity = q3Velocity(mapperTime + horizon);
    result.q3Acceleration = q3Acceleration(mapperTime + horizon);
    result.q5Position = q5Predict(mapperTime);
    result.innovation = std::abs(m_innovation);
    result.disagreement = std::abs(q3Predict(mapperTime + horizon) - q5Predict(mapperTime + horizon));
    result.curvature = m_curvature;
    result.sourceTravel = m_travel;
    result.sourceAgeSeconds = m_reports ? std::max(0.0, mapperTime - m_lastReportTime) : 0.0;
    result.sourceIntervalSeconds = m_period;
    result.risk = selectedRisk;
    result.riskBin = selectedBin;
    result.calibrationBound = kCal95[selected][selectedBin];
    result.qualityBudget = budget;
    result.horizonSeconds = horizon;
    result.rawForecast = predicted;
    result.rawLead = rawLead;
    result.boundedLead = boundedLead;
    result.endpointLead = endpointLead;
    result.finalShadowOutput = terminalVeto ? physical : endpoint;
    result.acceptedSourceSequence = m_lastSequence;
    result.ordinarySafetyVeto = ordinaryVeto;
    result.terminalOwnerVeto = terminalVeto;
    if (m_reversalGuard > 0U) --m_reversalGuard;
    return result;
}

} // namespace hotas
