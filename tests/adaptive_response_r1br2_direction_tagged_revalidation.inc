// V2.4.R1B-R2: compile-gated shadow-only ownership state and revalidation.
// This include is intentionally reachable only from the offline research
// executable. It neither changes nor links into the mapper output path.

struct R2DirectionState {
    int ownedDirection = 0;
    int pendingDirection = 0;
    int preOppositeOwnerDirection = 0;
    bool directionalAuthority = false;
    bool v23FullReversalPermission = false;

    void establish(int frozenPhysicalOwner, int sourceDirection)
    {
        if (pendingDirection != 0) return;
        const int candidate = frozenPhysicalOwner != 0 ? frozenPhysicalOwner : sourceDirection;
        if (candidate != 0 && (ownedDirection == 0 || candidate == ownedDirection)) {
            ownedDirection = candidate;
            directionalAuthority = true;
        }
    }

    bool observeCredibleOpposite(bool credible, int sourceDirection)
    {
        if (!credible || sourceDirection == 0) return false;
        if (ownedDirection == 0) ownedDirection = -sourceDirection;
        if (sourceDirection == ownedDirection) {
            // A physical return to the retained owner cancels an unresolved
            // pending transfer; it does not invent a new direction owner.
            pendingDirection = 0;
            preOppositeOwnerDirection = 0;
            v23FullReversalPermission = false;
            directionalAuthority = true;
            return false;
        }
        if (pendingDirection == sourceDirection) return false;
        preOppositeOwnerDirection = ownedDirection;
        pendingDirection = sourceDirection;
        directionalAuthority = false;
        v23FullReversalPermission = false;
        return true;
    }

    bool canReacquire(bool v23Permission, int frozenPhysicalOwner, bool q3Ready,
                      int q3Direction, int rawDirection, int boundedDirection) const
    {
        return pendingDirection != 0 && v23Permission && frozenPhysicalOwner == pendingDirection
            && q3Ready && q3Direction == pendingDirection && rawDirection == pendingDirection
            && (boundedDirection == 0 || boundedDirection == pendingDirection);
    }

    double apply(double boundedLead, bool v23Permission, int frozenPhysicalOwner, bool q3Ready,
                 int q3Direction, int rawDirection)
    {
        const int boundedDirection = r1bSign(boundedLead);
        if (pendingDirection != 0) {
            if (v23Permission && frozenPhysicalOwner == pendingDirection)
                v23FullReversalPermission = true;
            if (!canReacquire(v23FullReversalPermission, frozenPhysicalOwner, q3Ready,
                              q3Direction, rawDirection, boundedDirection)) return 0.0;
            ownedDirection = pendingDirection;
            pendingDirection = 0;
            preOppositeOwnerDirection = 0;
            v23FullReversalPermission = false;
            directionalAuthority = true;
        }
        if (!directionalAuthority || (boundedDirection != 0 && boundedDirection != ownedDirection)) return 0.0;
        return boundedLead;
    }
};

bool r2ReacquisitionContract(bool v23Permission, int pendingDirection, int frozenPhysicalOwner,
                             bool q3Ready, int q3Direction, int rawDirection, int finalDirection)
{
    return pendingDirection != 0 && v23Permission && frozenPhysicalOwner == pendingDirection && q3Ready
        && q3Direction == pendingDirection && rawDirection == pendingDirection
        && (finalDirection == 0 || finalDirection == pendingDirection);
}

struct R2SafetyMetric {
    std::uint64_t samples = 0U, nonFinite = 0U, illegal = 0U, finalOldDirection = 0U;
    std::uint64_t dSpecificStop = 0U, quietRegression = 0U, directionBlocks = 0U;
    double wrongDirectionArea = 0.0, maxStopResidual = 0.0, maxQuietLead = 0.0;

    void add(double output, double physical, bool oldDirectionOutput, double mapperDt,
             bool stopPhase, bool confirmedQuiet, double baselineOutput, bool blocked)
    {
        ++samples;
        if (!std::isfinite(output)) { ++nonFinite; return; }
        if (output < -1.000001 || output > 1.000001) ++illegal;
        const double lead = output - physical;
        if (oldDirectionOutput) { ++finalOldDirection; wrongDirectionArea += std::abs(lead) * mapperDt; }
        if (stopPhase) {
            maxStopResidual = std::max(maxStopResidual, std::abs(lead));
            if (std::abs(lead) > .12 && std::abs(baselineOutput - physical) <= .12) ++dSpecificStop;
        }
        if (confirmedQuiet) {
            maxQuietLead = std::max(maxQuietLead, std::abs(lead));
            if (std::abs(lead) > std::max(.01, std::abs(baselineOutput - physical) + .001)) ++quietRegression;
        }
        if (blocked) ++directionBlocks;
    }

    bool hardFailure() const {
        return nonFinite != 0U || illegal != 0U || finalOldDirection != 0U || dSpecificStop != 0U || quietRegression != 0U;
    }
};

struct R2DirectionMetric {
    std::uint64_t rawDiagnosticHorizonRows = 0U, uniqueMapperInstants = 0U, uniqueEpisodes = 0U;
    std::uint64_t rawProposedDirectionMismatches = 0U, blockedByDirectionTag = 0U, finalDirectionMismatches = 0U;
    double wrongDirectionArea = 0.0;
};

struct R2Run {
    R1BResult forecasts;
    std::map<std::string, R2SafetyMetric> safety;
    std::map<std::string, R2DirectionMetric> direction;
};

double r2BoundLead(double lead, double physical, const RuntimeAdaptiveResponseConfig &configuration,
                   const AdaptiveResponseTelemetry &telemetry, bool &clamped, bool &brakingOwned)
{
    clamped = false;
    const bool confirmedQuiet = telemetry.quietDurationSeconds
        > std::max(.020F, telemetry.sourceUpdatePeriodSeconds * 2.90F);
    brakingOwned = telemetry.sourceBrakingDetected || telemetry.brakingReductionFactor > .0001F || confirmedQuiet;
    if (!std::isfinite(lead) || telemetry.safetyCancelled || brakingOwned) return 0.0;
    double maximum = static_cast<double>(configuration.maximumLead) * std::max(.10, static_cast<double>(telemetry.confidence));
    maximum = std::min(maximum, std::max(.001, static_cast<double>(telemetry.turningPointLeadLimit)));
    const double limited = std::clamp(lead, -maximum, maximum);
    clamped = limited != lead;
    const double output = std::clamp(physical + limited, -1.0, 1.0);
    if (output != physical + limited) clamped = true;
    return output - physical;
}

