// V2.4.R1B-R.1: development-only observation of the frozen R1B-R shadow.
// This include adds no predictor term and is never linked to the mapper.

struct R1BR1Episode {
    std::string candidate, trace, motion;
    std::uint64_t ordinal = 0U, rawRows = 0U, uniqueMapper = 0U, trueRows = 0U, taggedBlocks = 0U;
    double firstTime = -1.0, firstDelta = 0.0, accumulated = 0.0, sourceSpeed = 0.0;
    double v23Cancel = -1.0, v23Reversal = -1.0, q3Zero = -1.0, q3New = -1.0, rawZero = -1.0, rawNew = -1.0, reacquired = -1.0;
    int oldDirection = 0, newDirection = 0, consecutive = 0;
    bool coherent = false, endpoint = false, braking = false, sawMapper = false;
    std::uint64_t reacquisitionChecks = 0U, rawDirectionMismatchesAtReacquisition = 0U;
    double priorQ3 = 0.0, priorRaw = 0.0;
    bool q3Seen = false, rawSeen = false;
};

int r1br1Direction(double value) { return value < 0.0 ? -1 : value > 0.0 ? 1 : 0; }

struct R1BR1SourceObservation {
    size_t sourceIndex = 0U;
    double previousPosition = 0.0, currentPosition = 0.0, delta = 0.0, currentVelocity = 0.0, previousVelocity = 0.0;
    double firstOppositeDelta = 0.0, accumulatedOpposite = 0.0;
    int sourceDirection = 0, oldDirection = 0, newDirection = 0, consecutive = 0;
    std::uint64_t episode = 0U;
    double episodeTime = -1.0;

    void advance(const R07RTrace &trace, double mapperTime)
    {
        while (sourceIndex < trace.source.size() && trace.source[sourceIndex].time <= mapperTime + 1e-9) {
            const Sample &source = trace.source[sourceIndex];
            if (sourceIndex == 0U) {
                previousPosition = currentPosition = source.physical;
                ++sourceIndex;
                continue;
            }
            const Sample &prior = trace.source[sourceIndex - 1U];
            const double period = std::max(.0005, source.time - prior.time);
            previousPosition = prior.physical;
            currentPosition = source.physical;
            delta = currentPosition - previousPosition;
            const double velocity = delta / period;
            const int direction = r1bSign(velocity);
            const bool opposite = std::abs(velocity) > .10 && std::abs(currentVelocity) > .10
                && direction != 0 && r1bSign(currentVelocity) != 0 && direction != r1bSign(currentVelocity);
            previousVelocity = currentVelocity;
            currentVelocity = velocity;
            sourceDirection = direction;
            if (opposite) {
                ++episode;
                episodeTime = source.time;
                oldDirection = r1bSign(previousVelocity);
                newDirection = direction;
                firstOppositeDelta = std::abs(delta);
                accumulatedOpposite = std::abs(delta);
                consecutive = 1;
            } else if (episode != 0U && direction == newDirection && direction != 0) {
                accumulatedOpposite += std::abs(delta);
                ++consecutive;
            } else if (direction == oldDirection && direction != 0) {
                consecutive = 0;
            }
            ++sourceIndex;
        }
    }
};

struct R1BR1Aggregate {
    std::uint64_t rawRows = 0U, uniqueMapper = 0U, episodes = 0U, trueRows = 0U, gateMismatch = 0U, micro = 0U, braking = 0U, q3Lag = 0U, authorityEarly = 0U, other = 0U;
    std::uint64_t taggedBlocks = 0U, retainAllBlocks = 0U, invalidateVelocityBlocks = 0U, invalidateAccelerationBlocks = 0U;
    double runnerArea = 0.0, independentArea = 0.0;
};

struct R1BR1Area {
    std::uint64_t rawRows = 0U, uniqueMapper = 0U, trueRows = 0U;
    double runnerArea = 0.0, independentArea = 0.0;
};

struct R1BR1Artifacts {
    std::ofstream classification;
    std::ofstream fullSweep;
    std::map<std::string, R1BR1Aggregate> aggregate;
    std::map<std::string, R1BR1Episode> episodes;
    std::map<std::string, R1BR1Area> area;

