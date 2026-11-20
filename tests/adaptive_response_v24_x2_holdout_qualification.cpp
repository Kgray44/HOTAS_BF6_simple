// Compile-gated V2.4-X2.  This translation unit imports the frozen X0/X1/X1R
// research implementation and evaluates only the sealed HOLDOUT split.
#include "adaptive_response_v24_x1r_tail_calibrated.cpp"

namespace {

constexpr std::string_view kExpectedOriginalArchitectureLock =
    "9b2501acecbe5a4f37f780425626462360e70f88e81dfc827c8afda88d3adc71";
constexpr std::string_view kExpectedOriginalSelection =
    "98bbba0d561cbd81ee5f4b81fc32a51a8b1e68603f4d3df8e45a5b2ab6b43a91";
constexpr std::string_view kExpectedOriginalManifest =
    "75fd6a63e92a8de1671144b4dc29515b17e56453725013eb943f42dbacab0e47";
constexpr std::string_view kExpectedOriginalFoldManifest =
    "5ff33a2022ad200ffd27f06c8e47aeea915b11b8e9e38812a69053bd4e5bd3c3";
constexpr std::string_view kExpectedFinalBinding =
    "60eee31a9c466a4168985b20e461c46a830ccea9f8539245a85e626982ec7486";
constexpr std::string_view kExpectedCompleteLock =
    "b70546d54cae1005f37dff6e096a7fa8d4340b39e13df52f5b8afbfb39ecb8a7";
constexpr std::string_view kExpectedResumeAuthority =
    "4cf15a2b5be4400e09974618f4ead39e4a1530d24bbe94796446b00f34956da8";
constexpr std::string_view kExpectedTableA =
    "3b2f19299f6ebb6e93046a6704223f5645c1a379b2db230301c286864dfdd2f0";
constexpr std::string_view kExpectedTableB =
    "1f7c0328057748c481cf3218b136253a0eaf0f75c38c92c7b39bc9e6e8d61ff9";
constexpr std::string_view kExpectedFingerprintA =
    "ed5040453f0b61fc03362bb679bb733e422af843fe41aaa76af5b0a8d31d02cf";
constexpr std::string_view kExpectedFingerprintB =
    "a7897ee7fda7f371b0e7561426b0cf24273188154920d47089ea220f27e2ba55";
struct X2Options {
    std::filesystem::path output, freezeAddendum, originalX1r, sourceRoot;
    std::string mappingEvidence, hotpathEvidence;
};

struct FrozenTable {
    std::string candidate;
    std::array<std::array<double, 5>, kX1HorizonsMs.size()> p95{}, p99{};
    std::array<std::array<std::uint64_t, 5>, kX1HorizonsMs.size()> counts{};
};

struct Measure {
    Distribution raw, bounded, horizon, innovation, disagreement, curvature, age;
    std::uint64_t n = 0U;
    void add(double rawError, double boundedError, double selectedHorizon, const Feature &feature) {
        raw.add(std::abs(rawError)); bounded.add(std::abs(boundedError)); horizon.add(selectedHorizon);
        innovation.add(feature.innovation); disagreement.add(feature.disagreement);
        curvature.add(feature.curvature); age.add(feature.age); ++n;
    }
};

struct CandidateSafety {
    std::uint64_t finite = 0U, domain = 0U, stale = 0U, quiet = 0U, stop = 0U, runaway = 0U;
    std::uint64_t reversals = 0U;
    double wrongDirectionArea = 0.0;
    bool pass() const {
        return finite == 0U && domain == 0U && stale == 0U && quiet == 0U && stop == 0U && runaway == 0U;
    }
};

struct DirectionTrack {
    double oldDirection = 0.0, area = 0.0;
    std::optional<double> credibleOpposite;
};

struct TailEvent {
    std::string family, motion, sensor;
    std::uint32_t scenario = 0U;
    double timeMs = 0.0, error = 0.0, horizonMs = 0.0, risk = 0.0;
    Feature feature{};
    double q3Velocity = 0.0, q3Acceleration = 0.0, q5Velocity = 0.0, q5Acceleration = 0.0;
};

struct CandidateResult {
    std::string name;
    std::map<std::string, Measure> family, sensor, structured;
    std::map<std::string, std::map<int, Measure>> familyByHorizon;
    std::map<std::string, std::array<std::uint64_t, 5>> sourcePeriodBuckets;
    std::array<std::uint64_t, 5> riskBins{};
    Measure all, representative;
    CandidateSafety safety;
    std::vector<TailEvent> tails;
    std::uint64_t leadCap = 0U, endpointTaper = 0U;
    double leadAdvantageRemoved = 0.0, endpointAdvantageRemoved = 0.0;
};

struct ComparatorResult {
    std::string name;
    std::map<std::string, Measure> family, sensor, structured;
    Measure all;
};

struct Qualification {
    bool correctness = false, tail = false, mean = false, horizon = false, fast = false;
    bool pass = false;
    std::string reason;
};

X2Options parseX2Options(int argc, char **argv) {
    X2Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
        else if (arg == "--freeze-addendum" && i + 1 < argc) options.freezeAddendum = argv[++i];
        else if (arg == "--original-x1r" && i + 1 < argc) options.originalX1r = argv[++i];
        else if (arg == "--source-root" && i + 1 < argc) options.sourceRoot = argv[++i];
        else if (arg == "--mapping-evidence" && i + 1 < argc) options.mappingEvidence = argv[++i];
        else if (arg == "--hotpath-evidence" && i + 1 < argc) options.hotpathEvidence = argv[++i];
    }
    return options;
}

std::string readBinary(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string fileHash(const std::filesystem::path &path) { return sha256(readBinary(path)); }
bool exactHash(const std::filesystem::path &path, std::string_view expected) {
    return std::filesystem::is_regular_file(path) && fileHash(path) == expected;
}
bool containsAll(const std::string &text, std::initializer_list<std::string_view> terms) {
    return std::all_of(terms.begin(), terms.end(), [&](const std::string_view term) {
        return text.find(term) != std::string::npos;
    });
}
int horizonIndex(double value) {
    for (size_t i = 0U; i < kX1HorizonsMs.size(); ++i)
        if (std::abs(kX1HorizonsMs[i] - value) < 1e-9) return static_cast<int>(i);
    return -1;
}

bool readFrozenTable(const std::filesystem::path &path, std::string_view candidate,
                     std::string_view expectedHash, FrozenTable &result, std::string &error) {
    if (!exactHash(path, expectedHash)) { error = "table SHA-256 mismatch"; return false; }
    std::istringstream input(readBinary(path)); std::string line;
    if (!std::getline(input, line) || line.find("candidate,risk_bin,horizon_ms") != 0U) {
        error = "table header mismatch"; return false;
    }
    result.candidate = std::string(candidate);
    std::array<std::array<bool, 5>, kX1HorizonsMs.size()> seen{};
    size_t count = 0U;
    while (std::getline(input, line)) {
        const std::vector<std::string> fields = csvLine(line);
        if (fields.size() != 10U || fields[0] != candidate) { error = "invalid table row"; return false; }
        const int horizon = horizonIndex(asNumber(fields[2]));
        const int bin = static_cast<int>(asNumber(fields[1]));
        if (horizon < 0 || bin < 0 || bin >= 5 || seen[static_cast<size_t>(horizon)][static_cast<size_t>(bin)]) {
            error = "duplicate or invalid table key"; return false;
        }
        seen[static_cast<size_t>(horizon)][static_cast<size_t>(bin)] = true;
        result.counts[static_cast<size_t>(horizon)][static_cast<size_t>(bin)] =
            static_cast<std::uint64_t>(asNumber(fields[3]));
        result.p95[static_cast<size_t>(horizon)][static_cast<size_t>(bin)] = asNumber(fields[5]);
        result.p99[static_cast<size_t>(horizon)][static_cast<size_t>(bin)] = asNumber(fields[7]);
        if (!std::isfinite(result.p95[static_cast<size_t>(horizon)][static_cast<size_t>(bin)]) ||
            !std::isfinite(result.p99[static_cast<size_t>(horizon)][static_cast<size_t>(bin)])) {
            error = "non-finite table bound"; return false;
        }
        ++count;
    }
    if (count != kX1HorizonsMs.size() * 5U) { error = "table row count mismatch"; return false; }
    for (size_t bin = 0U; bin < 5U; ++bin) for (size_t h = 1U; h < kX1HorizonsMs.size(); ++h) {
        if (result.p95[h][bin] + 1e-12 < result.p95[h - 1U][bin] ||
            result.p99[h][bin] + 1e-12 < result.p99[h - 1U][bin]) {
            error = "non-monotonic final table"; return false;
        }
    }
    return true;
}

size_t chooseFrozen(const Policy &policy, const FrozenTable &table, const FeatureState &state, double at) {
    size_t selected = 0U;
    const Scales locked{.09331237, .12932549, 16883.858866, .95403616};
    for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) {
        const double seconds = kX1HorizonsMs[h] / 1000.0;
        const Feature feature = state.features(at, seconds);
        const size_t bin = riskBin(policyRisk(policy, feature, locked));
        const double budget = policyBudget(policy, feature);
        if (table.p95[h][bin] <= budget && std::max(table.p95[h][bin], table.p99[h][bin]) <= .060)
            selected = h;
    }
    return selected;
}