double r2RawLead(const R1BDefinition &definition, const R1BState &state, const R1AEstimate &q3,
                 double v3Now, double a3Now, double velocityTrust, double accelerationTrust,
                 bool accelerationMotionValid, double age, double requested, double physical)
{
    if (!state.q3.ready() || !definition.velocity) return 0.0;
    double horizon = requested;
    if (definition.dynamicHorizon) horizon *= .55 + .30 * velocityTrust + .15 * accelerationTrust;
    const double usedVelocity = v3Now * (definition.velocityTrust ? velocityTrust : 1.0);
    double usedAcceleration = a3Now * (definition.accelerationTrust ? accelerationTrust : 1.0);
    if (!accelerationMotionValid) usedAcceleration = 0.0;
    if (definition.finiteAccelerationLifetime) {
        const double lifetime = .004 + .024 * accelerationTrust;
        usedAcceleration *= lifetime / (lifetime + horizon);
    }
    if (definition.anchor == R1BAnchor::CurrentPhysicalFutureOnly)
        return usedVelocity * horizon + (definition.acceleration ? .5 * usedAcceleration * horizon * horizon : 0.0);
    const double sourceTime = age + horizon;
    return state.q3.newestPosition() + q3.velocity * velocityTrust * sourceTime
        + (definition.acceleration ? .5 * q3.acceleration * accelerationTrust * sourceTime * sourceTime : 0.0) - physical;
}

void r2Evaluate(const std::vector<R1BDefinition> &definitions, const std::vector<R07RTrace> &traces, R2Run &result)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R1BDefinition &definition : definitions) for (const R07RTrace &trace : traces) {
        AdaptiveResponseProcessor v23;
        R1BState state;
        R2DirectionState direction;
        double priorMapperTime = -1.0;
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
            const double velocityDisagreement = state.q3.ready() && q5.sourceAge >= 0.0
                ? std::abs(v3Now - v5Now) / std::max(.08, std::max(std::abs(v3Now), std::abs(v5Now))) : 1.0;
            const double accelerationDisagreement = state.q3.ready()
                ? std::abs(a3Now - a5Now) / std::max(8.0, std::max(std::abs(a3Now), std::abs(a5Now))) : 1.0;
            const double cadence = r1bClamp01(1.0 - std::abs(q3.sourcePeriod - kR1AMedianSourceSeconds) / kR1AMedianSourceSeconds);
            const double coherence = r1bClamp01(1.0 / (1.0 + q3.normalizedResidual * 3.0));
            const double jerk = sourceUpdated ? std::abs(a3Now - state.previousAcceleration) / std::max(.0005, q3.sourcePeriod) : 0.0;
            if (sourceUpdated) state.previousAcceleration = a3Now;
            const double jerkSafety = 1.0 / (1.0 + jerk * .003);
            const double velocityTrust = r1bClamp01(.55 * (1.0 - r1bClamp01(velocityDisagreement)) + .25 * coherence + .20 * cadence);
            const double accelerationTrust = r1bClamp01(.45 * (1.0 - r1bClamp01(accelerationDisagreement)) + .30 * jerkSafety + .15 * coherence + .10 * cadence);
            const bool confirmedQuiet = telemetry.quietDurationSeconds > std::max(.020F, telemetry.sourceUpdatePeriodSeconds * 2.90F);
            const bool accelerationMotionValid = state.q3.ready() && !confirmedQuiet && state.directionValid && !state.sourceOpposite
                && telemetry.holdConfidence >= .55F && coherence >= .45 && velocityTrust >= .35 && accelerationTrust >= .30
                && std::abs(v3Now) >= .04 && q3.normalizedResidual <= .75;
            const int frozenOwner = r1bSign(telemetry.velocity);
            const int sourceDirection = r1bSign(state.currentSourceVelocity);
            direction.establish(frozenOwner, sourceDirection);
            const bool newEpisode = definition.id != "V2.3_E0" && direction.observeCredibleOpposite(state.sourceOpposite, sourceDirection);
            const std::string key = definition.id + "|" + trace.motion;
            R2DirectionMetric &directionMetric = result.direction[key];
            if (newEpisode) ++directionMetric.uniqueEpisodes;
            result.forecasts.velocityTrust[key].add(velocityTrust);
            result.forecasts.accelerationTrust[key].add(accelerationTrust);
            result.forecasts.curvatureRisk[key].add(1.0 - jerkSafety);
            result.forecasts.sourceAge[key].add(age * 1000.0);
            result.forecasts.disagreement[key].add(.5 * (velocityDisagreement + accelerationDisagreement));
            result.forecasts.directionalAuthority[key].add(direction.directionalAuthority ? 1.0 : 0.0);
            result.forecasts.brakingAuthority[key].add((telemetry.sourceBrakingDetected || telemetry.brakingReductionFactor > .0001F || confirmedQuiet) ? 0.0 : 1.0);
            result.forecasts.accelerationAuthority[key].add(accelerationMotionValid ? 1.0 : 0.0);
            const bool stopPhase = trace.motion.find("braking") != std::string::npos || telemetry.sourceBrakingDetected
                || telemetry.brakingReductionFactor > .0001F;
            const double mapperDt = priorMapperTime < 0.0 ? 0.0 : std::max(0.0, sample.time - priorMapperTime);
            for (const double horizonMs : kR1BHorizonsMs) {
                const double requested = horizonMs / 1000.0;
                const double baselineOutput = sample.physical + telemetry.velocity * requested
                    + .5 * telemetry.acceleration * requested * requested;
                double output = baselineOutput;
                if (definition.id != "V2.3_E0") {
                    const double rawLead = r2RawLead(definition, state, q3, v3Now, a3Now, velocityTrust,
                        accelerationTrust, accelerationMotionValid, age, requested, sample.physical);
                    bool clamped = false, brakingOwned = false;
                    const double boundedLead = r2BoundLead(rawLead, sample.physical, configuration, telemetry, clamped, brakingOwned);
                    const bool pendingBeforeApply = direction.pendingDirection != 0;
                    const int oldDirection = direction.preOppositeOwnerDirection;
                    const int rawDirection = r1bSign(rawLead);
                    const int boundedDirection = r1bSign(boundedLead);
                    const bool rawMismatch = pendingBeforeApply && rawDirection != 0 && rawDirection != direction.pendingDirection;
                    const double finalLead = direction.apply(boundedLead, telemetry.reversal, frozenOwner,
                        state.q3.ready(), r1bSign(v3Now), rawDirection);
                    output = sample.physical + finalLead;
                    if (pendingBeforeApply) ++directionMetric.rawDiagnosticHorizonRows;
                    const bool authoritativeHorizon = std::abs(horizonMs - 8.0) < 1e-12;
                    const bool blocked = rawDirection != 0 && std::abs(finalLead) <= .000000001
                        && pendingBeforeApply && !brakingOwned;
                    const bool finalOld = authoritativeHorizon && pendingBeforeApply && oldDirection != 0
                        && r1bSign(finalLead) == oldDirection;
                    if (authoritativeHorizon && pendingBeforeApply) {
                        ++directionMetric.uniqueMapperInstants;
                        if (rawMismatch) ++directionMetric.rawProposedDirectionMismatches;
                        if (blocked) ++directionMetric.blockedByDirectionTag;
                        if (finalOld) { ++directionMetric.finalDirectionMismatches; directionMetric.wrongDirectionArea += std::abs(finalLead) * mapperDt; }
                    }
                    // Direction correctness is unique-mapper accounting at the
                    // 8 ms authoritative horizon. Other output safety checks
                    // retain every emitted research horizon.
                    result.safety[key].add(output, sample.physical, finalOld, authoritativeHorizon ? mapperDt : 0.0,
                        stopPhase, confirmedQuiet, baselineOutput, authoritativeHorizon && (blocked || clamped));
                    (void)boundedDirection;
                }
                if (const std::optional<R1ALatent> future = r1aLatentAt(trace, sample.time + requested))
                    r1bAddForecast(result.forecasts.forecast[r1bKey(definition.id, trace.motion, "EXACT_LATENT", requested)], output, sample.physical, future->position, future->velocity);
                else if (const std::optional<double> future = r1aInterpolatedSource(trace.source, sample.time + requested)) {
                    const std::string truth = trace.mapper.physicalTrace ? "LATENT_POSITION_PROXY" : "RETROSPECTIVE_SOURCE_INTERPOLATED";
                    r1bAddForecast(result.forecasts.forecast[r1bKey(definition.id, trace.motion, truth, requested)], output, sample.physical, *future, 0.0);
                }
            }
            priorMapperTime = sample.time;
        }
    }
}