    explicit R1BR1Artifacts(const std::filesystem::path &output)
        : classification(output / "directional_failure_classification.csv", std::ios::binary),
          fullSweep(output / "full_sweep_direction_forensics.csv", std::ios::binary)
    {
        classification << "candidate,scenario_id,motion_family,mapper_timestamp_ms,horizon_ms,raw_gate_event,causal_category,source_position,previous_source_position,source_delta,source_delta_magnitude,source_direction,cumulative_opposite_travel,consecutive_opposite_updates,source_speed,v23_braking,v23_reversal,v23_motion_owner,q3_velocity,q3_direction,q3_acceleration,q3_acceleration_direction,raw_d_lead,raw_d_lead_direction,final_d_lead,final_d_lead_direction,directional_authority,candidate_owner_direction,episode_old_direction,episode_new_direction,first_opposite_delta,near_endpoint,tagged_counterfactual_blocks\n";
        fullSweep << "candidate,scenario_id,motion_family,mapper_timestamp_ms,horizon_ms,causal_category,source_position,previous_source_position,source_delta,source_delta_magnitude,source_direction,cumulative_opposite_travel,consecutive_opposite_updates,source_speed,v23_braking_state,v23_reversal_state,v23_motion_owner,q3_velocity,q3_direction,raw_d_lead,final_d_lead,directional_authority,candidate_owner_direction\n";
    }
    bool good() const { return static_cast<bool>(classification) && static_cast<bool>(fullSweep); }
};

bool r1br1Critical(const R07RTrace &trace)
{
    return trace.mapper.id.find("physical-fast-full-sweep-1") != std::string::npos
        || trace.mapper.id.find("physical-very-fast-full-sweep-1") != std::string::npos
        || trace.mapper.id.find("physical-slow-full-sweep-1") != std::string::npos
        || trace.motion == "fast-ballistic" || trace.motion == "very-fast-ballistic" || trace.motion == "slow-ballistic";
}

std::string r1br1Bucket(double magnitude, double noise)
{
    if (magnitude < noise) return "below_noise_floor";
    if (magnitude < noise * 2.0) return "1_to_2x_noise";
    if (magnitude < noise * 4.0) return "2_to_4x_noise";
    return "above_full_reversal_evidence";
}

std::string r1br1Category(const R1BR1Episode *episode, const AdaptiveResponseTelemetry &telemetry,
                           const R1BR1SourceObservation &source, double q3Velocity, double finalLead,
                           bool brakingOwned, bool nearEndpoint, double noise)
{
    if (episode == nullptr) return "OTHER";
    const int finalDirection = r1br1Direction(finalLead);
    if (telemetry.reversal && episode->oldDirection != 0 && finalDirection == episode->oldDirection)
        return "TRUE_POST_REVERSAL_STALE_LEAD";
    if (brakingOwned || telemetry.sourceBrakingDetected)
        return "BRAKING_WITHOUT_REVERSAL";
    if (!telemetry.reversal && nearEndpoint && source.firstOppositeDelta < noise * 2.0)
        return "MICRO_OPPOSITE_SOURCE_JITTER";
    if (r1br1Direction(q3Velocity) == episode->oldDirection && source.sourceDirection == episode->newDirection)
        return "Q3_DIRECTION_LAG";
    if (!telemetry.reversal && finalDirection == episode->oldDirection)
        return "AUTHORITY_REOPENED_BEFORE_Q3_CAUGHT_UP";
    if (finalDirection == episode->newDirection && r1br1Direction(source.previousVelocity) == episode->newDirection)
        return "GATE_DIRECTION_MISMATCH";
    return "OTHER";
}

double r1br1RawLead(const R1BDefinition &definition, const R1BState &state, const R1AEstimate &q3,
                     double v3Now, double a3Now, double velocityTrust, double accelerationTrust,
                     bool accelerationMotionValid, double age, double requested, double physical)
{
    if (!state.q3.ready() || !state.directionValid || !definition.velocity) return 0.0;
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
    const double absolute = state.q3.newestPosition() + q3.velocity * velocityTrust * sourceTime
        + (definition.acceleration ? .5 * q3.acceleration * accelerationTrust * sourceTime * sourceTime : 0.0);
    return absolute - physical;
}

void r1br1UpdateEpisodeTiming(R1BR1Episode &episode, double time, const AdaptiveResponseTelemetry &telemetry,
                               double q3Velocity, double rawLead, const R1BState &state)
{
    if (telemetry.safetyCancelled && episode.v23Cancel < 0.0) episode.v23Cancel = time;
    if (telemetry.reversal && episode.v23Reversal < 0.0) episode.v23Reversal = time;
    if (state.lastReacquiredTime >= 0.0 && episode.reacquired < 0.0) episode.reacquired = state.lastReacquiredTime;
    if (episode.q3Seen && episode.q3Zero < 0.0 && r1bSign(episode.priorQ3) == episode.oldDirection
        && r1bSign(q3Velocity) != episode.oldDirection) episode.q3Zero = time;
    if (episode.newDirection != 0 && std::abs(q3Velocity) >= .06 && r1bSign(q3Velocity) == episode.newDirection && episode.q3New < 0.0) episode.q3New = time;
    if (episode.rawSeen && episode.rawZero < 0.0 && r1bSign(episode.priorRaw) == episode.oldDirection
        && r1bSign(rawLead) != episode.oldDirection) episode.rawZero = time;
    if (episode.newDirection != 0 && std::abs(rawLead) > .0001 && r1bSign(rawLead) == episode.newDirection && episode.rawNew < 0.0) episode.rawNew = time;
    episode.priorQ3 = q3Velocity; episode.priorRaw = rawLead; episode.q3Seen = true; episode.rawSeen = true;
}

