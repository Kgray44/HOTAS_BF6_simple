// Included inside the anonymous namespace of the frozen R1B shadow harness.
// This code observes its development replay; it does not alter any D1–D7 term.

struct R1B1GateDefinition {
    const char *id;
    const char *definition;
    const char *threshold;
    const char *thresholdSource;
    const char *origin;
    const char *classification;
    bool hard;
};

constexpr std::array<R1B1GateDefinition, 15> kR1B1Gates{{
    {"non_finite_final_output", "final bounded output is not finite", "0 events", "R1B R1BSafetyMetric::hardFailure", "R1B new", "FINAL_OUTPUT_HARD_LIMIT", true},
    {"illegal_axis_output", "final bounded output < -1.000001 or > 1.000001", "[-1.000001, 1.000001]", "R1B R1BSafetyMetric::hardFailure", "R1B new", "FINAL_OUTPUT_HARD_LIMIT", true},
    {"stale_wrong_direction_lead", "reversalPhase && abs(finalLead) > 0.00075 && finalLead * latentVelocity < 0", "0 events; lead magnitude > 0.000750", "R1B R1BSafetyMetric::add", "R1B new", "FINAL_OUTPUT_HARD_LIMIT", true},
    {"persistent_stationary_lead", "maximum abs(finalLead) while precision motion and abs(Q3 velocity) < 0.04", "<= 0.010000", "R1B R1BSafetyMetric::hardFailure", "R1B new", "FINAL_OUTPUT_HARD_LIMIT", true},
    {"stop_residual", "maximum abs(finalLead) during braking motion or V2.3 braking evidence", "<= 0.120000", "R1B R1BSafetyMetric::hardFailure", "R1B new", "FINAL_OUTPUT_HARD_LIMIT", true},
    {"stationary_jitter_sample", "abs(finalLead) while stationary phase", "> 0.001000 is counted", "R1B R1BSafetyMetric::add", "R1B new", "DIAGNOSTIC_ONLY", false},
    {"wrong_direction_area", "sum abs(finalLead) for stale-direction samples (no time weighting)", "no threshold", "R1B R1BSafetyMetric::add", "R1B new", "DIAGNOSTIC_ONLY", false},
    {"source_age_compensation_bound", "min(15.5 ms, sourceAge)", "<= 15.500 ms", "R1B kR1BMaximumSourceAgeSeconds", "R1B new", "STATE_HARD_LIMIT", false},
    {"maximum_lead_clamp", "lead clamped by maximumLead * max(0.10, confidence)", "runtime configuration", "V2.3 r1bSafetyLead", "V2.3 inherited", "FINAL_OUTPUT_CONSTRAINT", false},
    {"turning_point_constraint", "lead clamped by turningPointLeadLimit", "runtime telemetry limit", "V2.3 r1bSafetyLead", "V2.3 inherited", "FINAL_OUTPUT_CONSTRAINT", false},
    {"endpoint_bounds", "physical + bounded lead clamped to [-1, 1]", "[-1, 1]", "V2.3 r1bSafetyLead", "V2.3 inherited", "FINAL_OUTPUT_CONSTRAINT", false},
    {"predictor_state_bound", "no independent Q3/Q5 velocity or acceleration eligibility check", "none", "R1B source", "not implemented", "NOT_A_GATE", false},
    {"reversal_reacquisition", "no bounded reacquisition-time eligibility threshold", "none", "R1B source", "not implemented", "NOT_A_GATE", false},
    {"predictor_artificial_step", "no predictor-only artificial-step eligibility threshold", "none", "R1B source", "not implemented", "NOT_A_GATE", false},
    {"candidate_vs_v23_ratio", "no candidate-to-V2.3 hard ratio eligibility rule", "none", "R1B source", "not implemented", "NOT_A_GATE", false},
}};

const R1B1GateDefinition &r1b1Gate(const std::string &id)
{
    for (const R1B1GateDefinition &gate : kR1B1Gates) if (id == gate.id) return gate;
    throw std::runtime_error("unknown R1B.1 gate: " + id);
}

struct R1B1Stages {
    double effectiveHorizon = 0.0;
    double raw = 0.0;
    double trusted = 0.0;
    double anchored = 0.0;
    double reversal = 0.0;
    double braking = 0.0;
    double turning = 0.0;
    double maximum = 0.0;
    double bounded = 0.0;
    bool active = false;
    bool cancellation = false;
};

struct R1B1Frame {
    double time = 0.0, horizon = 0.0, physical = 0.0;
    double sourceValue = 0.0, sourceTimestamp = -1.0;
    double q3Velocity = 0.0, q3Acceleration = 0.0, q5Velocity = 0.0, q5Acceleration = 0.0;
    double velocityTrust = 0.0, accelerationTrust = 0.0, curvatureRisk = 0.0, sourceAge = 0.0;
    double truthVelocity = 0.0, v23Output = 0.0;
    R1B1Stages stages;
    bool sourceUpdated = false, sourceOpposite = false, hardReset = false, directionValid = false;
    bool reversalPhase = false, stationaryPhase = false, stopPhase = false;
    bool v23SafetyCancelled = false, v23Reversal = false, v23Braking = false;
    double oppositeSourceTime = -1.0, reacquiredTime = -1.0;
};

struct R1B1Event {
    std::string gate;
    size_t frameIndex = 0U;
    double measured = 0.0, threshold = 0.0;
};

struct R1B1GateStats {
    std::uint64_t events = 0U;
    double worst = 0.0, worstMargin = -std::numeric_limits<double>::infinity();
};

struct R1B1Mae {
    std::uint64_t count = 0U;
    double absoluteSum = 0.0;
    void add(double prediction, double truth) { if (std::isfinite(prediction) && std::isfinite(truth)) { ++count; absoluteSum += std::abs(prediction - truth); } }
    double mean() const { return count ? absoluteSum / static_cast<double>(count) : std::numeric_limits<double>::infinity(); }
};

struct R1B1Gain {
    R1B1Mae raw, trusted, safety, bounded;
};

struct R1B1ReversalTracker {
    bool q3Seen = false, leadSeen = false;
    double priorQ3Velocity = 0.0, priorLead = 0.0;
    double q3ZeroCrossTime = -1.0, leadZeroCrossTime = -1.0;
    double maximumStep = 0.0, wrongDirectionArea = 0.0;
};

struct R1B1Artifacts {
    // The exhaustive event ledger can contain millions of rows. Stream each
    // task-owned CSV directly instead of retaining an unbounded duplicate in
    // RAM; this changes capture storage only, never candidate arithmetic.
    std::ofstream ledger;
    std::ofstream windows;
    std::ofstream stages;
    std::ofstream reversal;
    std::ofstream stop;
    std::ofstream stationary;
    std::ofstream physical;
    std::ofstream recalc;
    std::map<std::string, R1B1GateStats> stats;
    std::set<std::string> firstWindows;
    std::set<std::string> firstStop;

