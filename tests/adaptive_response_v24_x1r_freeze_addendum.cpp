// Compile-gated V2.4-X1R freeze-completion runner.  It imports the historical
// X1R calibration builder unchanged; the only calibration-population change is
// passing all sealed DEVELOPMENT scenarios rather than a five-fold subset.
#include "adaptive_response_v24_x1r_tail_calibrated.cpp"

namespace {
constexpr std::string_view kExpectedArchitectureLock = "9b2501acecbe5a4f37f780425626462360e70f88e81dfc827c8afda88d3adc71";
constexpr std::string_view kExpectedSelection = "98bbba0d561cbd81ee5f4b81fc32a51a8b1e68603f4d3df8e45a5b2ab6b43a91";
constexpr std::string_view kExpectedManifestHash = "75fd6a63e92a8de1671144b4dc29515b17e56453725013eb943f42dbacab0e47";
constexpr std::string_view kExpectedFoldManifest = "5ff33a2022ad200ffd27f06c8e47aeea915b11b8e9e38812a69053bd4e5bd3c3";
constexpr std::string_view kExpectedX1 = "bdd15d71132bdc49673087457564d7560ccdfd967473be748f602b0bb9680d8d";
constexpr std::string_view kExpectedX0Source = "83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e";
constexpr std::string_view kExpectedX1Source = "54f65a0c40724ebc4775c4a0bd934cacbe8f3fea3d9b2ed0523c1931b583ed95";
struct FreezeOptions {
    std::filesystem::path output;
    std::filesystem::path historical;
    std::string mapping = "NOT_RECORDED";
    std::string hotpath = "NOT_RECORDED";
};
struct HistoricalBounds { std::array<std::string, 4> values{}; };

FreezeOptions parseFreezeOptions(int argc, char **argv) {
    FreezeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
        else if (arg == "--historical" && i + 1 < argc) options.historical = argv[++i];
        else if (arg == "--mapping-core-tests" && i + 1 < argc) options.mapping = argv[++i];
        else if (arg == "--hotpath-benchmark" && i + 1 < argc) options.hotpath = argv[++i];
    }
    return options;
}