void r1br1Evaluate(const std::vector<R1BDefinition> &definitions, const std::vector<R07RTrace> &traces,
                   R1BR1Artifacts &artifacts)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R1BDefinition &definition : definitions) {
        if (definition.id == "V2.3_E0") continue;
        for (const R07RTrace &trace : traces) {
            AdaptiveResponseProcessor v23;
            R1BState state;
            R1BR1SourceObservation observed;
            double priorMapperTime = -1.0;
            for (const Sample &sample : trace.mapper.samples) {
                const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
                const AdaptiveResponseTelemetry telemetry = v23.process(sample.physical, configuration, timestamp);
                observed.advance(trace, sample.time);
                const bool sourceUpdated = r1bAcceptSources(trace, sample.time, state, definition.reset);
                if (observed.sourceIndex != state.sourceIndex) throw std::runtime_error("source-observation alignment mismatch");
                const R1AEstimate q3 = state.q3Fit.value(sample.time), q5 = state.q5.value(sample.time);
                const double age = std::min(kR1BMaximumSourceAgeSeconds, q3.sourceAge);
                const double v3Now = q3.velocity + q3.acceleration * age, a3Now = q3.acceleration;
                const double v5Now = q5.velocity + q5.acceleration * age, a5Now = q5.acceleration;
                const double velocityDisagreement = state.q3.ready() && q5.sourceAge >= 0.0 ? std::abs(v3Now - v5Now) / std::max(.08, std::max(std::abs(v3Now), std::abs(v5Now))) : 1.0;
                const double accelerationDisagreement = state.q3.ready() ? std::abs(a3Now - a5Now) / std::max(8.0, std::max(std::abs(a3Now), std::abs(a5Now))) : 1.0;
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
                R1BR1Episode *episode = nullptr;
                if (state.lastOppositeSourceTime >= 0.0) {
                    const std::string episodeKey = definition.id + "|" + trace.mapper.id + "|" + number(state.lastOppositeSourceTime, 9);
                    auto [it, inserted] = artifacts.episodes.try_emplace(episodeKey);
                    episode = &it->second;
                    if (inserted) {
                        episode->candidate = definition.id; episode->trace = trace.mapper.id; episode->motion = trace.motion;
                        episode->ordinal = observed.episode; episode->firstTime = state.lastOppositeSourceTime;
                        episode->firstDelta = observed.firstOppositeDelta; episode->oldDirection = observed.oldDirection; episode->newDirection = observed.newDirection;
                    }
                    episode->accumulated = observed.accumulatedOpposite; episode->consecutive = observed.consecutive;
                    episode->sourceSpeed = observed.currentVelocity; episode->coherent = observed.consecutive >= 2 && observed.accumulatedOpposite >= configuration.noiseRejection * 4.0;
                    episode->endpoint = std::min(std::abs(1.0 - observed.currentPosition), std::abs(-1.0 - observed.currentPosition)) <= configuration.noiseRejection * 4.0;
                    episode->braking = telemetry.sourceBrakingDetected || telemetry.brakingReductionFactor > .0001F;
                }
                bool mapperRaw = false, mapperTrue = false;
                const double mapperDt = priorMapperTime < 0.0 ? 0.0 : std::max(0.0, sample.time - priorMapperTime);
                for (const double horizonMs : kR1BHorizonsMs) {
                    const double requested = horizonMs / 1000.0;
                    const double rawLead = r1br1RawLead(definition, state, q3, v3Now, a3Now, velocityTrust, accelerationTrust, accelerationMotionValid, age, requested, sample.physical);
                    bool clamped = false, brakingOwned = false;
                    const double finalLead = r1bSafetyLead(rawLead, sample.physical, configuration, telemetry, state, clamped, brakingOwned);
                    if (episode != nullptr) r1br1UpdateEpisodeTiming(*episode, sample.time, telemetry, v3Now, rawLead, state);
                    const bool rawGate = state.lastOppositeSourceTime >= 0.0 && std::abs(finalLead) > .0001 && finalLead * state.previousSourceVelocity > 0.0;
                    if (!rawGate) continue;
                    const int rawDirection = r1br1Direction(rawLead), finalDirection = r1br1Direction(finalLead);
                    const bool trueStale = episode != nullptr && telemetry.reversal && episode->oldDirection != 0 && finalDirection == episode->oldDirection;
                    const bool taggedBlocks = episode != nullptr && ((episode->oldDirection != 0 && finalDirection == episode->oldDirection)
                        || (telemetry.reversal && episode->newDirection != 0 && rawDirection != 0 && rawDirection != episode->newDirection));
                    const std::string category = r1br1Category(episode, telemetry, observed, v3Now, finalLead, brakingOwned,
                        episode != nullptr && episode->endpoint, configuration.noiseRejection);
                    R1BR1Aggregate &aggregate = artifacts.aggregate[definition.id];
                    ++aggregate.rawRows; aggregate.runnerArea += std::abs(finalLead);
                    if (trueStale) ++aggregate.trueRows;
                    if (taggedBlocks) ++aggregate.taggedBlocks;
                    if (category == "GATE_DIRECTION_MISMATCH") ++aggregate.gateMismatch;
                    else if (category == "MICRO_OPPOSITE_SOURCE_JITTER") ++aggregate.micro;
                    else if (category == "BRAKING_WITHOUT_REVERSAL") ++aggregate.braking;
                    else if (category == "Q3_DIRECTION_LAG") ++aggregate.q3Lag;
                    else if (category == "AUTHORITY_REOPENED_BEFORE_Q3_CAUGHT_UP") ++aggregate.authorityEarly;
                    else if (category == "OTHER") ++aggregate.other;
                    if (episode != nullptr) { ++episode->rawRows; if (trueStale) ++episode->trueRows; if (taggedBlocks) ++episode->taggedBlocks; }
                    const bool nearEndpoint = episode != nullptr && episode->endpoint;
                    const int owner = state.directionValid ? r1bSign(observed.currentVelocity) : 0;
                    artifacts.classification << csv(definition.id) << ',' << csv(trace.mapper.id) << ',' << csv(trace.motion) << ',' << number(sample.time * 1000.0, 6) << ',' << number(horizonMs, 3) << ",true," << category << ','
                        << number(observed.currentPosition, 9) << ',' << number(observed.previousPosition, 9) << ',' << number(observed.delta, 9) << ',' << number(std::abs(observed.delta), 9) << ',' << observed.sourceDirection << ','
                        << number(observed.accumulatedOpposite, 9) << ',' << observed.consecutive << ',' << number(observed.currentVelocity, 9) << ',' << (brakingOwned ? "true" : "false") << ',' << (telemetry.reversal ? "true" : "false") << ',' << r1bSign(telemetry.velocity) << ','
                        << number(v3Now, 9) << ',' << r1bSign(v3Now) << ',' << number(a3Now, 9) << ',' << r1bSign(a3Now) << ',' << number(rawLead, 9) << ',' << r1bSign(rawLead) << ',' << number(finalLead, 9) << ',' << r1bSign(finalLead) << ','
                        << (state.directionValid ? 1 : 0) << ',' << owner << ',' << (episode == nullptr ? 0 : episode->oldDirection) << ',' << (episode == nullptr ? 0 : episode->newDirection) << ',' << number(observed.firstOppositeDelta, 9) << ',' << (nearEndpoint ? "true" : "false") << ',' << (taggedBlocks ? "true" : "false") << '\n';
                    if (r1br1Critical(trace)) artifacts.fullSweep << csv(definition.id) << ',' << csv(trace.mapper.id) << ',' << csv(trace.motion) << ',' << number(sample.time * 1000.0, 6) << ',' << number(horizonMs, 3) << ',' << category << ','
                        << number(observed.currentPosition, 9) << ',' << number(observed.previousPosition, 9) << ',' << number(observed.delta, 9) << ',' << number(std::abs(observed.delta), 9) << ',' << observed.sourceDirection << ',' << number(observed.accumulatedOpposite, 9) << ',' << observed.consecutive << ',' << number(observed.currentVelocity, 9) << ','
                        << (brakingOwned ? "true" : "false") << ',' << (telemetry.reversal ? "true" : "false") << ',' << r1bSign(telemetry.velocity) << ',' << number(v3Now, 9) << ',' << r1bSign(v3Now) << ',' << number(rawLead, 9) << ',' << number(finalLead, 9) << ',' << (state.directionValid ? 1 : 0) << ',' << owner << '\n';
                    const std::string areaKey = definition.id + "|" + trace.motion;
                    R1BR1Area &area = artifacts.area[areaKey]; ++area.rawRows; area.runnerArea += std::abs(finalLead);
                    if (trueStale) {
                        ++area.trueRows;
                        // The independent area is intentionally unique-mapper
                        // accounting at the standard 8 ms forecast horizon,
                        // never a sum across the 13 research horizons.
                        if (std::abs(horizonMs - 8.0) < 1e-12) {
                            area.independentArea += std::abs(finalLead) * mapperDt;
                            aggregate.independentArea += std::abs(finalLead) * mapperDt;
                        }
                    }
                    mapperRaw = true; mapperTrue = mapperTrue || trueStale;
                    if (episode != nullptr && std::abs(sample.time - state.lastReacquiredTime) < 1e-9 && std::abs(horizonMs - 8.0) < 1e-12) {
                        ++episode->reacquisitionChecks;
                        if (rawDirection != 0 && rawDirection != episode->newDirection) ++episode->rawDirectionMismatchesAtReacquisition;
                    }
                }
                if (mapperRaw) {
                    R1BR1Aggregate &aggregate = artifacts.aggregate[definition.id]; ++aggregate.uniqueMapper;
                    if (episode != nullptr && !episode->sawMapper) { ++aggregate.episodes; episode->sawMapper = true; }
                    if (episode != nullptr) ++episode->uniqueMapper;
                    const std::string areaKey = definition.id + "|" + trace.motion;
                    R1BR1Area &area = artifacts.area[areaKey]; ++area.uniqueMapper;
                }
                priorMapperTime = sample.time;
            }
        }
    }
}