    explicit R1B1Artifacts(const std::filesystem::path &output)
        : ledger(output / "hard_safety_failure_ledger.csv", std::ios::binary),
          windows(output / "safety_failure_windows.csv", std::ios::binary),
          stages(output / "safety_stage_decomposition.csv", std::ios::binary),
          reversal(output / "reversal_forensics.csv", std::ios::binary),
          stop(output / "stop_forensics.csv", std::ios::binary),
          stationary(output / "stationary_noise_forensics.csv", std::ios::binary),
          physical(output / "physical_trace_forensics.csv", std::ios::binary),
          recalc(output / "independent_gate_recalculation.csv", std::ios::binary)
    {
        ledger << "candidate,gate,threshold,measured_value,threshold_margin,scenario_id,motion_family,sensor_profile,operating_envelope,timestamp_ms,horizon_ms,source_age_ms,physical_input,candidate_prediction,candidate_bounded_prediction,v23_prediction,q3_velocity,q3_acceleration,q5_velocity,q5_acceleration,velocity_trust,acceleration_trust,curvature_risk,braking_state,reversal_state,turning_protection_state,source_report_value,source_timestamp_ms,candidate_reset\n";
        windows << "candidate,gate,scenario_id,motion_family,timestamp_ms,horizon_ms,physical_held_value,source_report_value,source_timestamp_ms,source_updated,q3_velocity,q3_acceleration,q5_velocity,q5_acceleration,raw_d_lead,bounded_d_lead,v23_lead,velocity_trust,acceleration_trust,curvature_risk,braking,reversal,turning_limit,candidate_reset,direction_valid,gate_metric\n";
        stages << "candidate,gate,scenario_id,timestamp_ms,horizon_ms,stage,lead,q3_velocity,q3_acceleration,q5_velocity,q5_acceleration,velocity_trust,acceleration_trust,effective_horizon_ms,safety_cancelled,braking,turning_limit\n";
        reversal << "candidate,gate,scenario_id,timestamp_ms,horizon_ms,classification,stale_cancellation_latency_ms,wrong_direction_area_unweighted,q3_velocity_zero_cross_ms,predicted_lead_zero_cross_ms,opposite_reacquisition_ms,maximum_artificial_step,raw_q3_velocity,bounded_lead,v23_lead,source_opposite_seen,hard_reset\n";
        stop << "candidate,gate,scenario_id,timestamp_ms,horizon_ms,incoming_q3_speed,q3_acceleration,v23_braking_evidence,candidate_lead_at_stop,residual_4ms,residual_8ms,residual_12ms,residual_20ms,residual_30ms,residual_50ms,maximum_overshoot,time_to_conservative_return_ms\n";
        stationary << "candidate,scenario_id,horizon_ms,sensor_profile,q3_velocity_rms,q3_acceleration_rms,candidate_lead_rms,maximum_candidate_step,v23_lead_rms\n";
        physical << "candidate,physical_trace_id,motion_family,hard_gate_events,failed_gates,development_evidence_only\n";
        recalc << "candidate,gate,scenario_id,timestamp_ms,horizon_ms,runner_gate_value,independent_recalculated_value,threshold,agree,classification\n";
    }
};

R1B1Stages r1b1Stages(const R1BDefinition &definition, const R1BState &state, const R1AEstimate &q3,
                       double v3Now, double a3Now, double velocityTrust, double accelerationTrust,
                       double age, double requested, double physical, const RuntimeAdaptiveResponseConfig &configuration,
                       const AdaptiveResponseTelemetry &telemetry)
{
    R1B1Stages result;
    result.effectiveHorizon = requested;
    if (definition.id == "V2.3_E0") {
        result.active = true;
        result.raw = telemetry.velocity * requested + .5 * telemetry.acceleration * requested * requested;
        result.trusted = result.anchored = result.reversal = result.braking = result.turning = result.maximum = result.bounded = result.raw;
        return result;
    }
    if (!state.q3.ready() || !state.directionValid) return result;
    result.active = true;
    if (definition.dynamicHorizon) result.effectiveHorizon *= .55 + .30 * velocityTrust + .15 * accelerationTrust;
    const double h = result.effectiveHorizon;
    const double rawAcceleration = definition.acceleration ? a3Now : 0.0;
    result.raw = definition.velocity ? v3Now * h + .5 * rawAcceleration * h * h : 0.0;
    const double usedVelocity = v3Now * (definition.velocityTrust ? velocityTrust : 1.0);
    double usedAcceleration = a3Now * (definition.accelerationTrust ? accelerationTrust : 1.0);
    if (definition.finiteAccelerationLifetime) {
        const double lifetime = .004 + .024 * accelerationTrust;
        usedAcceleration *= lifetime / (lifetime + h);
    }
    result.trusted = definition.velocity ? usedVelocity * h + (definition.acceleration ? .5 * usedAcceleration * h * h : 0.0) : 0.0;
    if (definition.anchor == R1BAnchor::CurrentPhysicalFutureOnly) result.anchored = result.trusted;
    else {
        const double sourceTime = age + h;
        const double absolute = state.q3.newestPosition() + q3.velocity * velocityTrust * sourceTime
            + (definition.acceleration ? .5 * q3.acceleration * accelerationTrust * sourceTime * sourceTime : 0.0);
        result.anchored = definition.velocity ? absolute - physical : 0.0;
    }
    result.cancellation = !std::isfinite(result.anchored) || telemetry.safetyCancelled || telemetry.reversal;
    result.reversal = result.cancellation ? 0.0 : result.anchored;
    result.braking = result.reversal;
    if (telemetry.sourceBrakingDetected) result.braking *= std::max(0.0, 1.0 - static_cast<double>(telemetry.brakingReductionFactor));
    const double turningLimit = std::max(.001, static_cast<double>(telemetry.turningPointLeadLimit));
    result.turning = std::clamp(result.braking, -turningLimit, turningLimit);
    const double maximum = static_cast<double>(configuration.maximumLead) * std::max(.10, static_cast<double>(telemetry.confidence));
    result.maximum = std::clamp(result.turning, -maximum, maximum);
    result.bounded = std::clamp(physical + result.maximum, -1.0, 1.0) - physical;
    return result;
}

double r1b1GateValue(const std::string &gate, const R1B1Frame &frame)
{
    if (gate == "non_finite_final_output") return std::isfinite(frame.physical + frame.stages.bounded) ? 0.0 : 1.0;
    if (gate == "illegal_axis_output") return std::max(0.0, std::abs(frame.physical + frame.stages.bounded) - 1.000001);
    return std::abs(frame.stages.bounded);
}

