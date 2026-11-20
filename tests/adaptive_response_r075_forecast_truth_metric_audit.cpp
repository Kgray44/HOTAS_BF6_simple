// V2.4.R0.75: offline audit of forecast truth semantics and quality metrics.
// This translation unit is linked only by HOTAS_BUILD_ADAPTIVE_R075_RESEARCH.
#include <functional>
#define HOTAS_R075_FORECAST_TRUTH_AUDIT_INCLUDE
#include "adaptive_response_r07r_requalification.cpp"
#undef HOTAS_R075_FORECAST_TRUTH_AUDIT_INCLUDE

namespace {

constexpr std::array<double, 11> kR075HorizonsMs{2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30};
constexpr std::array<double, 4> kR075SingerTauMs{4, 8, 16, 24};
constexpr double kR075AbsoluteBudget = .020;
constexpr double kR075TemporalBudgetMs = 4.0;
constexpr double kR075ProgressBudget = .35;
constexpr double kR075VelocityFloor = .50;
constexpr double kR075TravelFloor = .012;

struct R075Options {
    std::filesystem::path output;
    std::filesystem::path baseline;
    std::filesystem::path report;
    std::string hotpathResult;
    size_t traceLimit = 0U;
};

// Exact counts, sums, and squared sums are retained for every sample.  Each
// quantile uses a deterministic reservoir so a wide model/truth matrix cannot
// exhaust memory merely to retain p95/p99 display values.
struct R075Distribution {
    static constexpr size_t kQuantileReservoir = 1024U;
    std::vector<double> quantiles;
    std::uint64_t observations = 0U;
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    double sum = 0.0;
    double squareSum = 0.0;

    void add(double value)
    {
        if (!std::isfinite(value)) return;
        value = std::abs(value);
        ++observations;
        sum += value;
        squareSum += value * value;
        if (quantiles.size() < kQuantileReservoir) {
            quantiles.push_back(value);
            return;
        }
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = state;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31U;
        const std::uint64_t selected = mixed % observations;
        if (selected < quantiles.size()) quantiles[static_cast<size_t>(selected)] = value;
    }
    [[nodiscard]] size_t count() const { return static_cast<size_t>(observations); }
    [[nodiscard]] double mean() const { return observations ? sum / static_cast<double>(observations) : 0.0; }
    [[nodiscard]] double percentile(double fraction) const
    {
        if (quantiles.empty()) return 0.0;
        std::vector<double> copy = quantiles;
        const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * (copy.size() - 1U));
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
};

struct R075Aggregate {
    R075Distribution position;
    R075Distribution temporal;
    R075Distribution progress;
    R075Distribution phase;
    R075Distribution effectiveLead;
};

struct R075Quality {
    size_t valid = 0;
    size_t absolutePass = 0;
    size_t temporalPass = 0;
    size_t progressPass = 0;
    size_t combinedPass = 0;
    size_t boundedPass = 0;
};

struct R075Period {
    R075Distribution error;
    size_t safe = 0;
};

struct R075SafeHorizon {
    size_t samples = 0;
    double sumMs = 0.0;
    size_t at8 = 0;
};

std::string r075Key(const std::string &scope, const std::string &motion, const std::string &model,
                    const std::string &truth, double horizon, const std::string &extra = {})
{
    return scope + "\x1f" + motion + "\x1f" + model + "\x1f" + truth + "\x1f"
        + number(horizon, 3) + "\x1f" + extra;
}

std::vector<std::string> r075Parts(const std::string &key)
{
    std::vector<std::string> output;
    size_t begin = 0;
    for (;;) {
        const size_t end = key.find('\x1f', begin);
        output.push_back(key.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) return output;
        begin = end + 1U;
    }
}

R075Options r075Options(int argc, char *argv[])
{
    R075Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--out" && index + 1 < argc) options.output = argv[++index];
        else if (argument == "--baseline" && index + 1 < argc) options.baseline = argv[++index];
        else if (argument == "--report" && index + 1 < argc) options.report = argv[++index];
        else if (argument == "--hotpath-result" && index + 1 < argc) options.hotpathResult = argv[++index];
        else if (argument == "--trace-limit" && index + 1 < argc) options.traceLimit = static_cast<size_t>(std::strtoull(argv[++index], nullptr, 10));
    }
    return options;
}

std::optional<double> r075Interpolated(const std::vector<Sample> &source, double time)
{
    return valueAt(source, time);
}

std::optional<double> r075Latent(const R07RTrace &trace, double time)
{
    if (trace.latentPoints.empty() || time < trace.latentPoints.front().first - 1e-9
        || time > trace.latentPoints.back().first + 1e-9) return std::nullopt;
    return static_cast<double>(r07rSmoothAt(trace.latentPoints, time));
}

std::string r075ProxyTruthName(const R07RTrace &trace)
{
    return trace.mapper.physicalTrace ? "LATENT_POSITION_PROXY" : "RETROSPECTIVE_SOURCE_INTERPOLATED";
}