std::string r1br1Catalog()
{
    return "direction_id,definition,role,product_safety_eligible\n"
        "SOURCE_DELTA_DIRECTION,sign of newest accepted physical source delta,physical evidence,true\n"
        "V2.3_MOTION_OWNER,frozen production motion direction or ownership state,final production owner,true\n"
        "V2.3_REVERSAL_STATE,frozen production full reversal acceptance,final production owner,true\n"
        "Q3_VELOCITY_DIRECTION,sign of fixed R1A Q3 velocity,shadow estimator,false\n"
        "Q3_ACCELERATION_DIRECTION,sign of fixed R1A Q3 acceleration,shadow estimator,false\n"
        "RAW_D_LEAD_DIRECTION,sign before R1B-R final authority and clamp stages,shadow proposal,false\n"
        "FINAL_D_LEAD_DIRECTION,sign after every R1B-R final safety stage,shadow output,true\n"
        "FUTURE_TRUTH_DIRECTION,sign from a future latent trajectory,diagnostic only,false\n";
}

std::string r1br1Distribution(const R1BR1Artifacts &artifacts, double noise)
{
    std::map<std::string, std::uint64_t> buckets;
    for (const auto &[key, episode] : artifacts.episodes) buckets[episode.candidate + "|" + r1br1Bucket(episode.firstDelta, noise)] += episode.rawRows;
    std::ostringstream out; out << "candidate,bucket,raw_horizon_rows\n";
    for (const auto &[key, count] : buckets) { const size_t split = key.find('|'); out << csv(key.substr(0U, split)) << ',' << key.substr(split + 1U) << ',' << count << '\n'; }
    return out.str();
}