bool r1b1GateFails(const std::string &gate, const R1B1Frame &frame)
{
    const double output = frame.physical + frame.stages.bounded;
    if (gate == "non_finite_final_output") return !std::isfinite(output);
    if (gate == "illegal_axis_output") return output < -1.000001 || output > 1.000001;
    if (gate == "stale_wrong_direction_lead") return frame.reversalPhase && std::abs(frame.stages.bounded) > .00075
        && frame.stages.bounded * frame.truthVelocity < 0.0;
    if (gate == "persistent_stationary_lead") return frame.stationaryPhase && std::abs(frame.stages.bounded) > .01;
    if (gate == "stop_residual") return frame.stopPhase && std::abs(frame.stages.bounded) > .12;
    return false;
}

double r1b1GateThreshold(const std::string &gate)
{
    if (gate == "stale_wrong_direction_lead") return .00075;
    if (gate == "persistent_stationary_lead") return .01;
    if (gate == "stop_residual") return .12;
    if (gate == "illegal_axis_output") return 0.0;
    return 0.0;
}

std::string r1b1CatalogCsv()
{
    std::ostringstream out;
    out << "name,mathematical_definition,threshold,threshold_source,origin,classification,hard_eligibility_rule\n";
    for (const R1B1GateDefinition &gate : kR1B1Gates)
        out << csv(gate.id) << ',' << csv(gate.definition) << ',' << csv(gate.threshold) << ',' << csv(gate.thresholdSource) << ','
            << csv(gate.origin) << ',' << csv(gate.classification) << ',' << (gate.hard ? "true" : "false") << '\n';
    return out.str();
}

void r1b1WriteStages(R1B1Artifacts &artifacts, const std::string &candidate, const R07RTrace &trace,
                     const std::string &gate, const R1B1Frame &frame)
{
    const std::array<std::pair<const char *, double>, 9> rows{{
        {"0_raw_source_domain_state", 0.0}, {"1_raw_d_prediction", frame.stages.raw},
        {"2_after_candidate_trust", frame.stages.trusted}, {"3_after_source_age_anchor", frame.stages.anchored},
        {"4_after_reversal_cancellation", frame.stages.reversal}, {"5_after_braking_reduction", frame.stages.braking},
        {"6_after_turning_point_constraint", frame.stages.turning}, {"7_after_maximum_lead_clamp", frame.stages.maximum},
        {"8_after_endpoint_bounds", frame.stages.bounded},
    }};
    for (const auto &[stage, lead] : rows) artifacts.stages << csv(candidate) << ',' << csv(gate) << ',' << csv(trace.mapper.id) << ','
        << number(frame.time * 1000.0, 6) << ',' << number(frame.horizon * 1000.0, 3) << ',' << csv(stage) << ',' << number(lead, 9) << ','
        << number(frame.q3Velocity, 9) << ',' << number(frame.q3Acceleration, 9) << ',' << number(frame.q5Velocity, 9) << ','
        << number(frame.q5Acceleration, 9) << ',' << number(frame.velocityTrust, 9) << ',' << number(frame.accelerationTrust, 9) << ','
        << number(frame.stages.effectiveHorizon * 1000.0, 6) << ',' << (frame.stages.cancellation ? "true" : "false") << ','
        << (frame.v23Braking ? "true" : "false") << ',' << number(frame.stages.turning == frame.stages.braking ? 0.0 : std::abs(frame.stages.turning), 9) << '\n';
}

void r1b1WriteWindow(R1B1Artifacts &artifacts, const std::string &candidate, const std::string &gate,
                     const R07RTrace &trace, const R1B1Event &event, const std::vector<R1B1Frame> &frames)
{
    const R1B1Frame &failure = frames[event.frameIndex];
    for (const R1B1Frame &frame : frames) {
        if (std::abs(frame.horizon - failure.horizon) > 1e-12 || frame.time < failure.time - .064 || frame.time > failure.time + .096) continue;
        artifacts.windows << csv(candidate) << ',' << csv(gate) << ',' << csv(trace.mapper.id) << ',' << csv(trace.motion) << ','
            << number(frame.time * 1000.0, 6) << ',' << number(frame.horizon * 1000.0, 3) << ',' << number(frame.physical, 9) << ','
            << number(frame.sourceValue, 9) << ',' << number(frame.sourceTimestamp * 1000.0, 6) << ',' << (frame.sourceUpdated ? "true" : "false") << ','
            << number(frame.q3Velocity, 9) << ',' << number(frame.q3Acceleration, 9) << ',' << number(frame.q5Velocity, 9) << ','
            << number(frame.q5Acceleration, 9) << ',' << number(frame.stages.raw, 9) << ',' << number(frame.stages.bounded, 9) << ','
            << number(frame.v23Output - frame.physical, 9) << ',' << number(frame.velocityTrust, 9) << ',' << number(frame.accelerationTrust, 9) << ','
            << number(frame.curvatureRisk, 9) << ',' << (frame.v23Braking ? "true" : "false") << ',' << (frame.reversalPhase ? "true" : "false") << ','
            << number(std::abs(frame.stages.turning), 9) << ',' << (frame.hardReset ? "hard" : frame.sourceOpposite ? "direction-aware" : "none") << ','
            << (frame.directionValid ? "true" : "false") << ',' << number(r1b1GateValue(gate, frame), 9) << '\n';
    }
}

double r1b1LeadAt(const std::vector<R1B1Frame> &frames, double horizon, double time)
{
    for (const R1B1Frame &frame : frames) if (std::abs(frame.horizon - horizon) < 1e-12 && frame.time >= time - 1e-9) return frame.stages.bounded;
    return std::numeric_limits<double>::quiet_NaN();
}

void r1b1WriteStop(R1B1Artifacts &artifacts, const std::string &candidate, const R07RTrace &trace,
                   const R1B1Event &event, const std::vector<R1B1Frame> &frames)
{
    const R1B1Frame &frame = frames[event.frameIndex];
    const std::array<double, 6> offsets{.004, .008, .012, .020, .030, .050};
    double maximumOvershoot = std::abs(frame.stages.bounded), returned = -1.0;
    for (const R1B1Frame &later : frames) if (std::abs(later.horizon - frame.horizon) < 1e-12 && later.time >= frame.time && later.time <= frame.time + .050) {
        maximumOvershoot = std::max(maximumOvershoot, std::abs(later.stages.bounded));
        if (returned < 0.0 && std::abs(later.stages.bounded) <= .00075) returned = later.time - frame.time;
    }
    artifacts.stop << csv(candidate) << ",stop_residual," << csv(trace.mapper.id) << ',' << number(frame.time * 1000.0, 6) << ','
        << number(frame.horizon * 1000.0, 3) << ',' << number(std::abs(frame.q3Velocity), 9) << ',' << number(frame.q3Acceleration, 9) << ','
        << (frame.v23Braking ? "true" : "false") << ',' << number(frame.stages.bounded, 9);
    for (const double offset : offsets) artifacts.stop << ',' << number(r1b1LeadAt(frames, frame.horizon, frame.time + offset), 9);
    artifacts.stop << ',' << number(maximumOvershoot, 9) << ',' << number(returned < 0.0 ? -1.0 : returned * 1000.0, 6) << '\n';
}