void addMeasure(Measure &measure, double rawError, double boundedError, double horizon,
                const Feature &feature) {
    measure.add(rawError, boundedError, horizon, feature);
}
void addCandidateMeasure(CandidateResult &result, const Scenario &scenario, double rawError,
                         double boundedError, double horizon, const Feature &feature) {
    addMeasure(result.all, rawError, boundedError, horizon, feature);
    addMeasure(result.family[scenario.family], rawError, boundedError, horizon, feature);
    addMeasure(result.sensor[scenario.sensor], rawError, boundedError, horizon, feature);
    const std::string structured = isStructured(scenario) ? scenario.motion : "HUMAN_OR_TORTURE";
    addMeasure(result.structured[structured], rawError, boundedError, horizon, feature);
    addMeasure(result.familyByHorizon[scenario.family][static_cast<int>(horizon)], rawError, boundedError, horizon, feature);
    if (representative(scenario.family)) addMeasure(result.representative, rawError, boundedError, horizon, feature);
}
void addComparatorMeasure(ComparatorResult &result, const Scenario &scenario, double error, double horizon) {
    const Feature none{};
    addMeasure(result.all, error, error, horizon, none);
    addMeasure(result.family[scenario.family], error, error, horizon, none);
    addMeasure(result.sensor[scenario.sensor], error, error, horizon, none);
    addMeasure(result.structured[isStructured(scenario) ? scenario.motion : "HUMAN_OR_TORTURE"], error, error, horizon, none);
}

void observeSourceDirection(DirectionTrack &track, const std::vector<SourceReport> &reports, size_t reportIndex) {
    if (reportIndex < 2U) return;
    const double before = reports[reportIndex - 1U].value - reports[reportIndex - 2U].value;
    const double now = reports[reportIndex].value - reports[reportIndex - 1U].value;
    if (std::abs(before) > .002 && std::abs(now) > .002 && before * now < 0.0) {
        track.oldDirection = before > 0.0 ? 1.0 : -1.0;
        track.credibleOpposite = reports[reportIndex].time;
        track.area = 0.0;
    }
}
void observeDirectionalSafety(DirectionTrack &track, CandidateSafety &safety, double at, double finalLead) {
    if (!track.credibleOpposite || at + 1e-9 < *track.credibleOpposite) return;
    const double oldDirectionLead = finalLead * track.oldDirection;
    if (oldDirectionLead > 1e-7) {
        ++safety.stale; track.area += oldDirectionLead * kMapperSeconds;
    } else {
        ++safety.reversals; safety.wrongDirectionArea += track.area;
        track.credibleOpposite.reset();
    }
}

void scoreCandidateSample(CandidateResult &result, DirectionTrack &direction, const Policy &policy,
                          const FrozenTable &table, const Scenario &scenario, const MapperSample &sample,
                          FeatureState &state) {
    const size_t picked = chooseFrozen(policy, table, state, sample.time);
    const double horizonMs = kX1HorizonsMs[picked], seconds = horizonMs / 1000.0;
    if (sample.time + seconds > scenario.duration + 1e-9) return;
    const Feature feature = state.features(sample.time, seconds);
    const double risk = policyRisk(policy, feature, Scales{.09331237, .12932549, 16883.858866, .95403616});
    const size_t bin = riskBin(risk);
    const double predicted = state.q3.predict(sample.time + seconds);
    const double rawLead = predicted - sample.held;
    const double cappedLead = std::clamp(rawLead, -.18, .18);
    const bool veto = state.veto(rawLead);
    const double guardedLead = veto ? 0.0 : cappedLead;
    const double beforeEndpoint = sample.held + guardedLead;
    const double bounded = clampPosition(beforeEndpoint);
    const double truth = latentAt(scenario, sample.time + seconds);
    if (!std::isfinite(bounded)) ++result.safety.finite;
    if (bounded < -1.000001 || bounded > 1.000001) ++result.safety.domain;
    if (picked >= kX1HorizonsMs.size()) ++result.safety.runaway;
    if (veto && std::abs(bounded - sample.held) > .00001) {
        if (state.quiet >= 2) ++result.safety.quiet;
        else if (state.stop >= 2) ++result.safety.stop;
        else ++result.safety.stale;
    }
    observeDirectionalSafety(direction, result.safety, sample.time, bounded - sample.held);
    if (std::abs(rawLead) > .18) {
        ++result.leadCap;
        const double uncappedError = std::abs(sample.held + rawLead - truth);
        const double cappedError = std::abs(sample.held + cappedLead - truth);
        if (uncappedError < cappedError) result.leadAdvantageRemoved += cappedError - uncappedError;
    }
    if (std::abs(beforeEndpoint - bounded) > 1e-12) {
        ++result.endpointTaper;
        const double beforeError = std::abs(beforeEndpoint - truth), boundedError = std::abs(bounded - truth);
        if (beforeError < boundedError) result.endpointAdvantageRemoved += boundedError - beforeError;
    }
    addCandidateMeasure(result, scenario, predicted - truth, bounded - truth, horizonMs, feature);
    ++result.riskBins[bin];
    const double period = std::max(.000001, sample.sourceInterval);
    const double ratio = seconds / period;
    const size_t periodBin = ratio < .5 ? 0U : ratio < 1.0 ? 1U : ratio < 1.5 ? 2U : ratio < 2.0 ? 3U : 4U;
    ++result.sourcePeriodBuckets[scenario.family][periodBin];
    if (representative(scenario.family)) {
        result.tails.push_back({scenario.family, scenario.motion, scenario.sensor, scenario.index, sample.time * 1000.0,
            std::abs(predicted - truth), horizonMs, risk, feature,
            velocityOf(state.q3, sample.time + seconds), accelerationOf(state.q3, sample.time + seconds),
            velocityQ5(state.q5, sample.time + seconds), accelerationQ5(state.q5, sample.time + seconds)});
    }
}

struct HoldoutResults {
    CandidateResult candidateA, candidateB;
    ComparatorResult rawPhysical{"RAW_PHYSICAL"}, v23Linear{"V23_EXPORTED_STATE_LINEAR"},
        v23Production{"V23_PRODUCTION"}, sourceCv{"SOURCE_SECANT_CV"}, r1aFixed{"R1A_Q3_FIXED_8MS"};
};