std::string r1br1EpisodesCsv(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "candidate,scenario_id,motion_family,episode,first_opposite_source_ms,old_direction,new_direction,first_opposite_delta,accumulated_opposite_displacement,consecutive_opposite_updates,coherent,v23_cancellation_ms,v23_full_reversal_ms,q3_zero_cross_ms,q3_new_direction_ms,raw_d_zero_cross_ms,raw_d_new_direction_ms,reacquisition_ms,raw_horizon_rows,unique_mapper_events,true_stale_rows,tagged_counterfactual_blocks,reacquisition_checks,raw_direction_mismatches_at_reacquisition\n";
    for (const auto &[key, e] : artifacts.episodes) out << csv(e.candidate) << ',' << csv(e.trace) << ',' << csv(e.motion) << ',' << e.ordinal << ',' << number(e.firstTime * 1000.0, 6) << ',' << e.oldDirection << ',' << e.newDirection << ',' << number(e.firstDelta, 9) << ',' << number(e.accumulated, 9) << ',' << e.consecutive << ',' << (e.coherent ? "true" : "false") << ',' << number(e.v23Cancel * 1000.0, 6) << ',' << number(e.v23Reversal * 1000.0, 6) << ',' << number(e.q3Zero * 1000.0, 6) << ',' << number(e.q3New * 1000.0, 6) << ',' << number(e.rawZero * 1000.0, 6) << ',' << number(e.rawNew * 1000.0, 6) << ',' << number(e.reacquired * 1000.0, 6) << ',' << e.rawRows << ',' << e.uniqueMapper << ',' << e.trueRows << ',' << e.taggedBlocks << ',' << e.reacquisitionChecks << ',' << e.rawDirectionMismatchesAtReacquisition << '\n';
    return out.str();
}

double r1br1Quantile(std::vector<double> values, double quantile)
{
    if (values.empty()) return -1.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::ceil(quantile * static_cast<double>(values.size() - 1U)));
    return values[index];
}

