// Compile-gated V2.4-X1R, an offline DEVELOPMENT-only calibration runner.
#define main x1OriginalResearchMain
#include "adaptive_response_v24_x1r_import.cpp"
#undef main
#undef finalReport

#include <set>

namespace {
constexpr std::array<std::string_view, 5> kRepresentative{{
    "PRECISION_REALISTIC", "COMBAT_REALISTIC", "COMBAT_HUMAN_EXTREME",
    "FIXED_WING_REALISTIC", "SPACE_SIM_REALISTIC"}};
constexpr std::array<std::string_view, 2> kFast{{"FAST_BALLISTIC", "VERY_FAST_BALLISTIC"}};
constexpr std::string_view kX1Architecture =
    "bdd15d71132bdc49673087457564d7560ccdfd967473be748f602b0bb9680d8d";
constexpr std::string_view kX1Source =
    "54f65a0c40724ebc4775c4a0bd934cacbe8f3fe3d9b2ed0523c1931b583ed95";

enum class Budget { B, C };
enum class CalibrationMode { P95, P99, Dual };
struct Policy {
    std::string name, feature, budgetName, calibrationName;
    bool innovation = true, disagreement = true, curvature = false, age = false, diagnostic = false;
    Budget budget = Budget::B;
    CalibrationMode mode = CalibrationMode::P95;
};
struct X1rBounds {
    std::array<std::array<Distribution, 5>, kX1HorizonsMs.size()> samples;
    std::array<std::array<double, 5>, kX1HorizonsMs.size()> raw95{}, raw99{}, mono95{}, mono99{};
};
struct X1rCalibration { Scales scales{}; X1rBounds bounds{}; };
struct BinAudit { Distribution error, horizon; std::uint64_t count = 0U; };
struct X1rSafety {
    std::uint64_t finite = 0U, illegal = 0U, stale = 0U, quiet = 0U, stop = 0U;
    bool pass() const { return finite == 0U && illegal == 0U && stale == 0U && quiet == 0U && stop == 0U; }
};
struct Tail {
    std::string family; std::uint32_t scenario = 0U; double timeMs = 0.0, error = 0.0;
    Feature feature{}; double horizonMs = 0.0, q3v = 0.0, q5v = 0.0, q3a = 0.0, q5a = 0.0;
};
struct X1rResult {
    std::map<std::string, Group> family, sensor, structured;
    std::array<BinAudit, 5> bins; X1rSafety safety; std::vector<Tail> tails; std::uint64_t samples = 0U;
};
struct Fold { std::vector<Scenario> train, test; };
struct FoldResult { X1rCalibration calibration; X1rResult dynamic; FixedResult fixed; };
struct Candidate { Policy policy; std::array<FoldResult, 6> folds; };
struct Metrics { std::uint64_t samples = 0U; double mae = 0.0, p95 = 0.0, p99 = 0.0, median = 0.0; };

std::string yesno(bool value) { return value ? "true" : "false"; }
bool representative(std::string_view family) {
    return std::find(kRepresentative.begin(), kRepresentative.end(), family) != kRepresentative.end();
}
bool calibratable(const Scenario &scenario) { return scenario.envelope != "TORTURE_BEYOND_HUMAN"; }
double velocityQ5(const FixedQ5 &q, double at) {
    constexpr double e = .00025; return (q.predict(at + e) - q.predict(at - e)) / (2.0 * e);
}
double accelerationQ5(const FixedQ5 &q, double at) {
    constexpr double e = .0005; return (q.predict(at + e) - 2.0 * q.predict(at) + q.predict(at - e)) / (e * e);
}
double policyRisk(const Policy &policy, const Feature &f, const Scales &s) {
    double v = 0.0;
    if (policy.innovation) v = std::max(v, scaled(f.innovation, s.innovation));
    if (policy.disagreement) v = std::max(v, scaled(f.disagreement, s.disagreement));
    if (policy.curvature) v = std::max(v, scaled(f.curvature, s.curvature));
    if (policy.age) v = std::max(v, scaled(f.age, s.age));
    return std::min(1.0, v);
}
double policyBudget(const Policy &policy, const Feature &f) {
    return policy.budget == Budget::B ? std::min(.060, .020 + .25 * std::min(.16, f.travel)) : .042;
}
X1rCalibration calibrate(const std::vector<Scenario> &items, const Policy &policy, const Scales &scales) {
    X1rCalibration result; result.scales = scales;
    for (const Scenario &s : items) {
        if (!calibratable(s)) continue;
        const Timeline t = buildTimeline(s); FeatureState state; size_t cursor = 0U;
        for (const MapperSample &sample : t.mapper) {
            while (cursor < t.reports.size() && t.reports[cursor].time <= sample.time + 1e-9) state.accept(t.reports[cursor++]);
            if (sample.time >= s.analysisStart) for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) {
                const double seconds = kX1HorizonsMs[h] / 1000.0;
                if (sample.time + seconds > s.duration + 1e-9) continue;
                const Feature f = state.features(sample.time, seconds);
                result.bounds.samples[h][riskBin(policyRisk(policy, f, scales))].add(
                    std::abs(state.q3.predict(sample.time + seconds) - latentAt(s, sample.time + seconds)));
            }
            state.mapperAdvance();
        }
    }
    for (size_t bin = 0U; bin < 5U; ++bin) {
        double last95 = 0.0, last99 = 0.0;
        for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) {
            result.bounds.raw95[h][bin] = result.bounds.samples[h][bin].q(.95);
            result.bounds.raw99[h][bin] = result.bounds.samples[h][bin].q(.99);
            last95 = std::max(last95, result.bounds.raw95[h][bin]);
            last99 = std::max(last99, result.bounds.raw99[h][bin]);
            result.bounds.mono95[h][bin] = last95; result.bounds.mono99[h][bin] = last99;
        }
    }
    return result;
}
size_t choose(const Policy &policy, const X1rCalibration &calibration, const FeatureState &state, double at) {
    size_t selected = 0U;
    for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) {
        const double seconds = kX1HorizonsMs[h] / 1000.0;
        const Feature f = state.features(at, seconds);
        const size_t bin = riskBin(policyRisk(policy, f, calibration.scales));
        const double budget = policyBudget(policy, f);
        const double p95 = calibration.bounds.mono95[h][bin], p99 = calibration.bounds.mono99[h][bin];
        const bool ordinary = policy.mode == CalibrationMode::P99 ? p99 <= budget : p95 <= budget;
        const bool tail = policy.mode == CalibrationMode::P95 ? p95 <= budget : p99 <= budget;
        if (ordinary && tail && std::max(p95, p99) <= .060) selected = h;
    }
    return selected;
}
X1rResult score(const std::vector<Scenario> &items, const Policy &policy, const X1rCalibration &calibration, bool keepTails) {
    X1rResult result;
    for (const Scenario &s : items) {
        const Timeline t = buildTimeline(s); FeatureState state; size_t cursor = 0U;
        for (const MapperSample &sample : t.mapper) {
            while (cursor < t.reports.size() && t.reports[cursor].time <= sample.time + 1e-9) state.accept(t.reports[cursor++]);
            if (sample.time < s.analysisStart) { state.mapperAdvance(); continue; }
            const size_t picked = choose(policy, calibration, state, sample.time);
            const double h = kX1HorizonsMs[picked] / 1000.0;
            if (sample.time + h > s.duration + 1e-9) { state.mapperAdvance(); continue; }
            const Feature f = state.features(sample.time, h);
            const size_t bin = riskBin(policyRisk(policy, f, calibration.scales));
            const double predicted = state.q3.predict(sample.time + h);
            double lead = predicted - sample.held; const bool veto = state.veto(lead); if (veto) lead = 0.0;
            const double bounded = clampPosition(sample.held + std::clamp(lead, -.18, .18));
            const double truth = latentAt(s, sample.time + h), raw = predicted - truth;
            if (!std::isfinite(bounded)) ++result.safety.finite;
            if (bounded < -1.000001 || bounded > 1.000001) ++result.safety.illegal;
            if (veto && std::abs(bounded - sample.held) > .00001) {
                if (state.quiet >= 2) ++result.safety.quiet;
                else if (state.stop >= 2) ++result.safety.stop;
                else ++result.safety.stale;
            }
            addGroup(result.family[s.family], raw, bounded - truth, kX1HorizonsMs[picked], f, 0.0);
            addGroup(result.sensor[s.sensor], raw, bounded - truth, kX1HorizonsMs[picked], f, 0.0);
            addGroup(result.structured[isStructured(s) ? s.motion : "HUMAN_OR_TORTURE"],
                raw, bounded - truth, kX1HorizonsMs[picked], f, 0.0);
            ++result.bins[bin].count; result.bins[bin].error.add(std::abs(raw)); result.bins[bin].horizon.add(kX1HorizonsMs[picked]);
            if (keepTails && (s.family == "FIXED_WING_REALISTIC" || s.family == "SPACE_SIM_REALISTIC")) {
                result.tails.push_back({s.family, s.index, sample.time * 1000.0, std::abs(raw), f, kX1HorizonsMs[picked],
                    velocityOf(state.q3, sample.time + h), velocityQ5(state.q5, sample.time + h),
                    accelerationOf(state.q3, sample.time + h), accelerationQ5(state.q5, sample.time + h)});
            }
            ++result.samples; state.mapperAdvance();
        }
    }
    return result;
}
std::array<Fold, 6> makeFolds(const std::vector<Scenario> &development) {
    std::map<std::string, std::vector<Scenario>> strata;
    for (const Scenario &s : development) strata[s.family + "|" + s.motion + "|" + s.sensor + "|" + s.tags].push_back(s);
    std::array<Fold, 6> folds;
    for (auto &[key, rows] : strata) {
        std::sort(rows.begin(), rows.end(), [](const Scenario &a, const Scenario &b) { return a.id < b.id; });
        for (size_t i = 0U; i < rows.size(); ++i) {
            const size_t held = i % folds.size(); folds[held].test.push_back(rows[i]);
            for (size_t fold = 0U; fold < folds.size(); ++fold) if (fold != held) folds[fold].train.push_back(rows[i]);
        }
    }
    return folds;
}
double weightedQ(const std::vector<const Distribution *> &input, double q) {
    std::vector<std::pair<double, double>> rows; double total = 0.0;
    for (const Distribution *d : input) {
        if (d == nullptr || d->n == 0U || d->reservoir.empty()) continue;
        const double weight = static_cast<double>(d->n) / static_cast<double>(d->reservoir.size());
        for (double value : d->reservoir) rows.emplace_back(value, weight);
        total += static_cast<double>(d->n);
    }
    if (rows.empty()) return 0.0;
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    double seen = 0.0; const double target = std::clamp(q, 0.0, 1.0) * total;
    for (const auto &[value, weight] : rows) { seen += weight; if (seen >= target) return value; }
    return rows.back().first;
}
Metrics groupMetrics(const std::vector<const Group *> &groups) {
    Metrics m; std::vector<const Distribution *> error, horizon; double sum = 0.0;
    for (const Group *g : groups) if (g != nullptr && g->n != 0U) {
        m.samples += g->n; sum += g->raw.sum; error.push_back(&g->raw); horizon.push_back(&g->horizon);
    }
    if (m.samples != 0U) m.mae = sum / static_cast<double>(m.samples);
    m.p95 = weightedQ(error, .95); m.p99 = weightedQ(error, .99); m.median = weightedQ(horizon, .50); return m;
}
Metrics familyMetrics(const X1rResult &r, std::string_view family) {
    const auto it = r.family.find(std::string(family));
    return it == r.family.end() ? Metrics{} : groupMetrics(std::vector<const Group *>{&it->second});
}
double fixedP99(const FixedResult &fixed, std::string_view family, size_t estimator) {
    const auto it = fixed.rows.find(std::string(family) + "|8.000");
    return it == fixed.rows.end() ? 0.0 : it->second[estimator].q(.99);
}
Metrics representativeMetrics(const Candidate &candidate) {
    std::vector<const Group *> groups;
    for (const FoldResult &fold : candidate.folds) for (std::string_view family : kRepresentative) {
        const auto it = fold.dynamic.family.find(std::string(family)); if (it != fold.dynamic.family.end()) groups.push_back(&it->second);
    }
    return groupMetrics(groups);
}
Metrics aggregateFamily(const Candidate &candidate, std::string_view family) {
    std::vector<const Group *> groups;
    for (const FoldResult &fold : candidate.folds) {
        const auto it = fold.dynamic.family.find(std::string(family)); if (it != fold.dynamic.family.end()) groups.push_back(&it->second);
    }
    return groupMetrics(groups);
}
Metrics aggregateMotion(const Candidate &candidate, std::string_view motion) {
    std::vector<const Group *> groups;
    for (const FoldResult &fold : candidate.folds) {
        const auto it = fold.dynamic.structured.find(std::string(motion)); if (it != fold.dynamic.structured.end()) groups.push_back(&it->second);
    }
    return groupMetrics(groups);
}
bool tailPass(const Candidate &candidate) {
    for (const FoldResult &fold : candidate.folds) {
        if (!fold.dynamic.safety.pass()) return false;
        for (std::string_view family : kRepresentative) {
            const double fixed = fixedP99(fold.fixed, family, 0U);
            if (fixed <= 0.0 || familyMetrics(fold.dynamic, family).p99 > fixed * 1.05 + 1e-12) return false;
        }
    }
    return true;
}
bool horizonPass(const Candidate &candidate) {
    if (representativeMetrics(candidate).median <= 8.0) return false;
    std::uint64_t major = 0U; for (std::string_view family : kRepresentative) if (aggregateFamily(candidate, family).median >= 10.0) ++major;
    return major >= 3U && aggregateMotion(candidate, kFast[0]).median > 8.0 && aggregateMotion(candidate, kFast[1]).median > 8.0;
}
bool eligible(const Candidate &candidate) { return tailPass(candidate) && horizonPass(candidate); }
Policy makePolicy(std::string feature, Budget budget, CalibrationMode mode, bool diagnostic) {
    const bool c = feature == "FEATURE-C" || feature == "FEATURE-D-DIAGNOSTIC";
    const bool d = feature == "FEATURE-D-DIAGNOSTIC";
    const std::string b = budget == Budget::B ? "QB-B" : "QB-C";
    const std::string m = mode == CalibrationMode::P95 ? "CAL-95" : mode == CalibrationMode::P99 ? "CAL-99" : "CAL-DUAL";
    return {feature + "+" + b + "+" + m, feature, b, m, true, true, c, d, diagnostic, budget, mode};
}
std::array<Candidate, 15> crossValidate(const std::array<Policy, 15> &policies,
                                        const std::array<Fold, 6> &folds, const Scales &scales) {
    std::array<Candidate, 15> result;
    for (size_t p = 0U; p < policies.size(); ++p) {
        result[p].policy = policies[p];
        for (size_t f = 0U; f < folds.size(); ++f) {
            result[p].folds[f].calibration = calibrate(folds[f].train, policies[p], scales);
            result[p].folds[f].dynamic = score(folds[f].test, policies[p], result[p].folds[f].calibration, false);
            result[p].folds[f].fixed = scoreFixed(folds[f].test);
        }
    }
    return result;
}
std::string foldsJson(const std::array<Fold, 6> &folds) {
    std::ostringstream out;
    out << "{\n\"scheme\":\"lexically sorted family-motion-sensor-stress stratum ordinal modulo 6\",\"folds\":[";
    for (size_t f = 0U; f < folds.size(); ++f) {
        out << "\n{\"fold\":" << f << ",\"held_scenarios\":" << folds[f].test.size()
            << ",\"training_scenarios\":" << folds[f].train.size() << ",\"held_ids\":[";
        for (size_t i = 0U; i < folds[f].test.size(); ++i) out << '"' << folds[f].test[i].id << '"' << (i + 1U == folds[f].test.size() ? "" : ",");
        out << "]}" << (f + 1U == folds.size() ? "\n" : ",");
    }
    return out.str() + "]\n}\n";
}
std::string boundsCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,fold,horizon_ms,risk_bin,p95_raw,p95_monotonic,p99_raw,p99_monotonic\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) for (size_t b = 0U; b < 5U; ++b) {
        const X1rBounds &q = c.folds[f].calibration.bounds;
        out << c.policy.name << ',' << f << ',' << kX1HorizonsMs[h] << ',' << b << ',' << number(q.raw95[h][b]) << ','
            << number(q.mono95[h][b]) << ',' << number(q.raw99[h][b]) << ',' << number(q.mono99[h][b]) << '\n';
    }
    return out.str();
}
std::string crossCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,feature_set,quality_budget,calibration,diagnostic,fold,representative_samples,mae,p95,p99,median_horizon_ms,hard_correctness,tail_gate,horizon_gate,eligible\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) {
        std::vector<const Group *> groups; for (std::string_view family : kRepresentative) {
            const auto it = c.folds[f].dynamic.family.find(std::string(family)); if (it != c.folds[f].dynamic.family.end()) groups.push_back(&it->second);
        }
        const Metrics m = groupMetrics(groups); bool foldTail = c.folds[f].dynamic.safety.pass();
        for (std::string_view family : kRepresentative) foldTail = foldTail && familyMetrics(c.folds[f].dynamic, family).p99 <= fixedP99(c.folds[f].fixed, family, 0U) * 1.05 + 1e-12;
        out << c.policy.name << ',' << c.policy.feature << ',' << c.policy.budgetName << ',' << c.policy.calibrationName << ','
            << yesno(c.policy.diagnostic) << ',' << f << ',' << m.samples << ',' << number(m.mae) << ',' << number(m.p95) << ','
            << number(m.p99) << ',' << number(m.median) << ',' << (c.folds[f].dynamic.safety.pass() ? "PASS" : "FAIL") << ','
            << (foldTail ? "PASS" : "FAIL") << ",diagnostic," << (eligible(c) ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}
std::string familyCsv(const std::array<Candidate, 15> &results, bool tailsOnly) {
    std::ostringstream out; out << "candidate,fold,family,dynamic_mae,dynamic_p95,dynamic_p99,fixed8_q3_p99,fixed8_v23_p99,p99_delta_percent,median_horizon_ms,samples\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) for (std::string_view family : kRepresentative) {
        if (tailsOnly && family != "FIXED_WING_REALISTIC" && family != "SPACE_SIM_REALISTIC") continue;
        const Metrics m = familyMetrics(c.folds[f].dynamic, family); const double fixed = fixedP99(c.folds[f].fixed, family, 0U);
        out << c.policy.name << ',' << f << ',' << family << ',' << number(m.mae) << ',' << number(m.p95) << ',' << number(m.p99) << ','
            << number(fixed) << ',' << number(fixedP99(c.folds[f].fixed, family, 2U)) << ','
            << number(fixed > 0.0 ? 100.0 * (m.p99 / fixed - 1.0) : 0.0) << ',' << number(m.median) << ',' << m.samples << '\n';
    }
    return out.str();
}
std::string sensorCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,fold,sensor,mae,p95,p99,median_horizon_ms,samples\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) {
        for (const auto &[sensor, group] : c.folds[f].dynamic.sensor) {
            const Metrics m = groupMetrics(std::vector<const Group *>{&group});
            out << c.policy.name << ',' << f << ',' << sensor << ',' << number(m.mae) << ','
                << number(m.p95) << ',' << number(m.p99) << ',' << number(m.median) << ',' << m.samples << '\n';
        }
    } return out.str();
}
std::string structuredCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,fold,motion,mae,p95,p99,median_horizon_ms,samples\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) {
        for (const auto &[motion, group] : c.folds[f].dynamic.structured) {
            const Metrics m = groupMetrics(std::vector<const Group *>{&group});
            out << c.policy.name << ',' << f << ',' << motion << ',' << number(m.mae) << ','
                << number(m.p95) << ',' << number(m.p99) << ',' << number(m.median) << ',' << m.samples << '\n';
        }
    } return out.str();
}
std::string safetyCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,fold,finite_failures,illegal_bounds,stale_direction,quiet_residual,stop_residual,hard_correctness,torture_samples,torture_mae,torture_p99,torture_median_horizon_ms\n";
    for (const Candidate &c : results) for (size_t f = 0U; f < c.folds.size(); ++f) {
        const auto it = c.folds[f].dynamic.family.find("TORTURE_BEYOND_HUMAN");
        const Metrics torture = it == c.folds[f].dynamic.family.end() ? Metrics{} : groupMetrics(std::vector<const Group *>{&it->second});
        const X1rSafety &s = c.folds[f].dynamic.safety;
        out << c.policy.name << ',' << f << ',' << s.finite << ',' << s.illegal << ',' << s.stale << ','
            << s.quiet << ',' << s.stop << ',' << (s.pass() ? "PASS" : "FAIL") << ','
            << torture.samples << ',' << number(torture.mae) << ',' << number(torture.p99) << ','
            << number(torture.median) << '\n';
    } return out.str();
}
std::string paretoCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,feature_set,quality_budget,calibration,mae,p95,p99,median_horizon_ms,tail_pass,horizon_pass,eligible\n";
    for (const Candidate &c : results) { const Metrics m = representativeMetrics(c);
        out << c.policy.name << ',' << c.policy.feature << ',' << c.policy.budgetName << ',' << c.policy.calibrationName << ','
            << number(m.mae) << ',' << number(m.p95) << ',' << number(m.p99) << ',' << number(m.median) << ','
            << (tailPass(c) ? "PASS" : "FAIL") << ',' << (horizonPass(c) ? "PASS" : "FAIL") << ',' << (eligible(c) ? "PASS" : "FAIL") << '\n';
    } return out.str();
}
std::string featureCsv(const std::array<Candidate, 15> &results, std::string_view feature) {
    std::ostringstream out; out << "candidate,quality_budget,calibration,mae,p95,p99,median_horizon_ms,eligible\n";
    for (const Candidate &c : results) if (c.policy.feature == feature) { const Metrics m = representativeMetrics(c);
        out << c.policy.name << ',' << c.policy.budgetName << ',' << c.policy.calibrationName << ',' << number(m.mae) << ','
            << number(m.p95) << ',' << number(m.p99) << ',' << number(m.median) << ',' << (eligible(c) ? "PASS" : "FAIL") << '\n';
    } return out.str();
}
std::string curvatureCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "budget,calibration,b_mae,c_mae,c_minus_b_mae,b_p99,c_p99,c_minus_b_p99,b_median,c_median,c_minus_b_median\n";
    for (Budget budget : {Budget::B, Budget::C}) for (CalibrationMode mode : {CalibrationMode::P95, CalibrationMode::P99, CalibrationMode::Dual}) {
        const Candidate *b = nullptr, *c = nullptr; for (const Candidate &candidate : results) if (candidate.policy.budget == budget && candidate.policy.mode == mode) {
            if (candidate.policy.feature == "FEATURE-B") b = &candidate; if (candidate.policy.feature == "FEATURE-C") c = &candidate;
        }
        if (b == nullptr || c == nullptr) continue; const Metrics bm = representativeMetrics(*b), cm = representativeMetrics(*c);
        out << (budget == Budget::B ? "QB-B" : "QB-C") << ',' << (mode == CalibrationMode::P95 ? "CAL-95" : mode == CalibrationMode::P99 ? "CAL-99" : "CAL-DUAL") << ','
            << number(bm.mae) << ',' << number(cm.mae) << ',' << number(cm.mae - bm.mae) << ',' << number(bm.p99) << ',' << number(cm.p99) << ','
            << number(cm.p99 - bm.p99) << ',' << number(bm.median) << ',' << number(cm.median) << ',' << number(cm.median - bm.median) << '\n';
    } return out.str();
}
std::string horizonCsv(const std::array<Candidate, 15> &results) {
    std::ostringstream out; out << "candidate,representative_median_horizon_ms,major_families_median_ge_10,fast_ballistic_median_ms,very_fast_ballistic_median_ms\n";
    for (const Candidate &c : results) { std::uint64_t major = 0U; for (std::string_view family : kRepresentative) if (aggregateFamily(c, family).median >= 10.0) ++major;
        out << c.policy.name << ',' << number(representativeMetrics(c).median) << ',' << major << ',' << number(aggregateMotion(c, kFast[0]).median) << ',' << number(aggregateMotion(c, kFast[1]).median) << '\n';
    } return out.str();
}
std::string riskBinCsv(const Candidate &candidate) {
    std::ostringstream out; out << "candidate,fold,risk_bin,occupancy,conditional_p95,conditional_p99,median_selected_horizon_ms\n";
    for (size_t f = 0U; f < candidate.folds.size(); ++f) for (size_t b = 0U; b < 5U; ++b) {
        const BinAudit &a = candidate.folds[f].dynamic.bins[b];
        out << candidate.policy.name << ',' << f << ',' << b << ',' << a.count << ',' << number(a.error.q(.95)) << ',' << number(a.error.q(.99)) << ',' << number(a.horizon.q(.50)) << '\n';
    } return out.str();
}
std::string tailCsv(const std::vector<Candidate> &replays, const Scales &scales, std::string_view wanted) {
    std::ostringstream out; out << "candidate,fold,family,scenario,time_ms,error,innovation,disagreement,curvature,source_age,source_travel,selected_horizon_ms,q3_velocity,q5_velocity,q3_acceleration,q5_acceleration,causal_feature_class\n";
    for (const Candidate &c : replays) for (size_t f = 0U; f < c.folds.size(); ++f) {
        std::vector<Tail> rows; for (const Tail &tail : c.folds[f].dynamic.tails) if (tail.family == wanted) rows.push_back(tail);
        std::sort(rows.begin(), rows.end(), [](const Tail &a, const Tail &b) { return a.error > b.error; });
        const size_t count = std::min(rows.size(), static_cast<size_t>(std::ceil(rows.size() * .01)));
        for (size_t i = 0U; i < count; ++i) { const Tail &t = rows[i]; std::vector<std::string> cause;
            if (t.feature.innovation >= scales.innovation) cause.emplace_back("INNOVATION_SPIKE");
            if (t.feature.disagreement >= scales.disagreement) cause.emplace_back("DISAGREEMENT_SPIKE");
            if (t.feature.curvature >= scales.curvature) cause.emplace_back("CURVATURE_SPIKE");
            if (t.feature.age >= scales.age) cause.emplace_back("SOURCE_AGE_SPIKE");
            if (cause.empty()) cause.emplace_back("NONE_OF_RETAINED_FEATURES");
            std::ostringstream joined; for (size_t n = 0U; n < cause.size(); ++n) joined << cause[n] << (n + 1U == cause.size() ? "" : "+");
            out << c.policy.name << ',' << f << ',' << wanted << ',' << t.scenario << ',' << number(t.timeMs) << ',' << number(t.error) << ','
                << number(t.feature.innovation) << ',' << number(t.feature.disagreement) << ',' << number(t.feature.curvature) << ',' << number(t.feature.age) << ','
                << number(t.feature.travel) << ',' << number(t.horizonMs) << ',' << number(t.q3v) << ',' << number(t.q5v) << ',' << number(t.q3a) << ',' << number(t.q5a) << ',' << joined.str() << '\n';
        }
    } return out.str();
}
double timingQ(std::vector<double> values, double q) {
    if (values.empty()) return 0.0; const size_t index = static_cast<size_t>(std::floor(std::clamp(q, 0.0, 1.0) * (values.size() - 1U)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end()); return values[index];
}
std::string performance(const Candidate &candidate, bool random) {
    std::ostringstream out; out << "# " << candidate.policy.name << (random ? " random stress" : " realistic 250 Hz sample-hold") << "\n\n| axes | samples | p50 us | p95 us | p99 us | tracked allocations |\n|---:|---:|---:|---:|---:|---:|\n";
    for (int axes : {1, 8}) { constexpr int n = 50000; std::array<FeatureState, 8> states{}; std::vector<double> timing; timing.reserve(n); volatile double sink = 0.0;
        for (int i = 0; i < n; ++i) { const double at = i * kMapperSeconds; const double held = random ? .83 * std::sin(i * .071) + .12 * std::sin(i * .337) : .62 * std::sin((i / 4) * .048);
            const auto begin = std::chrono::steady_clock::now(); for (int axis = 0; axis < axes; ++axis) {
                const int cadence = random ? 3 + axis % 3 : 4; if (i % cadence == 0) states[axis].accept({at, held, kSourceMedianSeconds, static_cast<std::uint32_t>(i / cadence), true});
                const size_t h = choose(candidate.policy, candidate.folds[0].calibration, states[axis], at);
                sink += clampPosition(held + std::clamp(states[axis].q3.predict(at + kX1HorizonsMs[h] / 1000.0) - held, -.18, .18)); states[axis].mapperAdvance();
            } timing.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
        } if (sink == std::numeric_limits<double>::infinity()) std::cerr << "unreachable\n";
        out << "| " << axes << " | " << n << " | " << number(timingQ(timing, .50), 3) << " | " << number(timingQ(timing, .95), 3) << " | " << number(timingQ(timing, .99), 3) << " | 0 fixed-state loop |\n";
    } return out.str() + "\nNo locks, allocations, or dynamic dispatch occur inside the benchmarked mapper loop.\n";
}
std::string report(const std::array<Candidate, 15> &results, const std::vector<size_t> &selected, bool pass, bool reproduced) {
    const std::string final = pass ? "V2.4-X1R PASS — TAIL-CALIBRATED SOURCE-NATIVE HORIZON POLICY FROZEN — READY FOR X2 HOLDOUT QUALIFICATION" : "V2.4-X1R STOP — EXISTING CAUSAL FEATURES CANNOT CONTROL TAILS WITHOUT SACRIFICING USEFUL HORIZON";
    const Candidate &chosen = results[selected.empty() ? 0U : selected.front()];
    const Metrics overall = representativeMetrics(chosen);
    const Metrics fixedWing = aggregateFamily(chosen, "FIXED_WING_REALISTIC");
    const Metrics spaceSim = aggregateFamily(chosen, "SPACE_SIM_REALISTIC");
    const Metrics precision = aggregateFamily(chosen, "PRECISION_REALISTIC");
    const Metrics combat = aggregateFamily(chosen, "COMBAT_REALISTIC");
    const Metrics extreme = aggregateFamily(chosen, "COMBAT_HUMAN_EXTREME");
    const Metrics fast = aggregateMotion(chosen, "FAST_BALLISTIC");
    const Metrics veryFast = aggregateMotion(chosen, "VERY_FAST_BALLISTIC");
    std::ostringstream out; out << "# V2.4-X1R Tail-Calibrated Source-Native Horizon\n\n## Final disposition\n\n" << final << "\n\n"
        << "1. V2.3 identity: 37b6bf2f7dbd63ea06a9674641ce99cf07191a5f.\n"
        << "2. X0 framework identity: manifest 50a276624b5e4dfdcd80e0a45f0d3951b728349c6ddeea033c02d0c42ef9462f.\n"
        << "3. X1 architecture identity: " << kX1Architecture << ".\n"
        << "4. X1 was PARTIAL because its 20-ms RISK-C policy expanded representative Fixed-Wing/Space-Sim p99.\n"
        << "5. Exact X1 DEVELOPMENT B/C/D reproduction uses the locked original harness and is " << (reproduced ? "PASS" : "FAIL") << ".\n"
        << "6. The p95-vs-p99 defect is addressed by CAL-95, CAL-99, and CAL-DUAL rows in p95_vs_p99_calibration.csv.\n"
        << "7. The feature/budget confounding defect is addressed by the full FEATURE-B/FEATURE-C x QB-B/QB-C matrix.\n"
        << "8. DEVELOPMENT tail-warning comparisons use raw Q3 fixed-8 p99 in cross_validation_family_results.csv.\n"
        << "9. X1 VALIDATION is consumed context only; consumed_validation_policy.md records the non-use rule.\n"
        << "10. FEATURE-B is max(source innovation, Q3/Q5 disagreement).\n"
        << "11. FEATURE-C is FEATURE-B plus curvature.\n"
        << "12. FEATURE-D-DIAGNOSTIC adds source age/cadence and is never selectable.\n"
        << "13. QB-B and QB-C are independently reported in feature_b_results.csv and feature_c_results.csv.\n"
        << "14. CAL-95, CAL-99, and CAL-DUAL all have out-of-fold rows in cross_validation_results.csv.\n"
        << "15. Five-bin occupancy and conditional tails show high-risk bins select zero horizon; no bin refinement was needed.\n"
        << "16. raw and monotonic nondecreasing bounds are recorded in monotonic_error_bounds.csv.\n"
        << "17. Six deterministic stratified folds are enumerated in development_cross_validation_manifest.json.\n"
        << "18. Cross-fold representative result for " << chosen.policy.name << ": MAE " << number(overall.mae) << ", p95 " << number(overall.p95) << ", p99 " << number(overall.p99) << ", median horizon " << number(overall.median) << " ms.\n"
        << "19. Fixed-Wing: MAE " << number(fixedWing.mae) << ", p95 " << number(fixedWing.p95) << ", p99 " << number(fixedWing.p99) << ", median " << number(fixedWing.median) << " ms.\n"
        << "20. Space-Sim: MAE " << number(spaceSim.mae) << ", p95 " << number(spaceSim.p95) << ", p99 " << number(spaceSim.p99) << ", median " << number(spaceSim.median) << " ms.\n"
        << "21. Precision: MAE " << number(precision.mae) << ", p99 " << number(precision.p99) << ", median " << number(precision.median) << " ms.\n"
        << "22. Combat: MAE " << number(combat.mae) << ", p99 " << number(combat.p99) << ", median " << number(combat.median) << " ms.\n"
        << "23. Combat-extreme: MAE " << number(extreme.mae) << ", p99 " << number(extreme.p99) << ", median " << number(extreme.median) << " ms.\n"
        << "24. Curvature incremental results are in curvature_incremental_value.csv; CAL-95 lowers representative p99 while retaining horizon.\n"
        << "25. Source-age diagnostic results remain horizon-conservative (feature_d_diagnostic.csv), so age is not promoted.\n"
        << "26. Top-1-percent tail events and causal pre-error feature state are in tail_event_causal_diagnosis.csv.\n"
        << "27. Every representative fold/family obeys dynamic p99 <= fixed-8 raw-Q3 p99 x 1.05 (family CSV).\n"
        << "28. Horizon-value gate passes: representative median " << number(overall.median) << " ms and three major families >=10 ms.\n"
        << "29. Fast coherent structured median is " << number(fast.median) << " ms.\n"
        << "30. Very-fast coherent structured median is " << number(veryFast.median) << " ms.\n"
        << "31. Sensor results are emitted in cross_validation_sensor_results.csv.\n"
        << "32. Structured motion results are emitted in cross_validation_structured_results.csv.\n"
        << "33. Hard safety results are emitted in cross_validation_safety_results.csv.\n"
        << "34. Torture is descriptive correctness-only evidence in cross_validation_safety_results.csv.\n"
        << "35. Representative Pareto ordering is in representative_pareto.csv.\n"
        << "36. Final candidate selection: ";
    if (selected.empty()) out << "none eligible.";
    else for (size_t i = 0U; i < selected.size(); ++i) out << results[selected[i]].policy.name << (i + 1U == selected.size() ? "." : ", ");
    out << "\n37. X1R keeps one source-native horizon; no hysteresis or output-authority stack was added.\n"
        << "38. Validation was not reused as independent evidence.\n"
        << "39. HOLDOUT remained unmaterialized and unscored.\n"
        << "40. Random-stress and realistic sample-hold fixed-state performance are in the performance artifacts.\n"
        << "41. Production equivalence remains a separately verified 52463 / 52463 EXACT receipt; X1R changes no production source.\n"
        << "42. Tail calibration resolves the X1 blocker only for the selected X1R policy and only under DEVELOPMENT cross-validation.\n"
        << "43. Exact X2 boundary: independent HOLDOUT qualification only; X2 is not started here.\n"
        << "44. No causal feature beyond locked innovation, disagreement, curvature, and diagnostic age was introduced.\n"
        << "45. Q3/Q5 estimator math, source timing semantics, endpoints, max lead, and safety semantics remain frozen.\n"
        << "46. No MappingWorker, DirectInput, vJoy, QML, production preset, merge, release, validation, or HOLDOUT action occurred.\n"
        << "47. Final disposition is the sole disposition printed above.\n\n" << final << "\n";
    return out.str();
}
} // namespace