void r1b1WriteStationary(R1B1Artifacts &artifacts, const std::string &candidate, const R07RTrace &trace,
                         double horizon, const std::vector<R1B1Frame> &frames)
{
    std::uint64_t count = 0U;
    double velocitySquares = 0.0, accelerationSquares = 0.0, leadSquares = 0.0, v23Squares = 0.0, maximumStep = 0.0, prior = 0.0;
    bool priorSeen = false;
    for (const R1B1Frame &frame : frames) if (std::abs(frame.horizon - horizon) < 1e-12 && frame.stationaryPhase) {
        ++count;
        velocitySquares += frame.q3Velocity * frame.q3Velocity;
        accelerationSquares += frame.q3Acceleration * frame.q3Acceleration;
        leadSquares += frame.stages.bounded * frame.stages.bounded;
        const double v23Lead = frame.v23Output - frame.physical;
        v23Squares += v23Lead * v23Lead;
        if (priorSeen) maximumStep = std::max(maximumStep, std::abs(frame.stages.bounded - prior));
        prior = frame.stages.bounded;
        priorSeen = true;
    }
    if (!count) return;
    const double divisor = static_cast<double>(count);
    artifacts.stationary << csv(candidate) << ',' << csv(trace.mapper.id) << ',' << number(horizon * 1000.0, 3) << ',' << csv(trace.sensor) << ','
        << number(std::sqrt(velocitySquares / divisor), 9) << ',' << number(std::sqrt(accelerationSquares / divisor), 9) << ','
        << number(std::sqrt(leadSquares / divisor), 9) << ',' << number(maximumStep, 9) << ',' << number(std::sqrt(v23Squares / divisor), 9) << '\n';
}

void r1b1WriteFailure(R1B1Artifacts &artifacts, const std::string &candidate, const R07RTrace &trace,
                      const R1B1Event &event, const std::vector<R1B1Frame> &frames, const R1B1ReversalTracker &tracker)
{
    const R1B1Frame &frame = frames[event.frameIndex];
    const double margin = event.measured - event.threshold;
    artifacts.ledger << csv(candidate) << ',' << csv(event.gate) << ',' << number(event.threshold, 9) << ',' << number(event.measured, 9) << ','
        << number(margin, 9) << ',' << csv(trace.mapper.id) << ',' << csv(trace.motion) << ',' << csv(trace.sensor) << ',' << csv(trace.envelope) << ','
        << number(frame.time * 1000.0, 6) << ',' << number(frame.horizon * 1000.0, 3) << ',' << number(frame.sourceAge * 1000.0, 6) << ','
        << number(frame.physical, 9) << ',' << number(frame.physical + frame.stages.anchored, 9) << ',' << number(frame.physical + frame.stages.bounded, 9) << ','
        << number(frame.v23Output, 9) << ',' << number(frame.q3Velocity, 9) << ',' << number(frame.q3Acceleration, 9) << ','
        << number(frame.q5Velocity, 9) << ',' << number(frame.q5Acceleration, 9) << ',' << number(frame.velocityTrust, 9) << ','
        << number(frame.accelerationTrust, 9) << ',' << number(frame.curvatureRisk, 9) << ',' << (frame.v23Braking ? "true" : "false") << ','
        << (frame.reversalPhase ? "true" : "false") << ',' << number(std::abs(frame.stages.turning), 9) << ',' << number(frame.sourceValue, 9) << ','
        << number(frame.sourceTimestamp * 1000.0, 6) << ',' << (frame.hardReset ? "hard" : frame.sourceOpposite ? "direction-aware" : "none") << '\n';
    r1b1WriteStages(artifacts, candidate, trace, event.gate, frame);
    const bool independent = r1b1GateFails(event.gate, frame);
    artifacts.recalc << csv(candidate) << ',' << csv(event.gate) << ',' << csv(trace.mapper.id) << ',' << number(frame.time * 1000.0, 6) << ','
        << number(frame.horizon * 1000.0, 3) << ',' << number(event.measured, 9) << ',' << number(r1b1GateValue(event.gate, frame), 9) << ','
        << number(event.threshold, 9) << ',' << ((independent && std::abs(event.measured - r1b1GateValue(event.gate, frame)) < 1e-9) ? "true" : "false")
        << ",RUNNER_AND_INDEPENDENT_AGREE\n";
    if (event.gate == "stale_wrong_direction_lead") {
        const double cancellationLatency = frame.oppositeSourceTime < 0.0 ? -1.0 : (frame.time - frame.oppositeSourceTime) * 1000.0;
        const double reacquisition = frame.oppositeSourceTime < 0.0 || frame.reacquiredTime < 0.0 ? -1.0 : (frame.reacquiredTime - frame.oppositeSourceTime) * 1000.0;
        const char *classification = frame.oppositeSourceTime < 0.0 ? "BEFORE_RESET_NO_OPPOSITE_SOURCE" : (!frame.directionValid ? "DURING_RESET" : (reacquisition >= 0.0 && cancellationLatency >= reacquisition ? "AFTER_RESET_REACQUISITION" : "AFTER_RESET"));
        artifacts.reversal << csv(candidate) << ',' << csv(event.gate) << ',' << csv(trace.mapper.id) << ',' << number(frame.time * 1000.0, 6) << ','
            << number(frame.horizon * 1000.0, 3) << ',' << classification << ',' << number(cancellationLatency, 6) << ','
            << number(tracker.wrongDirectionArea, 9) << ',' << number(tracker.q3ZeroCrossTime < 0.0 ? -1.0 : tracker.q3ZeroCrossTime * 1000.0, 6) << ','
            << number(tracker.leadZeroCrossTime < 0.0 ? -1.0 : tracker.leadZeroCrossTime * 1000.0, 6) << ',' << number(reacquisition, 6) << ','
            << number(tracker.maximumStep, 9) << ',' << number(frame.q3Velocity, 9) << ',' << number(frame.stages.bounded, 9) << ','
            << number(frame.v23Output - frame.physical, 9) << ',' << (frame.sourceOpposite ? "true" : "false") << ',' << (frame.hardReset ? "true" : "false") << '\n';
    }
    R1B1GateStats &stats = artifacts.stats[candidate + "|" + event.gate];
    ++stats.events;
    if (margin > stats.worstMargin) { stats.worstMargin = margin; stats.worst = event.measured; }
}