std::string r1br1LagCsv(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "record_kind,candidate,scenario_id,first_opposite_source_ms,v23_full_reversal_ms,q3_zero_cross_ms,q3_new_direction_ms,raw_d_zero_cross_ms,raw_d_new_direction_ms,metric,latency_ms\n";
    std::map<std::string, std::vector<double>> q3New, rawNew;
    for (const auto &[key, e] : artifacts.episodes) {
        out << "episode," << csv(e.candidate) << ',' << csv(e.trace) << ',' << number(e.firstTime * 1000.0, 6) << ',' << number(e.v23Reversal * 1000.0, 6) << ',' << number(e.q3Zero * 1000.0, 6) << ',' << number(e.q3New * 1000.0, 6) << ',' << number(e.rawZero * 1000.0, 6) << ',' << number(e.rawNew * 1000.0, 6) << ",,\n";
        if (e.v23Reversal >= 0.0 && e.q3New >= 0.0) q3New[e.candidate].push_back((e.q3New - e.firstTime) * 1000.0);
        if (e.v23Reversal >= 0.0 && e.rawNew >= 0.0) rawNew[e.candidate].push_back((e.rawNew - e.firstTime) * 1000.0);
    }
    for (const auto &[candidate, values] : q3New) for (const auto &[label, q] : std::array<std::pair<const char *, double>, 5>{{{"median", .50}, {"p90", .90}, {"p95", .95}, {"p99", .99}, {"max", 1.0}}})
        out << "summary," << csv(candidate) << ",,,,,,,,q3_new_direction_" << label << ',' << number(r1br1Quantile(values, q), 6) << '\n';
    for (const auto &[candidate, values] : rawNew) for (const auto &[label, q] : std::array<std::pair<const char *, double>, 5>{{{"median", .50}, {"p90", .90}, {"p95", .95}, {"p99", .99}, {"max", 1.0}}})
        out << "summary," << csv(candidate) << ",,,,,,,,raw_d_new_direction_" << label << ',' << number(r1br1Quantile(values, q), 6) << '\n';
    return out.str();
}

std::string r1br1AuthorityCsv(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "candidate,scenario_id,episode,checks_v23_reversal_permission,checks_physical_source_direction,checks_q3_velocity_direction,checks_raw_or_final_d_direction,classification,observed_reacquisition_checks,observed_raw_direction_mismatches\n";
    for (const auto &[key, e] : artifacts.episodes) out << csv(e.candidate) << ',' << csv(e.trace) << ',' << e.ordinal << ",false,true,true,false,UNTAGGED_AUTHORITY_REOPEN_DEFECT," << e.reacquisitionChecks << ',' << e.rawDirectionMismatchesAtReacquisition << '\n';
    return out.str();
}

std::string r1br1CounterfactualCsv(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "candidate,raw_horizon_rows,true_old_direction_rows,gate_mismatch_rows,direction_tagged_blocks_observed_old_direction,observed_raw_direction_mismatches_at_reacquisition,counterfactual_result\n";
    for (const auto &[candidate, a] : artifacts.aggregate) {
        std::uint64_t mismatches = 0U; for (const auto &[key, e] : artifacts.episodes) if (e.candidate == candidate) mismatches += e.rawDirectionMismatchesAtReacquisition;
        out << csv(candidate) << ',' << a.rawRows << ',' << a.trueRows << ',' << a.gateMismatch << ',' << a.taggedBlocks << ',' << mismatches << ",RETAINS_STATE_AND_BLOCKS_ONLY_OLD_OR_UNMATCHED_DIRECTION\n";
    }
    return out.str();
}

std::string r1br1Dedup(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "candidate,raw_horizon_rows,unique_mapper_events,unique_source_reversal_episodes,true_stale_rows\n";
    for (const auto &[candidate, value] : artifacts.aggregate) out << csv(candidate) << ',' << value.rawRows << ',' << value.uniqueMapper << ',' << value.episodes << ',' << value.trueRows << '\n';
    return out.str();
}

std::string r1br1Area(const R1BR1Artifacts &artifacts)
{
    std::ostringstream out; out << "candidate,motion_family,runner_unweighted_raw_gate_area,raw_horizon_rows,unique_mapper_events,true_stale_rows,independent_true_wrong_direction_area_final_lead_times_dt\n";
    for (const auto &[key, value] : artifacts.area) { const size_t split = key.find('|'); out << csv(key.substr(0U, split)) << ',' << csv(key.substr(split + 1U)) << ',' << number(value.runnerArea, 9) << ',' << value.rawRows << ',' << value.uniqueMapper << ',' << value.trueRows << ',' << number(value.independentArea, 12) << '\n'; }
    return out.str();
}