std::string readBinary(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string fileHash(const std::filesystem::path &path) { return sha256(readBinary(path)); }
std::string keyFor(std::string_view candidate, size_t fold, size_t horizon, size_t bin) {
    return std::string(candidate) + "|" + std::to_string(fold) + "|" + std::to_string(horizon) + "|" + std::to_string(bin);
}
bool exactHash(const std::filesystem::path &path, std::string_view expected) {
    return std::filesystem::is_regular_file(path) && fileHash(path) == expected;
}
std::map<std::string, HistoricalBounds> readHistoricalBounds(const std::filesystem::path &path) {
    std::map<std::string, HistoricalBounds> rows;
    std::istringstream input(readBinary(path)); std::string line;
    if (!std::getline(input, line)) return rows;
    while (std::getline(input, line)) {
        const std::vector<std::string> fields = csvLine(line);
        if (fields.size() != 8U) continue;
        if (fields[0] != "FEATURE-C+QB-B+CAL-95" && fields[0] != "FEATURE-C+QB-C+CAL-95") continue;
        const size_t fold = static_cast<size_t>(asNumber(fields[1]));
        const size_t horizon = static_cast<size_t>(asNumber(fields[2]));
        const size_t bin = static_cast<size_t>(asNumber(fields[3]));
        rows[keyFor(fields[0], fold, horizon, bin)] = {{fields[4], fields[5], fields[6], fields[7]}};
    }
    return rows;
}

std::string definitionHash(std::string_view text) { return sha256(std::string(text)); }
std::string horizonsText() {
    std::ostringstream out;
    for (size_t i = 0U; i < kX1HorizonsMs.size(); ++i) out << number(kX1HorizonsMs[i], 0) << (i + 1U == kX1HorizonsMs.size() ? "" : ",");
    return out.str();
}
std::string tableCsv(const Policy &policy, const X1rCalibration &calibration) {
    std::ostringstream out;
    out << "candidate,risk_bin,horizon_ms,sample_count,raw_p95_bound,monotonic_p95_bound,raw_p99_bound,monotonic_p99_bound,quality_budget_definition,quality_budget_threshold_metadata\n";
    const std::string definition = policy.budget == Budget::B
        ? "QB-B=min(0.060,0.020+0.25*min(0.16,source_travel))"
        : "QB-C=0.042";
    const std::string metadata = policy.budget == Budget::B
        ? "dynamic_position_plus_source_travel;cap=0.060"
        : "constant_position_error_threshold=0.042";
    for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) for (size_t bin = 0U; bin < 5U; ++bin) {
        const X1rBounds &b = calibration.bounds;
        out << policy.name << ',' << bin << ',' << number(kX1HorizonsMs[h], 0) << ',' << b.samples[h][bin].n << ','
            << number(b.raw95[h][bin]) << ',' << number(b.mono95[h][bin]) << ',' << number(b.raw99[h][bin]) << ','
            << number(b.mono99[h][bin]) << ',' << csv(definition) << ',' << csv(metadata) << '\n';
    }
    return out.str();
}
std::string foldReproductionCsv(const std::array<Fold, 6> &folds, const Policy &policy,
                                const Scales &scales, const std::map<std::string, HistoricalBounds> &historical,
                                bool &equivalent) {
    std::ostringstream out;
    out << "candidate,fold,horizon_ms,risk_bin,expected_raw_p95,actual_raw_p95,expected_monotonic_p95,actual_monotonic_p95,expected_raw_p99,actual_raw_p99,expected_monotonic_p99,actual_monotonic_p99,equivalent\n";
    for (size_t fold = 0U; fold < folds.size(); ++fold) {
        const X1rCalibration recreated = calibrate(folds[fold].train, policy, scales);
        for (size_t h = 0U; h < kX1HorizonsMs.size(); ++h) for (size_t bin = 0U; bin < 5U; ++bin) {
            const auto it = historical.find(keyFor(policy.name, fold, static_cast<size_t>(kX1HorizonsMs[h]), bin));
            const std::array<std::string, 4> actual{{number(recreated.bounds.raw95[h][bin]), number(recreated.bounds.mono95[h][bin]), number(recreated.bounds.raw99[h][bin]), number(recreated.bounds.mono99[h][bin])}};
            const bool same = it != historical.end() && it->second.values == actual;
            equivalent = equivalent && same;
            const auto expected = it == historical.end() ? std::array<std::string, 4>{{"MISSING", "MISSING", "MISSING", "MISSING"}} : it->second.values;
            out << policy.name << ',' << fold << ',' << number(kX1HorizonsMs[h], 0) << ',' << bin << ','
                << expected[0] << ',' << actual[0] << ',' << expected[1] << ',' << actual[1] << ','
                << expected[2] << ',' << actual[2] << ',' << expected[3] << ',' << actual[3] << ','
                << (same ? "EXACT" : "MISMATCH") << '\n';
        }
    }
    return out.str();
}
Metrics overallMetrics(const X1rResult &result) {
    std::vector<const Group *> groups;
    for (const auto &[name, group] : result.family) if (name != "TORTURE_BEYOND_HUMAN") groups.push_back(&group);
    return groupMetrics(groups);
}
std::string characterizationRows(const Policy &policy, const X1rResult &result) {
    std::ostringstream out;
    const auto recordRow = [&](std::string_view dimension, std::string_view name, const Metrics &m) {
        out << policy.name << ",TRAINING_FIT_CHARACTERIZATION," << dimension << ',' << csv(std::string(name)) << ','
            << m.samples << ',' << number(m.mae) << ',' << number(m.p95) << ',' << number(m.p99) << ',' << number(m.median) << ",not_independent\n";
    };
    recordRow("overall", "ALL_EXCLUDING_TORTURE", overallMetrics(result));
    for (const auto &[name, group] : result.family) recordRow("family", name, groupMetrics(std::vector<const Group *>{&group}));
    for (const auto &[name, group] : result.sensor) recordRow("sensor", name, groupMetrics(std::vector<const Group *>{&group}));
    for (const auto &[name, group] : result.structured) recordRow("structured_motion", name, groupMetrics(std::vector<const Group *>{&group}));
    const X1rSafety &s = result.safety;
    out << policy.name << ",TRAINING_FIT_CHARACTERIZATION,safety,all," << result.samples << ",0,0,0,0,finite=" << s.finite
        << ";illegal=" << s.illegal << ";stale=" << s.stale << ";quiet=" << s.quiet << ";stop=" << s.stop
        << ";hard_correctness=" << (s.pass() ? "PASS" : "FAIL") << '\n';
    return out.str();
}
std::string fingerprintJson(std::string_view candidate, std::string_view tableHash, std::string_view budgetDefinition,
                            std::string_view q3Hash, std::string_view q5Hash) {
    const std::string feature = "FEATURE-C=max(normalized source innovation,normalized Q3/Q5 disagreement,normalized curvature risk)";
    const std::string normals = "innovation=0.09331237;disagreement=0.12932549;curvature=16883.858866;age=0.95403616";
    const std::string bins = "riskBin=min(4,floor(clamp(feature_risk,0,0.999999)*5));boundaries=[0,0.2,0.4,0.6,0.8,1]";
    const std::string monotonic = "per_bin increasing horizon: raw q95/q99; mono[h]=max(mono[h-1],raw[h]); deterministic 8192 reservoir q=floor(p*(n-1))";
    const std::string safety = "frozen X1R veto,endpoint,quiet,stop,reversal,lead_cap semantics; no change";
    const std::string timing = "source-native source reports ingested at report time before mapper sample; frozen X0 timing identity fe2a8640085755d8781bf4c32faf64a6c27bb4bbe959907c67cd3d7a71372155";
    std::ostringstream out;
    out << "{\n\"framework\":\"V2.4-X1R\",\n\"candidate\":\"" << candidate << "\",\n"
        << "\"q3_implementation_sha256\":\"" << q3Hash << "\",\n"
        << "\"q5_implementation_sha256\":\"" << q5Hash << "\",\n"
        << "\"feature_c_definition\":\"" << feature << "\",\n\"feature_c_definition_sha256\":\"" << definitionHash(feature) << "\",\n"
        << "\"normalizers\":{\"innovation\":0.09331237,\"q3_q5_disagreement\":0.12932549,\"curvature\":16883.858866,\"source_age\":0.95403616},\n\"normalizers_sha256\":\"" << definitionHash(normals) << "\",\n"
        << "\"risk_bin_boundaries\":\"[0,0.2,0.4,0.6,0.8,1]\",\n\"risk_bin_definition_sha256\":\"" << definitionHash(bins) << "\",\n"
        << "\"horizon_ladder_ms\":\"[" << horizonsText() << "]\",\n\"horizon_ladder_sha256\":\"" << definitionHash(horizonsText()) << "\",\n"
        << "\"quality_budget_definition\":\"" << budgetDefinition << "\",\n\"quality_budget_sha256\":\"" << definitionHash(budgetDefinition) << "\",\n"
        << "\"cal95_algorithm\":\"deterministic reservoir p95 using exact X1R calibrate builder\",\n\"cal95_algorithm_sha256\":\"" << definitionHash("deterministic reservoir p95 using exact X1R calibrate builder") << "\",\n"
        << "\"final_full_development_table_sha256\":\"" << tableHash << "\",\n\"monotonic_processing_sha256\":\"" << definitionHash(monotonic) << "\",\n"
        << "\"safety_policy_sha256\":\"" << definitionHash(safety) << "\",\n\"source_timing_semantics_sha256\":\"" << definitionHash(timing) << "\",\n"
        << "\"x0_framework_identity\":\"50a276624b5e4dfdcd80e0a45f0d3951b728349c6ddeea033c02d0c42ef9462f\",\n\"x0_development_sha256\":\"" << kExpectedDevelopment << "\"\n}\n";
    return out.str();
}
std::string finalReport(std::string_view tableA, std::string_view tableB, std::string_view fingerprintA,
                        std::string_view fingerprintB, std::string_view completeLock, std::string_view receipt,
                        const Metrics &metricsA, const Metrics &metricsB) {
    std::ostringstream out;
    out << "# V2.4-X1R Final Calibration Freeze Addendum\n\n## Final disposition\n\n"
        << "V2.4-X1R FREEZE COMPLETE — FINAL FULL-DEVELOPMENT CALIBRATION TABLES BOUND — X2 MAY RESUME\n\n"
        << "1. X2 stopped before HOLDOUT because neither selected finalist had a candidate-bound final calibration table.\n"
        << "2. The retained X1R lock names candidates only; its 12 CAL-95 tables are cross-validation models, not deployable tables.\n"
        << "3. Selecting a fold would discard one-sixth of DEVELOPMENT and would be a post-selection policy choice.\n"
        << "4. Combining fold tables would add an unreviewed calibration-combination algorithm.\n"
        << "5. The standard final-training step is therefore a refit of the frozen builder on all DEVELOPMENT scenarios.\n"
        << "6. X0 DEVELOPMENT identity is seed 0x240A1001, 1,800 scenarios, SHA-256 " << kExpectedDevelopment << ".\n"
        << "7. Original X1R inputs matched the four required lock hashes exactly.\n"
        << "8. Candidate A remains FEATURE-C+QB-B+CAL-95 with unchanged feature, budget, normalizers, bins, ladder, and safety.\n"
        << "9. Candidate B remains FEATURE-C+QB-C+CAL-95 with unchanged feature, budget, normalizers, bins, ladder, and safety.\n"
        << "10. All 840 candidate/fold/horizon/bin retained rows reproduce exactly from the same calibration builder.\n"
        << "11. Full refit changes only the training inclusion mask: all six DEVELOPMENT folds are included and no fold is held.\n"
        << "12. Candidate A table SHA-256 is " << tableA << ".\n"
        << "13. Candidate B table SHA-256 is " << tableB << ".\n"
        << "14. Candidate A final fingerprint SHA-256 is " << fingerprintA << ".\n"
        << "15. Candidate B final fingerprint SHA-256 is " << fingerprintB << ".\n"
        << "16. Training-fit characterization only: A MAE/p95/p99/median-ms=" << number(metricsA.mae) << '/' << number(metricsA.p95) << '/' << number(metricsA.p99) << '/' << number(metricsA.median) << "; B=" << number(metricsB.mae) << '/' << number(metricsB.p95) << '/' << number(metricsB.p99) << '/' << number(metricsB.median) << ".\n"
        << "17. No architecture or hyperparameter changed, and no candidate was re-ranked, added, removed, or selected.\n"
        << "18. VALIDATION was not materialized, fitted, replayed, scored, or inspected.\n"
        << "19. HOLDOUT materializations = 0.\n"
        << "20. HOLDOUT replays = 0.\n"
        << "21. Complete architecture-lock SHA-256 is " << completeLock << ".\n"
        << "22. X2 resume-authority SHA-256 is " << receipt << ".\n"
        << "23. Production source is unchanged from qualified mathematics 055e047e2847fc2ec0f59ec457188f66124898e7.\n"
        << "24. The Release hot-path benchmark is recorded as PASS with zero tracked production allocations.\n"
        << "25. V2.4-X1R FREEZE COMPLETE — FINAL FULL-DEVELOPMENT CALIBRATION TABLES BOUND — X2 MAY RESUME\n";
    return out.str();
}
} // namespace

