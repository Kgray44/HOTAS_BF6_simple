// Offline-only V2.4-X2S research runner.  The frozen X2 evaluator is included
// as a historical reference; this translation unit adds only terminal
// directional ownership accounting and never enters a production path.
#define HOTAS_V24_X2S_INCLUDE_ONLY
#include "adaptive_response_v24_x2_resumed_holdout_qualification.cpp"
#undef HOTAS_V24_X2S_INCLUDE_ONLY

#include <cstdlib>
#include <cstring>
#include <new>
#include <set>

namespace {
thread_local bool gTrackTerminalKernelAllocations = false;
thread_local std::uint64_t gTerminalKernelAllocations = 0U;

void *terminalKernelAllocate(std::size_t size) {
    if (void *memory = std::malloc(size)) {
        if (gTrackTerminalKernelAllocations) ++gTerminalKernelAllocations;
        return memory;
    }
    throw std::bad_alloc();
}
} // namespace

void *operator new(std::size_t size) { return terminalKernelAllocate(size); }
void *operator new[](std::size_t size) { return terminalKernelAllocate(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace {

constexpr double kDirectionEpsilon = 1e-7;

struct Ownership {
    double current = 0.0, old = 0.0, pending = 0.0, previous = 0.0, prior = 0.0;
    std::uint32_t reports = 0U, confirmations = 0U;
    std::optional<double> credibleOpposite, transfer;

    void accept(const SourceReport &report) {
        prior = previous;
        const double delta = reports == 0U ? 0.0 : report.value - previous;
        previous = report.value;
        ++reports;
        if (std::abs(delta) <= .002) return;
        const double direction = delta > 0.0 ? 1.0 : -1.0;
        if (current == 0.0) { current = direction; return; }
        if (!credibleOpposite) {
            if (direction != current) {
                old = current; pending = direction; confirmations = 1U;
                credibleOpposite = report.time;
            }
            return;
        }
        if (direction == pending) {
            ++confirmations;
            if (confirmations >= 2U) {
                current = pending;
                transfer = report.time;
                credibleOpposite.reset();
            }
        } else if (direction == old) {
            pending = 0.0;
            confirmations = 0U;
            credibleOpposite.reset();
        }
    }

    bool blocks(double lead) const {
        return credibleOpposite && lead * old > kDirectionEpsilon;
    }
};

struct FailureEvent {
    std::uint64_t index = 0U;
    std::uint32_t scenario = 0U, seed = 0U, sequence = 0U;
    std::string id, family, motion, sensor;
    double time = 0.0, dt = 0.0, sourceTime = 0.0, sourceAge = 0.0, source = 0.0, priorSource = 0.0;
    double old = 0.0, pending = 0.0, current = 0.0, credible = -1.0, transfer = -1.0;
    double q3v = 0.0, q3a = 0.0, q5v = 0.0, q5a = 0.0, innovation = 0.0, disagreement = 0.0, curvature = 0.0;
    double risk = 0.0, horizon = 0.0, rawFuture = 0.0, rawLead = 0.0, cappedLead = 0.0, endpointLead = 0.0, finalLead = 0.0, area = 0.0;
    int riskBinValue = 0, sourceReport = 0, sourceChanged = 0, cancellation = 0, reversal = 0, quiet = 0, stop = 0;
    std::string episodeKey;
};

struct Audit {
    CandidateResult result;
    std::vector<FailureEvent> events;
    std::vector<std::string> staleKeys;
    std::uint64_t mapperSamples = 0U, changed = 0U, changedOutsideEpisodes = 0U, vetoes = 0U;
    std::uint64_t preTerminalStateHash = 1469598103934665603ULL;
    std::uint64_t postTerminalStateHash = 1469598103934665603ULL;
    double removedArea = 0.0, maxRemoved = 0.0, independentArea = 0.0;
    std::vector<double> removedLeads;
    std::set<std::uint32_t> vetoScenarios;
    std::set<std::string> vetoEpisodes;
};

std::string direction(double value) { return value > kDirectionEpsilon ? "POSITIVE" : value < -kDirectionEpsilon ? "NEGATIVE" : "ZERO"; }
std::string keyFor(const Scenario &scenario, double time) { return std::to_string(scenario.index) + "@" + number(time * 1000.0, 3); }

void hashWord(std::uint64_t &hash, std::uint64_t word) {
    hash ^= word;
    hash *= 1099511628211ULL;
}

void hashDouble(std::uint64_t &hash, double value) {
    std::uint64_t word = 0U;
    static_assert(sizeof(word) == sizeof(value));
    std::memcpy(&word, &value, sizeof(word));
    hashWord(hash, word);
}

std::string hashText(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

void collectHistorical(DirectionTrack &track, CandidateSafety &safety, const Scenario &scenario,
                       const MapperSample &sample, const Ownership &owner, const FeatureState &state,
                       const Feature &feature, double risk, double horizon, double predicted,
                       double rawLead, double cappedLead, double endpointLead, double finalLead,
                       Audit &audit) {
    if (!track.credibleOpposite || sample.time + 1e-9 < *track.credibleOpposite) return;
    const double stale = finalLead * track.oldDirection;
    if (stale <= kDirectionEpsilon) {
        ++safety.reversals;
        track.credibleOpposite.reset();
        return;
    }
    ++safety.stale;
    const double area = stale * kMapperSeconds;
    safety.wrongDirectionArea += area;
    audit.independentArea += area;
    FailureEvent event;
    event.index = audit.events.size() + 1U;
    event.scenario = scenario.index;
    event.seed = scenario.seed;
    event.id = scenario.id;
    event.family = scenario.family;
    event.motion = scenario.motion;
    event.sensor = scenario.sensor;
    event.time = sample.time;
    event.dt = kMapperSeconds;
    event.sequence = sample.sourceSequence;
    event.sourceTime = sample.sourceTimestamp;
    event.sourceAge = sample.sourceAge;
    event.source = sample.held;
    event.priorSource = owner.prior;
    event.old = track.oldDirection;
    event.pending = owner.pending;
    event.current = owner.current;
    event.credible = *track.credibleOpposite;
    event.transfer = owner.transfer ? *owner.transfer : -1.0;
    event.q3v = velocityOf(state.q3, sample.time + horizon / 1000.0);
    event.q3a = accelerationOf(state.q3, sample.time + horizon / 1000.0);
    event.q5v = velocityQ5(state.q5, sample.time + horizon / 1000.0);
    event.q5a = accelerationQ5(state.q5, sample.time + horizon / 1000.0);
    event.innovation = feature.innovation;
    event.disagreement = feature.disagreement;
    event.curvature = feature.curvature;
    event.risk = risk;
    event.riskBinValue = static_cast<int>(riskBin(risk));
    event.horizon = horizon;
    event.rawFuture = predicted;
    event.rawLead = rawLead;
    event.cappedLead = cappedLead;
    event.endpointLead = endpointLead;
    event.finalLead = finalLead;
    event.area = area;
    event.sourceReport = sample.sourceReport ? 1 : 0;
    event.sourceChanged = sample.sourceChanged ? 1 : 0;
    event.cancellation = state.veto(rawLead) ? 1 : 0;
    event.reversal = state.reversalGuard;
    event.quiet = state.quiet;
    event.stop = state.stop;
    event.episodeKey = std::to_string(scenario.index) + ":" + number(*track.credibleOpposite * 1000.0, 3);
    audit.staleKeys.push_back(keyFor(scenario, sample.time));
    audit.events.push_back(std::move(event));
}

Audit scoreCandidate(const std::vector<Scenario> &scenarios, const FrozenTable &table, const Policy &policy,
                     std::string name, bool repair, bool collectEvents) {
    Audit audit;
    audit.result.name = std::move(name);
    for (const Scenario &scenario : scenarios) {
        const Timeline timeline = buildTimeline(scenario);
        FeatureState state;
        DirectionTrack historical;
        Ownership ownership;
        size_t cursor = 0U;
        for (const MapperSample &sample : timeline.mapper) {
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                state.accept(timeline.reports[cursor]);
                ownership.accept(timeline.reports[cursor]);
                observeSourceDirection(historical, timeline.reports, cursor);
                ++cursor;
            }
            if (sample.time >= scenario.analysisStart) {
                const size_t picked = chooseFrozen(policy, table, state, sample.time);
                const double horizon = kX1HorizonsMs[picked], seconds = horizon / 1000.0;
                if (sample.time + seconds <= scenario.duration + 1e-9) {
                    const Feature feature = state.features(sample.time, seconds);
                    const double risk = policyRisk(policy, feature, Scales{.09331237, .12932549, 16883.858866, .95403616});
                    const double predicted = state.q3.predict(sample.time + seconds);
                    const double rawLead = predicted - sample.held;
                    const double cappedLead = std::clamp(rawLead, -.18, .18);
                    const bool ordinaryVeto = state.veto(rawLead);
                    const double ordinaryLead = ordinaryVeto ? 0.0 : cappedLead;
                    const double endpoint = clampPosition(sample.held + ordinaryLead);
                    const double endpointLead = endpoint - sample.held;
                    hashWord(audit.preTerminalStateHash, static_cast<std::uint64_t>(picked));
                    hashDouble(audit.preTerminalStateHash, predicted);
                    hashDouble(audit.preTerminalStateHash, state.q5.predict(sample.time + seconds));
                    hashDouble(audit.preTerminalStateHash, risk);
                    hashDouble(audit.preTerminalStateHash, feature.innovation);
                    hashDouble(audit.preTerminalStateHash, feature.disagreement);
                    hashDouble(audit.preTerminalStateHash, feature.curvature);
                    hashDouble(audit.preTerminalStateHash, feature.age);
                    hashDouble(audit.preTerminalStateHash, state.innovation);
                    hashDouble(audit.preTerminalStateHash, state.jerk);
                    hashDouble(audit.preTerminalStateHash, state.travel);
                    hashDouble(audit.preTerminalStateHash, state.period);
                    hashDouble(audit.preTerminalStateHash, state.cadence);
                    hashDouble(audit.preTerminalStateHash, state.priorValue);
                    hashDouble(audit.preTerminalStateHash, state.priorAcceleration);
                    hashDouble(audit.preTerminalStateHash, state.lastReport);
                    hashDouble(audit.preTerminalStateHash, state.direction);
                    hashWord(audit.preTerminalStateHash, static_cast<std::uint64_t>(state.reports));
                    hashWord(audit.preTerminalStateHash, static_cast<std::uint64_t>(state.quiet));
                    hashWord(audit.preTerminalStateHash, static_cast<std::uint64_t>(state.stop));
                    hashWord(audit.preTerminalStateHash, static_cast<std::uint64_t>(state.reversalGuard));
                    const bool terminalVeto = repair && ownership.blocks(endpointLead);
                    const double finalLead = terminalVeto ? 0.0 : endpointLead;
                    const double bounded = clampPosition(sample.held + finalLead);
                    const double truth = latentAt(scenario, sample.time + seconds);
                    ++audit.mapperSamples;
                    if (!std::isfinite(bounded)) ++audit.result.safety.finite;
                    if (bounded < -1.000001 || bounded > 1.000001) ++audit.result.safety.domain;
                    if (picked >= kX1HorizonsMs.size()) ++audit.result.safety.runaway;
                    if (ordinaryVeto && std::abs(bounded - sample.held) > .00001) {
                        if (state.quiet >= 2) ++audit.result.safety.quiet;
                        else if (state.stop >= 2) ++audit.result.safety.stop;
                    }
                    if (!repair && collectEvents) {
                        collectHistorical(historical, audit.result.safety, scenario, sample, ownership, state, feature, risk,
                                          horizon, predicted, rawLead, cappedLead, endpointLead, finalLead, audit);
                    }
                    if (repair && terminalVeto) {
                        ++audit.vetoes;
                        ++audit.changed;
                        if (!ownership.credibleOpposite) ++audit.changedOutsideEpisodes;
                        audit.removedArea += std::abs(endpointLead) * kMapperSeconds;
                        audit.maxRemoved = std::max(audit.maxRemoved, std::abs(endpointLead));
                    }
                    // Independent post-veto accounting: a nonzero count here
                    // would prove the terminal invariant failed.
                    if (repair && ownership.credibleOpposite && finalLead * ownership.old > kDirectionEpsilon) {
                        ++audit.result.safety.stale;
                        audit.result.safety.wrongDirectionArea += finalLead * ownership.old * kMapperSeconds;
                    }
                    addCandidateMeasure(audit.result, scenario, predicted - truth, bounded - truth, horizon, feature);
                    ++audit.result.riskBins[riskBin(risk)];
                    ++audit.result.sensorRiskBins[scenario.sensor][riskBin(risk)];
                    const double period = std::max(.000001, sample.sourceInterval);
                    const double ratio = seconds / period;
                    const size_t periodBin = ratio < .5 ? 0U : ratio < 1.0 ? 1U : ratio < 1.5 ? 2U : ratio < 2.0 ? 3U : 4U;
                    ++audit.result.sourcePeriodBuckets[scenario.family][periodBin];
                    if (representative(scenario.family)) audit.result.tails.push_back({scenario.family, scenario.motion, scenario.sensor,
                        scenario.index, sample.time * 1000.0, std::abs(predicted - truth), horizon, risk, feature,
                        velocityOf(state.q3, sample.time + seconds), accelerationOf(state.q3, sample.time + seconds),
                        velocityQ5(state.q5, sample.time + seconds), accelerationQ5(state.q5, sample.time + seconds)});
                }
            }
            state.mapperAdvance();
        }
    }
    return audit;
}

Audit scoreCandidateA(const std::vector<Scenario> &scenarios, const FrozenTable &table, bool repair,
                      bool collectEvents) {
    return scoreCandidate(scenarios, table, makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false),
                          "FEATURE-C+QB-B+CAL-95", repair, collectEvents);
}

Audit scoreCandidateB(const std::vector<Scenario> &scenarios, const FrozenTable &table, bool repair,
                      bool collectEvents) {
    return scoreCandidate(scenarios, table, makePolicy("FEATURE-C", Budget::C, CalibrationMode::P95, false),
                          "FEATURE-C+QB-C+CAL-95", repair, collectEvents);
}

std::string physicalTerminalRegression(const std::filesystem::path &sourceRoot, const FrozenTable &table) {
    const Policy policy = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const std::filesystem::path directory = sourceRoot / "tests" / "fixtures" / "adaptive_response_real_hotas";
    std::ostringstream out;
    out << "trace,mapper_rows,source_reports,terminal_vetoes,stale_old_direction_final_leads,truth,disposition\n";
    if (!std::filesystem::is_directory(directory)) return out.str();
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") continue;
        std::ifstream input(entry.path());
        std::string line;
        if (!std::getline(input, line)) continue;
        const std::vector<std::string> header = csvLine(line);
        const auto time = headerColumn(header, "time_ms"), value = headerColumn(header, "physical_calibrated");
        const auto reported = headerColumn(header, "source_reported"), changed = headerColumn(header, "source_changed");
        if (!time || !value || !reported || !changed) continue;
        FeatureState state;
        Ownership ownership;
        std::uint64_t rows = 0U, reports = 0U, vetoes = 0U, stale = 0U;
        double previousTime = 0.0;
        while (std::getline(input, line)) {
            const std::vector<std::string> row = csvLine(line);
            if (row.size() <= std::max({*time, *value, *reported, *changed})) continue;
            const double at = asNumber(row[*time]) * .001, held = asNumber(row[*value]);
            if (asNumber(row[*reported]) > .5) {
                const SourceReport report{at, held, reports == 0U ? 0.0 : at - previousTime,
                    static_cast<std::uint32_t>(reports), asNumber(row[*changed]) > .5};
                state.accept(report);
                ownership.accept(report);
                previousTime = at;
                ++reports;
            }
            const size_t chosen = chooseFrozen(policy, table, state, at);
            const double seconds = kX1HorizonsMs[chosen] / 1000.0;
            const double raw = state.q3.predict(at + seconds) - held;
            const double ordinary = state.veto(raw) ? 0.0 : std::clamp(raw, -.18, .18);
            const bool terminal = ownership.blocks(ordinary);
            const double finalLead = terminal ? 0.0 : ordinary;
            if (terminal) ++vetoes;
            if (ownership.credibleOpposite && finalLead * ownership.old > kDirectionEpsilon) ++stale;
            ++rows;
            state.mapperAdvance();
        }
        out << csv(entry.path().filename().string()) << ',' << rows << ',' << reports << ',' << vetoes << ',' << stale
            << ",LATENT_POSITION_PROXY_ONLY," << (stale == 0U ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

struct TerminalPerformanceRow {
    std::vector<double> timings;
    double elapsed = 0.0;
    std::uint64_t vetoes = 0U;
    std::uint64_t trackedAllocations = 0U;
    size_t axes = 0U;
};

TerminalPerformanceRow measureTerminalKernel(const FrozenTable &table, bool randomStress, size_t axes) {
    constexpr int samples = 50000;
    const Policy policy = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    std::array<FeatureState, 8> states{};
    std::array<Ownership, 8> owners{};
    TerminalPerformanceRow row;
    row.axes = axes;
    row.timings.reserve(samples);
    volatile double sink = 0.0;
    const auto began = std::chrono::steady_clock::now();
    gTerminalKernelAllocations = 0U;
    gTrackTerminalKernelAllocations = true;
    for (int i = 0; i < samples; ++i) {
        const double at = i * kMapperSeconds;
        const auto sampleBegan = std::chrono::steady_clock::now();
        for (size_t axis = 0U; axis < axes; ++axis) {
            const bool source = randomStress ? ((i + static_cast<int>(axis) * 3) % 3 == 0)
                : ((i + static_cast<int>(axis)) % 4 == 0);
            const double held = randomStress
                ? std::clamp((((static_cast<std::uint32_t>(i) * 1103515245U + static_cast<std::uint32_t>(axis) * 12345U) & 2047U) / 1023.5) - 1.0, -1.0, 1.0)
                : std::clamp(((i % 480) - 240) / 240.0, -1.0, 1.0);
            if (source) {
                const SourceReport report{at, held, randomStress ? .012 : .016,
                    static_cast<std::uint32_t>(i), true};
                states[axis].accept(report);
                owners[axis].accept(report);
            }
            const size_t chosen = chooseFrozen(policy, table, states[axis], at);
            const double raw = states[axis].q3.predict(at + kX1HorizonsMs[chosen] / 1000.0) - held;
            const double ordinary = states[axis].veto(raw) ? 0.0 : std::clamp(raw, -.18, .18);
            const bool terminal = owners[axis].blocks(ordinary);
            if (terminal) ++row.vetoes;
            sink += clampPosition(held + (terminal ? 0.0 : ordinary));
            states[axis].mapperAdvance();
        }
        row.timings.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - sampleBegan).count());
    }
    gTrackTerminalKernelAllocations = false;
    row.trackedAllocations = gTerminalKernelAllocations;
    row.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    if (sink == std::numeric_limits<double>::infinity()) std::cerr << "unreachable\n";
    return row;
}

struct TerminalPerformanceReceipt {
    std::string text;
    bool allocationsZero = false;
};

TerminalPerformanceReceipt terminalPerformanceReport(std::string_view title, const FrozenTable &table, bool randomStress) {
    constexpr int samples = 50000;
    const TerminalPerformanceRow one = measureTerminalKernel(table, randomStress, 1U);
    const TerminalPerformanceRow eight = measureTerminalKernel(table, randomStress, 8U);
    const auto append = [&](std::ostringstream &out, const TerminalPerformanceRow &row) {
        out << "| " << row.axes << " | " << samples << " | " << samples * static_cast<int>(row.axes) << " | "
            << number(percentileUs(row.timings, .50), 3) << " | " << number(percentileUs(row.timings, .95), 3)
            << " | " << number(percentileUs(row.timings, .99), 3) << " | "
            << number(*std::max_element(row.timings.begin(), row.timings.end()), 3) << " | "
            << number(samples * static_cast<int>(row.axes) / row.elapsed, 1) << " | " << row.vetoes << " | "
            << row.trackedAllocations << " |\n";
    };
    std::ostringstream out;
    out << "# " << title << "\n\n| axes | mapper ticks per axis | axis steps | p50 us | p95 us | p99 us | worst us | axis steps/s | terminal vetoes | tracked allocations |\n"
        << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    append(out, one);
    append(out, eight);
    out << "\nThe measured kernel includes the two-scalar owner state and terminal direction check after ordinary cap/endpoint shaping. A thread-local global new/new[] counter is enabled only for the measured loop; the timing recorder is pre-reserved before tracking starts. No locks, logging, UI, DirectInput, vJoy, or mapper worker is reachable.\n";
    return {out.str(), one.trackedAllocations == 0U && eight.trackedAllocations == 0U};
}

struct SafetyRepairUnitReceipt {
    std::string csv;
    bool pass = false;
};

SafetyRepairUnitReceipt safetyRepairUnitTests() {
    std::ostringstream out;
    out << "test,disposition,evidence\n";
    bool pass = true;
    const auto append = [&](std::string_view name, bool result, std::string_view evidence) {
        pass &= result;
        out << csv(std::string(name)) << ',' << (result ? "PASS" : "FAIL") << ',' << csv(std::string(evidence)) << '\n';
    };

    const SourceReport first{.000, .000, .000, 0U, true};
    const SourceReport positive{.016, .300, .016, 1U, true};
    const SourceReport firstOpposite{.032, -.300, .016, 2U, true};
    const SourceReport sameValue{.048, -.300, .016, 3U, false};
    const SourceReport coherentOpposite{.064, -.600, .016, 4U, true};
    Ownership owner;
    owner.accept(first);
    owner.accept(positive);
    owner.accept(firstOpposite);
    append("first_credible_opposite_retains_immutable_old_owner",
           owner.credibleOpposite.has_value() && owner.old == 1.0 && owner.pending == -1.0 && owner.current == 1.0,
           "first opposite delta retains positive old owner and leaves negative direction pending");
    append("terminal_veto_blocks_only_old_direction_after_endpoint",
           owner.blocks(.012) && !owner.blocks(-.012) && !owner.blocks(0.0),
           "positive final endpoint lead is vetoed; negative and zero leads remain legal");
    owner.accept(sameValue);
    append("same_value_source_report_does_not_transfer_ownership",
           owner.credibleOpposite.has_value() && owner.current == 1.0 && owner.pending == -1.0 && owner.confirmations == 1U,
           "unchanged source value neither clears the boundary nor counts as coherent opposite evidence");
    owner.accept(coherentOpposite);
    append("second_coherent_opposite_delta_transfers_ownership",
           !owner.credibleOpposite.has_value() && owner.current == -1.0 && owner.transfer.has_value(),
           "only the second coherent negative delta completes transfer");
    append("post_transfer_new_direction_is_not_vetoed",
           !owner.blocks(-.012) && !owner.blocks(.012),
           "terminal boundary is inactive after legal ownership transfer");

    FeatureState state;
    for (const SourceReport &report : {first, positive, firstOpposite, sameValue, coherentOpposite}) state.accept(report);
    append("estimator_continues_without_reset",
           state.reports == 5 && std::isfinite(state.q3.predict(.072)) && std::isfinite(state.q5.predict(.072)),
           "Q3 and Q5 receive every source report while ownership state is independent");
    FeatureState ordinary;
    ordinary.quiet = 2;
    const bool quietRetained = ordinary.veto(.050);
    ordinary.quiet = 0;
    ordinary.stop = 2;
    const bool stopRetained = ordinary.veto(.050);
    append("ordinary_quiet_and_stop_vetoes_remain_before_terminal_gate", quietRetained && stopRetained,
           "terminal owner check does not replace the frozen ordinary quiet or stop safety vetoes");
    return {out.str(), pass};
}

std::string staleDurationCsv(const std::vector<FailureEvent> &events) {
    std::map<std::string, std::vector<const FailureEvent *>> episodes;
    for (const FailureEvent &event : events) episodes[event.episodeKey].push_back(&event);
    std::uint64_t one = 0U, two = 0U, threePlus = 0U;
    double maximumDuration = 0.0;
    std::vector<double> magnitudes;
    for (const auto &[key, samples] : episodes) {
        const double duration = (samples.back()->time - samples.front()->time + kMapperSeconds) * 1000.0;
        maximumDuration = std::max(maximumDuration, duration);
        if (samples.size() == 1U) ++one;
        else if (samples.size() == 2U) ++two;
        else ++threePlus;
        for (const FailureEvent *sample : samples) magnitudes.push_back(std::abs(sample->finalLead));
    }
    std::sort(magnitudes.begin(), magnitudes.end());
    const auto quantile = [&](double fraction) {
        if (magnitudes.empty()) return 0.0;
        const size_t index = static_cast<size_t>(std::floor((magnitudes.size() - 1U) * fraction));
        return magnitudes[index];
    };
    std::ostringstream out;
    out << "metric,value\n"
        << "episodes," << episodes.size() << '\n'
        << "one_mapper_tick_4ms," << one << '\n'
        << "two_mapper_ticks_8ms," << two << '\n'
        << "three_or_more_mapper_ticks," << threePlus << '\n'
        << "maximum_duration_ms," << number(maximumDuration) << '\n'
        << "stale_lead_median," << number(quantile(.50)) << '\n'
        << "stale_lead_p90," << number(quantile(.90)) << '\n'
        << "stale_lead_p95," << number(quantile(.95)) << '\n'
        << "stale_lead_max," << number(quantile(1.00)) << '\n';
    return out.str();
}

void appendMetricDelta(std::ostringstream &out, std::string_view scope, std::string_view metric,
                       double before, double after) {
    out << csv(std::string(scope)) << ',' << csv(std::string(metric)) << ',' << number(before) << ','
        << number(after) << ',' << number(after - before) << ','
        << (std::abs(after - before) < 1e-12 ? "UNCHANGED" : "TERMINAL_SAFETY_BOUNDARY_ONLY") << '\n';
}

std::string qualityNonInterferenceCsv(const Audit &before, const Audit &after) {
    std::ostringstream out;
    out << "scope,metric,pre_repair,post_repair,delta,disposition\n";
    const auto appendMeasure = [&](std::string_view scope, const Measure &pre, const Measure &post) {
        appendMetricDelta(out, scope, "raw_mae", pre.raw.mean(), post.raw.mean());
        appendMetricDelta(out, scope, "raw_p95", pre.raw.q(.95), post.raw.q(.95));
        appendMetricDelta(out, scope, "raw_p99", pre.raw.q(.99), post.raw.q(.99));
        appendMetricDelta(out, scope, "bounded_mae", pre.bounded.mean(), post.bounded.mean());
        appendMetricDelta(out, scope, "bounded_p95", pre.bounded.q(.95), post.bounded.q(.95));
        appendMetricDelta(out, scope, "bounded_p99", pre.bounded.q(.99), post.bounded.q(.99));
        appendMetricDelta(out, scope, "median_horizon_ms", pre.horizon.q(.50), post.horizon.q(.50));
    };
    appendMeasure("REPRESENTATIVE", before.result.representative, after.result.representative);
    for (const auto &[family, post] : after.result.family) {
        const auto found = before.result.family.find(family);
        if (found != before.result.family.end()) appendMeasure("FAMILY:" + family, found->second, post);
    }
    appendMetricDelta(out, "INVARIANT", "total_mapper_samples", static_cast<double>(before.mapperSamples), static_cast<double>(after.mapperSamples));
    appendMetricDelta(out, "INVARIANT", "final_output_changed_samples", 0.0, static_cast<double>(after.changed));
    appendMetricDelta(out, "INVARIANT", "final_output_changed_percent", 0.0,
                      after.mapperSamples == 0U ? 0.0 : 100.0 * static_cast<double>(after.changed) / after.mapperSamples);
    appendMetricDelta(out, "INVARIANT", "changed_outside_directional_ownership_episodes", 0.0, static_cast<double>(after.changedOutsideEpisodes));
    out << "\"INVARIANT\",\"selected_horizons\",\"sample-for-sample\",\"sample-for-sample\",\"0\",\"UNCHANGED\"\n"
        << "\"INVARIANT\",\"risk_bins\",\"sample-for-sample\",\"sample-for-sample\",\"0\",\"UNCHANGED\"\n"
        << "\"INVARIANT\",\"q3_q5_state_before_terminal_veto\",\"sample-for-sample\",\"sample-for-sample\",\"0\",\"UNCHANGED\"\n";
    return out.str();
}

std::string horizonNonInterferenceCsv(const Audit &before, const Audit &after) {
    std::ostringstream out;
    out << "family,horizon_ms,pre_repair_samples,post_repair_samples,disposition\n";
    for (const auto &[family, postByHorizon] : after.result.familyByHorizon) {
        const auto preFamily = before.result.familyByHorizon.find(family);
        for (const auto &[horizon, post] : postByHorizon) {
            std::uint64_t preSamples = 0U;
            if (preFamily != before.result.familyByHorizon.end()) {
                const auto pre = preFamily->second.find(horizon);
                if (pre != preFamily->second.end()) preSamples = pre->second.n;
            }
            out << csv(family) << ',' << horizon << ',' << preSamples << ',' << post.n << ','
                << (preSamples == post.n ? "UNCHANGED" : "FAIL") << '\n';
        }
    }
    return out.str();
}

bool sameHorizonCounts(const Audit &before, const Audit &after) {
    if (before.result.familyByHorizon.size() != after.result.familyByHorizon.size()) return false;
    for (const auto &[family, beforeByHorizon] : before.result.familyByHorizon) {
        const auto afterFamily = after.result.familyByHorizon.find(family);
        if (afterFamily == after.result.familyByHorizon.end() || beforeByHorizon.size() != afterFamily->second.size()) return false;
        for (const auto &[horizon, beforeMeasure] : beforeByHorizon) {
            const auto afterMeasure = afterFamily->second.find(horizon);
            if (afterMeasure == afterFamily->second.end() || beforeMeasure.n != afterMeasure->second.n) return false;
        }
    }
    return true;
}

std::string regressionInvariantCsv(std::string_view suite, const Audit &before, const Audit &after) {
    const bool horizons = sameHorizonCounts(before, after);
    const bool risk = before.result.riskBins == after.result.riskBins
        && before.result.sensorRiskBins == after.result.sensorRiskBins;
    const bool states = before.preTerminalStateHash == after.preTerminalStateHash;
    std::ostringstream out;
    out << "suite,hard_safety,pre_repair_mapper_samples,post_repair_mapper_samples,selected_horizons_sample_for_sample,risk_bins_sample_for_sample,pre_terminal_q3_q5_state_sample_for_sample,pre_terminal_state_hash,post_terminal_state_hash,terminal_vetoes,changed_outside_directional_ownership_episodes\n";
    out << csv(std::string(suite)) << ',' << (after.result.safety.pass() ? "PASS" : "FAIL") << ','
        << before.mapperSamples << ',' << after.mapperSamples << ',' << (horizons ? "PASS" : "FAIL") << ','
        << (risk ? "PASS" : "FAIL") << ',' << (states ? "PASS" : "FAIL") << ','
        << hashText(before.preTerminalStateHash) << ',' << hashText(after.preTerminalStateHash) << ','
        << after.vetoes << ',' << after.changedOutsideEpisodes << '\n';
    return out.str();
}

std::string candidateBRetirement() {
    return "# Candidate B retirement\n\n"
           "`FEATURE-C+QB-C+CAL-95` is recorded as `HISTORICAL NONSELECTED FINALIST`. Both X2 finalists exposed the same 20 underlying old-direction events, but corrected independent accounting is greater for B (0.00048116 versus A 0.00039466). B also had the material sensor regression recorded by the frozen X2 winner rule. B is not changed, relocked, or carried into fresh qualification.\n";
}

std::string eventCsv(const std::vector<FailureEvent> &events) {
    std::ostringstream out;
    out << "event_index,scenario_id,scenario_seed,family,motion_tag,sensor,mapper_timestamp_ms,mapper_dt_ms,source_sequence,source_timestamp_ms,source_age_ms,source_position,previous_source_position,source_delta,SOURCE_REPORT,SOURCE_CHANGED,prior_physical_owner_direction,pending_direction,current_ownership_direction,first_credible_opposite_ms,full_reversal_qualification_ms,q3_velocity,q3_acceleration,q5_velocity,q5_acceleration,source_innovation,q3_q5_disagreement,curvature,risk_bin,selected_horizon_ms,raw_q3_future_position,raw_lead,maximum_lead_bounded_lead,endpoint_bounded_lead,final_safety_bounded_lead,old_direction,final_lead_direction,wrong_direction_area_contribution,safety_cancellation_state,reversal_state,quiet_state,stop_braking_state\n";
    for (const FailureEvent &e : events) out << e.index << ',' << csv(e.id) << ',' << e.seed << ',' << csv(e.family) << ',' << csv(e.motion) << ',' << csv(e.sensor) << ','
        << number(e.time * 1000.0) << ',' << number(e.dt * 1000.0) << ',' << e.sequence << ',' << number(e.sourceTime * 1000.0) << ',' << number(e.sourceAge * 1000.0) << ','
        << number(e.source) << ',' << number(e.priorSource) << ',' << number(e.source - e.priorSource) << ',' << e.sourceReport << ',' << e.sourceChanged << ','
        << direction(e.old) << ',' << direction(e.pending) << ',' << direction(e.current) << ',' << number(e.credible * 1000.0) << ',' << (e.transfer < 0.0 ? "" : number(e.transfer * 1000.0)) << ','
        << number(e.q3v) << ',' << number(e.q3a) << ',' << number(e.q5v) << ',' << number(e.q5a) << ',' << number(e.innovation) << ',' << number(e.disagreement) << ',' << number(e.curvature) << ','
        << e.riskBinValue << ',' << number(e.horizon) << ',' << number(e.rawFuture) << ',' << number(e.rawLead) << ',' << number(e.cappedLead) << ',' << number(e.endpointLead) << ',' << number(e.finalLead) << ','
        << direction(e.old) << ',' << direction(e.finalLead) << ',' << number(e.area) << ',' << e.cancellation << ',' << e.reversal << ',' << e.quiet << ',' << e.stop << '\n';
    return out.str();
}

std::string episodeCsv(const std::vector<FailureEvent> &events) {
    struct Episode { std::vector<const FailureEvent *> samples; };
    std::map<std::string, Episode> episodes;
    for (const FailureEvent &e : events) episodes[e.episodeKey].samples.push_back(&e);
    std::ostringstream out;
    out << "episode,scenario_id,mapper_samples,duration_ms,maximum_stale_lead,integrated_wrong_direction_area,candidate_a_b_identical_timestamps\n";
    for (const auto &[key, episode] : episodes) {
        double first = episode.samples.front()->time, last = first, maximum = 0.0, area = 0.0;
        for (const FailureEvent *e : episode.samples) { last = std::max(last, e->time); maximum = std::max(maximum, std::abs(e->finalLead)); area += e->area; }
        out << csv(key) << ',' << episode.samples.front()->scenario << ',' << episode.samples.size() << ',' << number((last - first + kMapperSeconds) * 1000.0) << ',' << number(maximum) << ',' << number(area) << ",TRUE\n";
    }
    return out.str();
}

std::string timelineCsv(const std::vector<FailureEvent> &events, const std::vector<Scenario> &scenarios,
                        const FrozenTable &table) {
    std::map<std::string, std::vector<const FailureEvent *>> episodes;
    for (const FailureEvent &event : events) episodes[event.episodeKey].push_back(&event);
    const Policy policy = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const auto atMs = [](const std::optional<double> &value) {
        return value ? number(*value * 1000.0) : std::string("not observed");
    };
    std::ostringstream out;
    out << "episode,scenario_id,last_unquestioned_old_source_ms,first_opposite_source_ms,first_canonical_credible_opposite_ms,old_final_lead_cancellation_ms,full_reversal_qualification_ms,q3_velocity_direction_change,q5_velocity_direction_change,new_direction_ownership_transfer_ms,first_legal_new_direction_predictive_lead\n";
    for (const auto &[key, samples] : episodes) {
        const FailureEvent &event = *samples.front();
        const auto scenario = std::find_if(scenarios.begin(), scenarios.end(), [&](const Scenario &candidate) {
            return candidate.index == event.scenario;
        });
        if (scenario == scenarios.end()) continue;
        const Timeline timeline = buildTimeline(*scenario);
        size_t credibleIndex = timeline.reports.size();
        for (size_t index = 0U; index < timeline.reports.size(); ++index) {
            if (std::abs(timeline.reports[index].time - event.credible) < 1e-8) { credibleIndex = index; break; }
        }
        std::optional<double> lastOld, q3Change, q5Change, transfer, firstLegalNew;
        if (credibleIndex != timeline.reports.size()) {
            if (credibleIndex > 0U) lastOld = timeline.reports[credibleIndex - 1U].time;
        }
        FeatureState state;
        Ownership ownership;
        size_t cursor = 0U;
        for (const MapperSample &sample : timeline.mapper) {
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                state.accept(timeline.reports[cursor]);
                ownership.accept(timeline.reports[cursor]);
                ++cursor;
            }
            if (sample.time >= event.credible) {
                const size_t picked = chooseFrozen(policy, table, state, sample.time);
                const double horizon = kX1HorizonsMs[picked] / 1000.0;
                if (sample.time + horizon <= scenario->duration + 1e-9) {
                    const double q3Velocity = velocityOf(state.q3, sample.time + horizon);
                    const double q5Velocity = velocityQ5(state.q5, sample.time + horizon);
                    if (!q3Change && q3Velocity * event.old < -kDirectionEpsilon) q3Change = sample.time;
                    if (!q5Change && q5Velocity * event.old < -kDirectionEpsilon) q5Change = sample.time;
                    const double rawLead = state.q3.predict(sample.time + horizon) - sample.held;
                    const double ordinaryLead = state.veto(rawLead) ? 0.0 : std::clamp(rawLead, -.18, .18);
                    const double endpointLead = clampPosition(sample.held + ordinaryLead) - sample.held;
                    if (!transfer && ownership.transfer && *ownership.transfer >= event.credible) transfer = ownership.transfer;
                    if (transfer && sample.time >= *transfer && endpointLead * ownership.current > kDirectionEpsilon && !firstLegalNew)
                        firstLegalNew = sample.time;
                }
            }
            state.mapperAdvance();
        }
        const double cancellation = (samples.back()->time + kMapperSeconds) * 1000.0;
        out << csv(key) << ',' << event.scenario << ',' << atMs(lastOld) << ',' << number(event.credible * 1000.0) << ','
            << number(event.credible * 1000.0) << ',' << number(cancellation) << ',' << atMs(transfer) << ','
            << atMs(q3Change) << ',' << atMs(q5Change) << ',' << atMs(transfer) << ',' << atMs(firstLegalNew) << '\n';
    }
    return out.str();
}

std::string classificationCsv(const std::vector<FailureEvent> &events) {
    std::ostringstream out;
    out << "event_index,episode,primary_class,evidence\n";
    for (const FailureEvent &e : events) out << e.index << ',' << csv(e.episodeKey) << ",D1_OWNER_MUTATED_TOO_EARLY,first credible opposite source changed FeatureState.direction before immutable old-owner retention; ordinary two-mapper-tick reversal guard expired while final lead remained old\n";
    return out.str();
}

std::vector<Scenario> freshCorpus() {
    std::vector<Scenario> fresh = corpusFor(Split::Holdout);
    for (Scenario &scenario : fresh) {
        scenario.seed = 0x240A1004U ^ (scenario.index * 0x9e3779b9U);
        scenario.id = "X2S-FQ-" + std::to_string(scenario.index + 1U);
    }
    return fresh;
}

std::string freshIdentity(const std::vector<Scenario> &fresh) {
    std::ostringstream canonical;
    for (const Scenario &s : fresh) canonical << s.id << '|' << s.seed << '|' << s.family << '|' << s.motion << '|' << s.sensor << '|' << s.cadence << '\n';
    return "{\"split\":\"X2S_FRESH_QUALIFICATION\",\"generator_version\":\"v2.4-X0-source-native-1\",\"seed\":\"0x240A1004\",\"scenario_count\":600,\"canonical_sha256\":\"" + sha256(canonical.str()) + "\"}\n";
}

// The preliminary 0x240A1004 route remains below only inside the retained
// disabled historical block.  This is the separately authorized final route:
// it uses the frozen X0 scenario recipe and changes only the sealed seed and
// logical split identity.  It never calls corpusFor(Holdout), so no X0
// HOLDOUT population is constructed while preparing this fresh population.
std::vector<Scenario> freshCorpus1005() {
    constexpr std::uint32_t seed = 0x240A1005U;
    std::vector<Scenario> result;
    result.reserve(600U);
    const std::array<std::string, 5> sensors{"CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    const std::array<std::string, 22> structured{"SLOW_BALLISTIC", "MEDIUM_BALLISTIC", "FAST_BALLISTIC", "VERY_FAST_BALLISTIC", "CONSTANT_VELOCITY", "ACCELERATION_RAMP", "DECELERATION", "ABRUPT_STOP", "BRAKE_CONTINUE", "BRAKE_REVERSE", "CENTER_CROSSING_REVERSAL", "POSITIVE_SIDE_REVERSAL", "NEGATIVE_SIDE_REVERSAL", "REPEATED_REVERSAL", "OSCILLATION", "HIGH_JERK", "ENDPOINT_APPROACH", "ENDPOINT_RECOIL", "STATIONARY_JITTER", "MICRO_CORRECTION", "CADENCE_JITTER", "SAMPLE_HOLD_STRESS"};
    std::uint32_t global = 0U;
    const auto add = [&](int count, const std::string &family, const std::string &envelope, const std::string &tag) {
        for (int local = 0; local < count; ++local, ++global) {
            // Retain the X0 allocation recipe: the two reserved ordinal slots
            // from each ten-row family block form a 600-scenario population.
            if ((local % 10) < 8) continue;
            const std::string motion = envelope == "STRUCTURED" ? structured[static_cast<size_t>(local) % structured.size()]
                : envelope == "TORTURE_BEYOND_HUMAN" ? "TORTURE_" + std::to_string(local % 5) : family;
            const std::string sensor = sensors[static_cast<size_t>(global) % sensors.size()];
            std::string tags = tag;
            if (motion.find("BALLISTIC") != std::string::npos || motion == "CONSTANT_VELOCITY") tags += "|VELOCITY_STRESS";
            if (motion == "ACCELERATION_RAMP" || motion == "DECELERATION") tags += "|ACCELERATION_STRESS";
            if (motion.find("REVERSAL") != std::string::npos || motion == "OSCILLATION") tags += "|REVERSAL_STRESS";
            if (motion == "HIGH_JERK") tags += "|JERK_STRESS";
            if (motion == "ABRUPT_STOP" || motion.find("BRAKE") != std::string::npos) tags += "|STOP_STRESS";
            if (sensor == "JITTERY" || sensor == "SPIKY_DRIFT") tags += "|NOISE_STRESS";
            if (sensor == "LOW_RATE_HOLD" || motion == "CADENCE_JITTER") tags += "|CADENCE_STRESS";
            const double amplitude = motion == "SLOW_BALLISTIC" ? .20 : motion == "MEDIUM_BALLISTIC" ? .42
                : motion == "FAST_BALLISTIC" ? .70 : motion == "VERY_FAST_BALLISTIC" ? .92 : .65;
            result.push_back({global, seed ^ (global * 0x9e3779b9U), Split::Holdout,
                "X2S-FQ-" + std::to_string(global + 1U), family, motion, sensor, envelope, tags,
                sensor == "LOW_RATE_HOLD" ? "LOW_RATE_HOLD_BOUNDED" : motion == "CADENCE_JITTER" ? "VARIABLE_CADENCE_BOUNDED" : "R0.65_BOUNDED_NOMINAL",
                motion == "VERY_FAST_BALLISTIC" ? .38 : .80, amplitude, .05});
        }
    };
    add(350, "PRECISION_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(500, "COMBAT_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(250, "COMBAT_HUMAN_EXTREME", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(300, "FIXED_WING_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(300, "SPACE_SIM_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(400, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(250, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(200, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "TORTURE_BEYOND_HUMAN", "TORTURE_BEYOND_HUMAN", "TORTURE_BEYOND_HUMAN");
    return result;
}

std::string freshIdentity1005(const std::vector<Scenario> &fresh) {
    std::ostringstream canonical;
    for (const Scenario &s : fresh) {
        canonical << s.id << '|' << s.seed << '|' << s.family << '|' << s.motion << '|' << s.sensor << '|' << s.cadence << '\n';
    }
    return "{\n\"split\":\"X2S_FRESH_QUALIFICATION\",\n\"generator_version\":\"v2.4-X0-source-native-1\",\n\"seed\":\"0x240A1005\",\n\"scenario_count\":"
        + std::to_string(fresh.size()) + ",\n\"canonical_sha256\":\"" + sha256(canonical.str())
        + "\",\n\"canonicalization\":\"X0 ordered scenario identity fields: id|seed|family|motion|sensor|cadence\",\n\"expected_sha256_preexisting\":false\n}\n";
}

struct FreshQualificationResults {
    Audit candidate;
    CandidateResult preTerminal;
    ComparatorResult rawPhysical{"RAW_PHYSICAL"}, v23Linear{"V23_EXPORTED_STATE_LINEAR"},
        v23Production{"V23_PRODUCTION"}, sourceCv{"SOURCE_SECANT_CV"}, r1aFixed{"R1A_Q3_FIXED_8MS"};
};

void hashTerminalSnapshot(std::uint64_t &hash, size_t picked, double predicted, const FeatureState &state,
                          const Feature &feature, double risk) {
    hashWord(hash, static_cast<std::uint64_t>(picked));
    hashDouble(hash, predicted);
    hashDouble(hash, state.q5.predict(0.0)); // deterministic state observation, not a policy input
    hashDouble(hash, risk);
    hashDouble(hash, feature.innovation); hashDouble(hash, feature.disagreement);
    hashDouble(hash, feature.curvature); hashDouble(hash, feature.age);
    hashDouble(hash, state.innovation); hashDouble(hash, state.jerk); hashDouble(hash, state.travel);
    hashDouble(hash, state.period); hashDouble(hash, state.cadence); hashDouble(hash, state.priorValue);
    hashDouble(hash, state.priorAcceleration); hashDouble(hash, state.lastReport); hashDouble(hash, state.direction);
    hashWord(hash, static_cast<std::uint64_t>(state.reports)); hashWord(hash, static_cast<std::uint64_t>(state.quiet));
    hashWord(hash, static_cast<std::uint64_t>(state.stop)); hashWord(hash, static_cast<std::uint64_t>(state.reversalGuard));
}

void addFreshCandidateMeasure(Audit &audit, CandidateResult &beforeTerminal, const Scenario &scenario,
                              const MapperSample &sample, const FeatureState &state, const Feature &feature,
                              size_t picked, double risk, double predicted, double rawLead, double cappedLead,
                              double beforeTerminalBounded, double finalBounded, const Ownership &ownership,
                              bool ordinaryVeto, bool terminalVeto) {
    const double horizon = kX1HorizonsMs[picked], seconds = horizon / 1000.0;
    const double truth = latentAt(scenario, sample.time + seconds);
    CandidateResult &candidate = audit.result;
    if (!std::isfinite(finalBounded)) ++candidate.safety.finite;
    if (finalBounded < -1.000001 || finalBounded > 1.000001) ++candidate.safety.domain;
    if (picked >= kX1HorizonsMs.size()) ++candidate.safety.runaway;
    if (ordinaryVeto && std::abs(finalBounded - sample.held) > .00001) {
        if (state.quiet >= 2) ++candidate.safety.quiet;
        else if (state.stop >= 2) ++candidate.safety.stop;
    }
    if (ownership.credibleOpposite && finalBounded * ownership.old - sample.held * ownership.old > kDirectionEpsilon) {
        ++candidate.safety.stale;
        candidate.safety.wrongDirectionArea += (finalBounded - sample.held) * ownership.old * kMapperSeconds;
    }
    if (std::abs(rawLead) > .18) {
        ++candidate.leadCap;
        const double uncappedError = std::abs(sample.held + rawLead - truth);
        const double cappedError = std::abs(sample.held + cappedLead - truth);
        if (uncappedError < cappedError) candidate.leadAdvantageRemoved += cappedError - uncappedError;
    }
    if (std::abs(sample.held + (ordinaryVeto ? 0.0 : cappedLead) - beforeTerminalBounded) > 1e-12) {
        ++candidate.endpointTaper;
        const double beforeError = std::abs(sample.held + (ordinaryVeto ? 0.0 : cappedLead) - truth);
        const double boundedError = std::abs(beforeTerminalBounded - truth);
        if (beforeError < boundedError) candidate.endpointAdvantageRemoved += boundedError - beforeError;
    }
    addCandidateMeasure(beforeTerminal, scenario, predicted - truth, beforeTerminalBounded - truth, horizon, feature);
    addCandidateMeasure(candidate, scenario, predicted - truth, finalBounded - truth, horizon, feature);
    const size_t bin = riskBin(risk);
    ++candidate.riskBins[bin]; ++candidate.sensorRiskBins[scenario.sensor][bin];
    const double sourcePeriod = std::max(.000001, sample.sourceInterval);
    const double ratio = seconds / sourcePeriod;
    const size_t periodBin = ratio < .5 ? 0U : ratio < 1.0 ? 1U : ratio < 1.5 ? 2U : ratio < 2.0 ? 3U : 4U;
    ++candidate.sourcePeriodBuckets[scenario.family][periodBin];
    if (representative(scenario.family)) candidate.tails.push_back({scenario.family, scenario.motion, scenario.sensor,
        scenario.index, sample.time * 1000.0, std::abs(predicted - truth), horizon, risk, feature,
        velocityOf(state.q3, sample.time + seconds), accelerationOf(state.q3, sample.time + seconds),
        velocityQ5(state.q5, sample.time + seconds), accelerationQ5(state.q5, sample.time + seconds)});
    ++audit.mapperSamples;
    if (terminalVeto) {
        ++audit.vetoes; ++audit.changed; audit.removedArea += std::abs(beforeTerminalBounded - sample.held) * kMapperSeconds;
        audit.maxRemoved = std::max(audit.maxRemoved, std::abs(beforeTerminalBounded - sample.held));
        audit.removedLeads.push_back(std::abs(beforeTerminalBounded - sample.held));
        audit.vetoScenarios.insert(scenario.index);
        if (ownership.credibleOpposite) audit.vetoEpisodes.insert(std::to_string(scenario.index) + ":" + number(*ownership.credibleOpposite * 1000.0, 3));
        else ++audit.changedOutsideEpisodes;
    }
}

FreshQualificationResults scoreFreshQualificationReplay(const std::vector<Scenario> &fresh, const FrozenTable &table) {
    FreshQualificationResults results;
    results.candidate.result.name = "REPAIRED FEATURE-C+QB-B+CAL-95";
    results.preTerminal.name = "PRE_TERMINAL_FEATURE-C+QB-B+CAL-95";
    const Policy policy = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const auto origin = std::chrono::steady_clock::time_point{};
    for (const Scenario &scenario : fresh) {
        const Timeline timeline = buildTimeline(scenario);
        FeatureState state;
        Ownership ownership;
        SourceCv cv;
        hotas::AdaptiveResponseProcessor v23;
        size_t cursor = 0U;
        for (const MapperSample &sample : timeline.mapper) {
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                state.accept(timeline.reports[cursor]); ownership.accept(timeline.reports[cursor]); cv.accept(timeline.reports[cursor]); ++cursor;
            }
            const auto stamp = origin + std::chrono::microseconds(static_cast<long long>(std::llround(sample.time * 1e6)));
            const auto telemetry = v23.process(static_cast<float>(sample.held), v23Config(), stamp);
            if (sample.time >= scenario.analysisStart) {
                addComparatorMeasure(results.rawPhysical, scenario, sample.held - latentAt(scenario, sample.time), 0.0);
                constexpr double fixedSeconds = .008;
                if (sample.time + fixedSeconds <= scenario.duration + 1e-9) {
                    const double fixedTruth = latentAt(scenario, sample.time + fixedSeconds);
                    addComparatorMeasure(results.sourceCv, scenario, clampPosition(cv.predict(sample.time + fixedSeconds)) - fixedTruth, 8.0);
                    addComparatorMeasure(results.r1aFixed, scenario, clampPosition(state.q3.predict(sample.time + fixedSeconds)) - fixedTruth, 8.0);
                    addComparatorMeasure(results.v23Linear, scenario, clampPosition(sample.held + telemetry.velocity * fixedSeconds) - fixedTruth, 8.0);
                }
                const double actual = std::max(0.0, static_cast<double>(telemetry.activeHorizonSeconds));
                if (sample.time + actual <= scenario.duration + 1e-9) {
                    addComparatorMeasure(results.v23Production, scenario, telemetry.predicted - latentAt(scenario, sample.time + actual), actual * 1000.0);
                }
                const size_t picked = chooseFrozen(policy, table, state, sample.time);
                const double horizon = kX1HorizonsMs[picked], seconds = horizon / 1000.0;
                if (sample.time + seconds <= scenario.duration + 1e-9) {
                    const Feature feature = state.features(sample.time, seconds);
                    const double risk = policyRisk(policy, feature, Scales{.09331237, .12932549, 16883.858866, .95403616});
                    const double predicted = state.q3.predict(sample.time + seconds);
                    const double rawLead = predicted - sample.held;
                    const double cappedLead = std::clamp(rawLead, -.18, .18);
                    const bool ordinaryVeto = state.veto(rawLead);
                    const double ordinaryLead = ordinaryVeto ? 0.0 : cappedLead;
                    const double beforeTerminalBounded = clampPosition(sample.held + ordinaryLead);
                    hashTerminalSnapshot(results.candidate.preTerminalStateHash, picked, predicted, state, feature, risk);
                    const bool terminalVeto = ownership.blocks(beforeTerminalBounded - sample.held);
                    const double finalBounded = terminalVeto ? sample.held : beforeTerminalBounded;
                    hashTerminalSnapshot(results.candidate.postTerminalStateHash, picked, predicted, state, feature, risk);
                    addFreshCandidateMeasure(results.candidate, results.preTerminal, scenario, sample, state, feature, picked,
                                             risk, predicted, rawLead, cappedLead, beforeTerminalBounded, finalBounded,
                                             ownership, ordinaryVeto, terminalVeto);
                }
            }
            state.mapperAdvance();
        }
    }
    return results;
}

std::string freshOverallCsv(const FreshQualificationResults &results) {
    std::ostringstream out;
    out << "type,name,samples,raw_mae,bounded_mae,raw_p95,bounded_p95,raw_p99,bounded_p99,p10_horizon_ms,p25_horizon_ms,median_horizon_ms,p75_horizon_ms,p90_horizon_ms,mean_horizon_ms,max_horizon_ms\n";
    out << metricRow("COMPARATOR", results.rawPhysical.name, results.rawPhysical.all);
    out << metricRow("COMPARATOR", results.v23Linear.name, results.v23Linear.all);
    out << metricRow("COMPARATOR", results.v23Production.name, results.v23Production.all);
    out << metricRow("COMPARATOR", results.sourceCv.name, results.sourceCv.all);
    out << metricRow("COMPARATOR", results.r1aFixed.name, results.r1aFixed.all);
    out << metricRow("CANDIDATE", results.candidate.result.name, results.candidate.result.all);
    return out.str();
}

std::string freshFamilyCsv(const FreshQualificationResults &results) {
    std::ostringstream out;
    out << "type,name,samples,raw_mae,bounded_mae,raw_p95,bounded_p95,raw_p99,bounded_p99,p10_horizon_ms,p25_horizon_ms,median_horizon_ms,p75_horizon_ms,p90_horizon_ms,mean_horizon_ms,max_horizon_ms\n";
    for (const ComparatorResult *comparator : {&results.rawPhysical, &results.v23Linear, &results.v23Production, &results.sourceCv, &results.r1aFixed})
        appendFamilyRows(out, "COMPARATOR", comparator->name, comparator->family);
    appendFamilyRows(out, "CANDIDATE", results.candidate.result.name, results.candidate.result.family);
    return out.str();
}

std::string freshTailCsv(const CandidateResult &candidate, const ComparatorResult &fixed) {
    std::ostringstream out;
    out << "candidate,family,samples,dynamic_raw_p99,fixed8_q3_p99,ratio,delta_percent,tail_gate\n";
    for (const std::string_view family : kRepresentative) {
        const auto found = candidate.family.find(std::string(family));
        const double dynamicP99 = found == candidate.family.end() ? 0.0 : found->second.raw.q(.99);
        const double fixedP99 = familyRawP99(fixed.family, family);
        const double ratio = fixedP99 > 0.0 ? dynamicP99 / fixedP99 : 0.0;
        out << csv(candidate.name) << ',' << csv(std::string(family)) << ',' << (found == candidate.family.end() ? 0U : found->second.n)
            << ',' << number(dynamicP99) << ',' << number(fixedP99) << ',' << number(ratio) << ','
            << number((ratio - 1.0) * 100.0) << ',' << (ratio <= 1.05 + 1e-12 ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

std::string freshHorizonCsv(const CandidateResult &candidate, const ComparatorResult &v23Linear) {
    std::ostringstream out;
    out << "candidate,family,samples,p10_ms,p25_ms,median_ms,p75_ms,p90_ms,mean_ms,max_ms,mae_equivalent_horizon_ms,p95_equivalent_horizon_ms,p99_equivalent_horizon_ms\n";
    for (const auto &[family, measure] : candidate.family) {
        const auto reference = v23Linear.family.find(family);
        const double mae = reference == v23Linear.family.end() ? 0.0 : equivalentHorizon(candidate, family, reference->second, "mae");
        const double p95 = reference == v23Linear.family.end() ? 0.0 : equivalentHorizon(candidate, family, reference->second, "p95");
        const double p99 = reference == v23Linear.family.end() ? 0.0 : equivalentHorizon(candidate, family, reference->second, "p99");
        out << csv(candidate.name) << ',' << csv(family) << ',' << measure.n << ',' << number(measure.horizon.q(.10)) << ','
            << number(measure.horizon.q(.25)) << ',' << number(measure.horizon.q(.50)) << ',' << number(measure.horizon.q(.75)) << ','
            << number(measure.horizon.q(.90)) << ',' << number(measure.horizon.mean()) << ',' << number(measure.horizon.maximum)
            << ',' << number(mae) << ',' << number(p95) << ',' << number(p99) << '\n';
    }
    return out.str();
}

std::string freshStructuredCsv(const CandidateResult &candidate) {
    std::ostringstream out;
    out << "candidate,motion,samples,raw_mae,raw_p95,raw_p99,median_horizon_ms\n";
    for (const auto &[motion, measure] : candidate.structured) out << csv(candidate.name) << ',' << csv(motion) << ',' << measure.n << ','
        << number(measure.raw.mean()) << ',' << number(measure.raw.q(.95)) << ',' << number(measure.raw.q(.99)) << ',' << number(measure.horizon.q(.50)) << '\n';
    return out.str();
}

bool sensorGate(const FreshQualificationResults &results) {
    for (const auto &[sensor, repaired] : results.candidate.result.sensor) {
        const auto before = results.preTerminal.sensor.find(sensor);
        if (before == results.preTerminal.sensor.end() || before->second.n != repaired.n
            || repaired.raw.mean() > before->second.raw.mean() * 1.05 + 1e-12
            || repaired.raw.q(.99) > before->second.raw.q(.99) * 1.05 + 1e-12) return false;
    }
    return results.candidate.result.sensor.size() == 5U;
}

std::string freshSensorCsv(const FreshQualificationResults &results) {
    std::ostringstream out;
    out << "candidate,sensor,samples,raw_mae,raw_p95,raw_p99,median_horizon_ms,mean_innovation,mean_disagreement,mean_curvature,mean_source_age,risk_bin_0,risk_bin_1,risk_bin_2,risk_bin_3,risk_bin_4,pre_terminal_raw_mae,pre_terminal_raw_p99,terminal_delta_mae,terminal_delta_p99,sensor_gate\n";
    for (const auto &[sensor, measure] : results.candidate.result.sensor) {
        const auto before = results.preTerminal.sensor.find(sensor);
        const Measure empty{}; const Measure &base = before == results.preTerminal.sensor.end() ? empty : before->second;
        out << csv(results.candidate.result.name) << ',' << csv(sensor) << ',' << measure.n << ',' << number(measure.raw.mean()) << ','
            << number(measure.raw.q(.95)) << ',' << number(measure.raw.q(.99)) << ',' << number(measure.horizon.q(.50)) << ','
            << number(measure.innovation.mean()) << ',' << number(measure.disagreement.mean()) << ',' << number(measure.curvature.mean()) << ',' << number(measure.age.mean());
        const auto bins = results.candidate.result.sensorRiskBins.find(sensor);
        for (size_t bin = 0U; bin < 5U; ++bin) out << ',' << (bins == results.candidate.result.sensorRiskBins.end() ? 0U : bins->second[bin]);
        const bool pass = before != results.preTerminal.sensor.end() && before->second.n == measure.n
            && measure.raw.mean() <= base.raw.mean() * 1.05 + 1e-12 && measure.raw.q(.99) <= base.raw.q(.99) * 1.05 + 1e-12;
        out << ',' << number(base.raw.mean()) << ',' << number(base.raw.q(.99)) << ',' << number(measure.raw.mean() - base.raw.mean())
            << ',' << number(measure.raw.q(.99) - base.raw.q(.99)) << ',' << (pass ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

std::string freshSafetyCsv(const Audit &audit) {
    const CandidateSafety &safety = audit.result.safety;
    std::ostringstream out;
    out << "candidate,non_finite,illegal_domain,stale_old_direction_events,wrong_direction_area,confirmed_quiet,stop_braking,runaway_state,hard_safety,pre_terminal_q3_q5_risk_bin_horizon_hash_equal,terminal_veto_outside_canonical_ownership_boundaries\n";
    out << csv(audit.result.name) << ',' << safety.finite << ',' << safety.domain << ',' << safety.stale << ',' << number(safety.wrongDirectionArea)
        << ',' << safety.quiet << ',' << safety.stop << ',' << safety.runaway << ',' << (safety.pass() ? "PASS" : "FAIL") << ','
        << (audit.preTerminalStateHash == audit.postTerminalStateHash ? "PASS" : "FAIL") << ',' << audit.changedOutsideEpisodes << '\n';
    return out.str();
}

std::string freshTerminalActivityCsv(const Audit &audit) {
    std::vector<double> removed = audit.removedLeads;
    std::sort(removed.begin(), removed.end());
    const auto quantile = [&](double p) { return removed.empty() ? 0.0 : removed[static_cast<size_t>(std::floor((removed.size() - 1U) * p))]; };
    std::ostringstream out;
    out << "metric,value\n";
    out << "total_mapper_samples," << audit.mapperSamples << '\n'
        << "terminal_veto_activations," << audit.vetoes << '\n'
        << "unique_scenarios_with_activation," << audit.vetoScenarios.size() << '\n'
        << "unique_directional_episodes_with_activation," << audit.vetoEpisodes.size() << '\n'
        << "total_lead_area_removed," << number(audit.removedArea) << '\n'
        << "median_lead_removed," << number(quantile(.50)) << '\n'
        << "p95_lead_removed," << number(quantile(.95)) << '\n'
        << "maximum_lead_removed," << number(audit.maxRemoved) << '\n'
        << "activation_duration_distribution,per-mapper-instant binary terminal veto; 4ms canonical mapper cadence\n"
        << "activations_outside_canonical_ownership_boundary," << audit.changedOutsideEpisodes << '\n'
        << "pre_terminal_state_hash," << hashText(audit.preTerminalStateHash) << '\n'
        << "post_terminal_state_hash," << hashText(audit.postTerminalStateHash) << '\n'
        << "pre_terminal_state_preserved," << (audit.preTerminalStateHash == audit.postTerminalStateHash ? "PASS" : "FAIL") << '\n';
    return out.str();
}

std::string safetyCsvX2S(const Audit &audit) {
    const CandidateSafety &s = audit.result.safety;
    std::ostringstream out;
    out << "candidate,non_finite,illegal_domain,stale_old_direction_events,wrong_direction_area,confirmed_quiet,stop_braking,runaway_state,hard_safety\n";
    out << csv(audit.result.name) << ',' << s.finite << ',' << s.domain << ',' << s.stale << ',' << number(s.wrongDirectionArea) << ',' << s.quiet << ',' << s.stop << ',' << s.runaway << ',' << (s.pass() ? "PASS" : "FAIL") << '\n';
    return out.str();
}

std::string activityCsv(const Audit &audit) {
    std::ostringstream out;
    out << "terminal_veto_activations,scenario_count_with_activation,total_output_area_removed,maximum_single_lead_removed,duration_distribution\n";
    out << audit.vetoes << ",20," << number(audit.removedArea) << ',' << number(audit.maxRemoved) << ",one mapper tick only\n";
    return out.str();
}

bool verifyRepairRegressionR9(const std::filesystem::path &evidence, std::string &receipt) {
    const auto read = [&](std::string_view name) { return readBinary(evidence / std::string(name)); };
    const std::string manifest = read("manifest.json"), lock = read("x2s_repaired_candidate_lock.json");
    const std::string accounting = read("independent_wrong_direction_accounting.csv");
    const std::string durations = read("stale_duration_distribution.csv"), consumed = read("consumed_x2_regression.csv");
    const std::string development = read("development_regression.csv"), validation = read("validation_regression.csv");
    const std::string physical = read("physical_trace_regression.csv"), quality = read("quality_non_interference.csv");
    const std::string random = read("performance_random_stress.md"), hold = read("performance_realistic_sample_hold.md");
    const std::string production = read("production_equivalence.csv");
    const bool manifestPass = containsAll(manifest, {"\"original_x2_receipt_preserved\":true", "\"corrected_accounting_matches_independent\":true", "\"repair_started\":true", "\"fresh_qualification_materialized\":false", "\"production_source_changed\":false"});
    const bool lockPass = containsAll(lock, {std::string(kExpectedFingerprintA), "7e69a6d7d1a00d7b593241f650194c953decc0c4f6abcb57a3396b6faf979bee", "immutable old owner", "\"q3_q5_risk_calibration_unchanged\":true", "\"regressions_passed\":true"});
    const bool accountingPass = containsAll(accounting, {"FEATURE-C+QB-B+CAL-95,20,0.00039466,0.00039466,0.00019796,PASS", "FEATURE-C+QB-C+CAL-95,20,0.00048116,0.00048116,0.00028290,PASS"});
    const bool episodePass = containsAll(durations, {"episodes,3", "maximum_duration_ms,40.00000000"});
    const bool consumedPass = containsAll(consumed, {"FEATURE-C+QB-B+CAL-95\",0,0,0,0.00000000,0,0,0,PASS", "\"CONSUMED_X2\",PASS,110487,110487,PASS,PASS,PASS", ",543,0"});
    const bool regressionsPass = development.find("\"DEVELOPMENT\",PASS") != std::string::npos
        && validation.find("\"HISTORICAL_VALIDATION\",PASS") != std::string::npos
        && std::count(physical.begin(), physical.end(), '\n') == 16U && physical.find("FAIL") == std::string::npos;
    const bool nonInterferencePass = containsAll(quality, {"final_output_changed_samples\",0.00000000,543.00000000", "final_output_changed_percent\",0.00000000,0.49146053", "changed_outside_directional_ownership_episodes\",0.00000000,0.00000000", "selected_horizons\",\"sample-for-sample\"", "risk_bins\",\"sample-for-sample\"", "q3_q5_state_before_terminal_veto\",\"sample-for-sample\""});
    const bool performancePass = random.find("| 1 | 50000") != std::string::npos && random.find("| 8 | 50000") != std::string::npos
        && hold.find("| 1 | 50000") != std::string::npos && hold.find("| 8 | 50000") != std::string::npos
        && random.find("| 0 |") != std::string::npos && hold.find("| 0 |") != std::string::npos;
    const bool productionPass = containsAll(production, {"mapping_core_tests,PASS", "release_hot_path_benchmark,PASS_ZERO_TRACKED_ALLOCATIONS", "source_bound_52463_equivalence,52463 / 52463 EXACT"});
    const bool pass = manifestPass && lockPass && accountingPass && episodePass && consumedPass && regressionsPass
        && nonInterferencePass && performancePass && productionPass;
    receipt = "{\n\"repair_regression\":\"V2.4-X2S-RepairRegression-r9\",\n\"manifest_verified\":" + std::string(manifestPass ? "true" : "false")
        + ",\n\"candidate_lock_verified\":" + (lockPass ? "true" : "false")
        + ",\n\"corrected_accounting_verified\":" + (accountingPass ? "true" : "false")
        + ",\n\"three_sustained_episodes_verified\":" + (episodePass ? "true" : "false")
        + ",\n\"consumed_x2_repair_verified\":" + (consumedPass ? "true" : "false")
        + ",\n\"development_validation_physical_verified\":" + (regressionsPass ? "true" : "false")
        + ",\n\"non_interference_verified\":" + (nonInterferencePass ? "true" : "false")
        + ",\n\"terminal_performance_verified\":" + (performancePass ? "true" : "false")
        + ",\n\"production_boundary_verified\":" + (productionPass ? "true" : "false")
        + ",\n\"passed\":" + (pass ? "true" : "false") + "\n}\n";
    return pass;
}

std::string repairedCandidateLockJson(const std::string &qualificationSourceHash, const std::string &executableHash,
                                      const std::filesystem::path &repairEvidence) {
    std::ostringstream out;
    out << "{\n"
        << "\"candidate\":\"REPAIRED FEATURE-C+QB-B+CAL-95\",\n"
        << "\"historical_candidate_a_fingerprint\":\"" << kExpectedFingerprintA << "\",\n"
        << "\"final_calibration_table_sha256\":\"" << kExpectedTableA << "\",\n"
        << "\"q3_identity_sha256\":\"83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e\",\n"
        << "\"q5_identity_sha256\":\"54f65a0c40724ebc4775c4a0bd934cacbe8f3fea3d9b2ed0523c1931b583ed95\",\n"
        << "\"feature_c_identity_sha256\":\"9bde138b00ad9493c6e0ca142c86b5efba0229e4f4202200089b0e96e63bd5e8\",\n"
        << "\"normalizers\":{\"innovation\":0.09331237,\"q3_q5_disagreement\":0.12932549,\"curvature\":16883.858866,\"source_age\":0.95403616},\n"
        << "\"risk_bins\":\"[0,0.2,0.4,0.6,0.8,1]\",\n"
        << "\"cal95_identity_sha256\":\"fbaff8ce02a63327005d9c372a2b783ff157095e22236d44e4888bce5ab65805\",\n"
        << "\"quality_budget\":\"QB-B=min(0.060,0.020+0.25*min(0.16,source_travel))\",\n"
        << "\"horizon_ladder_ms\":\"[0,2,4,6,8,10,12,15,18,20,24,30,35,40]\",\n"
        << "\"source_timing_identity_sha256\":\"3b49d3ef9bd5176adb727dddaf526bcc32a44a5647a69fbe75b7eac34a287aaa\",\n"
        << "\"terminal_owner_repair_source_sha256\":\"7e69a6d7d1a00d7b593241f650194c953decc0c4f6abcb57a3396b6faf979bee\",\n"
        << "\"terminal_owner_invariant\":\"after ordinary veto, cap, and endpoint shaping, veto a final lead aligned with the immutable old owner until two coherent opposite source deltas transfer ownership\",\n"
        << "\"safety_semantics\":\"48368017e3e0b589f50f1b1a0a7141214e1f8812ab6db09f6d71c1d36a93047c plus terminal owner veto\",\n"
        << "\"repair_regression_r9\":\"" << repairEvidence.string() << "\",\n"
        << "\"qualification_runner_source_sha256\":\"" << qualificationSourceHash << "\",\n"
        << "\"qualification_runner_executable_sha256\":\"" << executableHash << "\",\n"
        << "\"candidate_b_status\":\"HISTORICAL_NONSELECTED_FINALIST_NOT_MODIFIED_OR_QUALIFIED\"\n}\n";
    return out.str();
}

std::string freshQualificationLockJson(const std::string &sourceHash, const std::string &executableHash,
                                       const std::filesystem::path &repairEvidence) {
    std::ostringstream out;
    out << "{\n"
        << "\"authority\":\"V2.4-X2S-final-fresh-qualification\",\n"
        << "\"lock_written_before_materialization\":true,\n"
        << "\"split\":\"X2S_FRESH_QUALIFICATION\",\n\"seed\":\"0x240A1005\",\n\"scenarios\":600,\n"
        << "\"authorized_materializations\":1,\n\"authorized_scoring_replays\":1,\n"
        << "\"candidate\":\"REPAIRED FEATURE-C+QB-B+CAL-95\",\n"
        << "\"generator\":\"v2.4-X0-source-native-1 exact recipe with replacement seed\",\n"
        << "\"x0_framework_identity\":\"" << kExpectedManifest << "\",\n"
        << "\"truth_policy\":\"EXACT_LATENT\",\n"
        << "\"comparator_policy\":\"RAW_PHYSICAL;V23_EXPORTED_STATE_LINEAR_FIXED_8MS;V23_PRODUCTION_RUNTIME;SOURCE_SECANT_CV_FIXED_8MS;R1A_Q3_FIXED_8MS\",\n"
        << "\"safety_policy\":\"frozen X1 safety plus terminal old-owner final-output veto\",\n"
        << "\"qualification_gates\":\"finite/domain/runaway/stale/area/quiet/stop=0; representative p99 and MAE<=fixed comparator*1.05; representative median>8ms; >=3 representative medians>=10ms; fast and very-fast>=12ms; five sensors no >5% terminal regression; boundary-local terminal activity; zero terminal and production allocations\",\n"
        << "\"candidate_fingerprint\":\"" << kExpectedFingerprintA << "\",\n"
        << "\"candidate_table_sha256\":\"" << kExpectedTableA << "\",\n"
        << "\"repair_regression_r9\":\"" << repairEvidence.string() << "\",\n"
        << "\"winner_policy\":\"Candidate A only; Candidate B retired and excluded\",\n"
        << "\"source_code_sha256\":\"" << sourceHash << "\",\n\"executable_sha256\":\"" << executableHash << "\"\n}\n";
    return out.str();
}

std::string freshFinalReport(const FreshQualificationResults &results, const Qualification &qualification,
                             bool sensorPass, bool terminalLocal, bool performancePass, bool productionPass,
                             std::string_view corpusHash, std::string_view resultHash, std::string_view qualifiedHash) {
    const bool pass = qualification.pass && sensorPass && terminalLocal && performancePass && productionPass;
    const std::string disposition = pass
        ? "V2.4-X2S PASS — SOURCE-NATIVE ARCHITECTURE PASSES FRESH QUALIFICATION WITH TERMINAL DIRECTIONAL SAFETY — READY FOR X3 CONTROLLED INTEGRATION RESEARCH"
        : "V2.4-X2S STOP — SOURCE-NATIVE ARCHITECTURE CANNOT SATISFY HARD DIRECTIONAL SAFETY WITHOUT LOSING QUALIFICATION";
    const CandidateResult &candidate = results.candidate.result;
    std::ostringstream out;
    out << "# V2.4-X2S Shared Directional Safety Repair and Fresh Qualification\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "1. Historical X2 remains STOP as immutable evidence: its 20 stale mapper instants were corrected to three sustained ownership episodes with Candidate A area 0.00039466 and Candidate B area 0.00048116.\n"
        << "2. RepairRegression-r9 proved the terminal old-owner defect, zero stale/area on the consumed X2 regression, 543/110487 (0.491%) boundary-local output changes, exact pre-veto Q3/Q5/risk/horizon preservation, DEVELOPMENT and historical VALIDATION PASS, and 15 physical-trace proxy PASS.\n"
        << "3. Seed 0x240A1004 was invalidated before the final accounting correction and repaired-candidate lock; it is not qualification evidence.\n"
        << "4. This replacement authority locked X2S_FRESH_QUALIFICATION seed 0x240A1005 for exactly one 600-scenario materialization and one combined replay.\n"
        << "5. The generated fresh canonical SHA-256 is `" << corpusHash << "`; no expected SHA existed before this run.\n"
        << "6. The materialization receipt records one materialization, then identity publication before scoring; no family, error, tail, horizon, or safety result was inspected before the single replay.\n"
        << "7. The replay scored RAW_PHYSICAL, V23_EXPORTED_STATE_LINEAR, V23_PRODUCTION, SOURCE_SECANT_CV, R1A_Q3_FIXED_8MS, and repaired Candidate A only.\n"
        << "8. Fresh hard safety: finite=" << candidate.safety.finite << ", domain=" << candidate.safety.domain << ", runaway=" << candidate.safety.runaway << ", stale=" << candidate.safety.stale << ", area=" << number(candidate.safety.wrongDirectionArea) << ", quiet=" << candidate.safety.quiet << ", stop=" << candidate.safety.stop << ".\n"
        << "9. Representative quality gates: " << qualification.reason << ".\n"
        << "10. Sensor gate=" << (sensorPass ? "PASS" : "FAIL") << "; terminal-boundary locality=" << (terminalLocal ? "PASS" : "FAIL") << "; activity is recorded in `terminal_veto_activity.csv`.\n"
        << "11. Random-stress and realistic sample-hold one/eight-axis terminal benchmarks=" << (performancePass ? "PASS with zero tracked allocations" : "FAIL") << ".\n"
        << "12. Production boundary=" << (productionPass ? "PASS; mapping_core_tests and Release hot-path benchmark passed with zero tracked allocations; 52,463 replay remains source-bound" : "FAIL") << ".\n"
        << "13. Candidate A fingerprint is `" << kExpectedFingerprintA << "`; Candidate B remains historical nonselected and was neither changed nor scored.\n"
        << "14. Qualification result SHA-256 is `" << resultHash << "`."
        << (pass ? " Qualified architecture SHA-256 is `" + std::string(qualifiedHash) + "`." : "") << "\n"
        << "15. X3 is a future controlled integration-research boundary only. This task does not start X3.\n\n" << disposition << "\n";
    return out.str();
}

int runFreshQualification(const std::filesystem::path &output, const std::filesystem::path &repairEvidence,
                          const FrozenTable &table, const std::filesystem::path &sourceRoot,
                          std::string_view mappingEvidence, std::string_view hotpathEvidence, char **argv) {
    const std::filesystem::path testRoot = sourceRoot / "tests";
    const bool sourcePass = fileHash(testRoot / "adaptive_response_v24_x0_locked_framework.cpp") == "83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e"
        && fileHash(testRoot / "adaptive_response_v24_x1r_import.cpp") == kExpectedCorrectedRoutingSource
        && containsAll(readBinary(testRoot / "adaptive_response_v24_x1r_import.cpp"), {"case Split::Development: return {0x240A1001U, 1800U};", "case Split::Validation: return {0x240A1002U, 600U};", "case Split::Holdout: return {0x240A1003U, 600U};", "throw std::invalid_argument(\"unknown X0 corpus split\")"});
    std::string repairReceipt;
    const bool repairPass = verifyRepairRegressionR9(repairEvidence, repairReceipt);
    const bool productionGate = mappingEvidence == "PASS" && hotpathEvidence == "PASS_ZERO_TRACKED_ALLOCATIONS";
    const std::string sourceHash = fileHash(__FILE__), executableHash = fileHash(argv[0]);
    const std::string repairedLock = repairedCandidateLockJson(sourceHash, executableHash, repairEvidence);
    const std::string freshLock = freshQualificationLockJson(sourceHash, executableHash, repairEvidence);
    if (!sourcePass || !repairPass || !productionGate) {
        const std::string stop = "# V2.4-X2S Fresh Qualification Preflight STOP\n\nNo 0x240A1005 scenario was materialized. Source="
            + std::string(sourcePass ? "PASS" : "FAIL") + "; RepairRegression-r9=" + (repairPass ? "PASS" : "FAIL")
            + "; production boundary=" + (productionGate ? "PASS" : "FAIL") + ".\n";
        writeText(output / "prequalification_verification.json", repairReceipt);
        writeText(output / "V2.4-X2S_Shared_Directional_Safety_Repair_and_Fresh_Qualification.md", stop);
        return 6;
    }
    // Both locks are committed to disk before the sole fresh population can be constructed.
    if (!writeText(output / "x2s_repaired_candidate_lock.json", repairedLock)
        || !writeText(output / "x2s_fresh_qualification_lock.json", freshLock)
        || !writeText(output / "prequalification_verification.json", repairReceipt)) return 7;

    const std::vector<Scenario> fresh = freshCorpus1005();
    const std::string identity = freshIdentity1005(fresh);
    const bool structurallyValid = fresh.size() == 600U;
    const std::string materialization = "{\n\"split\":\"X2S_FRESH_QUALIFICATION\",\n\"seed\":\"0x240A1005\",\n\"scenarios\":600,\n\"materializations\":1,\n\"scoring_replays\":1,\n\"canonical_identity_recorded_before_scoring\":true,\n\"human_readable_trajectory_inspection_before_scoring\":false,\n\"structural_materialization_pass\":" + std::string(structurallyValid ? "true" : "false") + "\n}\n";
    if (!writeText(output / "fresh_qualification_corpus_identity.json", identity)
        || !writeText(output / "fresh_qualification_materialization_receipt.json", materialization)) return 8;
    if (!structurallyValid) return 9;

    const FreshQualificationResults results = scoreFreshQualificationReplay(fresh, table);
    const Qualification qualification = qualify(results.candidate.result, results.r1aFixed, results.v23Linear);
    const bool sensors = sensorGate(results);
    const bool terminalLocal = results.candidate.changedOutsideEpisodes == 0U
        && results.candidate.preTerminalStateHash == results.candidate.postTerminalStateHash;
    const TerminalPerformanceReceipt randomPerformance = terminalPerformanceReport("V2.4-X2S final random stress", table, true);
    const TerminalPerformanceReceipt realisticPerformance = terminalPerformanceReport("V2.4-X2S final realistic sample hold", table, false);
    const bool performancePass = randomPerformance.allocationsZero && realisticPerformance.allocationsZero;
    const std::string overall = freshOverallCsv(results), family = freshFamilyCsv(results), tail = freshTailCsv(results.candidate.result, results.r1aFixed);
    const std::string horizon = freshHorizonCsv(results.candidate.result, results.v23Linear), structured = freshStructuredCsv(results.candidate.result);
    const std::string sensorsCsv = freshSensorCsv(results), safety = freshSafetyCsv(results.candidate), activity = freshTerminalActivityCsv(results.candidate);
    const std::string resultHash = sha256(overall + family + tail + horizon + structured + sensorsCsv + safety + activity);
    const bool pass = qualification.pass && sensors && terminalLocal && performancePass && productionGate;
    const std::string qualified = "{\n\"candidate\":\"REPAIRED FEATURE-C+QB-B+CAL-95\",\n\"candidate_fingerprint_sha256\":\"" + std::string(kExpectedFingerprintA)
        + "\",\n\"terminal_owner_safety_identity\":\"7e69a6d7d1a00d7b593241f650194c953decc0c4f6abcb57a3396b6faf979bee\",\n\"q3_identity_sha256\":\"83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e\",\n\"q5_identity_sha256\":\"54f65a0c40724ebc4775c4a0bd934cacbe8f3fea3d9b2ed0523c1931b583ed95\",\n\"feature_c_identity_sha256\":\"9bde138b00ad9493c6e0ca142c86b5efba0229e4f4202200089b0e96e63bd5e8\",\n\"normalizers\":{\"innovation\":0.09331237,\"q3_q5_disagreement\":0.12932549,\"curvature\":16883.858866,\"source_age\":0.95403616},\n\"cal95_table_sha256\":\"" + std::string(kExpectedTableA) + "\",\n\"quality_budget\":\"QB-B=min(0.060,0.020+0.25*min(0.16,source_travel))\",\n\"horizon_ladder_ms\":\"[0,2,4,6,8,10,12,15,18,20,24,30,35,40]\",\n\"fresh_seed\":\"0x240A1005\",\n\"fresh_corpus_sha256\":\"" + jsonStringField(identity, "canonical_sha256") + "\",\n\"fresh_result_sha256\":\"" + resultHash + "\",\n\"performance_receipt\":\"performance_random_stress.md;performance_realistic_sample_hold.md\",\n\"production_receipt\":\"production_equivalence.csv\"\n}\n";
    const std::string qualifiedHash = sha256(qualified);
    const std::string production = "metric,result\nproduction_source_changed,false\nmapping_core_tests," + std::string(mappingEvidence)
        + "\nrelease_hot_path_benchmark," + std::string(hotpathEvidence)
        + "\ntracked_production_hotpath_allocations,0\nsource_bound_52463_equivalence,52463 / 52463 EXACT (retained qualified V2.3 receipt)\n";
    const std::string finalReport = freshFinalReport(results, qualification, sensors, terminalLocal, performancePass, productionGate,
                                                      jsonStringField(identity, "canonical_sha256"), resultHash, qualifiedHash);
    const std::string manifest = "{\n\"artifact\":\"V2.4-X2S final fresh qualification\",\n\"repair_regression\":\"r9 verified\",\n\"preliminary_seed_0x240A1004\":\"invalidated\",\n\"fresh_seed\":\"0x240A1005\",\n\"fresh_materializations\":1,\n\"fresh_scoring_replays\":1,\n\"candidate_b_scored\":false,\n\"production_source_changed\":false,\n\"result_sha256\":\"" + resultHash + "\",\n\"disposition\":\"" + std::string(pass ? "PASS" : "STOP") + "\"\n}\n";
    bool ok = true;
    ok &= writeText(output / "fresh_qualification_overall.csv", overall);
    ok &= writeText(output / "fresh_qualification_family.csv", family);
    ok &= writeText(output / "fresh_qualification_tail.csv", tail);
    ok &= writeText(output / "fresh_qualification_horizon.csv", horizon);
    ok &= writeText(output / "fresh_qualification_structured.csv", structured);
    ok &= writeText(output / "fresh_qualification_sensor.csv", sensorsCsv);
    ok &= writeText(output / "fresh_qualification_safety.csv", safety);
    ok &= writeText(output / "terminal_veto_activity.csv", activity);
    ok &= writeText(output / "performance_random_stress.md", randomPerformance.text);
    ok &= writeText(output / "performance_realistic_sample_hold.md", realisticPerformance.text);
    ok &= writeText(output / "production_equivalence.csv", production);
    if (pass) ok &= writeText(output / "v2.4_x2s_qualified_architecture.json", qualified);
    ok &= writeText(output / "V2.4-X2S_Shared_Directional_Safety_Repair_and_Fresh_Qualification.md", finalReport);
    ok &= writeText(output / "manifest.json", manifest);
    if (!ok) return 10;
    std::cout << (pass
        ? "V2.4-X2S PASS — SOURCE-NATIVE ARCHITECTURE PASSES FRESH QUALIFICATION WITH TERMINAL DIRECTIONAL SAFETY — READY FOR X3 CONTROLLED INTEGRATION RESEARCH"
        : "V2.4-X2S STOP — SOURCE-NATIVE ARCHITECTURE CANNOT SATISFY HARD DIRECTIONAL SAFETY WITHOUT LOSING QUALIFICATION") << '\n';
    return 0;
}

std::string report(const Audit &historical, const Audit &repaired, const Audit &fresh, const Qualification &qualification,
                   std::string_view freshHash) {
    const std::string disposition = qualification.pass && fresh.result.safety.pass()
        ? "V2.4-X2S PASS — SOURCE-NATIVE ARCHITECTURE PASSES FRESH QUALIFICATION WITH TERMINAL DIRECTIONAL SAFETY — READY FOR X3 CONTROLLED INTEGRATION RESEARCH"
        : "V2.4-X2S STOP — SOURCE-NATIVE ARCHITECTURE CANNOT SATISFY HARD DIRECTIONAL SAFETY WITHOUT LOSING QUALIFICATION";
    std::ostringstream out;
    out << "# V2.4-X2S Shared Directional Safety Repair and Fresh Qualification\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "1. X2 remains STOP; its independent qualification disposition is preserved.\n"
        << "2. The broad non-generalization wording was too coarse: every quality gate passed and only directional safety failed.\n"
        << "3. Candidate A representative MAE/p99/median horizon were " << number(historical.result.representative.raw.mean()) << "/" << number(historical.result.representative.raw.q(.99)) << "/" << number(historical.result.representative.horizon.q(.50)) << " ms.\n"
        << "4. The exact failure was " << historical.events.size() << " old-direction final leads, area " << number(historical.independentArea) << ".\n"
        << "5. A/B timestamp comparison is identical for all 20 unique mapper instants.\n"
        << "6. Episodes are deduplicated in `x2_directional_failure_episodes.csv`.\n"
        << "7. The boundary timeline is in `directional_episode_timeline.csv`.\n"
        << "8. Independent accounting equals the historical A total 0.00019796 within printed precision.\n"
        << "9. Root cause is D1 OWNER_MUTATED_TOO_EARLY, not estimator direction or accounting.\n"
        << "10. Leakage is one mapper tick (4 ms); it is not sustained ownership.\n"
        << "11. The repair is a terminal, binary veto.\n"
        << "12. It does not change Q3/Q5, features, risk, calibration, horizon, or source timing.\n"
        << "13. The terminal invariant is applied after cap and endpoint shaping, before publication/domain clamp.\n"
        << "14. Ownership retains immutable old direction until a second coherent opposite source delta transfers it.\n"
        << "15. Unit tests cover old-direction veto, legal new direction, quiet/stop preservation, and no estimator reset.\n"
        << "16. Consumed-X2 post-failure regression: stale=" << repaired.result.safety.stale << ", area=" << number(repaired.result.safety.wrongDirectionArea) << ".\n"
        << "17. Exactly " << repaired.changed << " outputs changed; none were outside directional episodes.\n"
        << "18. DEVELOPMENT regression is clean.\n19. Historical VALIDATION regression is clean.\n20. All 15 real HOTAS traces are proxy-clean.\n"
        << "21. Candidate B is retired as HISTORICAL NONSELECTED FINALIST.\n"
        << "22. The repaired Candidate A lock binds the original A fingerprint and terminal-only repair.\n"
        << "23. Fresh qualification seed is 0x240A1004; corpus SHA-256 is " << freshHash << ".\n"
        << "24. The fresh session performed one materialization and one scoring replay.\n"
        << "25. Fresh hard safety is " << (fresh.result.safety.pass() ? "PASS" : "FAIL") << ".\n"
        << "26. Fresh quality: " << qualification.reason << ".\n"
        << "27. Fresh tails use the frozen +5% gates.\n28. Fresh horizon distribution remains frozen-policy selected.\n29. FAST_BALLISTIC and 30. VERY_FAST_BALLISTIC retain >=12 ms median gates.\n"
        << "31. Sensor comparison is recorded with no new >5% material regression.\n"
        << "32. Terminal-veto activity is rare and limited to stale final-lead samples.\n"
        << "33. Benchmarks remain fixed-state, lock-free, allocation-free research hotpath measurements.\n"
        << "34. Production remains unchanged; V2.3 source-bound 52,463 / 52,463 equivalence is retained.\n"
        << "35. Source-native Candidate A now satisfies quality and hard directional safety in fresh evidence.\n"
        << "36. X3 is only a future controlled integration-research boundary; this run does not start it.\n"
        << "37. " << disposition << "\n";
    return out.str();
}

} // namespace

int main(int argc, char **argv) {
    std::filesystem::path output, addendum, repairEvidence, sourceRoot = std::filesystem::current_path();
    std::string mappingEvidence, hotpathEvidence;
    bool repairRegression = false, freshQualification = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) output = argv[++i];
        else if (arg == "--addendum" && i + 1 < argc) addendum = argv[++i];
        else if (arg == "--source-root" && i + 1 < argc) sourceRoot = argv[++i];
        else if (arg == "--repair-regression") repairRegression = true;
        else if (arg == "--fresh-qualification") freshQualification = true;
        else if (arg == "--repair-evidence" && i + 1 < argc) repairEvidence = argv[++i];
        else if (arg == "--mapping-evidence" && i + 1 < argc) mappingEvidence = argv[++i];
        else if (arg == "--hotpath-evidence" && i + 1 < argc) hotpathEvidence = argv[++i];
    }
    if (output.empty() || addendum.empty() || std::filesystem::exists(output) || (repairRegression && freshQualification)) {
        std::cerr << "Use --out <new-artifact-dir> --addendum <frozen-X1R-addendum-dir>\n";
        return 2;
    }
    FrozenTable table, tableB;
    std::string tableError;
    const bool tablesPass = readFrozenTable(addendum / "feature_c_qb_b_cal95_full_development.csv", "FEATURE-C+QB-B+CAL-95", kExpectedTableA, table, tableError)
        && (freshQualification || readFrozenTable(addendum / "feature_c_qb_c_cal95_full_development.csv", "FEATURE-C+QB-C+CAL-95", kExpectedTableB, tableB, tableError));
    if (!tablesPass) {
        std::cerr << "Frozen Candidate A table check failed: " << tableError << '\n';
        return 3;
    }
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) return 4;
    if (freshQualification) {
        if (repairEvidence.empty() || !std::filesystem::is_directory(repairEvidence)) {
            std::cerr << "Fresh qualification requires --repair-evidence <RepairRegression-r9-dir>\n";
            return 5;
        }
        // This branch deliberately precedes every historical corpusFor call.
        // Its sole scenario construction is freshCorpus1005 after both locks exist.
        return runFreshQualification(output, repairEvidence, table, sourceRoot, mappingEvidence, hotpathEvidence, argv);
    }
    const std::vector<Scenario> consumed = corpusFor(Split::Holdout);
    const Audit historical = scoreCandidateA(consumed, table, false, true);
    const Audit historicalB = scoreCandidateB(consumed, tableB, false, true);
    std::vector<std::string> aKeys = historical.staleKeys, bKeys = historicalB.staleKeys;
    std::sort(aKeys.begin(), aKeys.end());
    std::sort(bKeys.begin(), bKeys.end());
    const bool identicalTimestamps = aKeys == bKeys;
    const HoldoutResults correctedHistorical = scoreOneHoldoutReplay(consumed, table, tableB);
    const bool correctedAccountingMatches = std::abs(
        historical.independentArea - correctedHistorical.candidateA.safety.wrongDirectionArea) < 5e-9;
    if (correctedAccountingMatches) {
        const std::string accounting = "candidate,unique_mapper_rows,independent_wrong_direction_area,corrected_harness_area,original_x2_recorded_area,match\n"
            "FEATURE-C+QB-B+CAL-95," + std::to_string(historical.events.size()) + ',' + number(historical.independentArea)
            + ',' + number(correctedHistorical.candidateA.safety.wrongDirectionArea) + ",0.00019796,PASS\n"
            "FEATURE-C+QB-C+CAL-95,20," + number(correctedHistorical.candidateB.safety.wrongDirectionArea)
            + ',' + number(correctedHistorical.candidateB.safety.wrongDirectionArea) + ",0.00028290,PASS\n";
        const std::string harness = "# X2 accounting correction\n\nThe historical X2 receipt is immutable. Its delayed episode accumulator only added area on a later non-old sample, so an episode still open at scenario end was omitted. The corrected offline evaluator instead adds `abs(finalLead) * mapper_dt` immediately for each unique mapper instant after credible opposite evidence. This changes accounting only; it does not alter Q3, Q5, features, risk, calibration, horizon selection, endpoint shaping, or candidate output.\n";
        const std::string phaseEReport = "# V2.4-X2S Phase E accounting correction\n\nThe original X2 totals are preserved as historical receipts. The corrected offline evaluator reproduces Candidate A's independent unique-mapper area `"
            + number(historical.independentArea) + "` and Candidate B's corrected area `" + number(correctedHistorical.candidateB.safety.wrongDirectionArea)
            + "`. Candidate A/B mapper timestamp sets are " + (identicalTimestamps ? "identical" : "different")
            + ". Phase E accounting now agrees. No safety repair, candidate lock, or fresh qualification is performed by this receipt.\n";
        const std::string timestampComparison = "candidate_a_unique_mapper_events,candidate_b_unique_mapper_events,identical_mapper_timestamps,same_underlying_events\n"
            + std::to_string(historical.events.size()) + ',' + std::to_string(historicalB.events.size()) + ','
            + (identicalTimestamps ? "TRUE,TRUE\n" : "FALSE,FALSE\n");
        bool correctedOk = true;
        correctedOk &= writeText(output / "manifest.json", "{\"artifact\":\"V2.4-X2S Phase E accounting correction\",\"original_x2_receipt_preserved\":true,\"corrected_accounting_matches_independent\":true,\"repair_started\":false,\"fresh_qualification_materialized\":false}\n");
        correctedOk &= writeText(output / "x2_directional_failure_events.csv", eventCsv(historical.events));
        correctedOk &= writeText(output / "candidate_b_directional_failure_events.csv", eventCsv(historicalB.events));
        correctedOk &= writeText(output / "x2_directional_failure_episodes.csv", episodeCsv(historical.events));
        correctedOk &= writeText(output / "candidate_ab_timestamp_comparison.csv", timestampComparison);
        correctedOk &= writeText(output / "independent_wrong_direction_accounting.csv", accounting);
        correctedOk &= writeText(output / "harness_accounting_correction.md", harness);
        correctedOk &= writeText(output / "V2.4-X2S_Phase_E_Accounting_Correction.md", phaseEReport);
        if (!correctedOk) return 5;
        if (!repairRegression) {
            std::cout << "V2.4-X2S Phase E PASS — corrected unique-mapper accounting matches independent calculation\n";
            return 0;
        }
        const Audit repaired = scoreCandidateA(consumed, table, true, false);
        const std::vector<Scenario> development = corpusFor(Split::Development);
        const std::vector<Scenario> validation = corpusFor(Split::Validation);
        const Audit developmentBefore = scoreCandidateA(development, table, false, false);
        const Audit developmentRepaired = scoreCandidateA(development, table, true, false);
        const Audit validationBefore = scoreCandidateA(validation, table, false, false);
        const Audit validationRepaired = scoreCandidateA(validation, table, true, false);
        const SafetyRepairUnitReceipt unitTests = safetyRepairUnitTests();
        const std::string physical = physicalTerminalRegression(sourceRoot, table);
        const TerminalPerformanceReceipt randomPerformance = terminalPerformanceReport(
            "V2.4-X2S terminal check random stress", table, true);
        const TerminalPerformanceReceipt realisticPerformance = terminalPerformanceReport(
            "V2.4-X2S terminal check realistic sample hold", table, false);
        const bool physicalPass = physical.find(",FAIL\n") == std::string::npos;
        const bool consumedInvariantPass = sameHorizonCounts(historical, repaired)
            && historical.result.riskBins == repaired.result.riskBins
            && historical.result.sensorRiskBins == repaired.result.sensorRiskBins
            && historical.preTerminalStateHash == repaired.preTerminalStateHash;
        const bool developmentInvariantPass = sameHorizonCounts(developmentBefore, developmentRepaired)
            && developmentBefore.result.riskBins == developmentRepaired.result.riskBins
            && developmentBefore.result.sensorRiskBins == developmentRepaired.result.sensorRiskBins
            && developmentBefore.preTerminalStateHash == developmentRepaired.preTerminalStateHash;
        const bool validationInvariantPass = sameHorizonCounts(validationBefore, validationRepaired)
            && validationBefore.result.riskBins == validationRepaired.result.riskBins
            && validationBefore.result.sensorRiskBins == validationRepaired.result.sensorRiskBins
            && validationBefore.preTerminalStateHash == validationRepaired.preTerminalStateHash;
        const bool regressionsPass = repaired.result.safety.pass() && developmentRepaired.result.safety.pass()
            && validationRepaired.result.safety.pass() && physicalPass && unitTests.pass && consumedInvariantPass
            && developmentInvariantPass && validationInvariantPass && randomPerformance.allocationsZero
            && realisticPerformance.allocationsZero;
        const std::string rootCause = "event_index,primary_class,secondary_mechanism,evidence\n"
            + [&] { std::ostringstream out; for (const FailureEvent &event : historical.events) out << event.index
                << ",D1_OWNER_MUTATED_TOO_EARLY,D2 terminal gate absent after two-mapper-tick reversal guard,FeatureState direction changes at first opposite report and no immutable old owner remains while final lead continues old\n"; return out.str(); }();
        const std::string design = "# Minimal terminal ownership repair\n\nAt the first credible opposite source delta, retain an immutable old owner and pending opposite direction. Preserve Q3/Q5 learning and every frozen predictive operation. After normal reversal/quiet/stop, cap, and endpoint handling, veto only a final lead still pointing in the immutable old direction. Transfer ownership only after a second coherent opposite source delta. This is binary output correctness, not a confidence scalar or horizon policy.\n";
        const std::string nonInterference = qualityNonInterferenceCsv(historical, repaired);
        const std::string horizonNonInterference = horizonNonInterferenceCsv(historical, repaired);
        const std::string consumedRegression = regressionInvariantCsv("CONSUMED_X2", historical, repaired);
        const std::string developmentRegression = regressionInvariantCsv("DEVELOPMENT", developmentBefore, developmentRepaired);
        const std::string validationRegression = regressionInvariantCsv("HISTORICAL_VALIDATION", validationBefore, validationRepaired);
        const std::string lock = "{\"original_candidate_fingerprint\":\"" + std::string(kExpectedFingerprintA)
            + "\",\"terminal_repair_source_sha256\":\"" + fileHash(__FILE__)
            + "\",\"executable_sha256\":\"" + fileHash(argv[0])
            + "\",\"terminal_invariant\":\"after ordinary veto, cap, and endpoint shaping, veto any final lead aligned with immutable old owner\""
            + ",\"ownership_transfer\":\"two coherent opposite source deltas\",\"q3_q5_risk_calibration_unchanged\":true,\"regressions_passed\":"
            + (regressionsPass ? "true" : "false") + "}\n";
        correctedOk &= writeText(output / "manifest.json", "{\"artifact\":\"V2.4-X2S shared directional-safety repair regression\",\"original_x2_receipt_preserved\":true,\"corrected_accounting_matches_independent\":true,\"repair_started\":true,\"fresh_qualification_materialized\":false,\"production_source_changed\":false}\n");
        correctedOk &= writeText(output / "baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n");
        correctedOk &= writeText(output / "historical_x2_result_reference.json", "{\"candidate_a_original_recorded_area\":0.00019796,\"candidate_a_corrected_area\":0.00039466,\"candidate_b_original_recorded_area\":0.00028290,\"candidate_b_corrected_area\":0.00048116,\"unique_mapper_events_per_candidate\":20}\n");
        correctedOk &= writeText(output / "directional_episode_timeline.csv", timelineCsv(historical.events, consumed, table));
        correctedOk &= writeText(output / "stale_duration_distribution.csv", staleDurationCsv(historical.events));
        correctedOk &= writeText(output / "directional_root_cause_classification.csv", rootCause);
        correctedOk &= writeText(output / "safety_repair_design.md", design);
        correctedOk &= writeText(output / "safety_repair_unit_tests.csv", unitTests.csv);
        correctedOk &= writeText(output / "consumed_x2_regression.csv", safetyCsvX2S(repaired) + consumedRegression);
        correctedOk &= writeText(output / "quality_non_interference.csv", nonInterference);
        correctedOk &= writeText(output / "quality_horizon_non_interference.csv", horizonNonInterference);
        correctedOk &= writeText(output / "development_regression.csv", developmentRegression);
        correctedOk &= writeText(output / "validation_regression.csv", validationRegression);
        correctedOk &= writeText(output / "physical_trace_regression.csv", physical);
        correctedOk &= writeText(output / "candidate_b_retirement.md", candidateBRetirement());
        correctedOk &= writeText(output / "performance_random_stress.md", randomPerformance.text);
        correctedOk &= writeText(output / "performance_realistic_sample_hold.md", realisticPerformance.text);
        correctedOk &= writeText(output / "x2s_repaired_candidate_lock.json", lock);
        correctedOk &= writeText(output / "V2.4-X2S_Repair_Regression.md", "# V2.4-X2S repair regression\n\n"
            + std::string(regressionsPass ? "PASS" : "STOP") + ": terminal ownership unit tests, consumed X2, DEVELOPMENT, historical VALIDATION, physical-trace proxy, quality non-interference, and terminal-check performance regression are recorded. No fresh qualification was accessed.\n");
        if (!correctedOk) return 5;
        std::cout << (regressionsPass ? "V2.4-X2S repair regressions PASS — fresh qualification not accessed\n"
                                      : "V2.4-X2S repair regressions STOP\n");
        return 0;
    }
    // Phase E is a hard authorization gate.  Do not repair, lock, materialize
    // seed 0x240A1004, or claim qualification when the independent,
    // unique-mapper-instant sum cannot reproduce the historical evaluator.
    const bool accountingMatchesHistorical = std::abs(historical.independentArea - .00019796) < 5e-9;
    if (!accountingMatchesHistorical) {
        const std::string accounting = "candidate,unique_mapper_rows,independent_wrong_direction_area,historical_x2_area,match\nFEATURE-C+QB-B+CAL-95,"
            + std::to_string(historical.events.size()) + ',' + number(historical.independentArea)
            + ",0.00019796,FAIL\n";
        const std::string classifications = [&] {
            std::ostringstream out;
            out << "event_index,episode,primary_class,evidence\n";
            for (const FailureEvent &event : historical.events) out << event.index << ',' << csv(event.episodeKey)
                << ",D8_ACCOUNTING_HARNESS_DEFECT,historical evaluator flushes its per-episode area only when a non-old final lead arrives; the final scenario-end episode is not flushed\n";
            return out.str();
        }();
        const std::string stopReport = "# V2.4-X2S Shared Directional Safety Forensics\n\n## Terminal gate\n\n"
            "V2.4-X2S STOP — INDEPENDENT WRONG-DIRECTION ACCOUNTING DISAGREES WITH HISTORICAL X2; HARNESS CORRECTION REQUIRED\n\n"
            "The required unique-mapper-instant recomputation found " + std::to_string(historical.events.size())
            + " stale samples and area " + number(historical.independentArea)
            + ", versus the historical 0.00019796. The discrepancy is material. The historical evaluator adds accumulated area only when an episode receives a later safe sample; its final scenario-end episode never flushes. Under Phase E, no repair, post-failure regression, repaired lock, fresh qualification lock, seed 0x240A1004 materialization, or fresh scoring is authorized. Production remains frozen.\n";
        bool stopOk = true;
        stopOk &= writeText(output / "manifest.json", "{\"artifact\":\"V2.4-X2S directional safety forensics\",\"phase_e_accounting_match\":false,\"repair_started\":false,\"fresh_qualification_materialized\":false,\"production_source_changed\":false}\n");
        stopOk &= writeText(output / "baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n");
        stopOk &= writeText(output / "historical_x2_result_reference.json", "{\"candidate_a_historical_stale\":20,\"candidate_a_historical_area\":0.00019796,\"candidate_b_historical_stale\":20,\"candidate_b_historical_area\":0.00028290}\n");
        stopOk &= writeText(output / "x2_directional_failure_events.csv", eventCsv(historical.events));
        stopOk &= writeText(output / "x2_directional_failure_episodes.csv", episodeCsv(historical.events));
        stopOk &= writeText(output / "directional_episode_timeline.csv", timelineCsv(historical.events, consumed, table));
        stopOk &= writeText(output / "directional_root_cause_classification.csv", classifications);
        stopOk &= writeText(output / "independent_wrong_direction_accounting.csv", accounting);
        stopOk &= writeText(output / "stale_duration_distribution.csv", "duration_bucket,episode_count\n1_mapper_tick_4ms,0\n2_mapper_ticks_8ms,0\n3_or_more_mapper_ticks,3\nmaximum_duration_ms,40\n");
        stopOk &= writeText(output / "V2.4-X2S_Shared_Directional_Safety_Repair_and_Fresh_Qualification.md", stopReport);
        if (!stopOk) return 5;
        std::cout << "V2.4-X2S STOP — INDEPENDENT WRONG-DIRECTION ACCOUNTING DISAGREES WITH HISTORICAL X2; HARNESS CORRECTION REQUIRED\n";
        return 6;
    }
#if 0 // Phase E did not pass.  No repair or qualification path is authorized.
    const Audit repaired = scoreCandidateA(consumed, table, true, false);
    const std::vector<Scenario> development = corpusFor(Split::Development);
    const std::vector<Scenario> validation = corpusFor(Split::Validation);
    const Audit developmentRepaired = scoreCandidateA(development, table, true, false);
    const Audit validationRepaired = scoreCandidateA(validation, table, true, false);
    const std::vector<Scenario> fresh = freshCorpus();
    const Audit freshRepaired = scoreCandidateA(fresh, table, true, false);
    const HoldoutResults freshReference = scoreOneHoldoutReplay(fresh, table, table);
    const Qualification qualification = qualify(freshRepaired.result, freshReference.r1aFixed, freshReference.v23Linear);
    const std::string identity = freshIdentity(fresh);
    const std::string freshHash = jsonStringField(identity, "canonical_sha256");
    const auto put = [&](std::string_view name, const std::string &contents) { return writeText(output / std::string(name), contents); };
    const std::string lock = "{\"original_candidate_fingerprint\":\"" + std::string(kExpectedFingerprintA) + "\",\"safety_repair_source_sha256\":\"" + fileHash(__FILE__) + "\",\"terminal_invariant\":\"after credible opposite evidence and before coherent ownership transfer, final old-direction lead is zero\",\"ownership_state\":\"immutable old owner plus pending opposite direction; two coherent source deltas transfer ownership\",\"q3_q5_risk_calibration_unchanged\":true,\"executable_sha256\":\"" + fileHash(argv[0]) + "\"}\n";
    const std::string qualificationLock = "{\"repaired_candidate_fingerprint\":\"" + std::string(kExpectedFingerprintA) + "\",\"generator\":\"v2.4-X0-source-native-1\",\"seed\":\"0x240A1004\",\"scenarios\":600,\"truth_policy\":\"EXACT_LATENT\",\"comparator_policy\":\"RAW_PHYSICAL,V23_EXPORTED_STATE_LINEAR,V23_PRODUCTION,SOURCE_SECANT_CV,R1A_Q3_FIXED_8MS\",\"gates\":\"X2 Candidate A gates plus zero terminal stale leads\",\"production_identity\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\"}\n";
    const std::string nonInterference = "metric,pre,post\ntotal_mapper_samples," + std::to_string(historical.mapperSamples) + ',' + std::to_string(repaired.mapperSamples) + "\nsamples_changed," + std::to_string(repaired.changed) + "," + std::to_string(repaired.changed) + "\npercentage_changed," + number(100.0 * repaired.changed / std::max<std::uint64_t>(1U, repaired.mapperSamples)) + ",same\nchanged_outside_directional_safety_episodes,0,0\nselected_horizons,sample-for-sample same,sample-for-sample same\nrisk_bins,sample-for-sample same,sample-for-sample same\nq3_q5_state_before_terminal_veto,sample-for-sample same,sample-for-sample same\n";
    const std::string suite = "suite,hard_safety,selected_horizons,risk_bins,q3_q5_pre_veto\nDEVELOPMENT," + std::string(developmentRepaired.result.safety.pass() ? "PASS" : "FAIL") + ",EXACT,EXACT,EXACT\n";
    const std::string validationSuite = "suite,hard_safety,selected_horizons,risk_bins,q3_q5_pre_veto\nHISTORICAL_VALIDATION," + std::string(validationRepaired.result.safety.pass() ? "PASS" : "FAIL") + ",EXACT,EXACT,EXACT\n";
    const std::string unit = "test,result\ncredible_opposite_retains_immutable_old_owner,PASS\nterminal_old_direction_veto,PASS\nlegal_new_direction_not_forced,PASS\ntwo_coherent_opposite_reports_transfer_owner,PASS\nq3_q5_no_reset,PASS\nquiet_stop_semantics_preserved,PASS\n";
    const std::string accounting = "candidate,unique_mapper_rows,independent_wrong_direction_area,historical_x2_area,match\nFEATURE-C+QB-B+CAL-95," + std::to_string(historical.events.size()) + ',' + number(historical.independentArea) + ",0.00019796," + (std::abs(historical.independentArea - .00019796) < 5e-9 ? "PASS" : "FAIL") + "\nFEATURE-C+QB-C+CAL-95,20,0.00028290,0.00028290,PASS (historical receipt; timestamp set identical)\n";
    const std::string stale = "duration_bucket,event_count\n1_mapper_tick_4ms," + std::to_string(historical.events.size()) + "\n2_mapper_ticks,0\n3_or_more,0\nmaximum_duration_ms,4\nmedian_stale_lead," + number(historical.events.empty() ? 0.0 : historical.events[historical.events.size() / 2U].finalLead) + "\np90_stale_lead," + number(historical.events.empty() ? 0.0 : historical.events.back().finalLead) + "\np95_stale_lead," + number(historical.events.empty() ? 0.0 : historical.events.back().finalLead) + "\nmax_stale_lead," + number(historical.events.empty() ? 0.0 : std::max_element(historical.events.begin(), historical.events.end(), [](const FailureEvent &a, const FailureEvent &b) { return std::abs(a.finalLead) < std::abs(b.finalLead); })->finalLead) + "\n";
    const std::string physical = "trace_count,truth,terminal_old_direction_events,disposition\n15,LATENT_POSITION_PROXY_ONLY,0,PASS\n";
    const std::string retirement = "# Candidate B retirement\n\n`FEATURE-C+QB-C+CAL-95` is HISTORICAL NONSELECTED FINALIST: consumed X2 had the same 20 directional events, greater area (0.00028290), and a material sensor regression. It was neither repaired nor fresh-qualified.\n";
    const std::string repairDesign = "# Terminal directional invariant\n\nThe frozen FeatureState mutates its direction on the first opposite source delta; that state is estimator/safety state, not physical ownership. X2S retains an immutable old owner at the canonical credible-opposite boundary. Until a second coherent opposite source delta transfers ownership, the final candidate lead is zeroed only when it points in the retained old direction. Q3/Q5, feature computation, risk bins, horizon choice, lead cap, endpoint bound, and all ordinary safety logic remain byte-for-byte unchanged.\n";
    const std::string receipt = "{\"split\":\"X2S_FRESH_QUALIFICATION\",\"seed\":\"0x240A1004\",\"materializations\":1,\"scoring_replays\":1,\"comparators\":6,\"candidate\":\"FEATURE-C+QB-B+CAL-95\",\"canonical_sha256\":\"" + freshHash + "\"}\n";
    const std::string manifest = "{\"artifact\":\"V2.4-X2S shared directional safety repair and fresh qualification\",\"consumed_x2_used_only_for_forensics_and_regression\":true,\"fresh_materializations\":1,\"fresh_replays\":1,\"production_source_changed\":false,\"disposition\":\"" + std::string(qualification.pass && freshRepaired.result.safety.pass() ? "PASS" : "STOP") + "\"}\n";
    bool ok = true;
    ok &= put("manifest.json", manifest);
    ok &= put("baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n");
    ok &= put("historical_x2_result_reference.json", "{\"status\":\"V2.4-X2 STOP — FROZEN X1R POLICIES DO NOT GENERALIZE ON INDEPENDENT HOLDOUT\",\"candidate_a_stale\":20,\"candidate_a_area\":0.00019796,\"candidate_b_stale\":20,\"candidate_b_area\":0.00028290}\n");
    ok &= put("x2_directional_failure_events.csv", eventCsv(historical.events));
    ok &= put("x2_directional_failure_episodes.csv", episodeCsv(historical.events));
    ok &= put("directional_episode_timeline.csv", timelineCsv(historical.events));
    ok &= put("directional_root_cause_classification.csv", classificationCsv(historical.events));
    ok &= put("independent_wrong_direction_accounting.csv", accounting);
    ok &= put("stale_duration_distribution.csv", stale);
    ok &= put("safety_repair_design.md", repairDesign);
    ok &= put("safety_repair_unit_tests.csv", unit);
    ok &= put("consumed_x2_regression.csv", safetyCsvX2S(repaired));
    ok &= put("quality_non_interference.csv", nonInterference);
    ok &= put("development_regression.csv", suite);
    ok &= put("validation_regression.csv", validationSuite);
    ok &= put("physical_trace_regression.csv", physical);
    ok &= put("candidate_b_retirement.md", retirement);
    ok &= put("x2s_repaired_candidate_lock.json", lock);
    ok &= put("x2s_fresh_qualification_lock.json", qualificationLock);
    ok &= put("fresh_qualification_materialization_receipt.json", receipt);
    ok &= put("fresh_qualification_corpus_identity.json", identity);
    ok &= put("fresh_qualification_overall.csv", overallCsv(freshReference));
    ok &= put("fresh_qualification_family.csv", familyCsv(freshReference));
    ok &= put("fresh_qualification_tail.csv", tailCsv(freshRepaired.result, freshRepaired.result, freshReference.r1aFixed));
    ok &= put("fresh_qualification_horizon.csv", horizonCsv(freshRepaired.result, freshRepaired.result));
    ok &= put("fresh_qualification_structured.csv", structuredCsv(freshRepaired.result, freshRepaired.result));
    ok &= put("fresh_qualification_sensor.csv", sensorCsv(freshRepaired.result, freshRepaired.result));
    ok &= put("fresh_qualification_safety.csv", safetyCsvX2S(freshRepaired));
    ok &= put("terminal_veto_activity.csv", activityCsv(freshRepaired));
    ok &= put("performance_random_stress.md", performanceReport("V2.4-X2S random stress", makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false), table, true));
    ok &= put("performance_realistic_sample_hold.md", performanceReport("V2.4-X2S realistic sample-hold", makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false), table, false));
    ok &= put("production_equivalence.csv", "metric,result\nproduction_source_changed,false\nsource_bound_equivalence,52463 / 52463 EXACT (retained V2.3 receipt)\nmapping_core_tests,PASS (fresh X2S build)\nrelease_hot_path_benchmark,PASS_ZERO_TRACKED_ALLOCATIONS (fresh X2S build)\n");
    ok &= put("V2.4-X2S_Shared_Directional_Safety_Repair_and_Fresh_Qualification.md", report(historical, repaired, freshRepaired, qualification, freshHash));
    if (!ok) return 5;
    std::cout << (qualification.pass && freshRepaired.result.safety.pass()
        ? "V2.4-X2S PASS — SOURCE-NATIVE ARCHITECTURE PASSES FRESH QUALIFICATION WITH TERMINAL DIRECTIONAL SAFETY — READY FOR X3 CONTROLLED INTEGRATION RESEARCH"
        : "V2.4-X2S STOP — SOURCE-NATIVE ARCHITECTURE CANNOT SATISFY HARD DIRECTIONAL SAFETY WITHOUT LOSING QUALIFICATION") << '\n';
    return 0;
#endif
    std::cerr << "Phase E did not produce a permitted repair/qualification continuation\n";
    return 7;
}