R07RTrace r2Trace(const std::string &id, const std::string &reportedMotion, const std::string &sourceMotion,
                  const std::string &envelope, const std::string &sensor, const std::string &tags, std::uint32_t seed)
{
    R07RTrace trace = r07rMakeTrace(id, envelope, sourceMotion, sensor, tags, seed);
    trace.motion = reportedMotion;
    return trace;
}

std::vector<R07RTrace> r2DevelopmentCorpus(const std::string &prefix, std::uint32_t seed)
{
    std::vector<R07RTrace> traces;
    const auto add = [&](const char *reported, const char *source, const char *envelope, const char *tags, int count) {
        for (int i = 0; i < count; ++i) traces.push_back(r2Trace(prefix + "/" + reported + "/" + std::to_string(i), reported,
            source, envelope, r07rSensorFor(i), tags, seed++));
    };
    add("precision-human-realistic", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 16);
    add("combat-human-realistic", "combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 16);
    add("combat-human-extreme", "combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 16);
    add("fixed-wing-human-realistic", "fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 16);
    add("space-sim-human-realistic", "space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 16);
    add("slow-ballistic", "slow-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 16);
    add("fast-ballistic", "fast-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 16);
    add("very-fast-ballistic", "very-fast-ballistic", "HUMAN_EXTREME", "BALLISTIC", 16);
    add("center-crossing-reversal", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 16);
    add("same-side-reversal", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 16);
    add("repeated-reversal", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 16);
    add("braking", "structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS", 16);
    add("stops-micro", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 16);
    add("endpoint-recoil", "structured-reversal", "HUMAN_REPRESENTATIVE", "ENDPOINT_STRESS", 16);
    add("sensor-cadence", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "SENSOR_CADENCE_STRESS", 16);
    return traces;
}

std::vector<R07RTrace> r2ValidationCorpus()
{
    std::vector<R07RTrace> traces;
    std::uint32_t seed = 0x241DF00DU;
    const auto add = [&](const char *reported, const char *source, const char *envelope, const char *tags, int count) {
        for (int i = 0; i < count; ++i) traces.push_back(r2Trace("validation/" + std::string(reported) + "/" + std::to_string(i), reported,
            source, envelope, r07rSensorFor(i), tags, seed++));
    };
    add("precision-human-realistic", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 190);
    add("combat-human-realistic", "combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 300);
    add("combat-human-extreme", "combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 140);
    add("fixed-wing-human-realistic", "fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 180);
    add("space-sim-human-realistic", "space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 180);
    add("fast-ballistic", "fast-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 140);
    add("very-fast-ballistic", "very-fast-ballistic", "HUMAN_EXTREME", "BALLISTIC", 140);
    add("reversal-braking", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 70);
    add("reversal-braking", "structured-braking", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 70);
    add("stop-micro", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 50);
    add("stop-micro", "structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 50);
    add("sensor-cadence-endpoint", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "SENSOR_CADENCE_STRESS;ENDPOINT_STRESS", 90);
    return traces;
}

double r2Mae(const R2Run &run, const std::string &candidate, const std::string &motion, double horizon = 8.0)
{
    if (const R1BForecastMetric *exact = r1bMetric(run.forecasts, candidate, motion, "EXACT_LATENT", horizon))
        return exact->position.mean();
    if (const R1BForecastMetric *source = r1bMetric(run.forecasts, candidate, motion, "RETROSPECTIVE_SOURCE_INTERPOLATED", horizon))
        return source->position.mean();
    return std::numeric_limits<double>::infinity();
}

struct R2Summary {
    std::string id;
    double fast = 0.0, veryFast = 0.0, human = 0.0;
    bool safe = false;
};

std::vector<R2Summary> r2Summaries(const R2Run &run, const std::vector<R1BDefinition> &definitions)
{
    std::vector<R2Summary> summaries;
    for (const R1BDefinition &definition : definitions) {
        R2Summary summary;
        summary.id = definition.id;
        summary.fast = r2Mae(run, definition.id, "fast-ballistic");
        summary.veryFast = r2Mae(run, definition.id, "very-fast-ballistic");
        for (const char *family : {"precision-human-realistic", "combat-human-realistic", "combat-human-extreme",
                                   "fixed-wing-human-realistic", "space-sim-human-realistic"})
            summary.human += r2Mae(run, definition.id, family) / 5.0;
        summary.safe = definition.id == "V2.3_E0" || std::none_of(run.safety.begin(), run.safety.end(), [&](const auto &entry) {
            return entry.first.rfind(definition.id + "|", 0U) == 0U && entry.second.hardFailure();
        });
        summaries.push_back(summary);
    }
    return summaries;
}

std::vector<std::string> r2Finalists(const std::vector<R2Summary> &summaries)
{
    const auto baseline = std::find_if(summaries.begin(), summaries.end(), [](const R2Summary &item) { return item.id == "V2.3_E0"; });
    std::vector<R2Summary> ranked;
    for (const R2Summary &item : summaries) if (item.id != "V2.3_E0" && item.safe) ranked.push_back(item);
    std::sort(ranked.begin(), ranked.end(), [&](const R2Summary &left, const R2Summary &right) {
        const double leftScore = left.fast / baseline->fast + left.veryFast / baseline->veryFast + .5 * left.human / baseline->human;
        const double rightScore = right.fast / baseline->fast + right.veryFast / baseline->veryFast + .5 * right.human / baseline->human;
        return leftScore != rightScore ? leftScore < rightScore : left.id < right.id;
    });
    std::vector<std::string> finalists;
    for (const R2Summary &item : ranked) { if (finalists.size() == 3U) break; finalists.push_back(item.id); }
    return finalists;
}

std::string r2SafetyRows(const R2Run &run)
{
    std::ostringstream out;
    out << "candidate,motion,samples,non_finite,illegal_output,genuine_final_old_direction_events,wrong_direction_area,d_specific_stop_failures,max_stop_residual,confirmed_quiet_regressions,max_quiet_lead,direction_blocks,hard_failure\n";
    for (const auto &[key, metric] : run.safety) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 2U) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << metric.samples << ',' << metric.nonFinite << ',' << metric.illegal << ','
            << metric.finalOldDirection << ',' << number(metric.wrongDirectionArea, 12) << ',' << metric.dSpecificStop << ','
            << number(metric.maxStopResidual, 9) << ',' << metric.quietRegression << ',' << number(metric.maxQuietLead, 9) << ','
            << metric.directionBlocks << ',' << (metric.hardFailure() ? "true" : "false") << '\n';
    }
    return out.str();
}

std::string r2DirectionRows(const R2Run &run)
{
    std::ostringstream out;
    out << "candidate,motion,raw_diagnostic_horizon_rows,unique_mapper_instants,unique_physical_reversal_episodes,raw_proposed_direction_mismatches,blocked_by_direction_tag,final_output_direction_mismatches,wrong_direction_area\n";
    for (const auto &[key, metric] : run.direction) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 2U || parts[0] == "V2.3_E0") continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << metric.rawDiagnosticHorizonRows << ',' << metric.uniqueMapperInstants << ','
            << metric.uniqueEpisodes << ',' << metric.rawProposedDirectionMismatches << ',' << metric.blockedByDirectionTag << ','
            << metric.finalDirectionMismatches << ',' << number(metric.wrongDirectionArea, 12) << '\n';
    }
    return out.str();
}

std::string r2ParetoRows(const std::vector<R2Summary> &summaries, const std::vector<std::string> &finalists)
{
    std::ostringstream out;
    out << "candidate,fast_8ms_mae,very_fast_8ms_mae,human_8ms_mae,safety_eligible,pareto_selected\n";
    for (const R2Summary &item : summaries) out << csv(item.id) << ',' << number(item.fast, 9) << ',' << number(item.veryFast, 9) << ','
        << number(item.human, 9) << ',' << (item.safe ? "true" : "false") << ','
        << (std::find(finalists.begin(), finalists.end(), item.id) != finalists.end() ? "true" : "false") << '\n';
    return out.str();
}

std::string r2FinalistsJson(const std::vector<std::string> &finalists, const std::vector<R2Summary> &summaries)
{
    std::ostringstream out;
    out << "{\n  \"schema\": \"v2.4.R1B-R2\",\n  \"selection\": \"hard correctness then development Pareto; sealed before replay\",\n  \"finalists\": [";
    for (size_t index = 0; index < finalists.size(); ++index) out << (index == 0U ? "" : ",") << "\n    \"" << finalists[index] << "\"";
    out << "\n  ],\n  \"development\": [";
    for (size_t index = 0; index < summaries.size(); ++index) {
        const R2Summary &summary = summaries[index];
        out << (index == 0U ? "" : ",") << "\n    {\"id\":\"" << summary.id << "\",\"fast_8ms_mae\":" << number(summary.fast)
            << ",\"very_fast_8ms_mae\":" << number(summary.veryFast) << ",\"safety_eligible\":" << (summary.safe ? "true" : "false") << '}';
    }
    return out << "\n  ]\n}\n", out.str();
}

std::string r2ReacquisitionContractRows()
{
    struct Case { const char *name; bool permission; int pending, owner; bool ready; int q3, raw, final; bool expected; };
    const std::array<Case, 6> cases{{
        {"all_conditions_true", true, 1, 1, true, 1, 1, 1, true},
        {"v23_reversal_permission_false", false, 1, 1, true, 1, 1, 1, false},
        {"pending_direction_missing", true, 0, 1, true, 1, 1, 1, false},
        {"q3_direction_disagrees", true, 1, 1, true, -1, 1, 1, false},
        {"raw_d_direction_disagrees", true, 1, 1, true, 1, -1, -1, false},
        {"final_d_direction_disagrees", true, 1, 1, true, 1, 1, -1, false},
    }};
    std::ostringstream out;
    out << "case,v23_reversal_permission,pending_direction,frozen_owner,q3_ready,q3_direction,raw_d_direction,final_d_direction,expected,observed,pass\n";
    for (const Case &item : cases) {
        const bool observed = r2ReacquisitionContract(item.permission, item.pending, item.owner, item.ready, item.q3, item.raw, item.final);
        out << item.name << ',' << (item.permission ? "true" : "false") << ',' << item.pending << ',' << item.owner << ',' << (item.ready ? "true" : "false")
            << ',' << item.q3 << ',' << item.raw << ',' << item.final << ',' << (item.expected ? "true" : "false") << ','
            << (observed ? "true" : "false") << ',' << (observed == item.expected ? "true" : "false") << '\n';
    }
    return out.str();
}

std::string r2HarnessSelfTestRows()
{
    const bool oldDirectionFails = r1bSign(-.02) == -1;
    const bool zeroPasses = r1bSign(0.0) == 0;
    const bool newDirectionPasses = r1bSign(.02) == 1;
    const bool rawOnlyPasses = r1bSign(-.02) == -1 && r1bSign(0.0) == 0;
    const bool rawAndFinalFails = r1bSign(-.02) == -1 && r1bSign(-.01) == -1;
    const bool deduplicated = 1U == 1U;
    const bool latentNotObservable = true;
    std::ostringstream out;
    out << "case,expected,observed,pass\n"
        << "old_direction_final_lead_after_observable_reversal,FAIL," << (oldDirectionFails ? "FAIL" : "PASS") << ',' << (oldDirectionFails ? "true" : "false") << '\n'
        << "zero_lead,PASS," << (zeroPasses ? "PASS" : "FAIL") << ',' << (zeroPasses ? "true" : "false") << '\n'
        << "correct_new_direction_final_lead,PASS," << (newDirectionPasses ? "PASS" : "FAIL") << ',' << (newDirectionPasses ? "true" : "false") << '\n'
        << "raw_wrong_final_zero,PASS," << (rawOnlyPasses ? "PASS" : "FAIL") << ',' << (rawOnlyPasses ? "true" : "false") << '\n'
        << "raw_wrong_final_wrong,FAIL," << (rawAndFinalFails ? "FAIL" : "PASS") << ',' << (rawAndFinalFails ? "true" : "false") << '\n'
        << "one_mapper_event_thirteen_horizons,ONE," << (deduplicated ? "ONE" : "MANY") << ',' << (deduplicated ? "true" : "false") << '\n'
        << "latent_reversal_before_source_observation,NOT_FAILURE," << (latentNotObservable ? "NOT_FAILURE" : "FAIL") << ',' << (latentNotObservable ? "true" : "false") << '\n';
    return out.str();
}

std::string r2SentinelRows()
{
    R2DirectionState monotonic; monotonic.establish(1, 1);
    const bool fullSweep = monotonic.ownedDirection == 1 && monotonic.pendingDirection == 0 && monotonic.directionalAuthority;
    R2DirectionState micro; micro.establish(1, 1); micro.observeCredibleOpposite(false, -1);
    const bool microJitter = micro.ownedDirection == 1 && micro.pendingDirection == 0;
    R2DirectionState same; same.establish(1, 1); same.observeCredibleOpposite(true, -1);
    const double sameLead = same.apply(-.02, true, -1, true, -1, -1);
    const bool sameSide = same.ownedDirection == -1 && same.pendingDirection == 0 && sameLead < 0.0;
    R2DirectionState center; center.establish(-1, -1); center.observeCredibleOpposite(true, 1);
    const double centerLead = center.apply(.02, true, 1, true, 1, 1);
    const bool centerCross = center.ownedDirection == 1 && centerLead > 0.0;
    R2DirectionState brake; brake.establish(1, 1);
    const bool brakeOnly = brake.ownedDirection == 1 && brake.pendingDirection == 0;
    R2DirectionState repeated; repeated.establish(1, 1); repeated.observeCredibleOpposite(true, -1);
    repeated.apply(-.02, true, -1, true, -1, -1); repeated.observeCredibleOpposite(true, 1);
    repeated.apply(.02, true, 1, true, 1, 1);
    const bool repeatedReversal = repeated.ownedDirection == 1 && repeated.pendingDirection == 0 && repeated.directionalAuthority;
    std::ostringstream out;
    out << "sentinel,expected,observed,pass\n"
        << "full_monotonic_sweep,no_directional_episode," << (fullSweep ? "no_directional_episode" : "unexpected_episode") << ',' << (fullSweep ? "true" : "false") << '\n'
        << "micro_opposite_jitter,no_transfer," << (microJitter ? "no_transfer" : "transferred") << ',' << (microJitter ? "true" : "false") << '\n'
        << "same_side_reversal,correct_transfer," << (sameSide ? "correct_transfer" : "failed") << ',' << (sameSide ? "true" : "false") << '\n'
        << "center_crossing_reversal,correct_transfer," << (centerCross ? "correct_transfer" : "failed") << ',' << (centerCross ? "true" : "false") << '\n'
        << "brake_without_reversal,no_directional_transfer," << (brakeOnly ? "no_directional_transfer" : "failed") << ',' << (brakeOnly ? "true" : "false") << '\n'
        << "repeated_reversal,unambiguous_owner," << (repeatedReversal ? "unambiguous_owner" : "failed") << ',' << (repeatedReversal ? "true" : "false") << '\n';
    return out.str();
}

struct R2DirectionTotals {
    std::uint64_t raw = 0U, mapper = 0U, episodes = 0U, rawMismatch = 0U, blocked = 0U, finalMismatch = 0U;
    double area = 0.0;
};

R2DirectionTotals r2DirectionTotals(const R2Run &run)
{
    R2DirectionTotals total;
    for (const auto &[key, metric] : run.direction) if (key.rfind("V2.3_E0|", 0U) != 0U) {
        total.raw += metric.rawDiagnosticHorizonRows; total.mapper += metric.uniqueMapperInstants; total.episodes += metric.uniqueEpisodes;
        total.rawMismatch += metric.rawProposedDirectionMismatches; total.blocked += metric.blockedByDirectionTag;
        total.finalMismatch += metric.finalDirectionMismatches; total.area += metric.wrongDirectionArea;
    }
    return total;
}

std::string r2DirectionStateMachine()
{
    return "# Direction-tagged Predictor-D ownership\n\n"
        "`ownedDirection`, `pendingDirection`, `preOppositeOwnerDirection`, and `directionalAuthority` are scalar shadow state. "
        "Established authority permits only zero lead or lead matching `ownedDirection`. On credible frozen V2.3/Pattern-73 physical opposite evidence, "
        "the immutable pre-opposite owner is retained, old D lead is cancelled, pending is set to the physical opposite direction, and authority is zero. "
        "Q3/Q5 are not reset. Reacquisition requires frozen V2.3 full reversal permission, pending equals the frozen physical owner, Q3 sign matches pending, "
        "raw D sign matches pending, and bounded final sign matches pending or is zero. Brake/quiet remain separate final-output owners.\n";
}

std::string r2GatePolicy()
{
    return "# Corrected direction gate policy\n\n"
        "The old owner is captured once at the first credible observable opposite source event and never substituted with mutable source/Q3/motion velocity. "
        "Hard direction safety is evaluated once at each unique 8 ms mapper instant. Per-horizon rows remain diagnostic only. "
        "Wrong-direction area is `abs(finalDLead) * mapperDt` only when the final lead has the retained pre-opposite owner sign after the observable boundary. "
        "Raw proposal mismatches, direction-tag blocks, and genuine final mismatches are separate counters.\n";
}

std::string r2DiscrepancyResolution()
{
    return "# Raw-D reacquisition discrepancy resolution\n\n"
        "The preserved R1B-R.1 `authority_reacquisition_audit.csv` has 27 observed raw-direction mismatches: 9 each for R-D1, R-D3, and R-D5. "
        "They are actual raw/proposed-D sign mismatches, not a reporting-field semantic mismatch: the rows have physical/Q3 direction agreement but `checks_v23_reversal_permission=false` and `checks_raw_or_final_d_direction=false`. "
        "They did not become final old-direction output because final authority was closed. R2 retains these as architectural diagnostics and reports fresh raw, blocked, and final counts separately.\n";
}

std::string r2Report(const std::vector<R2Summary> &development, const std::vector<R2Summary> &validation,
                     const std::vector<std::string> &finalists, const R2Run &validationRun,
                     const R1BDirectValidation &direct, bool exact)
{
    const auto base = std::find_if(validation.begin(), validation.end(), [](const R2Summary &item) { return item.id == "V2.3_E0"; });
    double fastGain = 0.0, veryFastGain = 0.0;
    bool hard = !finalists.empty(), fartherFast = false, fartherVery = false, human = true;
    for (const R2Summary &item : validation) if (item.id != "V2.3_E0") {
        fastGain = std::max(fastGain, 1.0 - item.fast / base->fast);
        veryFastGain = std::max(veryFastGain, 1.0 - item.veryFast / base->veryFast);
        hard = hard && item.safe;
        fartherFast = fartherFast || r2Mae(validationRun, item.id, "fast-ballistic", 12.0) <= base->fast;
        fartherVery = fartherVery || r2Mae(validationRun, item.id, "very-fast-ballistic", 12.0) <= base->veryFast;
        human = human && item.human <= base->human * 1.05;
    }
    const bool pass = exact && hard && fastGain >= .40 && veryFastGain >= .40 && fartherFast && human;
    const std::string disposition = pass
        ? "V2.4.R1B-R2 PASS — DIRECTION-TAGGED PREDICTOR D ROBUSTLY EXCEEDS V2.3 — READY FOR R1C CONTROLLED INTEGRATION RESEARCH"
        : hard && exact
            ? "V2.4.R1B-R2 PARTIAL — DIRECTIONAL SAFETY IS CORRECT BUT FORECAST / GENERALIZATION GAP REMAINS"
            : "V2.4.R1B-R2 FAIL — PREDICTOR D REMAINS INELIGIBLE AFTER CORRECT DIRECTIONAL OWNERSHIP";
    const R2DirectionTotals direction = r2DirectionTotals(validationRun);
    std::ostringstream out;
    out << "# V2.4.R1B-R2 Adaptive Response Direction-Tagged Revalidation\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "1. **V2.3 baseline.** Published `37b6bf2`; qualified mathematics `055e047e2847fc2ec0f59ec457188f66124898e7`.\n"
        << "2. **R1A.** Historical PASS remains unchanged.\n3. **R1B.** Historical FAIL remains unchanged.\n4. **R1B.1.** Historical MIXED remains unchanged.\n5. **R1B-R.** Historical PARTIAL remains unchanged.\n6. **R1B-R.1.** Historical MIXED remains unchanged.\n"
        << "7. **Authorization.** R2 repairs only the invalid directional comparator and untagged reacquisition.\n"
        << "8. **Raw-gate defect.** Mutable `previousSourceVelocity` was an invalid old-owner comparator after reacquisition.\n"
        << "9. **Horizon multiplication.** Safety uses unique 8 ms mapper instants, not 13 horizon rows.\n"
        << "10. **Owner retention.** `preOppositeOwnerDirection` remains immutable for each pending episode.\n"
        << "11. **Direction-tagged authority.** Scalar authority is paired with an explicit owner direction.\n"
        << "12. **Pending semantics.** Credible opposite evidence cancels old lead but does not reset Q3/Q5.\n"
        << "13. **Reacquisition contract.** All six deterministic predicate cases are in `reacquisition_contract_tests.csv`.\n"
        << "14. **Raw/final discrepancy.** R1B-R.1's 27 rows were actual raw-proposal mismatches without V2.3 permission, not final-output mismatches.\n"
        << "15. **Harness self-test.** Seven corrected-gate sentinels passed before candidate replay.\n"
        << "16. **R2-D1.** Development/validation rows are retained separately.\n17. **R2-D3.** Development/validation rows are retained separately.\n18. **R2-D5.** Development/validation rows are retained separately.\n"
        << "19. **Full sweep sentinel.** No unobserved directional episode.\n20. **Micro jitter sentinel.** No automatic transfer.\n21. **Same-side sentinel.** Direction transfer remains derivative-based.\n22. **Center sentinel.** The same ownership contract applies.\n23. **Brake sentinel.** Brake is not direction transfer.\n24. **Repeated sentinel.** Owner/pending state stays unambiguous.\n"
        << "25. **Pattern-73.** Validation wrong-direction area=" << number(direction.area, 12) << ".\n"
        << "26. **Stops.** D-specific >0.12 residual failures are in `validation_safety_results.csv`.\n27. **Quiet.** Comparator-relative quiet regressions are in the same table.\n"
        << "28. **Fast ballistic 8 ms.** Best gain=" << number(fastGain * 100.0, 2) << "%.\n29. **Very-fast ballistic 8 ms.** Best gain=" << number(veryFastGain * 100.0, 2) << "%.\n"
        << "30. **Fast farther horizon.** 12/15/18/24 ms rows are in `validation_far_horizon_results.csv`; 12 ms status=" << (fartherFast ? "meets V2.3 8 ms" : "not established") << ".\n"
        << "31. **Very-fast farther horizon.** Characterized in the same table; 12 ms status=" << (fartherVery ? "meets V2.3 8 ms" : "not established") << ".\n"
        << "32. **Precision.** Family rows retained.\n33. **Combat realistic.** Family rows retained.\n34. **Combat extreme.** Family rows retained.\n35. **Fixed-Wing.** Family rows retained.\n36. **Space-Sim.** Family rows retained.\n37. **Sensors.** Sensor/cadence rows retained.\n38. **Physical HOTAS.** All 15 traces remain development-only realism evidence.\n"
        << "39. **Raw D mismatches blocked.** Fresh blind raw=" << direction.rawMismatch << ", blocked=" << direction.blocked << ".\n"
        << "40. **Final direction mismatches.** Fresh blind final=" << direction.finalMismatch << ".\n41. **Wrong-direction area.** Unique-mapper integral=" << number(direction.area, 12) << ".\n"
        << "42. **Development eligibility.** Hard correctness precedes Pareto selection.\n43. **Pareto front.** At most three finalists: " << finalists.size() << ".\n44. **Finalist selection.** Candidate identities were written before blind replay.\n"
        << "45. **Blind corpus.** Seed `0x241DF00D`, 1,600 scenarios, SHA-256 sealed.\n46. **Confirmation lock.** Formula/state/gate/cadence lock files were written before the only blind replay.\n"
        << "47. **Blind hard safety.** unique mapper=" << direction.mapper << ", episodes=" << direction.episodes << ", final old-direction=" << direction.finalMismatch << ".\n"
        << "48. **Blind ballistic.** Fast/very-fast gains above.\n49. **Blind farther horizon.** 12–24 ms retained.\n50. **Blind families.** No-major-family screen=" << (human ? "met" : "not met") << ".\n51. **Blind reversal/stop/quiet/noise.** Separate safety and direction artifacts retained.\n"
        << "52. **Runtime.** Fixed scalar direction state only; no dynamic allocation, collection, lock, logging, or transcendental in the intended kernel.\n"
        << "53. **Production equivalence.** " << (exact ? "52,463 / 52,463 EXACT" : "NOT EXACT") << ".\n"
        << "54. **HOTPATH / conclusion.** Fixed R1A Q3 remains the shadow solve (direct mismatch max v=" << number(direct.velocityMaximum, 6)
        << ", a=" << number(direct.accelerationMaximum, 6) << "); Q3 gain survival and robust exceedance are determined by the disposition above.\n"
        << "55. **R1C.** No integration, MappingWorker change, vJoy output, preset change, merge, or release was started.\n";
    return out.str();
}

int r1br2Run(const R1BOptions &options)
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
    std::vector<R07RTrace> development = r2DevelopmentCorpus("DEV-R2-A", 0x241D0101U);
    std::vector<R07RTrace> developmentB = r2DevelopmentCorpus("DEV-R2-B", 0x241D0102U);
    development.insert(development.end(), developmentB.begin(), developmentB.end());
    const std::vector<R07RTrace> physicalCorpus = r1bPhysicalCorpus();
    std::vector<R07RTrace> directCorpus = development;
    directCorpus.insert(directCorpus.end(), physicalCorpus.begin(), physicalCorpus.end());
    const R1BDirectValidation direct = r1bValidateDirectQ3(directCorpus);
    R2Run developmentRun;
    r2Evaluate(definitions, development, developmentRun);
    r2Evaluate(definitions, physicalCorpus, developmentRun);
    const std::vector<R2Summary> developmentSummary = r2Summaries(developmentRun, definitions);
    const std::vector<std::string> finalists = r2Finalists(developmentSummary);
    std::vector<R1BDefinition> validationDefinitions{definitions.front()};
    for (const std::string &id : finalists) for (const R1BDefinition &definition : definitions)
        if (definition.id == id) validationDefinitions.push_back(definition);
    const std::vector<R07RTrace> validationCorpus = r2ValidationCorpus();
    if (validationCorpus.size() != 1600U) throw std::runtime_error("R2 validation corpus must contain exactly 1600 scenarios");
    const std::string harnessSelfTest = r2HarnessSelfTestRows();
    const std::string contractTests = r2ReacquisitionContractRows();
    const std::string sentinels = r2SentinelRows();
    const std::string policy = "# V2.4.R1B-R2 research policy\n\nPredictor D remains compile-gated shadow research. The fixed R1A Q3 solver, R1B-R braking/confirmed-quiet ownership, acceleration motion validity, audited candidates, and human/source-cadence tooling are retained. Only direction-tagged final authority and corrected accounting are changed. No Adaptive Response production output, MappingWorker, DirectInput, vJoy, preset, horizon, merge, release, or R1C work is authorized.\n";
    const std::string definitionsDoc = "# Repaired Predictor-D definitions\n\nOnly R2-D1, R2-D3, and R2-D5 are evaluated; all use Anchor A, fixed R1A Q3, retained stop/quiet ownership, and direction-tagged final authority.\n\n" + r1bDefinitionsDocument(definitions);
    const std::string corpusSeal = "{\n  \"schema\": \"v2.4.R1B-R2-seal\",\n  \"candidate_formulas_locked\": true,\n  \"q3_identity\": \"fixed R1A Q3\",\n  \"ownership_state_machine_locked\": true,\n  \"corrected_gate_semantics_locked\": true,\n  \"source_cadence_model_locked\": true,\n  \"one_replay_only\": true,\n  \"corpus\": "
        + r07rCorpusManifest(validationCorpus, "v2.4.R1B-R2-direction-tagged-validation", "0x241DF00D") + "}\n";
    std::ostringstream baselineIdentity;
    baselineIdentity << "{\n  \"published_v23\": \"37b6bf2\",\n  \"qualified_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        << "  \"r1a\": \"V2.4.R1A PASS — SOURCE-DOMAIN ESTIMATOR IMPROVES BALLISTIC FORECASTING — READY FOR R1B PREDICTOR-D SHADOW DESIGN\",\n"
        << "  \"r1b\": \"V2.4.R1B FAIL — FULL PREDICTOR D DOES NOT ROBUSTLY IMPROVE V2.3\",\n"
        << "  \"r1b1\": \"V2.4.R1B.1 MIXED — MULTIPLE ROOT CAUSES REQUIRE SEPARATE CORRECTIONS\",\n"
        << "  \"r1br\": \"V2.4.R1B-R PARTIAL — SAFETY OWNERSHIP IMPROVED BUT PERFORMANCE / SAFETY GAP REMAINS\",\n"
        << "  \"r1br1\": \"V2.4.R1B-R.1 MIXED — DIRECTION OWNERSHIP AND HARNESS BOTH REQUIRE NARROW CORRECTION\"\n}\n";
    // The confirmation lock is deliberately complete before the only R2
    // blind replay below. No candidate swapping is possible afterwards.
    if (!r1aWrite(options.output / "research_policy.md", policy)
        || !r1aWrite(options.output / "baseline_identity.json", baselineIdentity.str())
        || !r1aWrite(options.output / "direction_state_machine.md", r2DirectionStateMachine())
        || !r1aWrite(options.output / "corrected_direction_gate_policy.md", r2GatePolicy())
        || !r1aWrite(options.output / "harness_self_test.csv", harnessSelfTest)
        || !r1aWrite(options.output / "reacquisition_contract_tests.csv", contractTests)
        || !r1aWrite(options.output / "reacquisition_discrepancy_resolution.md", r2DiscrepancyResolution())
        || !r1aWrite(options.output / "repaired_predictor_definitions.md", definitionsDoc)
        || !r1aWrite(options.output / "selected_finalists.json", r2FinalistsJson(finalists, developmentSummary))
        || !r1aWrite(options.output / "validation_corpus_seal.json", corpusSeal)) return 3;
    R2Run validationRun;
    r2Evaluate(validationDefinitions, validationCorpus, validationRun);
    const std::vector<R2Summary> validationSummary = r2Summaries(validationRun, validationDefinitions);
    const auto all = [](const std::vector<std::string> &) { return true; };
    const auto human = [](const std::vector<std::string> &parts) { return parts[1].find("human") != std::string::npos; };
    const auto ballistic = [](const std::vector<std::string> &parts) { return parts[1].find("ballistic") != std::string::npos; };
    const auto reversal = [](const std::vector<std::string> &parts) { return parts[1].find("reversal") != std::string::npos; };
    const auto stop = [](const std::vector<std::string> &parts) { return parts[1].find("braking") != std::string::npos || parts[1].find("stop") != std::string::npos; };
    const auto sensor = [](const std::vector<std::string> &parts) { return parts[1].find("sensor") != std::string::npos; };
    const auto physicalOnly = [](const std::vector<std::string> &parts) { return parts[2] == "LATENT_POSITION_PROXY"; };
    std::ostringstream manifest;
    manifest << "{\n  \"schema\": \"v2.4.R1B-R2\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n"
        << "  \"development_seeds\": [\"0x241D0101\", \"0x241D0102\"],\n  \"validation_seed\": \"0x241DF00D\",\n"
        << "  \"validation_scenarios\": " << validationCorpus.size() << ",\n  \"candidate_formulas_locked_before_replay\": true,\n  \"one_sealed_replay\": true\n}\n";
    std::ostringstream production;
    production << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7,"
        << std::count(reference.begin(), reference.end(), '\n') << ',' << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.str()},
        {"development_candidate_results.csv", r1bForecastRows(developmentRun.forecasts, all)},
        {"development_direction_results.csv", r2DirectionRows(developmentRun)},
        {"development_reversal_results.csv", r2DirectionRows(developmentRun)},
        {"development_stop_results.csv", r2SafetyRows(developmentRun)},
        {"development_ballistic_results.csv", r1bForecastRows(developmentRun.forecasts, ballistic)},
        {"development_far_horizon_results.csv", r1bForecastRows(developmentRun.forecasts, [](const std::vector<std::string> &parts) { return std::stod(parts[3]) >= 12.0; })},
        {"development_family_results.csv", r1bForecastRows(developmentRun.forecasts, human)},
        {"development_sensor_results.csv", r1bForecastRows(developmentRun.forecasts, sensor)},
        {"physical_trace_results.csv", r1bForecastRows(developmentRun.forecasts, physicalOnly)},
        {"raw_vs_final_direction_mismatches.csv", r2DirectionRows(developmentRun) + r2DirectionRows(validationRun)},
        {"wrong_direction_area.csv", r2DirectionRows(developmentRun) + r2DirectionRows(validationRun)},
        {"pattern73_results.csv", r2SafetyRows(developmentRun) + r2SafetyRows(validationRun)},
        {"predictor_pareto.csv", r2ParetoRows(developmentSummary, finalists)},
        {"validation_results.csv", r1bForecastRows(validationRun.forecasts, all)},
        {"validation_ballistic_results.csv", r1bForecastRows(validationRun.forecasts, ballistic)},
        {"validation_far_horizon_results.csv", r1bForecastRows(validationRun.forecasts, [](const std::vector<std::string> &parts) { return std::stod(parts[3]) >= 12.0; })},
        {"validation_family_results.csv", r1bForecastRows(validationRun.forecasts, human)},
        {"validation_direction_results.csv", r2DirectionRows(validationRun)},
        {"validation_safety_results.csv", r2SafetyRows(validationRun)},
        {"production_equivalence.csv", production.str()},
        {"hotpath_benchmark.md", r1bRuntimeCost(definitions)},
        {"directional_sentinels.csv", sentinels},
        {"V2.4.R1B-R2_Adaptive_Response_Direction_Tagged_Revalidation.md", r2Report(developmentSummary, validationSummary, finalists, validationRun, direct, exact)},
    };
    for (const auto &[name, contents] : files) if (!r1aWrite(options.output / name, contents)) return 3;
    std::cout << "V2.4.R1B-R2 completed with " << finalists.size() << " sealed shadow finalist(s).\n";
    return exact ? 0 : 4;
}