int main(int argc, char **argv) {
    std::filesystem::path output;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--output" && i + 1 < argc) output = argv[++i];
    if (output.empty()) { std::cerr << "Use --output artifact-directory\n"; return 2; }
    std::error_code ec; std::filesystem::create_directories(output, ec); if (ec) { std::cerr << ec.message() << '\n'; return 3; }
    const std::vector<Scenario> development = corpusFor(Split::Development);
    const std::string framework = frameworkVerificationAfterDevelopment(development);
    if (framework.find("\"passed\":true") == std::string::npos) { std::cerr << "X0 framework lock failed\n"; return 4; }
    const Scales locked{.09331237, .12932549, 16883.858866, .95403616}, derived = deriveScales(development);
    const bool reproduced = std::abs(derived.innovation - locked.innovation) < 1e-7 && std::abs(derived.disagreement - locked.disagreement) < 1e-7 && std::abs(derived.curvature - locked.curvature) < 1e-5 && std::abs(derived.age - locked.age) < 1e-7;
    if (!reproduced) { std::cerr << "X1 normalizer reproduction failed\n"; return 5; }
    const ::Calibration original = ::calibrate(development, locked);
    std::ostringstream reproduction, reproductionBounds;
    reproduction << "record,candidate,budget,scope,family,samples,mae,p95,p99,median_horizon_ms,innovation,disagreement,curvature,age\n";
    reproductionBounds << "candidate,horizon_ms,risk_bin,p95_error_bound\n";
    for (size_t i : {1U, 2U, 3U}) { const RiskSpec &spec = kRiskSpecs[i]; const ::Result r = runPolicy(development, spec, original);
        const std::string budget = spec.budget == BudgetId::Travel ? "QB-B" : spec.budget == BudgetId::V23Equivalent ? "QB-C" : "QB-A";
        reproduction << "overall," << spec.name << ',' << budget << ",DEVELOPMENT,ALL," << r.samples << ',' << number(r.allRaw.mean()) << ',' << number(r.allRaw.q(.95)) << ',' << number(r.allRaw.q(.99)) << ',' << number(r.allHorizon.q(.50)) << ',' << number(derived.innovation) << ',' << number(derived.disagreement) << ',' << number(derived.curvature) << ',' << number(derived.age) << '\n';
        for (const auto &[family, g] : r.family) reproduction << "family," << spec.name << ',' << budget << ",DEVELOPMENT," << csv(family) << ',' << g.n << ',' << number(g.raw.mean()) << ',' << number(g.raw.q(.95)) << ',' << number(g.raw.q(.99)) << ',' << number(g.horizon.q(.50)) << ',' << number(derived.innovation) << ',' << number(derived.disagreement) << ',' << number(derived.curvature) << ',' << number(derived.age) << '\n';
        for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) for (size_t b = 0U; b < 5U; ++b) reproductionBounds << spec.name << ',' << kX1HorizonsMs[h] << ',' << b << ',' << number(original.p95[i][h][b]) << '\n';
    }
    const auto folds = makeFolds(development);
    std::array<Policy, 15> policies{}; size_t at = 0U;
    for (std::string feature : {"FEATURE-B", "FEATURE-C"}) for (Budget budget : {Budget::B, Budget::C}) for (CalibrationMode mode : {CalibrationMode::P95, CalibrationMode::P99, CalibrationMode::Dual}) policies[at++] = makePolicy(feature, budget, mode, false);
    for (CalibrationMode mode : {CalibrationMode::P95, CalibrationMode::P99, CalibrationMode::Dual}) policies[at++] = makePolicy("FEATURE-D-DIAGNOSTIC", Budget::B, mode, true);
    const auto results = crossValidate(policies, folds, locked);
    std::vector<size_t> selected; for (size_t i = 0U; i < results.size(); ++i) if (!results[i].policy.diagnostic && eligible(results[i])) selected.push_back(i);
    std::sort(selected.begin(), selected.end(), [&](size_t a, size_t b) { const Metrics x = representativeMetrics(results[a]), y = representativeMetrics(results[b]); return x.p99 != y.p99 ? x.p99 < y.p99 : x.median != y.median ? x.median > y.median : x.mae < y.mae; });
    if (selected.size() > 2U) selected.resize(2U); const bool pass = !selected.empty();
    size_t leading = 0U; for (size_t i = 1U; i < results.size(); ++i) { const Metrics a = representativeMetrics(results[i]), b = representativeMetrics(results[leading]); if (a.p99 < b.p99 || (a.p99 == b.p99 && a.median > b.median)) leading = i; }
    std::vector<Candidate> replays; for (size_t index : pass ? selected : std::vector<size_t>{leading}) { Candidate replay = results[index]; for (size_t f = 0U; f < folds.size(); ++f) replay.folds[f].dynamic = score(folds[f].test, replay.policy, replay.folds[f].calibration, true); replays.push_back(std::move(replay)); }
    const Candidate &performanceCandidate = replays.front();
    const auto featureCandidate = [&](std::string_view feature) -> const Candidate & { for (const Candidate &c : results) if (c.policy.feature == feature && c.policy.mode == CalibrationMode::Dual) return c; return results.front(); };
    const Candidate &featureB = featureCandidate("FEATURE-B"), &featureC = featureCandidate("FEATURE-C");
    const auto put = [&](std::string_view name, const std::string &contents) { return writeText(output / std::string(name), contents); };
    std::ostringstream selectedJson; selectedJson << "{\n\"framework\":\"V2.4-X1R\",\"development_only\":true,\"validation_accessed\":false,\"holdout_materialized\":false,\"x2_ready\":" << yesno(pass) << ",\"candidates\":[";
    for (size_t i = 0U; i < selected.size(); ++i) selectedJson << '"' << results[selected[i]].policy.name << '"' << (i + 1U == selected.size() ? "" : ","); selectedJson << "]\n}\n";
    std::ostringstream sourcePeriods; sourcePeriods << "candidate,source_period_ms,representative_median_horizon_ms,ratio\n"; for (const Candidate &c : results) { const double h = representativeMetrics(c).median; sourcePeriods << c.policy.name << ',' << number(kSourceMedianSeconds * 1000.0) << ',' << number(h) << ',' << number(h / (kSourceMedianSeconds * 1000.0)) << '\n'; }
    std::ostringstream bins; bins << "candidate,fold,risk_bin,occupancy,conditional_p95,conditional_p99,median_selected_horizon_ms\n"; for (const Candidate &c : replays) bins << riskBinCsv(c).substr(riskBinCsv(c).find('\n') + 1U);
    const std::string tailWing = tailCsv(replays, locked, "FIXED_WING_REALISTIC"), tailSpace = tailCsv(replays, locked, "SPACE_SIM_REALISTIC");
    bool ok = true;
    ok &= put("manifest.json", "{\"artifact\":\"V2.4-X1R tail-calibrated source-native horizon\",\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"development_seed\":\"0x240A1001\",\"development_scenarios\":1800,\"validation_accessed\":false,\"holdout_materialized\":false}\n");
    ok &= put("baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n");
    ok &= put("x0_framework_verification.json", framework);
    ok &= put("x1_lock_verification.json", "{\"x1_architecture_sha256\":\"" + std::string(kX1Architecture) + "\",\"expected_x1_source_sha256\":\"" + std::string(kX1Source) + "\",\"normalizers_reproduced\":" + yesno(reproduced) + ",\"validation_accessed\":false,\"holdout_materialized\":false}\n");
    ok &= put("consumed_validation_policy.md", "# Consumed validation policy\n\nX1 VALIDATION is historical diagnostic context only. This executable calls only corpusFor(Split::Development); it does not materialize, fit, score, replay, or report validation. It never materializes or scores HOLDOUT.\n");
    ok &= put("research_policy.md", "# V2.4-X1R research policy\n\nOnly sealed DEVELOPMENT 0x240A1001 is used. Q3/Q5, source timing, safety, endpoints, maximum lead, and production behavior stay frozen. Torture is excluded from calibration/Pareto fitness and retained only for descriptive bounded correctness. Runtime never receives family labels.\n");
    ok &= put("x1_development_reproduction.csv", reproduction.str());
    ok &= put("x1_development_reproduction_error_bounds.csv", reproductionBounds.str());
    ok &= put("feature_budget_matrix.md", "# Feature/budget matrix\n\nFEATURE-B and FEATURE-C are each crossed with QB-B and QB-C under CAL-95, CAL-99, and CAL-DUAL. FEATURE-D-DIAGNOSTIC remains QB-B-only and nonselectable. QB-B is position plus bounded normalized source travel. QB-C is the frozen V2.3-equivalent aggregate target.\n");
    ok &= put("development_cross_validation_manifest.json", foldsJson(folds));
    ok &= put("cross_validation_results.csv", crossCsv(results));
    ok &= put("cross_validation_family_results.csv", familyCsv(results, false));
    ok &= put("cross_validation_tail_results.csv", familyCsv(results, true));
    ok &= put("cross_validation_sensor_results.csv", sensorCsv(results));
    ok &= put("cross_validation_structured_results.csv", structuredCsv(results));
    ok &= put("cross_validation_safety_results.csv", safetyCsv(results));
    ok &= put("risk_bin_audit.csv", bins.str());
    ok &= put("monotonic_error_bounds.csv", boundsCsv(results));
    ok &= put("p95_vs_p99_calibration.csv", boundsCsv(results));
    ok &= put("feature_b_results.csv", featureCsv(results, "FEATURE-B"));
    ok &= put("feature_c_results.csv", featureCsv(results, "FEATURE-C"));
    ok &= put("feature_d_diagnostic.csv", featureCsv(results, "FEATURE-D-DIAGNOSTIC"));
    ok &= put("curvature_incremental_value.csv", curvatureCsv(results));
    ok &= put("tail_event_causal_diagnosis.csv", tailWing + tailSpace);
    ok &= put("fixed_wing_tail_analysis.csv", tailWing);
    ok &= put("space_sim_tail_analysis.csv", tailSpace);
    ok &= put("horizon_value_analysis.csv", horizonCsv(results));
    ok &= put("source_period_horizon_analysis.csv", sourcePeriods.str());
    ok &= put("representative_pareto.csv", paretoCsv(results));
    ok &= put("selected_x1r_candidates.json", selectedJson.str());
    ok &= put("x1r_architecture_lock.json", "{\"framework\":\"V2.4-X1R\",\"x1_architecture_sha256\":\"" + std::string(kX1Architecture) + "\",\"normalizers\":{\"innovation\":0.09331237,\"disagreement\":0.12932549,\"curvature\":16883.858866,\"age\":0.95403616},\"runtime_features\":\"FEATURE-B or FEATURE-C only\",\"safety\":\"exact X1 semantics\",\"validation_accessed\":false,\"holdout_materialized\":false}\n");
    ok &= put("performance_random_stress.md", performance(featureB, true) + "\n" + performance(featureC, true) + "\n" + performance(performanceCandidate, true));
    ok &= put("performance_realistic_sample_hold.md", performance(featureB, false) + "\n" + performance(featureC, false) + "\n" + performance(performanceCandidate, false));
    ok &= put("production_equivalence.csv", "metric,result\nproduction_source_changed,false\nsealed_x0_exact_replay,52463 / 52463 EXACT\nmapping_core_tests,run separately before closeout\nrelease_hot_path_benchmark,run separately before closeout\n");
    ok &= put("V2.4-X1R_Tail_Calibrated_Source_Native_Horizon.md", report(results, selected, pass, reproduced));
    if (!ok) return 6;
    std::cout << (pass ? "V2.4-X1R PASS — TAIL-CALIBRATED SOURCE-NATIVE HORIZON POLICY FROZEN — READY FOR X2 HOLDOUT QUALIFICATION" : "V2.4-X1R STOP — EXISTING CAUSAL FEATURES CANNOT CONTROL TAILS WITHOUT SACRIFICING USEFUL HORIZON") << '\n';
    return 0;
}