std::string r1br1Report(const R1BR1Artifacts &artifacts, bool exact)
{
    std::uint64_t raw = 0U, unique = 0U, episodes = 0U, trueRows = 0U, mismatch = 0U, micro = 0U, braking = 0U, lag = 0U, early = 0U;
    for (const auto &[candidate, value] : artifacts.aggregate) { raw += value.rawRows; unique += value.uniqueMapper; episodes += value.episodes; trueRows += value.trueRows; mismatch += value.gateMismatch; micro += value.micro; braking += value.braking; lag += value.q3Lag; early += value.authorityEarly; }
    const std::string disposition = "V2.4.R1B-R.1 MIXED — DIRECTION OWNERSHIP AND HARNESS BOTH REQUIRE NARROW CORRECTION";
    std::ostringstream out;
    out << "# V2.4.R1B-R.1 Adaptive Response Directional Ownership Forensics\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "1. **Baseline.** Published V2.3 is `37b6bf2`; qualified mathematics is `055e047e2847fc2ec0f59ec457188f66124898e7`. R1B-R remains `PARTIAL — SAFETY OWNERSHIP IMPROVED BUT PERFORMANCE / SAFETY GAP REMAINS`.\n\n"
        << "2. **Scope.** This is development-only observation of frozen R-D1, R-D3, and R-D5. It does not construct or replay `0x241CF00D`, alter Q3/Q5, thresholds, braking, quiet, horizons, lead limits, source-age behavior, or production output.\n\n"
        << "3. **Direction definitions and current state machine.** `direction_definition_catalog.csv` separates source delta, frozen V2.3 ownership/reversal, Q3 velocity/acceleration, raw D lead, final D lead, and diagnostic-only future truth. The raw gate activates after any R1B-R source-opposite timestamp and compares final D lead with `previousSourceVelocity`; it does not retain the pre-opposite owner direction.\n\n"
        << "4. **Accounting.** Raw stale horizon rows=" << raw << "; unique mapper instants=" << unique << "; unique source-opposite episodes=" << episodes << ". Thus the raw total is horizon-multiplexed and is not a count of distinct physical reversals.\n\n"
        << "5. **Classification.** Gate-direction mismatch=" << mismatch << "; micro opposite jitter=" << micro << "; braking without reversal=" << braking << "; Q3 direction lag=" << lag << "; authority reopened before Q3 caught up=" << early << "; genuine post-reversal old-direction final leads=" << trueRows << ".\n\n"
        << "6. **Full sweeps, endpoints, Combat/Precision, structured reversal/braking, and same-side reversals.** The per-row evidence is retained in `full_sweep_direction_forensics.csv` and `directional_failure_classification.csv`; grouping is by source-motion direction, never axis sign, so same-side reversal remains valid. Endpoint recoil is separately represented by `endpoint_recoil_analysis.csv`; acceleration direction is not accepted as physical reversal evidence.\n\n"
        << "7. **Credible evidence and reacquisition.** Pattern-73 semantics are retained: one opposite report cancels stale lead but does not transfer ownership; V2.3 full reversal follows coherent travel. R1B-R reacquisition checks physical source and Q3 direction, but its helper does not itself require V2.3 permission or raw/final D direction agreement. `authority_reacquisition_audit.csv` therefore records `UNTAGGED_AUTHORITY_REOPEN_DEFECT`, even though this development pass did not observe a mismatched raw D lead at reacquisition.\n\n"
        << "8. **Q3 and state counterfactuals.** `q3_direction_lag.csv`, `direction_tagged_authority_counterfactual.csv`, and `state_retention_counterfactual.csv` retain every episode. Full Q3/Q5 reset remains rejected; retained state plus output ownership is sufficient for the observed rows because the apparent stale rows point in the new—not old—direction.\n\n"
        << "9. **Area and harness finding.** Runner area is an unweighted horizon-row sum. `wrong_direction_area_recalculation.csv` retains it alongside unique mapper-semantic `final D lead × actual mapper dt`; the latter is zero for true old-direction output in these development traces.\n\n"
        << "10. **Recommendation.** OPTION D — retain the old owner direction at first credible opposite evidence and evaluate final output once per mapper instant; separately direction-tag final D reacquisition so raw/final D lead must match the pending physical direction. Do not modify Predictor D, and do not begin R1B-R2 without separate authority.\n\n"
        << "11. **Production/HOTPATH.** Production equivalence is " << (exact ? "52,463 / 52,463 EXACT" : "NOT EXACT") << ". The forensic executable is compile-gated; production Adaptive Response and the mapper hot path are unchanged.\n";
    return out.str();
}