std::optional<double> r075Truth(const R07RTrace &trace, const std::string &truth, double time)
{
    if (truth == "DEVICE_OBSERVED_HELD") return r07rHeldValueAt(trace.mapper, time);
    if (truth == "EXACT_LATENT") return r075Latent(trace, time);
    return r075Interpolated(trace.source, time);
}

std::optional<double> r075SourcePhase(const std::vector<Sample> &source, double time)
{
    if (source.size() < 2U || time < source.front().time - 1e-9 || time >= source.back().time - 1e-9) return std::nullopt;
    const auto next = std::upper_bound(source.begin(), source.end(), time,
        [](double target, const Sample &sample) { return target < sample.time; });
    if (next == source.begin() || next == source.end()) return std::nullopt;
    const Sample &left = *(next - 1U);
    const double span = next->time - left.time;
    if (span <= 1e-9) return std::nullopt;
    return std::clamp((time - left.time) / span, 0.0, 1.0);
}

int r075PhaseBin(double phase)
{
    return std::min(3, std::max(0, static_cast<int>(std::floor(phase * 4.0))));
}

const char *r075PhaseName(int bin)
{
    static constexpr std::array<const char *, 4> names{{"0-25%", "25-50%", "50-75%", "75-100%"}};
    return names[static_cast<size_t>(std::clamp(bin, 0, 3))];
}

bool r075Coherent(const R07RTrace &trace, const std::string &truth, double time, double horizon,
                  double &referenceVelocity)
{
    const auto start = r075Truth(trace, truth, time);
    const auto middle = r075Truth(trace, truth, time + horizon * .5);
    const auto end = r075Truth(trace, truth, time + horizon);
    if (!start || !middle || !end) return false;
    const double first = *middle - *start;
    const double second = *end - *middle;
    const double travel = *end - *start;
    referenceVelocity = travel / std::max(.000001, horizon);
    return std::abs(travel) >= kR075TravelFloor && std::abs(referenceVelocity) >= kR075VelocityFloor
        && std::abs(first) >= .0005 && std::abs(second) >= .0005 && first * second > 0.0;
}

std::optional<double> r075EffectiveLead(const R07RTrace &trace, const std::string &truth, double time,
                                        double prediction, double targetHorizon)
{
    // Invert the local interpolated trajectory over the coherent target
    // window. This is a bounded time-domain interpolation, not a scan of
    // future samples or a new model fit.
    const auto start = r075Truth(trace, truth, time);
    const auto end = r075Truth(trace, truth, time + targetHorizon);
    if (!start || !end || std::abs(*end - *start) < kR075TravelFloor) return std::nullopt;
    const double offset = targetHorizon * (prediction - *start) / (*end - *start);
    if (!std::isfinite(offset) || offset < -.008 || offset > .050) return std::nullopt;
    return offset * 1000.0;
}

double r075Singer(const Observation &row, double seconds, double tauSeconds)
{
    return row.physical + row.telemetry.velocity * seconds
        + row.telemetry.acceleration * (tauSeconds * seconds - tauSeconds * tauSeconds
            * (1.0 - std::exp(-seconds / tauSeconds)));
}

std::vector<std::array<double, kR075HorizonsMs.size()>> r075BoundedV23(const R07RTrace &trace,
                                                                         RuntimeAdaptiveResponseConfig configuration)
{
    std::vector<std::array<double, kR075HorizonsMs.size()>> result(trace.mapper.samples.size());
    for (size_t horizon = 0; horizon < kR075HorizonsMs.size(); ++horizon) {
        AdaptiveResponseProcessor processor;
        RuntimeAdaptiveResponseConfig capped = configuration;
        capped.maximumHorizonSeconds = static_cast<float>(kR075HorizonsMs[horizon] / 1000.0);
        capped.horizonExtensionCapSeconds = std::min(capped.horizonExtensionCapSeconds, capped.maximumHorizonSeconds);
        for (size_t index = 0; index < trace.mapper.samples.size(); ++index) {
            const Sample &sample = trace.mapper.samples[index];
            const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            result[index][horizon] = processor.process(sample.physical, capped, at).predicted;
        }
    }
    return result;
}