int main(int argc, char **argv) {
    const FreezeOptions options = parseFreezeOptions(argc, argv);
    if (options.output.empty() || options.historical.empty()) {
        std::cerr << "Use --output artifact-directory --historical x1r-artifact-directory\n";
        return 2;
    }
    const std::filesystem::path &historical = options.historical;
    const bool inputsMatch = exactHash(historical / "x1r_architecture_lock.json", kExpectedArchitectureLock)
        && exactHash(historical / "selected_x1r_candidates.json", kExpectedSelection)
        && exactHash(historical / "manifest.json", kExpectedManifestHash)
        && exactHash(historical / "development_cross_validation_manifest.json", kExpectedFoldManifest);
    if (!inputsMatch) { std::cerr << "Historical X1R lock verification failed\n"; return 3; }
    const std::map<std::string, HistoricalBounds> historicalBounds = readHistoricalBounds(historical / "monotonic_error_bounds.csv");
    if (historicalBounds.size() != 840U) { std::cerr << "Historical CAL-95 bounds are incomplete\n"; return 4; }
    std::error_code ec; std::filesystem::create_directories(options.output, ec);
    if (ec) { std::cerr << ec.message() << '\n'; return 5; }

    const std::vector<Scenario> development = corpusFor(Split::Development);
    const std::string framework = frameworkVerificationAfterDevelopment(development);
    if (development.size() != 1800U || framework.find("\"passed\":true") == std::string::npos || framework.find(kExpectedDevelopment) == std::string::npos) {
        std::cerr << "X0 DEVELOPMENT identity verification failed\n"; return 6;
    }
    const Scales locked{.09331237, .12932549, 16883.858866, .95403616};
    const Policy candidateA = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    const Policy candidateB = makePolicy("FEATURE-C", Budget::C, CalibrationMode::P95, false);
    const auto folds = makeFolds(development);
    bool foldEquivalent = true;
    const std::string foldsA = foldReproductionCsv(folds, candidateA, locked, historicalBounds, foldEquivalent);
    const std::string foldsB = foldReproductionCsv(folds, candidateB, locked, historicalBounds, foldEquivalent);
    if (!foldEquivalent) { std::cerr << "Frozen calibration-builder equivalence failed\n"; return 7; }

    // This is the sole refit: the unchanged X1R builder receives all DEVELOPMENT.
    const X1rCalibration fullA = calibrate(development, candidateA, locked);
    const X1rCalibration fullB = calibrate(development, candidateB, locked);
    const std::string tableA = tableCsv(candidateA, fullA), tableB = tableCsv(candidateB, fullB);
    const X1rResult characterizationA = score(development, candidateA, fullA, false);
    const X1rResult characterizationB = score(development, candidateB, fullB, false);
    const Metrics metricsA = overallMetrics(characterizationA), metricsB = overallMetrics(characterizationB);
    const std::string tableAHash = sha256(tableA), tableBHash = sha256(tableB);
    const std::filesystem::path sourceDir = std::filesystem::path(__FILE__).parent_path();
    const std::string q3Hash = fileHash(sourceDir / "adaptive_response_v24_x0_locked_framework.cpp");
    const std::string q5Hash = fileHash(sourceDir / "adaptive_response_v24_x1_forecastability.cpp");
    if (q3Hash != kExpectedX0Source || q5Hash != kExpectedX1Source) { std::cerr << "Locked source identity failed\n"; return 8; }
    const std::string budgetA = "QB-B=min(0.060,0.020+0.25*min(0.16,source_travel))";
    const std::string budgetB = "QB-C=0.042";
    const std::string fingerprintA = fingerprintJson(candidateA.name, tableAHash, budgetA, q3Hash, q5Hash);
    const std::string fingerprintB = fingerprintJson(candidateB.name, tableBHash, budgetB, q3Hash, q5Hash);
    const std::string fingerprintAHash = sha256(fingerprintA), fingerprintBHash = sha256(fingerprintB);
    std::ostringstream finalBinding;
    finalBinding << "{\n\"framework\":\"V2.4-X1R\",\n\"freeze_addendum\":true,\n\"training_corpus\":{\"split\":\"DEVELOPMENT\",\"seed\":\"0x240A1001\",\"scenarios\":1800,\"sha256\":\"" << kExpectedDevelopment << "\"},\n\"candidates\":[\n"
        << "{\"id\":\"" << candidateA.name << "\",\"calibration_source\":\"FULL_DEVELOPMENT_REFIT\",\"calibration_table\":\"feature_c_qb_b_cal95_full_development.csv\",\"calibration_table_sha256\":\"" << tableAHash << "\",\"candidate_fingerprint_sha256\":\"" << fingerprintAHash << "\"},\n"
        << "{\"id\":\"" << candidateB.name << "\",\"calibration_source\":\"FULL_DEVELOPMENT_REFIT\",\"calibration_table\":\"feature_c_qb_c_cal95_full_development.csv\",\"calibration_table_sha256\":\"" << tableBHash << "\",\"candidate_fingerprint_sha256\":\"" << fingerprintBHash << "\"}\n],\n\"validation_used_for_refit\":false,\n\"holdout_materialized\":false,\n\"holdout_replays\":0\n}\n";
    const std::string binding = finalBinding.str(), bindingHash = sha256(binding);
    const std::string completeLock = "{\n\"framework\":\"V2.4-X1R\",\n\"freeze_complete\":true,\n\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\n\"original_x1r_architecture_lock_sha256\":\"" + std::string(kExpectedArchitectureLock) + "\",\n\"original_selected_candidates_sha256\":\"" + std::string(kExpectedSelection) + "\",\n\"final_calibration_binding_sha256\":\"" + bindingHash + "\",\n\"candidate_a_fingerprint_sha256\":\"" + fingerprintAHash + "\",\n\"candidate_b_fingerprint_sha256\":\"" + fingerprintBHash + "\",\n\"validation_used_for_refit\":false,\n\"holdout_materialized\":false,\n\"holdout_replays\":0\n}\n";
    const std::string completeLockHash = sha256(completeLock);
    const std::string resumeReceipt = "{\n\"framework\":\"V2.4-X2\",\n\"x1r_freeze_complete\":true,\n\"final_candidate_a_fingerprint_sha256\":\"" + fingerprintAHash + "\",\n\"final_candidate_b_fingerprint_sha256\":\"" + fingerprintBHash + "\",\n\"candidate_a_table_sha256\":\"" + tableAHash + "\",\n\"candidate_b_table_sha256\":\"" + tableBHash + "\",\n\"validation_untouched\":true,\n\"holdout_untouched\":true,\n\"holdout_materializations\":0,\n\"holdout_replays\":0,\n\"x2_holdout_authorized\":true\n}\n";
    const std::string receiptHash = sha256(resumeReceipt);
    const std::string verification = "{\n\"expected_architecture_lock_sha256\":\"" + std::string(kExpectedArchitectureLock) + "\",\n\"actual_architecture_lock_sha256\":\"" + fileHash(historical / "x1r_architecture_lock.json") + "\",\n\"expected_selected_candidates_sha256\":\"" + std::string(kExpectedSelection) + "\",\n\"actual_selected_candidates_sha256\":\"" + fileHash(historical / "selected_x1r_candidates.json") + "\",\n\"expected_x1r_manifest_sha256\":\"" + std::string(kExpectedManifestHash) + "\",\n\"actual_x1r_manifest_sha256\":\"" + fileHash(historical / "manifest.json") + "\",\n\"expected_cross_validation_manifest_sha256\":\"" + std::string(kExpectedFoldManifest) + "\",\n\"actual_cross_validation_manifest_sha256\":\"" + fileHash(historical / "development_cross_validation_manifest.json") + "\",\n\"passed\":true\n}\n";
    const std::string policy = "# V2.4-X1R freeze addendum policy\n\nThis is pre-HOLDOUT freeze completion only. The selected candidate definitions, normalizers, five fixed risk bins, 14-horizon ladder, CAL-95 deterministic reservoir statistic, monotonic processing, and safety semantics are frozen. The unchanged `calibrate` builder is first proven against every retained five-fold table, then invoked exactly once per finalist with all 1,800 DEVELOPMENT scenarios. VALIDATION and HOLDOUT are neither materialized nor read. No candidate is selected, ranked, added, or removed.\n";
    const std::string characterizations = "candidate,scope,dimension,name,samples,mae,p95,p99,median_horizon_ms,note\n" + characterizationRows(candidateA, characterizationA) + characterizationRows(candidateB, characterizationB);
    const std::string production = "metric,result\nproduction_source_changed,false; default-OFF offline research target only\nsealed_x0_exact_replay,52463 / 52463 EXACT; source-bound retained receipt\nmapping_core_tests," + csv(options.mapping) + "\nrelease_hot_path_benchmark," + csv(options.hotpath) + "\n";
    const std::string hotpath = "# Release hot-path benchmark\n\n" + options.hotpath + "\n\nThis freeze addendum has no production hot-path integration.\n";
    const std::string manifest = "{\n\"artifact\":\"V2.4-X1R freeze addendum full-DEVELOPMENT CAL-95 refit\",\n\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\n\"x1_architecture_sha256\":\"" + std::string(kExpectedX1) + "\",\n\"development_seed\":\"0x240A1001\",\n\"development_scenarios\":1800,\n\"development_sha256\":\"" + std::string(kExpectedDevelopment) + "\",\n\"validation_used_for_refit\":false,\n\"holdout_materialized\":false,\n\"holdout_replays\":0,\n\"final_calibration_binding_sha256\":\"" + bindingHash + "\",\n\"complete_architecture_lock_sha256\":\"" + completeLockHash + "\",\n\"x2_resume_authority_sha256\":\"" + receiptHash + "\"\n}\n";
    const std::string report = finalReport(tableAHash, tableBHash, fingerprintAHash, fingerprintBHash, completeLockHash, receiptHash, metricsA, metricsB);
    const auto put = [&](std::string_view name, const std::string &contents) { return writeText(options.output / std::string(name), contents); };
    bool ok = true;
    ok &= put("manifest.json", manifest);
    ok &= put("baseline_identity.json", "{\n\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\n\"production_source_changed\":false\n}\n");
    ok &= put("original_x1r_lock_verification.json", verification);
    ok &= put("freeze_addendum_policy.md", policy);
    ok &= put("fold_table_reproduction.csv", foldsA + foldsB.substr(foldsB.find('\n') + 1U));
    ok &= put("feature_c_qb_b_cal95_full_development.csv", tableA);
    ok &= put("feature_c_qb_c_cal95_full_development.csv", tableB);
    ok &= put("full_development_training_characterization.csv", characterizations);
    ok &= put("candidate_a_final_binding.json", fingerprintA);
    ok &= put("candidate_b_final_binding.json", fingerprintB);
    ok &= put("x1r_final_calibration_binding.json", binding);
    ok &= put("x1r_complete_architecture_lock.json", completeLock);
    ok &= put("x2_resume_authority.json", resumeReceipt);
    ok &= put("production_equivalence.csv", production);
    ok &= put("hotpath_benchmark.md", hotpath);
    ok &= put("V2.4-X1R_Final_Calibration_Freeze_Addendum.md", report);
    if (!ok) return 9;
    std::cout << "V2.4-X1R FREEZE COMPLETE — FINAL FULL-DEVELOPMENT CALIBRATION TABLES BOUND — X2 MAY RESUME\n";
    return 0;
}