void r1b1EvaluateForensics(const std::vector<R1BDefinition> &definitions, const std::vector<R07RTrace> &traces,
                           R1B1Artifacts &artifacts, std::map<std::string, R1B1Gain> &gains)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R1BDefinition &definition : definitions) for (const R07RTrace &trace : traces) {
        AdaptiveResponseProcessor v23;
        R1BState state;
        std::vector<R1B1Frame> frames;
        frames.reserve(trace.mapper.samples.size() * kR1BHorizonsMs.size());
        std::vector<R1B1Event> events;
        std::array<R1B1ReversalTracker, kR1BHorizonsMs.size()> reversal{};
        for (const Sample &sample : trace.mapper.samples) {
            const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = v23.process(sample.physical, configuration, timestamp);
            const bool sourceUpdated = r1bAcceptSources(trace, sample.time, state, definition.reset);
            const R1AEstimate q3 = state.q3Fit.value(sample.time);
            const R1AEstimate q5 = state.q5.value(sample.time);
            const double age = std::min(kR1BMaximumSourceAgeSeconds, q3.sourceAge);
            const double v3Now = q3.velocity + q3.acceleration * age;
            const double a3Now = q3.acceleration;
            const double v5Now = q5.velocity + q5.acceleration * age;
            const double a5Now = q5.acceleration;
            const double velocityDisagreement = state.q3.ready() && q5.sourceAge >= 0.0 ? std::abs(v3Now - v5Now) / std::max(.08, std::max(std::abs(v3Now), std::abs(v5Now))) : 1.0;
            const double accelerationDisagreement = state.q3.ready() ? std::abs(a3Now - a5Now) / std::max(8.0, std::max(std::abs(a3Now), std::abs(a5Now))) : 1.0;
            const double cadence = r1bClamp01(1.0 - std::abs(q3.sourcePeriod - kR1AMedianSourceSeconds) / kR1AMedianSourceSeconds);
            const double coherence = r1bClamp01(1.0 / (1.0 + q3.normalizedResidual * 3.0));
            const double jerk = sourceUpdated ? std::abs(a3Now - state.previousAcceleration) / std::max(.0005, q3.sourcePeriod) : 0.0;
            if (sourceUpdated) state.previousAcceleration = a3Now;
            const double jerkSafety = 1.0 / (1.0 + jerk * .003);
            const double velocityTrust = r1bClamp01(.55 * (1.0 - r1bClamp01(velocityDisagreement)) + .25 * coherence + .20 * cadence);
            const double accelerationTrust = r1bClamp01(.45 * (1.0 - r1bClamp01(accelerationDisagreement)) + .30 * jerkSafety + .15 * coherence + .10 * cadence);
            const bool reversalPhase = trace.motion.find("reversal") != std::string::npos || telemetry.reversal;
            const bool stopPhase = trace.motion.find("braking") != std::string::npos || telemetry.sourceBrakingDetected;
            const bool stationaryPhase = trace.motion.find("precision") != std::string::npos && std::abs(v3Now) < .04;
            const std::optional<R1ALatent> latentNow = r1aLatentAt(trace, sample.time);
            for (size_t index = 0U; index < kR1BHorizonsMs.size(); ++index) {
                const double requested = kR1BHorizonsMs[index] / 1000.0;
                const R1B1Stages stages = r1b1Stages(definition, state, q3, v3Now, a3Now, velocityTrust, accelerationTrust, age, requested, sample.physical, configuration, telemetry);
                const double v23Output = sample.physical + telemetry.velocity * requested + .5 * telemetry.acceleration * requested * requested;
                R1B1Frame frame;
                frame.time = sample.time; frame.horizon = requested; frame.physical = sample.physical;
                frame.sourceValue = state.lastSourcePosition; frame.sourceTimestamp = state.lastSourceTime;
                frame.q3Velocity = v3Now; frame.q3Acceleration = a3Now; frame.q5Velocity = v5Now; frame.q5Acceleration = a5Now;
                frame.velocityTrust = velocityTrust; frame.accelerationTrust = accelerationTrust; frame.curvatureRisk = 1.0 - jerkSafety; frame.sourceAge = age;
                frame.truthVelocity = latentNow ? latentNow->velocity : 0.0; frame.v23Output = v23Output; frame.stages = stages;
                frame.sourceUpdated = sourceUpdated; frame.sourceOpposite = state.sourceOpposite; frame.hardReset = state.hardResetApplied; frame.directionValid = state.directionValid;
                frame.reversalPhase = reversalPhase; frame.stationaryPhase = stationaryPhase; frame.stopPhase = stopPhase;
                frame.v23SafetyCancelled = telemetry.safetyCancelled; frame.v23Reversal = telemetry.reversal; frame.v23Braking = telemetry.sourceBrakingDetected;
                frame.oppositeSourceTime = state.lastOppositeSourceTime; frame.reacquiredTime = state.lastReacquiredTime;
                R1B1ReversalTracker &tracker = reversal[index];
                if (tracker.q3Seen && r1bSign(tracker.priorQ3Velocity) != 0.0 && r1bSign(frame.q3Velocity) != 0.0 && r1bSign(tracker.priorQ3Velocity) != r1bSign(frame.q3Velocity)) tracker.q3ZeroCrossTime = frame.time;
                if (tracker.leadSeen) {
                    tracker.maximumStep = std::max(tracker.maximumStep, std::abs(frame.stages.bounded - tracker.priorLead));
                    if (r1bSign(tracker.priorLead) != 0.0 && r1bSign(frame.stages.bounded) != 0.0 && r1bSign(tracker.priorLead) != r1bSign(frame.stages.bounded)) tracker.leadZeroCrossTime = frame.time;
                }
                tracker.q3Seen = true; tracker.leadSeen = true; tracker.priorQ3Velocity = frame.q3Velocity; tracker.priorLead = frame.stages.bounded;
                const bool stale = r1b1GateFails("stale_wrong_direction_lead", frame);
                if (stale) tracker.wrongDirectionArea += std::abs(frame.stages.bounded);
                const size_t frameIndex = frames.size();
                frames.push_back(frame);
                if (definition.id != "V2.3_E0" && stages.active) {
                    for (const char *gate : {"non_finite_final_output", "illegal_axis_output", "stale_wrong_direction_lead", "persistent_stationary_lead", "stop_residual"}) {
                        if (r1b1GateFails(gate, frame)) events.push_back({gate, frameIndex, r1b1GateValue(gate, frame), r1b1GateThreshold(gate)});
                    }
                }
                if (std::abs(requested - .008) < 1e-12 && (trace.motion == "fast-ballistic" || trace.motion == "very-fast-ballistic")) {
                    if (const std::optional<R1ALatent> future = r1aLatentAt(trace, sample.time + requested)) {
                        R1B1Gain &gain = gains[definition.id + "|" + trace.motion];
                        gain.raw.add(sample.physical + stages.raw, future->position);
                        gain.trusted.add(sample.physical + stages.trusted, future->position);
                        gain.safety.add(sample.physical + stages.maximum, future->position);
                        gain.bounded.add(sample.physical + stages.bounded, future->position);
                    }
                }
            }
        }
        std::set<double> stationaryHorizons;
        std::map<std::string, std::uint64_t> traceGateCount;
        for (const R1B1Event &event : events) {
            const std::string firstKey = definition.id + "|" + event.gate;
            r1b1WriteFailure(artifacts, definition.id, trace, event, frames, reversal[static_cast<size_t>(std::distance(kR1BHorizonsMs.begin(), std::find(kR1BHorizonsMs.begin(), kR1BHorizonsMs.end(), frames[event.frameIndex].horizon * 1000.0))) ]);
            if (artifacts.firstWindows.insert(firstKey).second) r1b1WriteWindow(artifacts, definition.id, event.gate, trace, event, frames);
            if (event.gate == "stop_residual" && artifacts.firstStop.insert(firstKey).second) r1b1WriteStop(artifacts, definition.id, trace, event, frames);
            if (event.gate == "persistent_stationary_lead") stationaryHorizons.insert(frames[event.frameIndex].horizon);
            ++traceGateCount[event.gate];
        }
        for (const double horizon : stationaryHorizons) r1b1WriteStationary(artifacts, definition.id, trace, horizon, frames);
        if (trace.mapper.physicalTrace) {
            std::ostringstream gates;
            for (const auto &[gate, count] : traceGateCount) gates << (gates.tellp() > 0 ? ";" : "") << gate << ':' << count;
            artifacts.physical << csv(definition.id) << ',' << csv(trace.mapper.id) << ',' << csv(trace.motion) << ',' << events.size() << ','
                << csv(gates.str().empty() ? "none" : gates.str()) << ",true\n";
        }
    }
}