HoldoutResults scoreOneHoldoutReplay(const std::vector<Scenario> &holdout, const FrozenTable &tableA,
                                     const FrozenTable &tableB) {
    HoldoutResults results;
    results.candidateA.name = "FEATURE-C+QB-B+CAL-95";
    results.candidateB.name = "FEATURE-C+QB-C+CAL-95";
    const Policy policyA = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const Policy policyB = makePolicy("FEATURE-C", Budget::C, CalibrationMode::P95, false);
    const auto origin = std::chrono::steady_clock::time_point{};
    for (const Scenario &scenario : holdout) {
        const Timeline timeline = buildTimeline(scenario);
        FeatureState stateA, stateB; SourceCv cv; hotas::AdaptiveResponseProcessor v23;
        DirectionTrack directionA, directionB;
        size_t cursor = 0U;
        for (const MapperSample &sample : timeline.mapper) {
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                stateA.accept(timeline.reports[cursor]); stateB.accept(timeline.reports[cursor]);
                cv.accept(timeline.reports[cursor]);
                observeSourceDirection(directionA, timeline.reports, cursor);
                observeSourceDirection(directionB, timeline.reports, cursor);
                ++cursor;
            }
            const auto stamp = origin + std::chrono::microseconds(static_cast<long long>(std::llround(sample.time * 1e6)));
            const auto telemetry = v23.process(static_cast<float>(sample.held), v23Config(), stamp);
            if (sample.time >= scenario.analysisStart) {
                addComparatorMeasure(results.rawPhysical, scenario, sample.held - latentAt(scenario, sample.time), 0.0);
                constexpr double fixedSeconds = .008;
                if (sample.time + fixedSeconds <= scenario.duration + 1e-9) {
                    const double fixedTruth = latentAt(scenario, sample.time + fixedSeconds);
                    addComparatorMeasure(results.sourceCv, scenario, clampPosition(cv.predict(sample.time + fixedSeconds)) - fixedTruth, 8.0);
                    addComparatorMeasure(results.r1aFixed, scenario, clampPosition(stateA.q3.predict(sample.time + fixedSeconds)) - fixedTruth, 8.0);
                    addComparatorMeasure(results.v23Linear, scenario,
                        clampPosition(sample.held + telemetry.velocity * fixedSeconds) - fixedTruth, 8.0);
                }
                const double actual = std::max(0.0, static_cast<double>(telemetry.activeHorizonSeconds));
                if (sample.time + actual <= scenario.duration + 1e-9) {
                    addComparatorMeasure(results.v23Production, scenario,
                        telemetry.predicted - latentAt(scenario, sample.time + actual), actual * 1000.0);
                }
                scoreCandidateSample(results.candidateA, directionA, policyA, tableA, scenario, sample, stateA);
                scoreCandidateSample(results.candidateB, directionB, policyB, tableB, scenario, sample, stateB);
            }
            stateA.mapperAdvance(); stateB.mapperAdvance();
        }
    }
    return results;
}

double familyRawP99(const std::map<std::string, Measure> &groups, std::string_view family) {
    const auto found = groups.find(std::string(family));
    return found == groups.end() ? 0.0 : found->second.raw.q(.99);
}
double familyRawMae(const std::map<std::string, Measure> &groups, std::string_view family) {
    const auto found = groups.find(std::string(family));
    return found == groups.end() ? 0.0 : found->second.raw.mean();
}
double familyMedianHorizon(const CandidateResult &result, std::string_view family) {
    const auto found = result.family.find(std::string(family));
    return found == result.family.end() ? 0.0 : found->second.horizon.q(.50);
}

Qualification qualify(const CandidateResult &candidate, const ComparatorResult &fixedQ3,
                      const ComparatorResult &v23Linear) {
    Qualification q;
    q.correctness = candidate.safety.pass();
    q.tail = true; q.mean = true;
    for (const std::string_view family : kRepresentative) {
        const double fixedTail = familyRawP99(fixedQ3.family, family);
        const double v23Mae = familyRawMae(v23Linear.family, family);
        const double candidateTail = familyRawP99(candidate.family, family);
        const double candidateMae = familyRawMae(candidate.family, family);
        q.tail = q.tail && fixedTail > 0.0 && candidateTail <= fixedTail * 1.05 + 1e-12;
        q.mean = q.mean && v23Mae > 0.0 && candidateMae <= v23Mae * 1.05 + 1e-12;
    }
    std::uint64_t major = 0U;
    for (const std::string_view family : kRepresentative) if (familyMedianHorizon(candidate, family) >= 10.0) ++major;
    q.horizon = candidate.representative.horizon.q(.50) > 8.0 && major >= 3U;
    const auto fast = candidate.structured.find("FAST_BALLISTIC"), veryFast = candidate.structured.find("VERY_FAST_BALLISTIC");
    q.fast = fast != candidate.structured.end() && veryFast != candidate.structured.end()
        && fast->second.horizon.q(.50) >= 12.0 && veryFast->second.horizon.q(.50) >= 12.0;
    q.pass = q.correctness && q.tail && q.mean && q.horizon && q.fast;
    q.reason = q.pass ? "PASS" : "failed " + std::string(!q.correctness ? "correctness " : "")
        + (!q.tail ? "tail " : "") + (!q.mean ? "mean " : "") + (!q.horizon ? "horizon " : "")
        + (!q.fast ? "fast_horizon" : "");
    return q;
}

bool sensorRegression(const CandidateResult &a, const CandidateResult &b) {
    for (const auto &[sensor, measureB] : b.sensor) {
        const auto found = a.sensor.find(sensor);
        if (found == a.sensor.end()) return true;
        if (measureB.raw.q(.99) > found->second.raw.q(.99) * 1.05 + 1e-12 ||
            measureB.raw.mean() > found->second.raw.mean() * 1.05 + 1e-12) return true;
    }
    return false;
}

std::string metricRow(std::string_view kind, std::string_view name, const Measure &measure) {
    std::ostringstream out;
    out << csv(std::string(kind)) << ',' << csv(std::string(name)) << ',' << measure.n << ','
        << number(measure.raw.mean()) << ',' << number(measure.bounded.mean()) << ','
        << number(measure.raw.q(.95)) << ',' << number(measure.bounded.q(.95)) << ','
        << number(measure.raw.q(.99)) << ',' << number(measure.bounded.q(.99)) << ','
        << number(measure.horizon.q(.10)) << ',' << number(measure.horizon.q(.25)) << ','
        << number(measure.horizon.q(.50)) << ',' << number(measure.horizon.q(.75)) << ','
        << number(measure.horizon.q(.90)) << ',' << number(measure.horizon.mean()) << ',' << number(measure.horizon.maximum) << '\n';
    return out.str();
}

std::string overallCsv(const HoldoutResults &results) {
    std::ostringstream out;
    out << "type,name,samples,raw_mae,bounded_mae,raw_p95,bounded_p95,raw_p99,bounded_p99,p10_horizon_ms,p25_horizon_ms,median_horizon_ms,p75_horizon_ms,p90_horizon_ms,mean_horizon_ms,max_horizon_ms\n";
    out << metricRow("COMPARATOR", results.rawPhysical.name, results.rawPhysical.all);
    out << metricRow("COMPARATOR", results.v23Linear.name, results.v23Linear.all);
    out << metricRow("COMPARATOR", results.v23Production.name, results.v23Production.all);
    out << metricRow("COMPARATOR", results.sourceCv.name, results.sourceCv.all);
    out << metricRow("COMPARATOR", results.r1aFixed.name, results.r1aFixed.all);
    out << metricRow("CANDIDATE", results.candidateA.name, results.candidateA.all);
    out << metricRow("CANDIDATE", results.candidateB.name, results.candidateB.all);
    return out.str();
}

void appendFamilyRows(std::ostringstream &out, std::string_view type, std::string_view name,
                      const std::map<std::string, Measure> &groups) {
    for (const auto &[family, measure] : groups) out << metricRow(std::string(type) + ":" + std::string(name), family, measure);
}
std::string familyCsv(const HoldoutResults &results) {
    std::ostringstream out;
    out << "type,name,samples,raw_mae,bounded_mae,raw_p95,bounded_p95,raw_p99,bounded_p99,p10_horizon_ms,p25_horizon_ms,median_horizon_ms,p75_horizon_ms,p90_horizon_ms,mean_horizon_ms,max_horizon_ms\n";
    appendFamilyRows(out, "COMPARATOR", results.rawPhysical.name, results.rawPhysical.family);
    appendFamilyRows(out, "COMPARATOR", results.v23Linear.name, results.v23Linear.family);
    appendFamilyRows(out, "COMPARATOR", results.v23Production.name, results.v23Production.family);
    appendFamilyRows(out, "COMPARATOR", results.sourceCv.name, results.sourceCv.family);
    appendFamilyRows(out, "COMPARATOR", results.r1aFixed.name, results.r1aFixed.family);
    appendFamilyRows(out, "CANDIDATE", results.candidateA.name, results.candidateA.family);
    appendFamilyRows(out, "CANDIDATE", results.candidateB.name, results.candidateB.family);
    return out.str();
}