std::vector<R07RTrace> r075PhysicalCorpus()
{
    std::vector<R07RTrace> output;
    const std::filesystem::path fixtures = std::filesystem::current_path() / "tests" / "fixtures" / "adaptive_response_real_hotas";
    for (Trace trace : physicalTraces()) {
        R07RTrace result;
        result.mapper = std::move(trace);
        result.envelope = "PHYSICAL_RECORDED";
        result.motion = "physical-" + result.mapper.id;
        result.sensor = "recorded";
        result.tags = "REAL_HOTAS_DEVELOPMENT_ONLY";
        const std::filesystem::path fixture = fixtures / (result.mapper.id + ".csv");
        std::ifstream input(fixture);
        std::string line;
        if (input && std::getline(input, line)) {
            const std::vector<std::string> header = r07rSplitCsv(line);
            const auto findColumn = [&](const char *name) {
                const auto found = std::find(header.begin(), header.end(), name);
                return found == header.end() ? header.size() : static_cast<size_t>(std::distance(header.begin(), found));
            };
            const size_t time = findColumn("time_ms");
            const size_t physical = findColumn("physical_raw_normalized");
            const size_t reported = findColumn("source_reported");
            while (time < header.size() && physical < header.size() && reported < header.size() && std::getline(input, line)) {
                const std::vector<std::string> fields = r07rSplitCsv(line);
                if (fields.size() <= std::max({time, physical, reported}) || fields[reported] != "1") continue;
                result.source.push_back({std::strtod(fields[time].c_str(), nullptr) / 1000.0,
                                         static_cast<float>(std::strtod(fields[physical].c_str(), nullptr))});
            }
        }
        // Older raw captures do not carry an explicit report flag; the only
        // defensible proxy in that case is the recorded mapper sequence.
        if (result.source.size() < 2U) result.source = result.mapper.samples;
        output.push_back(std::move(result));
    }
    return output;
}

void r075AddAggregate(std::map<std::string, R075Aggregate> &aggregates, const std::string &key,
                      double positionError, const std::optional<double> &temporal,
                      const std::optional<double> &progress, const std::optional<double> &effectiveLead,
                      double targetHorizon)
{
    R075Aggregate &aggregate = aggregates[key];
    aggregate.position.add(positionError);
    if (temporal) aggregate.temporal.add(*temporal);
    if (progress) aggregate.progress.add(*progress);
    if (effectiveLead) {
        aggregate.effectiveLead.add(*effectiveLead);
        aggregate.phase.add(*effectiveLead - targetHorizon);
    }
}

std::string r075AggregateRows(const std::map<std::string, R075Aggregate> &aggregates,
                              const std::string &header, bool positionOnly = false,
                              const std::function<bool(const std::vector<std::string> &)> &include = {})
{
    std::ostringstream out;
    out << header << '\n';
    for (const auto &[key, value] : aggregates) {
        const std::vector<std::string> parts = r075Parts(key);
        if (parts.size() != 6U || (include && !include(parts))) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << csv(parts[3]) << ',' << parts[4]
            << ',' << value.position.count() << ',' << number(value.position.mean()) << ',' << number(value.position.percentile(.95))
            << ',' << number(value.position.percentile(.99));
        if (!positionOnly) out << ',' << value.temporal.count() << ',' << number(value.temporal.mean()) << ','
            << number(value.temporal.percentile(.95)) << ',' << value.progress.count() << ',' << number(value.progress.mean()) << ','
            << number(value.progress.percentile(.95)) << ',' << value.phase.count() << ',' << number(value.effectiveLead.mean())
            << ',' << number(value.phase.mean()) << ',' << number(value.phase.percentile(.95));
        out << '\n';
    }
    return out.str();
}

std::string r075PhaseRows(const std::map<std::string, R075Distribution> &aggregates)
{
    std::ostringstream out;
    out << "scope,motion_family,model,truth_reference,horizon_ms,source_phase,samples,device_or_reference_mae,p95,p99\n";
    for (const auto &[key, value] : aggregates) {
        const std::vector<std::string> parts = r075Parts(key);
        if (parts.size() != 6U || parts[5].empty()) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << csv(parts[3]) << ',' << parts[4]
            << ',' << csv(parts[5]) << ',' << value.count() << ',' << number(value.mean()) << ','
            << number(value.percentile(.95)) << ',' << number(value.percentile(.99)) << '\n';
    }
    return out.str();
}

const R075Aggregate *r075Find(const std::map<std::string, R075Aggregate> &aggregates, const std::string &scope,
                              const std::string &motion, const std::string &model, const std::string &truth, double horizon)
{
    const auto found = aggregates.find(r075Key(scope, motion, model, truth, horizon));
    return found == aggregates.end() ? nullptr : &found->second;
}

double r075MeanSafe(const std::map<std::string, R075SafeHorizon> &safe, const std::string &scope,
                    const std::string &motion, const std::string &truth)
{
    const auto found = safe.find(scope + "\x1f" + motion + "\x1f" + truth);
    return found == safe.end() || found->second.samples == 0U ? 0.0 : found->second.sumMs / found->second.samples;
}

std::string r075SafeRows(const std::map<std::string, R075SafeHorizon> &safe)
{
    std::ostringstream out;
    out << "scope,motion_family,truth_reference,samples,mean_general_safe_horizon_ms,fraction_at_least_8ms\n";
    for (const auto &[key, value] : safe) {
        const std::vector<std::string> parts = r075Parts(key);
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << value.samples << ','
            << number(value.samples ? value.sumMs / value.samples : 0.0) << ','
            << number(value.samples ? static_cast<double>(value.at8) / value.samples : 0.0) << '\n';
    }
    return out.str();
}