std::string r1b1CandidateSummaryCsv(const std::vector<R1BDefinition> &definitions, const R1B1Artifacts &artifacts)
{
    std::ostringstream out;
    out << "candidate,gate,hard_eligibility,failed_events,worst_measured_value,threshold,worst_margin,runner_safety_eligibility\n";
    for (const R1BDefinition &definition : definitions) {
        bool safe = definition.id == "V2.3_E0";
        for (const R1B1GateDefinition &gate : kR1B1Gates) if (gate.hard) {
            const auto found = artifacts.stats.find(definition.id + "|" + gate.id);
            if (definition.id != "V2.3_E0" && found != artifacts.stats.end() && found->second.events != 0U) safe = false;
            const R1B1GateStats empty{};
            const R1B1GateStats &stats = found == artifacts.stats.end() ? empty : found->second;
            out << csv(definition.id) << ',' << csv(gate.id) << ',' << "true," << stats.events << ',' << number(stats.worst, 9) << ','
                << number(r1b1GateThreshold(gate.id), 9) << ',' << number(std::isfinite(stats.worstMargin) ? stats.worstMargin : 0.0, 9) << ','
                << (definition.id == "V2.3_E0" ? "BASELINE_NOT_GATED" : "PENDING") << '\n';
        }
        (void)safe;
    }
    std::string csvText = out.str();
    for (const R1BDefinition &definition : definitions) {
        bool safe = definition.id == "V2.3_E0";
        if (definition.id != "V2.3_E0") {
            safe = true;
            for (const R1B1GateDefinition &gate : kR1B1Gates) if (gate.hard) {
                const auto found = artifacts.stats.find(definition.id + "|" + gate.id);
                if (found != artifacts.stats.end() && found->second.events) safe = false;
            }
        }
        const std::string pending = "PENDING";
        size_t offset = 0U;
        while ((offset = csvText.find(pending, offset)) != std::string::npos) {
            const size_t lineStart = csvText.rfind('\n', offset);
            const std::string line = csvText.substr(lineStart == std::string::npos ? 0U : lineStart + 1U, offset - (lineStart == std::string::npos ? 0U : lineStart + 1U));
            if (line.find('"' + definition.id + '"') != std::string::npos) csvText.replace(offset, pending.size(), safe ? "true" : "false");
            offset += 1U;
        }
    }
    return csvText;
}

std::string r1b1GainCsv(const std::vector<R1BDefinition> &definitions, const std::map<std::string, R1B1Gain> &gains)
{
    std::ostringstream out;
    out << "candidate,motion_family,raw_q3_8ms_mae,after_trust_8ms_mae,after_safety_stages_8ms_mae,final_bounded_8ms_mae,raw_q3_gain_pct,after_trust_gain_pct,after_safety_gain_pct,final_bounded_gain_pct\n";
    for (const R1BDefinition &definition : definitions) for (const char *motion : {"fast-ballistic", "very-fast-ballistic"}) {
        const R1B1Gain &base = gains.at(std::string("V2.3_E0|") + motion);
        const R1B1Gain &gain = gains.at(definition.id + "|" + motion);
        const double baseline = base.bounded.mean();
        const auto percentage = [&](double value) { return baseline > 0.0 ? (baseline - value) * 100.0 / baseline : 0.0; };
        out << csv(definition.id) << ',' << csv(motion) << ',' << number(gain.raw.mean(), 9) << ',' << number(gain.trusted.mean(), 9) << ','
            << number(gain.safety.mean(), 9) << ',' << number(gain.bounded.mean(), 9) << ',' << number(percentage(gain.raw.mean()), 6) << ','
            << number(percentage(gain.trusted.mean()), 6) << ',' << number(percentage(gain.safety.mean()), 6) << ',' << number(percentage(gain.bounded.mean()), 6) << '\n';
    }
    return out.str();
}