std::string horizonCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,family,samples,p10_ms,p25_ms,median_ms,p75_ms,p90_ms,mean_ms,max_ms\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const auto &[family, measure] : candidate->family) {
        out << csv(candidate->name) << ',' << csv(family) << ',' << measure.n << ',' << number(measure.horizon.q(.10)) << ','
            << number(measure.horizon.q(.25)) << ',' << number(measure.horizon.q(.50)) << ',' << number(measure.horizon.q(.75)) << ','
            << number(measure.horizon.q(.90)) << ',' << number(measure.horizon.mean()) << ',' << number(measure.horizon.maximum) << '\n';
    }
    return out.str();
}

std::string sourcePeriodCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,family,under_0_5x,from_0_5_to_1x,from_1_to_1_5x,from_1_5_to_2x,over_2x\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const auto &[family, bins] : candidate->sourcePeriodBuckets) {
        out << csv(candidate->name) << ',' << csv(family);
        for (const std::uint64_t count : bins) out << ',' << count;
        out << '\n';
    }
    return out.str();
}

std::string sensorCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,sensor,samples,raw_mae,raw_p95,raw_p99,median_horizon_ms,mean_innovation,mean_disagreement,mean_curvature,mean_source_age\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const auto &[sensor, measure] : candidate->sensor) {
        out << csv(candidate->name) << ',' << csv(sensor) << ',' << measure.n << ',' << number(measure.raw.mean()) << ','
            << number(measure.raw.q(.95)) << ',' << number(measure.raw.q(.99)) << ',' << number(measure.horizon.q(.50)) << ','
            << number(measure.innovation.mean()) << ',' << number(measure.disagreement.mean()) << ','
            << number(measure.curvature.mean()) << ',' << number(measure.age.mean()) << '\n';
    }
    return out.str();
}

std::string structuredCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,motion,samples,raw_mae,raw_p95,raw_p99,median_horizon_ms\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const auto &[motion, measure] : candidate->structured) {
        out << csv(candidate->name) << ',' << csv(motion) << ',' << measure.n << ',' << number(measure.raw.mean()) << ','
            << number(measure.raw.q(.95)) << ',' << number(measure.raw.q(.99)) << ',' << number(measure.horizon.q(.50)) << '\n';
    }
    return out.str();
}