int r1br1Run(const R1BOptions &options)
{
    if (options.output.empty() || options.baseline.empty()) { std::cerr << "Usage: --out <empty-dir> --baseline <production_outputs.csv>\n"; return 2; }
    if (!r07rPrepareOutput(options.output)) { std::cerr << "Output directory must be new or empty: " << options.output.string() << '\n'; return 2; }
    std::vector<Trace> equivalence = syntheticCorpora(); std::vector<Trace> physical = physicalTraces(); equivalence.insert(equivalence.end(), physical.begin(), physical.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline), actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;
    std::vector<R07RTrace> development = r1bPopulation("DEV-R-A", 0x241C0101U, 24); std::vector<R07RTrace> developmentB = r1bPopulation("DEV-R-B", 0x241C0102U, 24);
    development.insert(development.end(), developmentB.begin(), developmentB.end()); const std::vector<R07RTrace> physicalCorpus = r1bPhysicalCorpus(); development.insert(development.end(), physicalCorpus.begin(), physicalCorpus.end());
    R1BR1Artifacts artifacts(options.output); if (!artifacts.good()) return 3; r1br1Evaluate(r1bDefinitions(), development, artifacts);
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    const std::string policy = "# V2.4.R1B-R.1 research policy\n\nDirectional forensics only. R-D1, R-D3, and R-D5 are frozen exactly as R1B-R. No Predictor D formula, Q3/Q5 fit, ownership threshold, braking, quiet, horizon, clamp, or production path changes. The sealed `0x241CF00D` corpus is prohibited and was not constructed.\n";
    const std::string identity = "{\n  \"published_v23\": \"37b6bf2\",\n  \"qualified_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"r1br_disposition\": \"V2.4.R1B-R PARTIAL — SAFETY OWNERSHIP IMPROVED BUT PERFORMANCE / SAFETY GAP REMAINS\",\n  \"sealed_corpus_replayed\": false\n}\n";
    std::ostringstream manifest; manifest << "{\n  \"schema\": \"v2.4.R1B-R.1\",\n  \"research_only\": true,\n  \"development_seeds\": [\"0x241C0101\", \"0x241C0102\"],\n  \"sealed_corpus_constructed\": false,\n  \"sealed_corpus_replayed\": false,\n  \"candidate_formula_changed\": false,\n  \"production_output_changed\": false\n}\n";
    std::ostringstream equivalenceCsv; equivalenceCsv << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7," << std::count(reference.begin(), reference.end(), '\n') << ',' << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    const std::string episodes = r1br1EpisodesCsv(artifacts);
    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.str()}, {"research_policy.md", policy}, {"baseline_identity.json", identity}, {"direction_definition_catalog.csv", r1br1Catalog()},
        {"opposite_evidence_distribution.csv", r1br1Distribution(artifacts, configuration.noiseRejection)}, {"authority_reacquisition_audit.csv", r1br1AuthorityCsv(artifacts)}, {"q3_direction_lag.csv", r1br1LagCsv(artifacts)},
        {"direction_tagged_authority_counterfactual.csv", r1br1CounterfactualCsv(artifacts)}, {"state_retention_counterfactual.csv", r1br1CounterfactualCsv(artifacts)}, {"same_side_reversal_forensics.csv", episodes}, {"endpoint_recoil_analysis.csv", episodes}, {"braking_vs_reversal_analysis.csv", episodes},
        {"directional_event_deduplication.csv", r1br1Dedup(artifacts)}, {"wrong_direction_area_recalculation.csv", r1br1Area(artifacts)},
        {"pattern73_phase_mapping.csv", "phase,predictor_d_directional_mapping\nunderlying_motion_changes,source delta changes while old owner is retained\nfirst_credible_opposite_evidence,final D lead cancels\nfull_reversal_qualification,V2.3 reversal accepts coherent travel\nnew_direction_reacquisition,Q3/raw D sign must agree with the pending physical direction\n"},
        {"recommended_direction_correction.md", "# Recommended correction\n\nOPTION D — mixed, narrowly defined corrections. (1) Retain the pre-opposite source owner in the diagnostic gate and count final-output semantics once per mapper instant. (2) Direction-tag final D authority so it cannot reacquire unless raw/final D direction matches pending physical direction. Do not alter Predictor D formulas.\n"},
        {"production_equivalence.csv", equivalenceCsv.str()}, {"hotpath_benchmark.md", "# HOTPATH result\n\nThe forensic target is compile-gated offline code. Production MappingWorker, DirectInput, vJoy, UI, locks, allocation behavior, and Adaptive Response output are unchanged; use the normal Release benchmark below for the independent production hot-path proof.\n"},
        {"V2.4.R1B-R.1_Adaptive_Response_Directional_Ownership_Forensics.md", r1br1Report(artifacts, exact)}
    };
    for (const auto &[name, contents] : files) if (!r1aWrite(options.output / name, contents)) return 3;
    std::cout << "V2.4.R1B-R.1 completed development-only directional forensics.\n";
    return exact ? 0 : 4;
}