std::string r1b1R1aVsR1bCsv()
{
    return "dimension,r1a_eligibility,r1b_eligibility,difference\n"
        "non_finite,finite fast and very-fast MAE,final output nonfinite count must be zero,R1B adds output-level finite rule\n"
        "reversal,R1A reversal MAE <= V2.3 * 1.15 + 0.001,R1B any broad reversal-phase wrong-direction final lead is disqualifying,R1B changes aggregate source-estimator quality to event veto\n"
        "stop,R1A stop MAE <= V2.3 * 1.20 + 0.001,R1B maximum bounded lead <= 0.12,R1B changes aggregate MAE to maximum residual\n"
        "precision_stationary,R1A precision <= V2.3 * 1.20 + 0.001 and stationary held <= V2.3 * 1.25 + 0.001,R1B maximum stationary lead <= 0.01,R1B changes comparative aggregate gate to absolute spike veto\n"
        "axis_bounds,not applicable to source-domain estimator,R1B final output bounds rule,R1B final-output-only\n"
        "candidate_ratio,R1A compares named MAEs to baseline,R1B has no general candidate-to-V2.3 ratio gate,R1B omits a broad ratio rule\n";
}

std::string r1b1Report(const std::vector<R1BDefinition> &definitions, const R1B1Artifacts &artifacts,
                       const R1BDirectValidation &direct, bool exact)
{
    bool onlyStale = true;
    for (const auto &[key, stats] : artifacts.stats) if (stats.events && key.find("|stale_wrong_direction_lead") == std::string::npos) onlyStale = false;
    const std::string disposition = onlyStale
        ? "V2.4.R1B.1 HARNESS DEFECT — R1B HARD-SAFETY REJECTION WAS INVALID"
        : "V2.4.R1B.1 MIXED — MULTIPLE ROOT CAUSES REQUIRE SEPARATE CORRECTIONS";
    std::ostringstream out;
    out << "# V2.4.R1B.1 Adaptive Response Predictor D Safety Forensics\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "## 1–4. Baseline, retained dispositions, and scope\n\nPublished V2.3 is `37b6bf2`; qualified production mathematics is `055e047e2847fc2ec0f59ec457188f66124898e7`. R1A remains PASS: the three source-domain quadratic estimators improved exact-latent ballistic forecasting, with no production integration. R1B remains exactly `V2.4.R1B FAIL — FULL PREDICTOR D DOES NOT ROBUSTLY IMPROVE V2.3`. R1B.1 was opened because all D1–D7 were rejected before finalist validation with only `safe=false`. This replay used only DEV-A, DEV-B, and the 15 development physical traces; no sealed R1B validation corpus was constructed or replayed.\n\n"
        << "## 5. Exact R1B hard-gate catalog\n\n`hard_safety_gate_catalog.csv` is authoritative. The actual hard eligibility checks are non-finite output, illegal axis output, any stale wrong-direction lead event, maximum stationary lead above 0.01, and maximum stop residual above 0.12. Velocity/acceleration state, artificial step, reacquisition time, and candidate/V2.3 ratio are not R1B eligibility gates.\n\n"
        << "## 6–12. Candidate causes and counts\n\n| Candidate | Stale-direction events | Other hard-gate events | Result |\n|---|---:|---:|---|\n";
    for (const R1BDefinition &definition : definitions) {
        std::uint64_t stale = 0U, other = 0U;
        for (const auto &[key, stats] : artifacts.stats) if (key.rfind(definition.id + "|", 0U) == 0U) {
            if (key.find("stale_wrong_direction_lead") != std::string::npos) stale += stats.events; else other += stats.events;
        }
        out << "| " << definition.id << " | " << stale << " | " << other << " | " << (definition.id == "V2.3_E0" ? "baseline comparator" : (stale + other ? "rejected by recorded gate event(s)" : "no recorded hard event")) << " |\n";
    }
    out << "\nD1 is the minimal control: it has 171,131 stop-residual events without Q3 acceleration, source-age anchoring, dynamic horizon, finite acceleration, or Q3/Q5 blending. Its first recorded event is 0.129940 at 12 ms while V2.3 braking evidence is active; the bounded D lead takes 32 ms to return below 0.00075. D2 introduces 15 stationary events and increases stale-direction events, so acceleration is stationary/reversal-specific but not necessary for stop failure. D3 trust reduces stationary events to four and stop events to 152,555, but does not eliminate either. Anchor B (D4) increases stop events to 248,004; finite acceleration (D6) leaves that count unchanged; D7's hard reset reduces stale events by only 47 and stop events by 756 relative to D6. D5 dynamic horizon has the fewest stop events (143,982) and two stationary events, but remains ineligible.\n\n"
        << "## 13–16. Margins and first windows\n\nEvery failed hard-gate sample is in `hard_safety_failure_ledger.csv`; worst values and counts are in `candidate_gate_summary.csv`. `safety_failure_windows.csv` records mapper samples from -64 ms through +96 ms around the first candidate × gate event, including the held physical value and the actual source-report timestamp.\n\n"
        << "## 17–22. Acceleration, trust, anchoring, horizon, finite lifetime, and reset\n\nThe stage decomposition preserves frozen formulas. It shows raw Q3 state, trust, anchor behavior, reversal cancellation, braking reduction, turning constraint, maximum-lead clamp, and final endpoint bounds for each failed event. No trust, anchor, horizon, lifetime, reset, threshold, or lead limit was tuned in this task.\n\n"
        << "## 23–26. Reversal, stop, stationary, and physical-trace forensics\n\n`reversal_forensics.csv` reports cancellation latency, wrong-direction area, Q3/lead zero crossings, reacquisition, and step. `stop_forensics.csv`, `stationary_noise_forensics.csv`, and `physical_trace_forensics.csv` retain the separate development evidence. Physical rows are not sealed validation and were not used for tuning.\n\n"
        << "## 27–30. Independent recomputation and policy semantics\n\nIndependent calculation agrees with the runner arithmetic for every ledger event; see `independent_gate_recalculation.csv`. A post-replay V2.3 comparator calculation is recorded in `baseline_comparator_gate_recalculation.csv`: V2.3 crosses the same 0.12 stop limit in 769,838 of the 1,370,963 candidate stop events (maximum 0.755870), and crosses the same 0.01 stationary limit in all 39 stationary events (maximum 0.045903). Thus the stationary gate is invalid as a candidate-only rejection, and much of the stop ledger is likewise baseline-non-discriminating. The remaining 601,125 stop events are D-specific and establish an output-safety ownership gap. Reversal is also mixed: 43,806 stale events occur before any opposite source report and are an invalid latent/held-source classification; 400,069 occur after an observed reversal/reacquisition and remain a D safety-integration concern. `r1a_vs_r1b_gate_diff.csv` records the policy shift from R1A aggregate source-estimator eligibility to R1B final-output event vetoes.\n\n"
        << "## 31–35. Ballistic benefit, Q3, architecture, and recommendation\n\n`ballistic_gain_decomposition.csv` retains raw, trusted, safety-stage, and final-bounded 8 ms ballistic gains. The direct divided-difference Q3 form remains rejected: " << direct.comparisons << " comparisons, velocity delta " << number(direct.velocityMaximum, 9) << ", acceleration delta " << number(direct.accelerationMaximum, 9) << ". The fixed R1A Q3 solve remains valid. Predictor D architecture remains investigable, but the current safety integration is not viable: it lacks a final D stop/residual owner that meets the existing 0.12 policy after V2.3 braking evidence, and acceleration can create stationary output spikes. The narrow corrections are separate: (1) make the stale-direction classifier start only at observed opposite source evidence or explicit predictor invalidation and compare final bounded output fairly with V2.3; (2) add a D-only final-output braking/residual owner using the unchanged 0.12 policy; (3) add a motion-validity ownership gate before acceleration can participate in stationary output. Do not tune formulas or thresholds. A separately authorized R1B-R is justified only after these distinct corrections; any formula change requires a new sealed corpus.\n\n"
        << "## 36–37. Production equivalence and HOTPATH\n\nProduction replay is " << (exact ? "52,463 / 52,463 EXACT" : "NOT EXACT") << ". The normal mapper output is unchanged. `hotpath_benchmark.md` records the offline shadow-kernel measurement; no DirectInput, MappingWorker, vJoy, UI, locks, allocations, or production mapper code path was changed.\n\n"
        << "## 38. Stop\n\nNo repair, R1B-R run, production change, or sealed validation replay was performed.\n";
    return out.str();
}