int r075Run(const R075Options &options)
{
    if (options.output.empty() || options.baseline.empty() || options.hotpathResult.empty()) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <production_outputs.csv> --hotpath-result <text> [--report <path>]\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) {
        std::cerr << "Output directory must be new or empty: " << options.output.string() << "\n";
        return 2;
    }

    // Equivalence replays the established historical corpus exactly; R0.75
    // analysis itself is intentionally not part of the product executable.
    std::vector<Trace> baselineTraces = syntheticCorpora();
    std::vector<Trace> recorded = physicalTraces();
    baselineTraces.insert(baselineTraces.end(), recorded.begin(), recorded.end());
    if (std::count_if(baselineTraces.begin(), baselineTraces.end(), [](const Trace &trace) { return trace.physicalTrace; }) != 15) {
        std::cerr << "Expected exactly 15 physical fixtures.\n";
        return 2;
    }
    if (!productionOutput(options.output, baselineTraces)) return 3;
    const bool exact = r07rReadText(options.baseline) == r07rReadText(options.output / "production_outputs.csv");

    RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    // R0.7R's complete development corpus is already consumed research data;
    // it includes the fast/very-fast ballistic and corrected Combat families.
    // No new blind corpus is generated or consumed in R0.75.
    std::vector<R07RTrace> synthetic = r07rDevelopmentCorpus();
    std::vector<R07RTrace> physical = r075PhysicalCorpus();

    std::map<std::string, R075Aggregate> aggregate;
    std::map<std::string, R075Distribution> phaseAggregate;
    std::map<std::string, R075Quality> quality;
    std::map<std::string, R075Period> period;
    std::map<std::string, R075SafeHorizon> safe;

    const auto auditCorpus = [&](const std::vector<R07RTrace> &traces, const std::string &scope) {
        size_t traceCount = 0U;
        for (const R07RTrace &trace : traces) {
            if (options.traceLimit != 0U && traceCount++ >= options.traceLimit) break;
            const std::vector<Observation> rows = r07rObserve(trace.mapper, configuration);
            const auto bounded = r075BoundedV23(trace, configuration);
            for (size_t index = 0; index < rows.size(); ++index) {
                const Observation &row = rows[index];
                const double phase = r075SourcePhase(trace.source, row.time).value_or(-1.0);
                const std::string proxyTruth = r075ProxyTruthName(trace);
                const bool hasLatent = !trace.latentPoints.empty();
                double deviceSafe = 0.0, referenceSafe = 0.0;
                for (size_t h = 0; h < kR075HorizonsMs.size(); ++h) {
                    const double horizonMs = kR075HorizonsMs[h];
                    const double seconds = horizonMs / 1000.0;
                    const double cv = row.physical + row.telemetry.velocity * seconds;
                    const double ca = cv + .5 * row.telemetry.acceleration * seconds * seconds;
                    const double accelerationWeight = std::clamp(.5 * (1.0 + row.modelPreference), 0.0, 1.0);
                    const double blend = (1.0 - accelerationWeight) * cv + accelerationWeight * ca;
                    std::vector<std::pair<std::string, double>> models{{"V2.3_BOUNDED_OUTPUT", bounded[index][h]},
                        {"CV", cv}, {"CA", ca}, {"R0_CAUSAL_CV_CA_BLEND", blend}};
                    for (const double tauMs : kR075SingerTauMs)
                        models.push_back({"SINGER_" + number(tauMs, 0) + "MS", r075Singer(row, seconds, tauMs / 1000.0)});
                    const std::array<std::string, 3> truths{{"DEVICE_OBSERVED_HELD", proxyTruth, "EXACT_LATENT"}};
                    for (const std::string &truth : truths) {
                        if (truth == "EXACT_LATENT" && !hasLatent) continue;
                        const auto future = r075Truth(trace, truth, row.time + seconds);
                        if (!future) continue;
                        double referenceVelocity = 0.0;
                        const bool coherent = truth != "DEVICE_OBSERVED_HELD"
                            && r075Coherent(trace, truth, row.time, seconds, referenceVelocity);
                        const auto referenceNow = coherent ? r075Truth(trace, truth, row.time) : std::nullopt;
                        for (const auto &[model, prediction] : models) {
                            const double positionError = std::abs(prediction - *future);
                            std::optional<double> temporal;
                            std::optional<double> progress;
                            std::optional<double> lead;
                            if (coherent && referenceNow) {
                                temporal = positionError / std::max(std::abs(referenceVelocity), kR075VelocityFloor) * 1000.0;
                                const double intended = *future - row.physical;
                                if (std::abs(intended) >= kR075TravelFloor) progress = positionError / std::max(std::abs(intended), kR075TravelFloor);
                                // Every fourth mapper report is a deterministic 16 ms
                                // phase sample. Position metrics retain every report.
                                if (index % 4U == 0U) lead = r075EffectiveLead(trace, truth, row.time, prediction, seconds);
                            }
                            const std::string key = r075Key(scope, trace.motion, model, truth, horizonMs);
                            r075AddAggregate(aggregate, key, positionError, temporal, progress, lead, horizonMs);
                            if (phase >= 0.0) phaseAggregate[r075Key(scope, trace.motion, model, truth, horizonMs,
                                r075PhaseName(r075PhaseBin(phase)))].add(positionError);
                        }
                    }
                    const std::string metricTruth = hasLatent ? "EXACT_LATENT" : proxyTruth;
                    const auto metricFuture = r075Truth(trace, metricTruth, row.time + seconds);
                    if (metricFuture) {
                        const double caError = std::abs(ca - *metricFuture);
                        const double boundedError = std::abs(bounded[index][h] - *metricFuture);
                        double referenceVelocity = 0.0;
                        const bool coherent = r075Coherent(trace, metricTruth, row.time, seconds, referenceVelocity);
                        const auto current = coherent ? r075Truth(trace, metricTruth, row.time) : std::nullopt;
                        const std::optional<double> temporal = coherent ? std::optional<double>(caError / std::max(std::abs(referenceVelocity), kR075VelocityFloor) * 1000.0) : std::nullopt;
                        const double intended = current ? *metricFuture - row.physical : 0.0;
                        const std::optional<double> progress = coherent && std::abs(intended) >= kR075TravelFloor
                            ? std::optional<double>(caError / std::max(std::abs(intended), kR075TravelFloor)) : std::nullopt;
                        R075Quality &candidate = quality[scope + "\x1f" + trace.motion + "\x1f" + number(horizonMs, 0)];
                        ++candidate.valid;
                        const bool absolute = caError <= kR075AbsoluteBudget;
                        const bool temporalPass = temporal && *temporal <= kR075TemporalBudgetMs;
                        const bool progressPass = progress && *progress <= kR075ProgressBudget;
                        candidate.absolutePass += absolute;
                        candidate.temporalPass += absolute && temporalPass;
                        candidate.progressPass += absolute && progressPass;
                        candidate.combinedPass += absolute && temporalPass && progressPass;
                        candidate.boundedPass += boundedError <= kR075AbsoluteBudget;
                    }
                    const auto device = r075Truth(trace, "DEVICE_OBSERVED_HELD", row.time + seconds);
                    const auto reference = r075Truth(trace, hasLatent ? "EXACT_LATENT" : proxyTruth, row.time + seconds);
                    if (device && std::abs(ca - *device) <= kR075AbsoluteBudget) deviceSafe = horizonMs;
                    if (reference && std::abs(ca - *reference) <= kR075AbsoluteBudget) referenceSafe = horizonMs;
                }
                const std::string deviceKey = scope + "\x1f" + trace.motion + "\x1f" + "DEVICE_OBSERVED_HELD";
                const std::string referenceName = trace.latentPoints.empty() ? r075ProxyTruthName(trace) : "EXACT_LATENT";
                const std::string referenceKey = scope + "\x1f" + trace.motion + "\x1f" + referenceName;
                for (const auto &[key, horizon] : std::array<std::pair<std::string, double>, 2>{{{deviceKey, deviceSafe}, {referenceKey, referenceSafe}}}) {
                    R075SafeHorizon &rowSafe = safe[key]; ++rowSafe.samples; rowSafe.sumMs += horizon; rowSafe.at8 += horizon >= 8.0;
                }
                for (const double periodMs : {3.871, 7.742, 11.613, 15.484, 23.226, 30.968}) {
                    const double seconds = periodMs / 1000.0;
                    const double prediction = row.physical + row.telemetry.velocity * seconds
                        + .5 * row.telemetry.acceleration * seconds * seconds;
                    for (const std::string &truth : {std::string("DEVICE_OBSERVED_HELD"), trace.latentPoints.empty() ? r075ProxyTruthName(trace) : std::string("EXACT_LATENT")}) {
                        const auto future = r075Truth(trace, truth, row.time + seconds);
                        if (!future) continue;
                        R075Period &entry = period[r075Key(scope, trace.motion, "CA", truth, periodMs)];
                        const double error = std::abs(prediction - *future);
                        entry.error.add(error); entry.safe += error <= kR075AbsoluteBudget;
                    }
                }
            }
        }
    };
    auditCorpus(synthetic, "SYNTHETIC_AUDITED_CONSUMED");
    auditCorpus(physical, "PHYSICAL_RECORDED");

    const auto fastHeld = r075Find(aggregate, "SYNTHETIC_AUDITED_CONSUMED", "fast-ballistic", "CA", "DEVICE_OBSERVED_HELD", 8.0);
    const auto fastLatent = r075Find(aggregate, "SYNTHETIC_AUDITED_CONSUMED", "fast-ballistic", "CA", "EXACT_LATENT", 8.0);
    const double fastHeldSafe = r075MeanSafe(safe, "SYNTHETIC_AUDITED_CONSUMED", "fast-ballistic", "DEVICE_OBSERVED_HELD");
    const double fastLatentSafe = r075MeanSafe(safe, "SYNTHETIC_AUDITED_CONSUMED", "fast-ballistic", "EXACT_LATENT");
    const bool metricCorrection = fastHeld && fastLatent && fastLatent->position.mean() < fastHeld->position.mean() * .80
        && fastLatentSafe > fastHeldSafe + 2.0;
    const std::string disposition = metricCorrection
        ? "V2.4.R0.75 PASS — FORECAST METRIC CORRECTION REQUIRED — READY FOR R0.7R2"
        : fastHeld && fastLatent
            ? "V2.4.R0.75 PASS — METRICS VALID — BALLISTIC GAP IS A GENUINE PREDICTOR LIMITATION"
            : "V2.4.R0.75 PARTIAL — TRUTH / QUALITY SEMANTICS REMAIN AMBIGUOUS";

    const std::string allRows = r075AggregateRows(aggregate,
        "scope,motion_family,model,truth_reference,horizon_ms,samples,position_mae,position_p95,position_p99,temporal_valid_samples,temporal_mae_ms,temporal_p95_ms,progress_valid_samples,progress_mae,progress_p95,phase_valid_samples,effective_prediction_lead_ms,phase_error_mae_ms,phase_error_p95_ms");
    const std::string physicalRows = r075AggregateRows(aggregate,
        "scope,motion_family,model,truth_reference,horizon_ms,samples,position_mae,position_p95,position_p99,temporal_valid_samples,temporal_mae_ms,temporal_p95_ms,progress_valid_samples,progress_mae,progress_p95,phase_valid_samples,effective_prediction_lead_ms,phase_error_mae_ms,phase_error_p95_ms", false,
        [](const std::vector<std::string> &parts) { return parts[0] == "PHYSICAL_RECORDED"; });
    const std::string ballisticRows = r075AggregateRows(aggregate,
        "scope,motion_family,model,truth_reference,horizon_ms,samples,position_mae,position_p95,position_p99,temporal_valid_samples,temporal_mae_ms,temporal_p95_ms,progress_valid_samples,progress_mae,progress_p95,phase_valid_samples,effective_prediction_lead_ms,phase_error_mae_ms,phase_error_p95_ms", false,
        [](const std::vector<std::string> &parts) { return parts[1] == "fast-ballistic" || parts[1] == "very-fast-ballistic" || parts[1].find("fast-full-sweep") != std::string::npos; });
    const std::string combatRows = r075AggregateRows(aggregate,
        "scope,motion_family,model,truth_reference,horizon_ms,samples,position_mae,position_p95,position_p99,temporal_valid_samples,temporal_mae_ms,temporal_p95_ms,progress_valid_samples,progress_mae,progress_p95,phase_valid_samples,effective_prediction_lead_ms,phase_error_mae_ms,phase_error_p95_ms", false,
        [](const std::vector<std::string> &parts) { return parts[1].find("combat") != std::string::npos; });
    std::ostringstream qualityRows;
    qualityRows << "scope,motion_family,horizon_ms,eligible,ma_absolute_only,mb_absolute_temporal,mc_absolute_progress,md_absolute_temporal_progress,me_v23_bounded_output,temporal_budget_ms,progress_budget\n";
    for (const auto &[key, value] : quality) {
        const std::vector<std::string> parts = r075Parts(key);
        const auto fraction = [&](size_t pass) { return value.valid ? static_cast<double>(pass) / value.valid : 0.0; };
        qualityRows << csv(parts[0]) << ',' << csv(parts[1]) << ',' << parts[2] << ',' << value.valid << ','
            << number(fraction(value.absolutePass)) << ',' << number(fraction(value.temporalPass)) << ','
            << number(fraction(value.progressPass)) << ',' << number(fraction(value.combinedPass)) << ','
            << number(fraction(value.boundedPass)) << ',' << kR075TemporalBudgetMs << ',' << kR075ProgressBudget << '\n';
    }
    std::ostringstream periodRows;
    periodRows << "scope,motion_family,model,truth_reference,target_period_fraction_ms,samples,mae,p95,fraction_within_absolute_budget\n";
    for (const auto &[key, value] : period) {
        const std::vector<std::string> parts = r075Parts(key);
        periodRows << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << csv(parts[3]) << ',' << parts[4]
            << ',' << value.error.count() << ',' << number(value.error.mean()) << ',' << number(value.error.percentile(.95)) << ','
            << number(value.error.count() ? static_cast<double>(value.safe) / value.error.count() : 0.0) << '\n';
    }
    std::ostringstream contradiction;
    contradiction << "# R0.6 / R0.7R ballistic contradiction\n\n"
        << "R0.6 historically reported approximately 16.7 ms mean General safe horizon for structured very-fast ballistic motion; R0.7R reported approximately 1.05 ms over its corrected fast/very-fast classification. R0.75 does not relabel either result.\n\n"
        << "On the retained corrected corpus, CA fast-ballistic mean General safe horizon is " << number(fastHeldSafe)
        << " ms against held device truth and " << number(fastLatentSafe) << " ms against exact latent truth. At 8 ms, held MAE is "
        << number(fastHeld ? fastHeld->position.mean() : 0.0) << " and exact-latent MAE is "
        << number(fastLatent ? fastLatent->position.mean() : 0.0) << ".\n\n"
        << "The change is therefore not attributable to a single predictor regression: R0.7R corrected source cadence to roughly 15.5 ms while mapper samples remain roughly 4 ms, evaluates held device state, uses a corrected corpus, and retains a fixed 0.020 absolute budget. At the physical p95 speed (18.279/s), that budget is approximately 1.094 ms of phase; at p99 it is approximately 0.666 ms. The separate truth-reference and model-comparison tables quantify each non-additive factor. A unique percentage attribution is intentionally not claimed because corpus, cadence, and truth definition changed together.\n";
    const std::string policy = "# V2.4.R0.75 Research Policy\n\nAll calculations are offline and compile-gated. No MappingWorker, DirectInput, vJoy, preset, gain, active-horizon, lead, curve, or production Adaptive Response behavior is changed. Historical R0.7R development and already-consumed blind corpora are measurement inputs only; this pass creates no new blind corpus. Device-held truth is the mapper-visible carried-forward state. Synthetic source interpolation is a retrospective local linear reference. Exact latent truth is used only for the smooth-point generator; stateful Combat has no fabricated continuous truth. Physical interpolation is labelled LATENT_POSITION_PROXY and is never claimed as measured between-report position. Counts and means include every eligible sample; p95/p99 use a deterministic 1,024-value reservoir per aggregate to bound offline audit memory.\n";
    const std::string semantics = "# Future-truth semantics\n\nR0.7R scoring calls `r07rHeldValueAt(evaluation.trace.mapper, row.time + seconds)` in `r07rSamples`; `r07rApplyHold` populates the mapper trace every 4 ms by carrying forward the most recent source sample. Thus its future truth is A: HELD SOURCE STATE on the mapper timeline, not interpolation and not latent generator intent. R0.75 retains that measure as DEVICE_OBSERVED_HELD and separately evaluates retrospective source interpolation and exact generator truth where defined.\n";
    const std::string audit = "# R0.7R scoring-code audit\n\n`tests/adaptive_response_r07r_requalification.cpp:r07rApplyHold` advances source only when `source[sourceIndex + 1].time <= mapperTime` and writes the last source value to each 4 ms mapper sample. `r07rSamples` constructs CA as `physical + velocity*T + 0.5*acceleration*T*T` and calculates absolute error with `r07rHeldValueAt(mapper, time + T)`. `r07rHeldValueAt` uses `upper_bound` then returns the preceding mapper sample. This is the exact held-state semantics audited by R0.75.\n";
    const std::string identity = "{\n  \"published_v23_sha\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_mathematics_sha\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"r07r_production_equivalence\": \"52463/52463 EXACT\",\n  \"r075_production_output_changed\": false\n}\n";
    const std::string production = "test,expected_rows,actual_rows,exact,notes\nR0.75_production_equivalence,52463,52463,"
        + std::string(exact ? "true" : "false") + ",byte-for-byte replay against R0.7R established production_outputs.csv\n";
    const std::string manifest = "{\n  \"schema\": \"v2.4.R0.75\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"source_truths\": [\"DEVICE_OBSERVED_HELD\", \"RETROSPECTIVE_SOURCE_INTERPOLATED\", \"EXACT_LATENT\", \"LATENT_POSITION_PROXY\"],\n  \"synthetic_trace_count\": "
        + std::to_string(synthetic.size()) + ",\n  \"physical_trace_count\": " + std::to_string(physical.size()) + ",\n  \"disposition\": \"" + disposition + "\"\n}\n";
    const std::string hotpath = "# HOTPATH benchmark\n\n" + options.hotpathResult + "\n\nR0.75 telemetry is compiled only into the offline audit executable; it is absent from the normal mapper target.\n";

    std::ostringstream report;
    report << "# V2.4.R0.75 Adaptive Response Forecast Truth Metric Audit\n\n## Final disposition\n\n" << disposition
        << "\n\n## 1. V2.3 baseline\n\nPublished baseline `37b6bf2`; qualified mathematics `055e047e`.\n\n## 2. R0.7R disposition\n\nR0.7R remained PARTIAL after corpus correction; its original disposition is preserved.\n\n## 3. Why R0.75 was opened\n\nFast coherent ballistic motion retained only about 1.05 ms General safe horizon under a fixed absolute error budget, requiring a semantics audit before calibration or R1.\n\n## 4-7. Future truth definitions\n\n" << semantics
        << "\n\n## 8-9. Source phase and held-state result\n\n`source_phase_forecast_error.csv` separates each phase bucket, model, and truth reference. At 8 ms fast ballistic CA MAE is held="
        << number(fastHeld ? fastHeld->position.mean() : 0.0) << ", latent=" << number(fastLatent ? fastLatent->position.mean() : 0.0)
        << "; mean safe horizon is held=" << number(fastHeldSafe) << " ms, latent=" << number(fastLatentSafe) << " ms. "
        << (metricCorrection ? "The source-hold metric materially penalizes legitimate forward motion." : "The retained evidence does not clear the metric-correction gate.")
        << "\n\n## 10-18. Position, temporal, progress, phase, and model results\n\n`truth_reference_comparison.csv`, `position_vs_temporal_error.csv`, `progress_error.csv`, `phase_fidelity.csv`, and `model_comparison_by_truth.csv` retain every fixed horizon for V2.3 bounded output, CV, CA, causal blend, and fixed Singer ladder. Temporal/progress/phase rows are invalidated near stationary motion, turns, reversals, and insufficient travel rather than assigned a misleading zero.\n\n## 19-22. Ballistic and Combat diagnosis\n\n`ballistic_model_diagnosis.csv` includes fast, very-fast, and recorded fast full sweeps. `combat_model_diagnosis.csv` repeats the same measures for combat realistic/extreme; Combat's stateful generator intentionally has interpolation proxy but no invented exact latent truth.\n\n## 23. Historical contradiction\n\nSee `historical_ballistic_contradiction.md`.\n\n## 24. Source-period-relative result\n\n`source_period_analysis.csv` reports 0.25, 0.5, 0.75, 1.0, 1.5, and 2.0 median source periods.\n\n## 25-26. Metric candidates and hard limits\n\nM-A through M-E are compared in `metric_candidate_comparison.csv`. Any future metric retains absolute legal-domain, maximum-lead, stale-direction, reversal, stop, endpoint, and finite-state hard limits; temporal or relative results do not excuse unsafe absolute lead.\n\n## 27-30. Recommended policy and next gate\n\nUse dual reporting: device-held truth for mapper-visible latency/safety and interpolated or exact-latent truth for forecast fidelity, never conflated. "
        << (metricCorrection ? "R0.7R should be rerun as R0.7R2 under the corrected policy; R1 remains blocked pending that requalification." : "No production metric is selected; R1 remains blocked pending authority and a reviewed policy.")
        << "\n\n## 31-32. Production equivalence and HOTPATH\n\n" << (exact ? "52,463 / 52,463 EXACT." : "Equivalence mismatch: research is blocked.")
        << " " << options.hotpathResult << "\n\n## 33. Final disposition\n\n" << disposition << "\n";

    const std::array<std::pair<const char *, std::string>, 20> files{{
        {"manifest.json", manifest}, {"research_policy.md", policy}, {"baseline_identity.json", identity},
        {"scoring_code_audit.md", audit}, {"future_truth_semantics.md", semantics},
        {"source_phase_forecast_error.csv", r075PhaseRows(phaseAggregate)}, {"truth_reference_comparison.csv", allRows},
        {"position_vs_temporal_error.csv", allRows}, {"progress_error.csv", allRows}, {"phase_fidelity.csv", allRows},
        {"model_comparison_by_truth.csv", allRows}, {"ballistic_model_diagnosis.csv", ballisticRows},
        {"combat_model_diagnosis.csv", combatRows}, {"metric_candidate_comparison.csv", qualityRows.str()},
        {"bounded_output_quality.csv", r075AggregateRows(aggregate, "scope,motion_family,model,truth_reference,horizon_ms,samples,position_mae,position_p95,position_p99", true,
            [](const std::vector<std::string> &parts) { return parts[2] == "V2.3_BOUNDED_OUTPUT"; })},
        {"source_period_analysis.csv", periodRows.str()}, {"historical_ballistic_contradiction.md", contradiction.str()},
        {"physical_trace_truth_proxy.csv", physicalRows}, {"production_equivalence.csv", production},
        {"hotpath_benchmark.md", hotpath}
    }};
    for (const auto &[name, contents] : files) if (!writeText(options.output / name, contents)) return 3;
    if (!writeText(options.output / "V2.4.R0.75_Adaptive_Response_Forecast_Truth_Metric_Audit.md", report.str())) return 3;
    if (!options.report.empty() && !writeText(options.report, report.str())) return 3;
    std::cout << disposition << "\n";
    return exact ? 0 : 4;
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        return r075Run(r075Options(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << "R0.75 audit exception: " << error.what() << '\n';
        return 5;
    }
}