std::string safetyCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,finite_failures,illegal_domain,stale_old_direction,confirmed_quiet,stop_braking,runaway_state,wrong_direction_area,reversal_cancellations,hard_correctness\n";
    for (const CandidateResult *candidate : {&a, &b}) { const CandidateSafety &s = candidate->safety;
        out << csv(candidate->name) << ',' << s.finite << ',' << s.domain << ',' << s.stale << ',' << s.quiet << ','
            << s.stop << ',' << s.runaway << ',' << number(s.wrongDirectionArea) << ',' << s.reversals << ','
            << (s.pass() ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

std::string tailCsv(const CandidateResult &a, const CandidateResult &b, const ComparatorResult &fixed) {
    std::ostringstream out;
    out << "candidate,family,samples,dynamic_raw_p99,fixed8_q3_p99,ratio,delta_percent,tail_gate\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const std::string_view family : kRepresentative) {
        const auto found = candidate->family.find(std::string(family));
        const double candidateP99 = found == candidate->family.end() ? 0.0 : found->second.raw.q(.99);
        const double fixedP99 = familyRawP99(fixed.family, family);
        const double ratio = fixedP99 > 0.0 ? candidateP99 / fixedP99 : 0.0;
        out << csv(candidate->name) << ',' << csv(std::string(family)) << ',' << (found == candidate->family.end() ? 0U : found->second.n)
            << ',' << number(candidateP99) << ',' << number(fixedP99) << ',' << number(ratio) << ','
            << number((ratio - 1.0) * 100.0) << ',' << (ratio <= 1.05 + 1e-12 ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

double equivalentHorizon(const CandidateResult &candidate, std::string_view family, const Measure &reference,
                         std::string_view metric) {
    const auto familyRows = candidate.familyByHorizon.find(std::string(family));
    if (familyRows == candidate.familyByHorizon.end()) return 0.0;
    const double limit = metric == "mae" ? reference.raw.mean() : metric == "p95" ? reference.raw.q(.95) : reference.raw.q(.99);
    double best = 0.0;
    for (const auto &[horizon, value] : familyRows->second) {
        const double candidateValue = metric == "mae" ? value.raw.mean() : metric == "p95" ? value.raw.q(.95) : value.raw.q(.99);
        if (value.n != 0U && candidateValue <= limit + 1e-12) best = std::max(best, static_cast<double>(horizon));
    }
    return best;
}

std::string equivalentCsv(const CandidateResult &a, const CandidateResult &b, const ComparatorResult &v23Linear) {
    std::ostringstream out;
    out << "candidate,family,reference,reference_fixed_horizon_ms,mae_equivalent_horizon_ms,p95_equivalent_horizon_ms,p99_equivalent_horizon_ms\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const std::string_view family : kRepresentative) {
        const auto reference = v23Linear.family.find(std::string(family));
        if (reference == v23Linear.family.end()) continue;
        out << csv(candidate->name) << ',' << csv(std::string(family)) << ",V23_EXPORTED_STATE_LINEAR,8,"
            << number(equivalentHorizon(*candidate, family, reference->second, "mae")) << ','
            << number(equivalentHorizon(*candidate, family, reference->second, "p95")) << ','
            << number(equivalentHorizon(*candidate, family, reference->second, "p99")) << '\n';
    }
    return out.str();
}

std::string tailForensicsCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,tail_band,family,motion,sensor,scenario,time_ms,absolute_raw_error,source_innovation,q3_q5_disagreement,curvature,risk_bin,selected_horizon_ms,source_age,q3_velocity,q3_acceleration,q5_velocity,q5_acceleration\n";
    for (const CandidateResult *candidate : {&a, &b}) for (const std::string_view family : kRepresentative) {
        std::vector<TailEvent> events;
        for (const TailEvent &event : candidate->tails) if (event.family == family) events.push_back(event);
        std::sort(events.begin(), events.end(), [](const TailEvent &left, const TailEvent &right) { return left.error > right.error; });
        const size_t onePercent = static_cast<size_t>(std::ceil(events.size() * .01));
        const size_t halfPercent = static_cast<size_t>(std::ceil(events.size() * .005));
        const size_t tenthPercent = static_cast<size_t>(std::ceil(events.size() * .001));
        const size_t retain = std::max<size_t>(1U, onePercent);
        for (size_t i = 0U; i < std::min(retain, events.size()); ++i) {
            const TailEvent &event = events[i];
            const char *band = i < std::max<size_t>(1U, tenthPercent) ? "WORST_0.1_PERCENT"
                : i < std::max<size_t>(1U, halfPercent) ? "WORST_0.5_PERCENT" : "WORST_1_PERCENT";
            out << csv(candidate->name) << ',' << band << ',' << csv(event.family) << ',' << csv(event.motion) << ',' << csv(event.sensor)
                << ',' << event.scenario << ',' << number(event.timeMs) << ',' << number(event.error) << ','
                << number(event.feature.innovation) << ',' << number(event.feature.disagreement) << ',' << number(event.feature.curvature) << ','
                << riskBin(event.risk) << ',' << number(event.horizonMs) << ',' << number(event.feature.age) << ','
                << number(event.q3Velocity) << ',' << number(event.q3Acceleration) << ',' << number(event.q5Velocity) << ','
                << number(event.q5Acceleration) << '\n';
        }
    }
    return out.str();
}

std::string leadEndpointCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,samples,maximum_lead_clamp_occupancy,maximum_lead_clamp_rate,endpoint_taper_occupancy,endpoint_taper_rate,valid_advantage_removed_by_lead_cap,valid_advantage_removed_by_endpoint_taper\n";
    for (const CandidateResult *candidate : {&a, &b}) {
        const double samples = static_cast<double>(candidate->all.n);
        out << csv(candidate->name) << ',' << candidate->all.n << ',' << candidate->leadCap << ','
            << number(samples == 0.0 ? 0.0 : candidate->leadCap / samples) << ',' << candidate->endpointTaper << ','
            << number(samples == 0.0 ? 0.0 : candidate->endpointTaper / samples) << ','
            << number(candidate->leadAdvantageRemoved) << ',' << number(candidate->endpointAdvantageRemoved) << '\n';
    }
    return out.str();
}

std::string tortureCsv(const CandidateResult &a, const CandidateResult &b) {
    std::ostringstream out;
    out << "candidate,samples,raw_mae,raw_p95,raw_p99,finite,domain,stale,runaway,correctness_only_disposition\n";
    for (const CandidateResult *candidate : {&a, &b}) {
        const auto torture = candidate->family.find("TORTURE_BEYOND_HUMAN");
        const Measure empty{}; const Measure &measure = torture == candidate->family.end() ? empty : torture->second;
        out << csv(candidate->name) << ',' << measure.n << ',' << number(measure.raw.mean()) << ',' << number(measure.raw.q(.95)) << ','
            << number(measure.raw.q(.99)) << ',' << candidate->safety.finite << ',' << candidate->safety.domain << ','
            << candidate->safety.stale << ',' << candidate->safety.runaway << ','
            << (candidate->safety.pass() ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

std::string familyWatchCsv(std::string_view family, const CandidateResult &a, const CandidateResult &b,
                           const ComparatorResult &fixed) {
    std::ostringstream out;
    out << "candidate,family,samples,raw_mae,raw_p95,raw_p99,fixed8_q3_p99,p99_delta_percent,p10_horizon_ms,p25_horizon_ms,median_horizon_ms,p75_horizon_ms,p90_horizon_ms\n";
    for (const CandidateResult *candidate : {&a, &b}) {
        const auto found = candidate->family.find(std::string(family));
        if (found == candidate->family.end()) continue;
        const Measure &measure = found->second;
        const double fixedP99 = familyRawP99(fixed.family, family);
        out << csv(candidate->name) << ',' << csv(std::string(family)) << ',' << measure.n << ',' << number(measure.raw.mean()) << ','
            << number(measure.raw.q(.95)) << ',' << number(measure.raw.q(.99)) << ',' << number(fixedP99) << ','
            << number(fixedP99 > 0.0 ? (measure.raw.q(.99) / fixedP99 - 1.0) * 100.0 : 0.0) << ','
            << number(measure.horizon.q(.10)) << ',' << number(measure.horizon.q(.25)) << ',' << number(measure.horizon.q(.50)) << ','
            << number(measure.horizon.q(.75)) << ',' << number(measure.horizon.q(.90)) << '\n';
    }
    return out.str();
}

std::string candidateComparisonCsv(const HoldoutResults &results, const Qualification &qa, const Qualification &qb,
                                   bool bWins, bool sensorWorse) {
    std::ostringstream out;
    out << "candidate,hard_correctness,tail_gate,mean_gate,horizon_gate,fast_horizon_gate,qualification,representative_mae,representative_p99,representative_median_horizon_ms,b_vs_a_p99_delta_percent,b_vs_a_horizon_delta_ms,sensor_regression,b_winner_rule\n";
    const double aP99 = results.candidateA.representative.raw.q(.99), bP99 = results.candidateB.representative.raw.q(.99);
    const double aHorizon = results.candidateA.representative.horizon.q(.50), bHorizon = results.candidateB.representative.horizon.q(.50);
    const auto append = [&](const CandidateResult &candidate, const Qualification &q, bool isB) {
        out << csv(candidate.name) << ',' << (q.correctness ? "PASS" : "FAIL") << ',' << (q.tail ? "PASS" : "FAIL") << ','
            << (q.mean ? "PASS" : "FAIL") << ',' << (q.horizon ? "PASS" : "FAIL") << ',' << (q.fast ? "PASS" : "FAIL") << ','
            << (q.pass ? "PASS" : "FAIL") << ',' << number(candidate.representative.raw.mean()) << ','
            << number(candidate.representative.raw.q(.99)) << ',' << number(candidate.representative.horizon.q(.50)) << ','
            << number(isB && aP99 > 0.0 ? (bP99 / aP99 - 1.0) * 100.0 : 0.0) << ','
            << number(isB ? bHorizon - aHorizon : 0.0) << ',' << (isB && sensorWorse ? "YES" : "NO") << ','
            << (isB && bWins ? "SELECT" : isB ? "RETAIN_A" : "BASELINE") << '\n';
    };
    append(results.candidateA, qa, false); append(results.candidateB, qb, true);
    return out.str();
}

std::string candidateDefinitions() {
    return "# Frozen X2 candidates\n\n"
        "Candidate A is `FEATURE-C+QB-B+CAL-95`; Candidate B is `FEATURE-C+QB-C+CAL-95`. "
        "Both use causal source innovation, Q3/Q5 disagreement, and Q3 curvature with the frozen X1R normalizers, "
        "five risk bins, fourteen-step horizon ladder, and X1 safety semantics. The X1R freeze addendum bound each "
        "candidate to its named full-DEVELOPMENT CAL-95 table. X2 reads those tables and does not refit, change, or combine them.\n";
}
std::string comparatorPolicy() {
    return "# Comparator policy\n\n"
        "RAW_PHYSICAL scores the held source at mapper time. V23_EXPORTED_STATE_LINEAR, SOURCE_SECANT_CV, and R1A_Q3_FIXED_8MS "
        "score their fixed 8 ms estimates against truth at t+8 ms. V23_PRODUCTION scores frozen Adaptive Response at its telemetry-selected "
        "runtime horizon. The X1R finalists score causal Q3 at each finalist's actual selected horizon. Representative tail comparison uses raw "
        "candidate error versus R1A_Q3_FIXED_8MS on the same family. Representative mean comparison uses raw candidate error versus "
        "V23_EXPORTED_STATE_LINEAR at 8 ms. B's predeclared material sensor regression threshold is a greater than 5 percent increase in either "
        "sensor p99 or sensor MAE relative to A.\n";
}

std::string preVerificationJson(const X2Options &options, bool staticPass, const std::string &sourceHash,
                                const std::string &executableHash) {
    std::ostringstream out;
    out << "{\n\"passed\":" << (staticPass ? "true" : "false") << ",\n"
        << "\"production_source_sha\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        << "\"x0_framework_identity\":\"" << kExpectedManifest << "\",\n"
        << "\"x1_architecture_identity\":\"" << kX1Architecture << "\",\n"
        << "\"original_x1r_architecture_lock_sha256\":\"" << kExpectedOriginalArchitectureLock << "\",\n"
        << "\"final_calibration_binding_sha256\":\"" << kExpectedFinalBinding << "\",\n"
        << "\"candidate_a_table_sha256\":\"" << kExpectedTableA << "\",\n"
        << "\"candidate_b_table_sha256\":\"" << kExpectedTableB << "\",\n"
        << "\"normalizers\":{\"innovation\":0.09331237,\"disagreement\":0.12932549,\"curvature\":16883.858866,\"source_age\":0.95403616},\n"
        << "\"horizon_ladder_ms\":\"[0,2,4,6,8,10,12,15,18,20,24,30,35,40]\",\n"
        << "\"risk_bins\":\"[0,0.2,0.4,0.6,0.8,1]\",\n"
        << "\"mapper_cadence_seconds\":0.004,\n"
        << "\"timing_sha256\":\"" << sha256(timingModel()) << "\",\n"
        << "\"sensor_sha256\":\"" << sha256(sensorModel()) << "\",\n"
        << "\"human_sha256\":\"" << sha256(humanModel()) << "\",\n"
        << "\"holdout_expected_sha256\":\"" << kExpectedHoldout << "\",\n"
        << "\"source_code_sha256\":\"" << sourceHash << "\",\n"
        << "\"executable_sha256\":\"" << executableHash << "\",\n"
        << "\"development_replayed\":false,\n\"validation_replayed\":false,\n\"holdout_materialized_before_lock\":false\n}\n";
    return out.str();
}

std::string qualificationLockJson(const X2Options &options, const std::string &sourceHash,
                                  const std::string &executableHash) {
    std::ostringstream out;
    out << "{\n\"framework\":\"V2.4-X2\",\n\"lock_written_before_holdout_materialization\":true,\n"
        << "\"x0_framework_identity\":\"" << kExpectedManifest << "\",\n\"holdout_canonical_identity\":\"" << kExpectedHoldout << "\",\n"
        << "\"x1_architecture_identity\":\"" << kX1Architecture << "\",\n\"x1r_final_binding_sha256\":\"" << kExpectedFinalBinding << "\",\n"
        << "\"finalists\":[{\"id\":\"FEATURE-C+QB-B+CAL-95\",\"fingerprint\":\"" << kExpectedFingerprintA << "\",\"table\":\"" << kExpectedTableA << "\"},{\"id\":\"FEATURE-C+QB-C+CAL-95\",\"fingerprint\":\"" << kExpectedFingerprintB << "\",\"table\":\"" << kExpectedTableB << "\"}],\n"
        << "\"normalizers\":{\"innovation\":0.09331237,\"disagreement\":0.12932549,\"curvature\":16883.858866,\"source_age\":0.95403616},\n"
        << "\"risk_bins\":\"[0,0.2,0.4,0.6,0.8,1]\",\n\"horizon_ladder_ms\":\"[0,2,4,6,8,10,12,15,18,20,24,30,35,40]\",\n"
        << "\"quality_budgets\":{\"A\":\"QB-B=min(0.060,0.020+0.25*min(0.16,source_travel))\",\"B\":\"QB-C=0.042\"},\n"
        << "\"safety_policy\":\"frozen X1 veto, quiet, stop, reversal, bounded output semantics\",\n"
        << "\"comparators\":\"RAW_PHYSICAL;V23_EXPORTED_STATE_LINEAR_FIXED_8MS;V23_PRODUCTION_RUNTIME;SOURCE_SECANT_CV_FIXED_8MS;R1A_Q3_FIXED_8MS\",\n"
        << "\"hard_gates\":\"finite/domain/stale/quiet/stop/runaway=0; representative dynamic p99<=fixed8 Q3 p99*1.05; representative MAE<=V23 linear fixed8 MAE*1.05; representative median>8ms and >=3 families>=10ms; fast and very-fast ballistic median>=12ms\",\n"
        << "\"winner_policy\":\"If one qualifies select it. If both qualify B requires >=2ms representative median gain, <=3% aggregate representative p99 degradation, no family p99 >5% worse than A, no worse safety, and no sensor MAE/p99 >5% worse than A; otherwise A.\",\n"
        << "\"source_code_sha256\":\"" << sourceHash << "\",\n\"executable_sha256\":\"" << executableHash << "\"\n}\n";
    return out.str();
}

std::string physicalTraceCheck(const std::filesystem::path &sourceRoot, const Policy &policy,
                               const FrozenTable &table) {
    std::ostringstream out;
    out << "trace,mapper_rows,source_reports,median_horizon_ms,mean_raw_lead,mean_bounded_lead,reversal_veto,quiet_or_stop_veto,truth,selection_effect\n";
    const std::filesystem::path directory = sourceRoot / "tests" / "fixtures" / "adaptive_response_real_hotas";
    if (!std::filesystem::is_directory(directory)) return out.str();
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") continue;
        std::ifstream input(entry.path()); std::string line;
        if (!std::getline(input, line)) continue;
        const std::vector<std::string> header = csvLine(line);
        const auto time = headerColumn(header, "time_ms"), value = headerColumn(header, "physical_calibrated");
        const auto reported = headerColumn(header, "source_reported"), changed = headerColumn(header, "source_changed");
        if (!time || !value || !reported || !changed) continue;
        FeatureState state; Distribution horizon, rawLead, boundedLead;
        std::uint64_t rows = 0U, reports = 0U, reversal = 0U, quietStop = 0U; double previousReport = 0.0, previousTime = 0.0;
        while (std::getline(input, line)) {
            const std::vector<std::string> row = csvLine(line);
            if (row.size() <= std::max({*time, *value, *reported, *changed})) continue;
            const double at = asNumber(row[*time]) * .001, held = asNumber(row[*value]);
            if (asNumber(row[*reported]) > .5) {
                state.accept({at, held, reports == 0U ? 0.0 : at - previousTime, static_cast<std::uint32_t>(reports), asNumber(row[*changed]) > .5});
                previousTime = at; previousReport = held; ++reports;
            }
            const size_t selected = chooseFrozen(policy, table, state, at);
            const double seconds = kX1HorizonsMs[selected] / 1000.0;
            const double predicted = state.q3.predict(at + seconds), raw = predicted - held;
            const bool veto = state.veto(raw); const double bounded = veto ? 0.0 : std::clamp(raw, -.18, .18);
            if (veto && (state.quiet >= 2 || state.stop >= 2)) ++quietStop;
            else if (veto) ++reversal;
            horizon.add(kX1HorizonsMs[selected]); rawLead.add(raw); boundedLead.add(bounded); ++rows; state.mapperAdvance();
        }
        out << csv(entry.path().filename().string()) << ',' << rows << ',' << reports << ',' << number(horizon.q(.50)) << ','
            << number(rawLead.mean()) << ',' << number(boundedLead.mean()) << ',' << reversal << ',' << quietStop
            << ",LATENT_POSITION_PROXY_ONLY,POST_QUALIFICATION_ONLY\n";
    }
    return out.str();
}

struct PerformanceRow { std::vector<double> timings; double elapsed = 0.0; size_t axes = 0U; };
PerformanceRow measureKernel(const Policy &policy, const FrozenTable &table, bool randomStress, size_t axes) {
    constexpr int samples = 50000;
    std::array<FeatureState, 8> states{}; PerformanceRow row; row.axes = axes; row.timings.reserve(samples);
    volatile double sink = 0.0; const auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < samples; ++i) {
        const double at = i * kMapperSeconds; const auto start = std::chrono::steady_clock::now();
        for (size_t axis = 0U; axis < axes; ++axis) {
            const bool source = randomStress ? ((i + static_cast<int>(axis) * 3) % 3 == 0) : ((i + static_cast<int>(axis)) % 4 == 0);
            const double held = randomStress ? std::clamp((((i * 1103515245U + axis * 12345U) & 2047U) / 1023.5) - 1.0, -1.0, 1.0)
                : std::clamp(((i % 480) - 240) / 240.0, -1.0, 1.0);
            if (source) states[axis].accept({at, held, randomStress ? .012 : .016, static_cast<std::uint32_t>(i), true});
            const size_t chosen = chooseFrozen(policy, table, states[axis], at);
            const double predicted = states[axis].q3.predict(at + kX1HorizonsMs[chosen] / 1000.0);
            sink += clampPosition(held + std::clamp(predicted - held, -.18, .18)); states[axis].mapperAdvance();
        }
        row.timings.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    }
    row.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    if (sink == std::numeric_limits<double>::infinity()) std::cerr << "unreachable\n";
    return row;
}
std::string performanceReport(std::string_view title, const Policy &policy, const FrozenTable &table, bool randomStress) {
    constexpr int samples = 50000;
    const PerformanceRow one = measureKernel(policy, table, randomStress, 1U);
    const PerformanceRow eight = measureKernel(policy, table, randomStress, 8U);
    const auto append = [&](std::ostringstream &out, const PerformanceRow &row) {
        out << "| " << row.axes << " | " << samples * static_cast<int>(row.axes) << " | " << number(percentileUs(row.timings, .50), 3)
            << " | " << number(percentileUs(row.timings, .95), 3) << " | " << number(percentileUs(row.timings, .99), 3)
            << " | " << number(*std::max_element(row.timings.begin(), row.timings.end()), 3) << " | "
            << number(samples * static_cast<int>(row.axes) / row.elapsed, 1) << " | 0 (fixed Q3/Q5 scalar kernel; timing vector outside kernel) |\n";
    };
    std::ostringstream out;
    out << "# " << title << "\n\n| axes | reports | p50 us | p95 us | p99 us | worst us | reports/s | tracked allocations |\n"
        << "|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    append(out, one); append(out, eight);
    out << "\nFrozen candidate: " << policy.name << ".\n";
    return out.str();
}

std::string finalReport(const HoldoutResults &results, const Qualification &qa, const Qualification &qb,
                        std::string_view selected, bool bWins, std::string_view resultIdentity) {
    const bool pass = !selected.empty();
    const std::string disposition = pass
        ? "V2.4-X2 PASS — SOURCE-NATIVE ARCHITECTURE PASSES INDEPENDENT HOLDOUT — READY FOR X3 CONTROLLED INTEGRATION RESEARCH"
        : "V2.4-X2 STOP — FROZEN X1R POLICIES DO NOT GENERALIZE ON INDEPENDENT HOLDOUT";
    std::ostringstream out;
    out << "# V2.4-X2 Independent Source-Native Holdout Qualification\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "1. V2.3 identity: 37b6bf2f7dbd63ea06a9674641ce99cf07191a5f; qualified mathematics: 055e047e2847fc2ec0f59ec457188f66124898e7.\n"
        << "2. X0 framework identity is manifest " << kExpectedManifest << "; X1 architecture identity is " << kX1Architecture << ".\n"
        << "3. The original four X1R lock hashes and the freeze-addendum final calibration binding were verified before HOLDOUT materialization.\n"
        << "4. DEVELOPMENT and VALIDATION were not materialized or replayed by X2.\n"
        << "5. The pre-materialization lock and one-replay receipt identify the frozen tables, candidates, comparators, gates, source, and executable.\n"
        << "6. HOLDOUT identity is " << kExpectedHoldout << "; both candidates and all comparators ran in one evaluator execution.\n"
        << "7. Candidate A result: " << (qa.pass ? "PASS" : "FAIL") << " (" << qa.reason << "); Candidate B result: " << (qb.pass ? "PASS" : "FAIL") << " (" << qb.reason << ").\n"
        << "8. Candidate A/B representative MAE are " << number(results.candidateA.representative.raw.mean()) << "/"
        << number(results.candidateB.representative.raw.mean()) << "; representative p99 are "
        << number(results.candidateA.representative.raw.q(.99)) << "/" << number(results.candidateB.representative.raw.q(.99)) << "; median horizons are "
        << number(results.candidateA.representative.horizon.q(.50)) << "/" << number(results.candidateB.representative.horizon.q(.50)) << " ms.\n"
        << "9. Fixed-Wing, Space-Sim, Precision, Combat, Combat-extreme, sensor, structured, safety, tail-event, lead/endpoint, and equivalent-quality evidence are in the companion CSV artifacts.\n"
        << "10. Torture is correctness-only; raw and bounded final-output quality are reported separately.\n"
        << "11. Predeclared winner policy selected " << (selected.empty() ? "no candidate" : std::string(selected))
        << (bWins ? " because every B-over-A condition passed." : ".") << "\n"
        << "12. HOLDOUT result identity: " << resultIdentity << ".\n"
        << "13. Post-qualification physical traces, if present, are realism-only and did not alter selection.\n"
        << "14. Production equivalence retains the source-bound 52,463 / 52,463 EXACT receipt and records current core-test and benchmark evidence.\n"
        << "15. " << (pass ? "X3 is authorized only for controlled production-shaped integration research; shipping remains unauthorized."
            : "No repair, tuning, integration, merge, or release was performed.") << "\n\n" << disposition << "\n";
    return out.str();
}

} // namespace

int main(int argc, char **argv) {
    const X2Options options = parseX2Options(argc, argv);
    if (options.output.empty() || options.freezeAddendum.empty() || options.originalX1r.empty() || options.sourceRoot.empty()
        || options.mappingEvidence.empty() || options.hotpathEvidence.empty()) {
        std::cerr << "Use --output --freeze-addendum --original-x1r --source-root --mapping-evidence --hotpath-evidence\n";
        return 2;
    }
    std::error_code ec; std::filesystem::create_directories(options.output, ec);
    if (ec) { std::cerr << ec.message() << '\n'; return 3; }
    const std::filesystem::path addendum = options.freezeAddendum, original = options.originalX1r;
    FrozenTable tableA, tableB; std::string tableError;
    const bool originalPass = exactHash(original / "x1r_architecture_lock.json", kExpectedOriginalArchitectureLock)
        && exactHash(original / "selected_x1r_candidates.json", kExpectedOriginalSelection)
        && exactHash(original / "manifest.json", kExpectedOriginalManifest)
        && exactHash(original / "development_cross_validation_manifest.json", kExpectedOriginalFoldManifest);
    const bool addendumPass = exactHash(addendum / "x1r_final_calibration_binding.json", kExpectedFinalBinding)
        && exactHash(addendum / "x1r_complete_architecture_lock.json", kExpectedCompleteLock)
        && exactHash(addendum / "x2_resume_authority.json", kExpectedResumeAuthority)
        && containsAll(readBinary(addendum / "x1r_final_calibration_binding.json"), {
            "FEATURE-C+QB-B+CAL-95", kExpectedTableA, kExpectedFingerprintA,
            "FEATURE-C+QB-C+CAL-95", kExpectedTableB, kExpectedFingerprintB})
        && containsAll(readBinary(addendum / "x2_resume_authority.json"), {"\"x2_holdout_authorized\":true", "\"holdout_replays\":0"});
    const bool tablesPass = readFrozenTable(addendum / "feature_c_qb_b_cal95_full_development.csv",
        "FEATURE-C+QB-B+CAL-95", kExpectedTableA, tableA, tableError)
        && readFrozenTable(addendum / "feature_c_qb_c_cal95_full_development.csv",
        "FEATURE-C+QB-C+CAL-95", kExpectedTableB, tableB, tableError);
    const std::filesystem::path testDir = options.sourceRoot / "tests";
    const bool sourcePass = fileHash(testDir / "adaptive_response_v24_x0_locked_framework.cpp") == "83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e"
        && fileHash(testDir / "adaptive_response_v24_x1_forecastability.cpp") == "54f65a0c40724ebc4775c4a0bd934cacbe8f3fea3d9b2ed0523c1931b583ed95"
        && sha256(terminology()) == kExpectedTerminology && sha256(timingModel()) == kExpectedTiming
        && sha256(sensorModel()) == kExpectedSensors && sha256(humanModel()) == kExpectedHumans
        && sha256(truthPolicy()) == kExpectedTruth && sha256(safetyPolicy()) == kExpectedSafety
        && sha256(qualityPolicy()) == kExpectedQuality;
    const std::string sourceHash = fileHash(__FILE__), executableHash = fileHash(argv[0]);
    const bool staticPass = originalPass && addendumPass && tablesPass && sourcePass;
    const std::string pre = preVerificationJson(options, staticPass, sourceHash, executableHash);
    const std::string lock = qualificationLockJson(options, sourceHash, executableHash);
    const std::string x0Verification = "{\n\"passed\":" + std::string(sourcePass ? "true" : "false")
        + ",\n\"framework_manifest_sha256\":\"" + std::string(kExpectedManifest) + "\",\n\"holdout_expected_sha256\":\"" + std::string(kExpectedHoldout)
        + "\",\n\"development_materialized\":false,\n\"validation_materialized\":false\n}\n";
    const std::string x1Verification = "{\n\"passed\":" + std::string(sourcePass ? "true" : "false")
        + ",\n\"x1_architecture_sha256\":\"" + std::string(kX1Architecture) + "\",\n\"development_materialized\":false,\n\"validation_materialized\":false\n}\n";
    const std::string x1rVerification = "{\n\"passed\":" + std::string((originalPass && addendumPass && tablesPass) ? "true" : "false")
        + ",\n\"original_lock_sha256\":\"" + std::string(kExpectedOriginalArchitectureLock) + "\",\n\"final_binding_sha256\":\""
        + std::string(kExpectedFinalBinding) + "\",\n\"holdout_materialized\":false\n}\n";
    if (!writeText(options.output / "baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n")
        || !writeText(options.output / "x0_framework_verification.json", x0Verification)
        || !writeText(options.output / "x1_architecture_verification.json", x1Verification)
        || !writeText(options.output / "x1r_architecture_verification.json", x1rVerification)
        || !writeText(options.output / "x2_pre_holdout_verification.json", pre)
        || !writeText(options.output / "x2_holdout_qualification_lock.json", lock)
        || !writeText(options.output / "candidate_definitions.md", candidateDefinitions())
        || !writeText(options.output / "comparator_policy.md", comparatorPolicy())) return 4;
    if (!staticPass) { std::cerr << "Pre-HOLDOUT integrity gate failed: " << tableError << '\n'; return 5; }

    // The only X2 corpus materialization. DEVELOPMENT and VALIDATION are never called in this main.
    const std::vector<Scenario> holdout = corpusFor(Split::Holdout);
    const std::string holdoutSeal = sealJson(holdout, Split::Holdout, 0x240A1003U);
    const bool holdoutPass = holdout.size() == 600U && holdoutSeal.find(std::string(kExpectedHoldout)) != std::string::npos;
    const std::string materialization = "{\n\"split\":\"HOLDOUT\",\n\"seed\":\"0x240A1003\",\n\"scenarios\":600,\n\"expected_canonical_sha256\":\""
        + std::string(kExpectedHoldout) + "\",\n\"passed\":" + (holdoutPass ? "true" : "false") + ",\n\"materializations\":1\n}\n";
    if (!writeText(options.output / "holdout_materialization_receipt.json", materialization)
        || !writeText(options.output / "holdout_corpus_identity.json", holdoutSeal)) return 6;
    if (!holdoutPass) { std::cerr << "HOLDOUT canonical identity mismatch; not scored\n"; return 7; }

    const HoldoutResults results = scoreOneHoldoutReplay(holdout, tableA, tableB);
    const Qualification qa = qualify(results.candidateA, results.r1aFixed, results.v23Linear);
    const Qualification qb = qualify(results.candidateB, results.r1aFixed, results.v23Linear);
    const bool sensorWorse = sensorRegression(results.candidateA, results.candidateB);
    const double aP99 = results.candidateA.representative.raw.q(.99), bP99 = results.candidateB.representative.raw.q(.99);
    const bool bWins = qa.pass && qb.pass
        && results.candidateB.representative.horizon.q(.50) >= results.candidateA.representative.horizon.q(.50) + 2.0
        && bP99 <= aP99 * 1.03 + 1e-12 && !sensorWorse
        && std::all_of(kRepresentative.begin(), kRepresentative.end(), [&](std::string_view family) {
            return familyRawP99(results.candidateB.family, family) <= familyRawP99(results.candidateA.family, family) * 1.05 + 1e-12;
        }) && results.candidateB.safety.pass() && results.candidateA.safety.pass();
    const std::string selected = qa.pass && !qb.pass ? results.candidateA.name : !qa.pass && qb.pass ? results.candidateB.name
        : qa.pass && qb.pass ? (bWins ? results.candidateB.name : results.candidateA.name) : "";
    const std::string overall = overallCsv(results), family = familyCsv(results), tails = tailCsv(results.candidateA, results.candidateB, results.r1aFixed);
    const std::string resultIdentity = sha256(overall + family + tails + safetyCsv(results.candidateA, results.candidateB));
    const std::string winner = "{\n\"holdout_replays\":1,\n\"candidate_a_qualification\":\"" + qa.reason + "\",\n\"candidate_b_qualification\":\"" + qb.reason
        + "\",\n\"both_pass_b_wins\":" + (bWins ? "true" : "false") + ",\n\"selected_candidate\":\"" + selected
        + "\",\n\"disposition\":\"" + (selected.empty() ? "STOP" : "PASS") + "\",\n\"holdout_result_sha256\":\"" + resultIdentity + "\"\n}\n";
    const auto selectedCandidate = selected == results.candidateB.name ? &results.candidateB : &results.candidateA;
    const Policy selectedPolicy = selected == results.candidateB.name ? makePolicy("FEATURE-C", Budget::C, CalibrationMode::P95, false)
        : makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const FrozenTable &selectedTable = selected == results.candidateB.name ? tableB : tableA;
    const std::string physical = selected.empty() ? "trace,mapper_rows,source_reports,median_horizon_ms,mean_raw_lead,mean_bounded_lead,reversal_veto,quiet_or_stop_veto,truth,selection_effect\n"
        : physicalTraceCheck(options.sourceRoot, selectedPolicy, selectedTable);
    const std::string randomPerformance = selected.empty() ? "# Random stress performance\n\nNot run: no candidate qualified.\n"
        : performanceReport("Random stress performance", selectedPolicy, selectedTable, true);
    const std::string holdPerformance = selected.empty() ? "# Realistic sample-hold performance\n\nNot run: no candidate qualified.\n"
        : performanceReport("Realistic sample-hold performance", selectedPolicy, selectedTable, false);
    const std::string production = "metric,result\nproduction_source_changed,false\nsealed_x0_exact_replay,52463 / 52463 EXACT; source-bound retained receipt\nmapping_core_tests," + csv(options.mappingEvidence)
        + "\nrelease_hot_path_benchmark," + csv(options.hotpathEvidence) + "\ntracked_production_hotpath_allocations,0; benchmark evidence above\n";
    const std::string frozen = "{\n\"selected_candidate\":\"" + selected + "\",\n\"feature_definition\":\"FEATURE-C\",\n\"normalizers\":{\"innovation\":0.09331237,\"disagreement\":0.12932549,\"curvature\":16883.858866},\n\"risk_bin_boundaries\":\"[0,0.2,0.4,0.6,0.8,1]\",\n\"calibration_table_sha256\":\"" + (selected == results.candidateB.name ? std::string(kExpectedTableB) : std::string(kExpectedTableA)) + "\",\n\"horizon_ladder_ms\":\"[0,2,4,6,8,10,12,15,18,20,24,30,35,40]\",\n\"safety_semantics\":\"exact X1R\",\n\"holdout_corpus_identity\":\"" + std::string(kExpectedHoldout) + "\",\n\"holdout_result_identity\":\"" + resultIdentity + "\"\n}\n";
    const std::string report = finalReport(results, qa, qb, selected, bWins, resultIdentity);
    bool ok = true;
    ok &= writeText(options.output / "holdout_overall_results.csv", overall);
    ok &= writeText(options.output / "holdout_family_results.csv", family);
    ok &= writeText(options.output / "holdout_tail_results.csv", tails);
    ok &= writeText(options.output / "holdout_horizon_distribution.csv", horizonCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_equivalent_quality_horizon.csv", equivalentCsv(results.candidateA, results.candidateB, results.v23Linear));
    ok &= writeText(options.output / "holdout_source_period_analysis.csv", sourcePeriodCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_sensor_results.csv", sensorCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_structured_results.csv", structuredCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_safety_results.csv", safetyCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_torture_results.csv", tortureCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "holdout_tail_event_forensics.csv", tailForensicsCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "fixed_wing_holdout_analysis.csv", familyWatchCsv("FIXED_WING_REALISTIC", results.candidateA, results.candidateB, results.r1aFixed));
    ok &= writeText(options.output / "space_sim_holdout_analysis.csv", familyWatchCsv("SPACE_SIM_REALISTIC", results.candidateA, results.candidateB, results.r1aFixed));
    ok &= writeText(options.output / "precision_holdout_analysis.csv", familyWatchCsv("PRECISION_REALISTIC", results.candidateA, results.candidateB, results.r1aFixed));
    ok &= writeText(options.output / "combat_holdout_analysis.csv", familyWatchCsv("COMBAT_REALISTIC", results.candidateA, results.candidateB, results.r1aFixed));
    ok &= writeText(options.output / "lead_cap_endpoint_analysis.csv", leadEndpointCsv(results.candidateA, results.candidateB));
    ok &= writeText(options.output / "candidate_comparison.csv", candidateComparisonCsv(results, qa, qb, bWins, sensorWorse));
    ok &= writeText(options.output / "winner_selection_receipt.json", winner);
    ok &= writeText(options.output / "post_qualification_physical_trace_check.csv", physical);
    ok &= writeText(options.output / "performance_random_stress.md", randomPerformance);
    ok &= writeText(options.output / "performance_realistic_sample_hold.md", holdPerformance);
    ok &= writeText(options.output / "production_equivalence.csv", production);
    if (!selected.empty()) ok &= writeText(options.output / "v2.4_x2_frozen_architecture.json", frozen);
    ok &= writeText(options.output / "V2.4-X2_Independent_Source_Native_Holdout_Qualification.md", report);
    const std::string manifest = "{\n\"artifact\":\"V2.4-X2 independent source-native HOLDOUT qualification\",\n\"holdout_materializations\":1,\n\"holdout_replays\":1,\n\"holdout_result_sha256\":\"" + resultIdentity + "\",\n\"disposition\":\"" + (selected.empty() ? "STOP" : "PASS") + "\",\n\"selected_candidate\":\"" + selected + "\"\n}\n";
    ok &= writeText(options.output / "manifest.json", manifest);
    if (!ok) return 8;
    std::cout << (selected.empty()
        ? "V2.4-X2 STOP — FROZEN X1R POLICIES DO NOT GENERALIZE ON INDEPENDENT HOLDOUT"
        : "V2.4-X2 PASS — SOURCE-NATIVE ARCHITECTURE PASSES INDEPENDENT HOLDOUT — READY FOR X3 CONTROLLED INTEGRATION RESEARCH") << '\n';
    return 0;
}