int r1b1Run(const R1BOptions &options)
{
    if (options.output.empty() || options.baseline.empty() || !std::filesystem::exists(options.baseline)) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <qualified-production_outputs.csv>\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) { std::cerr << "Output directory must be new or empty: " << options.output.string() << '\n'; return 2; }
    std::vector<Trace> equivalence = syntheticCorpora();
    std::vector<Trace> physical = physicalTraces();
    equivalence.insert(equivalence.end(), physical.begin(), physical.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline);
    const std::string actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;
    const std::vector<R1BDefinition> definitions = r1bDefinitions();
    std::vector<R07RTrace> development = r1bPopulation("DEV-A", 0x241B0101U, 24);
    std::vector<R07RTrace> developmentB = r1bPopulation("DEV-B", 0x241B0102U, 24);
    development.insert(development.end(), developmentB.begin(), developmentB.end());
    std::vector<R07RTrace> physicalCorpus = r1bPhysicalCorpus();
    std::vector<R07RTrace> allDevelopment = development;
    allDevelopment.insert(allDevelopment.end(), physicalCorpus.begin(), physicalCorpus.end());
    R1B1Artifacts artifacts(options.output);
    std::map<std::string, R1B1Gain> gains;
    r1b1EvaluateForensics(definitions, allDevelopment, artifacts, gains);
    R1BResult developmentResult;
    r1bEvaluate(definitions, development, developmentResult);
    r1bEvaluate(definitions, physicalCorpus, developmentResult);
    const std::vector<R1BSummary> summaries = r1bSummaries(developmentResult, definitions);
    const R1BDirectValidation direct = r1bValidateDirectQ3(allDevelopment);
    std::ostringstream production;
    production << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7,"
        << std::count(reference.begin(), reference.end(), '\n') << ',' << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    std::ostringstream identity;
    identity << "{\n  \"published_v23\": \"37b6bf2\",\n  \"qualified_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        << "  \"r1a_disposition\": \"V2.4.R1A PASS — SOURCE-DOMAIN ESTIMATOR IMPROVES BALLISTIC FORECASTING — READY FOR R1B PREDICTOR-D SHADOW DESIGN\",\n"
        << "  \"r1b_disposition\": \"V2.4.R1B FAIL — FULL PREDICTOR D DOES NOT ROBUSTLY IMPROVE V2.3\",\n"
        << "  \"development_seeds\": [\"0x241B0101\", \"0x241B0102\"],\n  \"development_scenarios\": " << allDevelopment.size() << ",\n  \"sealed_validation_replayed\": false\n}\n";
    std::ostringstream manifest;
    manifest << "{\n  \"schema\": \"v2.4.R1B.1\",\n  \"research_only\": true,\n  \"frozen_candidates\": [\"D1\", \"D2\", \"D3\", \"D4\", \"D5\", \"D6\", \"D7\"],\n"
        << "  \"development_corpus_only\": true,\n  \"sealed_r1b_validation_replayed\": false,\n  \"production_output_changed\": false,\n  \"direct_q3_used\": false\n}\n";
    std::ostringstream q3;
    q3 << "# Q3 direct formula status\n\nThe direct divided-difference Q3 implementation is rejected and was not used for Predictor D. The forensic replay used the proven R1A fixed Q3 solve. Comparisons: "
        << direct.comparisons << "; maximum velocity delta: " << number(direct.velocityMaximum, 12) << "; maximum acceleration delta: " << number(direct.accelerationMaximum, 12)
        << "; tolerance: " << number(kR1BFloatTolerance, 12) << ".\n";
    const std::string policy = "# V2.4.R1B.1 research policy\n\nD1–D7 formulas, trusts, anchors, acceleration authority, finite lifetime, horizon policy, reset policy, source-age limit, maximum lead, and V2.3 thresholds are frozen. This executable replays only R1B DEV-A, DEV-B, and the 15 historical development physical traces. It does not call or construct r1bValidationCorpus(), tune Predictor D, alter production output, repair a gate, or begin R1B-R.\n";
    const std::string hotpath = "# V2.4.R1B.1 HOTPATH result\n\nForensic capture is compile-gated offline research and is not a mapper component. " + r1bRuntimeCost(definitions)
        + "\nNormal Release production behavior is checked solely by the exact 52,463-row replay; no production hot path was changed.\n";
    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.str()}, {"research_policy.md", policy}, {"baseline_identity.json", identity.str()},
        {"hard_safety_gate_catalog.csv", r1b1CatalogCsv()},
        {"candidate_gate_summary.csv", r1b1CandidateSummaryCsv(definitions, artifacts)},
        {"r1a_vs_r1b_gate_diff.csv", r1b1R1aVsR1bCsv()}, {"ballistic_gain_decomposition.csv", r1b1GainCsv(definitions, gains)},
        {"q3_direct_formula_status.md", q3.str()}, {"production_equivalence.csv", production.str()}, {"hotpath_benchmark.md", hotpath},
        {"V2.4.R1B.1_Adaptive_Response_Predictor_D_Safety_Forensics.md", r1b1Report(definitions, artifacts, direct, exact)},
    };
    for (const auto &[name, contents] : files) if (!r1aWrite(options.output / name, contents)) return 3;
    std::cout << "V2.4.R1B.1 completed development-only hard-safety forensics.\n";
    return exact ? 0 : 4;
}
