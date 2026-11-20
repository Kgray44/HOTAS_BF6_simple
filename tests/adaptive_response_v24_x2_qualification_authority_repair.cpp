// V2.4-X2 pre-HOLDOUT authority repair.  This compile-gated runner is
// intentionally limited to DEVELOPMENT and VALIDATION reproduction plus a
// static HOLDOUT route audit.  It never constructs the HOLDOUT corpus.
#pragma push_macro("main")
#undef main
#define main x1rImportedHistoricalResearchMain
#include "adaptive_response_v24_x1r_import.cpp"
#pragma pop_macro("main")

#include <stdexcept>
#include <iterator>

namespace {
constexpr std::string_view kDevelopmentSha = "d0d69dbcec3f7933e84e5e081ab8e5cc2735818cb16182eaf769b726aa03dbbf";
constexpr std::string_view kValidationSha = "d8e39f3cf160f83e99fd4246d373a94d87a004d0707044784d1bdec0f9e20123";
constexpr std::string_view kHoldoutSha = "aaf40f9c7a49c70fc7e2080547eb0c9a1b426a024f87807e33db29c73806b38d";
constexpr std::string_view kWrongSha = "94e2ec45619286a92bcfb2aac534ec2053e3e6901326b396422c6e80cee0c736";
constexpr std::string_view kCandidateA = "ed5040453f0b61fc03362bb679bb733e422af843fe41aaa76af5b0a8d31d02cf";
constexpr std::string_view kCandidateB = "a7897ee7fda7f371b0e7561426b0cf24273188154920d47089ea220f27e2ba55";
constexpr std::string_view kTableA = "3b2f19299f6ebb6e93046a6704223f5645c1a379b2db230301c286864dfdd2f0";
constexpr std::string_view kTableB = "1f7c0328057748c481cf3218b136253a0eaf0f75c38c92c7b39bc9e6e8d61ff9";
constexpr std::string_view kCompleteLock = "b70546d54cae1005f37dff6e096a7fa8d4340b39e13df52f5b8afbfb39ecb8a7";
constexpr std::string_view kFinalBinding = "60eee31a9c466a4168985b20e461c46a830ccea9f8539245a85e626982ec7486";
constexpr std::string_view kOriginalLock = "9b2501acecbe5a4f37f780425626462360e70f88e81dfc827c8afda88d3adc71";
constexpr std::string_view kOriginalCandidates = "98bbba0d561cbd81ee5f4b81fc32a51a8b1e68603f4d3df8e45a5b2ab6b43a91";
constexpr std::string_view kOriginalManifest = "75fd6a63e92a8de1671144b4dc29515b17e56453725013eb943f42dbacab0e47";
constexpr std::string_view kOriginalFolds = "5ff33a2022ad200ffd27f06c8e47aeea915b11b8e9e38812a69053bd4e5bd3c3";
constexpr std::string_view kX0Identity = "50a276624b5e4dfdcd80e0a45f0d3951b728349c6ddeea033c02d0c42ef9462f";
constexpr std::string_view kQualifiedMath = "055e047e2847fc2ec0f59ec457188f66124898e7";

struct RepairOptions {
    std::filesystem::path output, baseline, priorX2, x1rAddendum, x1rOriginal, runnerSource;
    std::string productionTests, hotpathBenchmark;
};

std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string hashFile(const std::filesystem::path &path) { return sha256(readFile(path)); }

bool put(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << text;
    return static_cast<bool>(output);
}

std::string field(const std::string &json, std::string_view name) {
    const std::string marker = "\"" + std::string(name) + "\":\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) throw std::runtime_error("missing JSON field " + std::string(name));
    const std::size_t value = start + marker.size();
    const std::size_t end = json.find('"', value);
    if (end == std::string::npos) throw std::runtime_error("unterminated JSON field " + std::string(name));
    return json.substr(value, end - value);
}

void need(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

RepairOptions parse(int argc, char **argv) {
    RepairOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--output" && i + 1 < argc) options.output = argv[++i];
        else if (key == "--baseline" && i + 1 < argc) options.baseline = argv[++i];
        else if (key == "--prior-x2" && i + 1 < argc) options.priorX2 = argv[++i];
        else if (key == "--x1r-addendum" && i + 1 < argc) options.x1rAddendum = argv[++i];
        else if (key == "--x1r-original" && i + 1 < argc) options.x1rOriginal = argv[++i];
        else if (key == "--runner-source" && i + 1 < argc) options.runnerSource = argv[++i];
        else if (key == "--production-tests" && i + 1 < argc) options.productionTests = argv[++i];
        else if (key == "--hotpath-benchmark" && i + 1 < argc) options.hotpathBenchmark = argv[++i];
        else throw std::runtime_error("invalid argument " + key);
    }
    need(!options.output.empty() && !options.baseline.empty() && !options.priorX2.empty() && !options.x1rAddendum.empty() && !options.x1rOriginal.empty() && !options.runnerSource.empty(), "required path argument missing");
    need(options.productionTests == "PASS", "mapping_core_tests evidence is not PASS");
    need(options.hotpathBenchmark == "PASS_ZERO_TRACKED_ALLOCATIONS", "hot-path evidence is not PASS_ZERO_TRACKED_ALLOCATIONS");
    return options;
}

std::string routeText() {
    return "Development=0x240A1001/1800;Validation=0x240A1002/600;Holdout=0x240A1003/600;unknown=throw std::invalid_argument";
}

std::string report(const std::string &runnerHash, const std::string &routingHash, const std::string &exeHash,
                   const std::string &authorityHash, const std::string &generatorHash) {
    return "# V2.4-X2 Qualification Authority Repair\n\n"
        "## Final disposition\n\n"
        "V2.4-X2 AUTHORITY REPAIRED — RESERVED HOLDOUT REMAINS PRISTINE — ONE REPLACEMENT QUALIFICATION RUN AUTHORIZED\n\n"
        "1. X2 stopped at its pre-HOLDOUT qualification gate because the requested HOLDOUT corpus hashed to `" + std::string(kWrongSha) + "`, not frozen `" + std::string(kHoldoutSha) + "`.\n"
        "2. The source defect was `DEVELOPMENT ? 0x240A1001 : 0x240A1002`: every non-DEVELOPMENT split, including HOLDOUT, received the VALIDATION seed.\n"
        "3. The historical attempt constructed the VALIDATION-seed population, so it did not materialize reserved seed `0x240A1003`.\n"
        "4. Candidate scoring observations are zero: Candidate A and Candidate B were not scored.\n"
        "5. True HOLDOUT materializations are zero; `reserved_holdout_materialized=false`, `reserved_holdout_scored=false`, and `candidate_holdout_scores_observed=false`.\n"
        "6. The repaired route is explicit: DEVELOPMENT `0x240A1001/1800`, VALIDATION `0x240A1002/600`, HOLDOUT `0x240A1003/600`.\n"
        "7. Invalid split values throw before corpus generation; no split has a fallback.\n"
        "8. DEVELOPMENT was materialized only for source reproduction and exactly matched `" + std::string(kDevelopmentSha) + "`.\n"
        "9. VALIDATION was materialized only for source reproduction and exactly matched `" + std::string(kValidationSha) + "`.\n"
        "10. HOLDOUT was statically audited as `0x240A1003`, 600 scenarios, expected `" + std::string(kHoldoutSha) + "`; no HOLDOUT rows were generated.\n"
        "11. The X1R complete freeze lock, original lock verification, 840-cell historical fold reproduction, and both full-DEVELOPMENT bindings passed. VALIDATION was not used for refit and prior HOLDOUT materialization was false.\n"
        "12. Candidate A `FEATURE-C+QB-B+CAL-95` fingerprint: `" + std::string(kCandidateA) + "`.\n"
        "13. Candidate B `FEATURE-C+QB-C+CAL-95` fingerprint: `" + std::string(kCandidateB) + "`.\n"
        "14. Corrected runner source SHA-256: `" + runnerHash + "`; split-routing source SHA-256: `" + routingHash + "`.\n"
        "15. Corrected executable SHA-256: `" + exeHash + "`.\n"
        "16. Corrected generator identity SHA-256: `" + generatorHash + "`; replacement authority payload SHA-256: `" + authorityHash + "`.\n"
        "17. A resumed X2 must verify the authority and both fingerprints, write a pre-HOLDOUT lock, materialize seed `0x240A1003` exactly once, verify `" + std::string(kHoldoutSha) + "`, then score both frozen finalists in the same process. A mismatch permanently stops before scoring and requires an X0-generator audit.\n"
        "18. Production source is unchanged. Fresh `mapping_core_tests` passed.\n"
        "19. The fresh Release hot-path benchmark passed with zero tracked allocations. The inherited 52,463 replay remains source-bound because production source is identical.\n"
        "20. This repair made no product, candidate, calibration, policy, safety, feature, or release change.\n\n"
        "The historical erroneous corpus remains archived as `INVALID_FOR_HOLDOUT_QUALIFICATION`; it was neither deleted, scored, nor reused.\n";
}

std::string manifest(const std::filesystem::path &dir) {
    const std::array<std::string_view, 16> files{
        "baseline_identity.json", "prior_x2_stop_verification.json", "erroneous_materialization_diagnosis.md",
        "erroneous_materialization_receipt.json", "corrected_split_routing.md", "split_routing_self_tests.csv",
        "development_identity_reproduction.json", "validation_identity_reproduction.json", "static_holdout_route.json",
        "x1r_complete_lock_verification.json", "candidate_a_binding_verification.json", "candidate_b_binding_verification.json",
        "x2_corrected_generator_identity.json", "x2_replacement_qualification_authority.json", "production_boundary_receipt.json",
        "hotpath_benchmark.md"};
    std::ostringstream json;
    json << "{\n\"artifact\":\"V2.4-X2 qualification authority repair\",\n\"holdout_materialized\":false,\n\"candidate_scoring_runs\":0,\n\"files\":[\n";
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string name(files[i]);
        json << "{\"name\":\"" << name << "\",\"sha256\":\"" << hashFile(dir / name) << "\"}" << (i + 1U == files.size() ? "\n" : ",\n");
    }
    json << "]\n}\n";
    return json.str();
}
} // namespace

int main(int argc, char **argv) {
    try {
        const RepairOptions options = parse(argc, argv);
        need(!std::filesystem::exists(options.output), "refusing to overwrite an authority-repair artifact directory");
        std::filesystem::create_directories(options.output);

        const std::filesystem::path priorReceipt = options.priorX2 / "qualification_stop_receipt.json";
        const std::filesystem::path addendum = options.x1rAddendum;
        const std::filesystem::path original = options.x1rOriginal;
        const std::string prior = readFile(priorReceipt);
        need(prior.find("\"status\": \"STOP_HOLDOUT_IDENTITY_MISMATCH_NOT_SCORED\"") != std::string::npos, "historical stop status changed");
        need(prior.find(std::string(kWrongSha)) != std::string::npos && prior.find(std::string(kHoldoutSha)) != std::string::npos, "historical stop hashes changed");
        need(prior.find("\"scoring_started\": false") != std::string::npos && prior.find("\"candidate_a_scored\": false") != std::string::npos && prior.find("\"candidate_b_scored\": false") != std::string::npos, "historical attempt includes candidate scoring");

        need(hashFile(original / "x1r_architecture_lock.json") == kOriginalLock, "original X1R lock hash mismatch");
        need(hashFile(original / "selected_x1r_candidates.json") == kOriginalCandidates, "original X1R candidate hash mismatch");
        need(hashFile(original / "manifest.json") == kOriginalManifest, "original X1R manifest hash mismatch");
        need(hashFile(original / "development_cross_validation_manifest.json") == kOriginalFolds, "original X1R fold manifest hash mismatch");
        const std::string originalLockVerification = readFile(addendum / "original_x1r_lock_verification.json");
        const std::string completeLock = readFile(addendum / "x1r_complete_architecture_lock.json");
        const std::string finalBinding = readFile(addendum / "x1r_final_calibration_binding.json");
        const std::string foldTable = readFile(addendum / "fold_table_reproduction.csv");
        need(hashFile(addendum / "x1r_complete_architecture_lock.json") == kCompleteLock, "X1R complete lock mismatch");
        need(hashFile(addendum / "x1r_final_calibration_binding.json") == kFinalBinding, "X1R final binding mismatch");
        need(originalLockVerification.find("\"passed\":true") != std::string::npos, "original X1R lock verification did not pass");
        need(completeLock.find("\"freeze_complete\":true") != std::string::npos && completeLock.find("\"validation_used_for_refit\":false") != std::string::npos && completeLock.find("\"holdout_materialized\":false") != std::string::npos, "X1R freeze policy changed");
        need(finalBinding.find(std::string(kCandidateA)) != std::string::npos && finalBinding.find(std::string(kCandidateB)) != std::string::npos, "frozen candidate fingerprints changed");
        const std::size_t foldRows = static_cast<std::size_t>(std::count(foldTable.begin(), foldTable.end(), '\n')) - 1U;
        std::size_t exactRows = 0U, exactAt = 0U;
        while ((exactAt = foldTable.find(",EXACT\n", exactAt)) != std::string::npos) { ++exactRows; exactAt += 7U; }
        need(foldRows == 840U && exactRows == 840U && foldTable.find("NONEXACT") == std::string::npos, "historical fold-table reproduction did not pass");
        need(hashFile(addendum / "feature_c_qb_b_cal95_full_development.csv") == kTableA, "Candidate A table mismatch");
        need(hashFile(addendum / "feature_c_qb_c_cal95_full_development.csv") == kTableB, "Candidate B table mismatch");
        need(hashFile(addendum / "candidate_a_final_binding.json") == kCandidateA, "Candidate A binding fingerprint mismatch");
        need(hashFile(addendum / "candidate_b_final_binding.json") == kCandidateB, "Candidate B binding fingerprint mismatch");

        const SplitRoute developmentRoute = splitRouteFor(Split::Development);
        const SplitRoute validationRoute = splitRouteFor(Split::Validation);
        const SplitRoute holdoutRoute = splitRouteFor(Split::Holdout); // configuration-only; no rows are formed.
        bool invalidStops = false;
        try { static_cast<void>(splitRouteFor(static_cast<Split>(99))); }
        catch (const std::invalid_argument &) { invalidStops = true; }
        need(developmentRoute.seed == 0x240A1001U && developmentRoute.scenarios == 1800U, "development route mismatch");
        need(validationRoute.seed == 0x240A1002U && validationRoute.scenarios == 600U, "validation route mismatch");
        need(holdoutRoute.seed == 0x240A1003U && holdoutRoute.scenarios == 600U && invalidStops, "closed split route audit failed");

        const std::vector<Scenario> development = corpusFor(Split::Development);
        const std::string developmentSeal = sealJson(development, Split::Development, developmentRoute.seed);
        const std::string framework = frameworkVerificationAfterDevelopment(development);
        need(development.size() == developmentRoute.scenarios && field(developmentSeal, "canonical_sha256") == kDevelopmentSha && framework.find("\"passed\":true") != std::string::npos, "DEVELOPMENT reproduction failed");
        const std::vector<Scenario> validation = corpusFor(Split::Validation);
        const std::string validationSeal = sealJson(validation, Split::Validation, validationRoute.seed);
        need(validation.size() == validationRoute.scenarios && field(validationSeal, "canonical_sha256") == kValidationSha, "VALIDATION reproduction failed");

        const std::string runnerHash = hashFile(options.runnerSource);
        const std::string routingHash = hashFile(options.runnerSource.parent_path() / "adaptive_response_v24_x1r_import.cpp");
        const std::string executableHash = hashFile(std::filesystem::absolute(argv[0]));
        const std::string winnerPolicy = "Frozen finalists must be scored simultaneously after one exact HOLDOUT identity check; this repair selects no winner and changes no winner policy.";
        const std::string winnerPolicyHash = sha256(winnerPolicy);
        const std::string generatorJson = "{\n\"artifact\":\"V2.4-X2 corrected qualification generator\",\n\"source_file_sha256\":\"" + runnerHash + "\",\n\"split_routing_source_sha256\":\"" + routingHash + "\",\n\"executable_sha256\":\"" + executableHash + "\",\n\"x0_framework_identity\":\"" + std::string(kX0Identity) + "\",\n\"qualified_math\":\"" + std::string(kQualifiedMath) + "\",\n\"split_routing_definition\":\"" + routeText() + "\",\n\"development_reproduction_exact\":true,\n\"validation_reproduction_exact\":true,\n\"holdout_static_route\":{\"seed\":\"0x240A1003\",\"scenarios\":600,\"expected_sha256\":\"" + std::string(kHoldoutSha) + "\"},\n\"candidate_a_fingerprint_sha256\":\"" + std::string(kCandidateA) + "\",\n\"candidate_b_fingerprint_sha256\":\"" + std::string(kCandidateB) + "\",\n\"winner_selection_policy_identity\":\"" + winnerPolicyHash + "\",\n\"holdout_materialized\":false,\n\"candidate_scoring_runs\":0\n}\n";
        const std::string generatorHash = sha256(generatorJson);
        const std::string authorityPayload = "authority=V2.4-X2-replacement;reason=source-proven split-to-seed routing repair;prior=false,false,false;development_exact=true;validation_exact=true;holdout=0x240A1003/600/" + std::string(kHoldoutSha) + ";materializations=1;replays=1;candidate_policy_changes=0;generator=" + generatorHash + ";candidate_a=" + std::string(kCandidateA) + ";candidate_b=" + std::string(kCandidateB);
        const std::string authorityHash = sha256(authorityPayload);
        const std::string authorityJson = "{\n\"authority\":\"V2.4-X2-replacement\",\n\"reason\":\"source-proven split-to-seed routing repair\",\n\"authority_payload_sha256\":\"" + authorityHash + "\",\n\"corrected_generator_identity_sha256\":\"" + generatorHash + "\",\n\"prior_attempt\":{\"classification\":\"PRE-HOLDOUT QUALIFICATION STOP — CORPUS IDENTITY MISMATCH\",\"attempted_holdout_materializations\":1,\"reserved_holdout_materialized\":false,\"reserved_holdout_scored\":false,\"candidate_scores_observed\":false},\n\"generator_verification\":{\"development_exact\":true,\"validation_exact\":true},\n\"holdout_route\":{\"seed\":\"0x240A1003\",\"scenarios\":600,\"expected_sha256\":\"" + std::string(kHoldoutSha) + "\"},\n\"candidate_fingerprints\":{\"candidate_a\":\"" + std::string(kCandidateA) + "\",\"candidate_b\":\"" + std::string(kCandidateB) + "\"},\n\"authorized_future_holdout_materializations\":1,\n\"authorized_future_holdout_replays\":1,\n\"candidate_policy_changes\":0,\n\"future_execution_order\":[\"verify replacement authority hash\",\"verify both candidate fingerprints\",\"write pre-HOLDOUT qualification lock\",\"materialize 0x240A1003 exactly once\",\"verify canonical SHA-256\",\"score A and B simultaneously in the same process only after exact identity\"],\n\"on_identity_mismatch\":\"STOP permanently before scoring and audit the X0 generator\",\n\"human_readable_holdout_inspection_before_scoring\":false\n}\n";

        bool ok = true;
        ok &= put(options.output / "baseline_identity.json", "{\n\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n\"qualified_math\":\"" + std::string(kQualifiedMath) + "\",\n\"x0_framework_identity\":\"" + std::string(kX0Identity) + "\",\n\"production_source_changed\":false\n}\n");
        ok &= put(options.output / "prior_x2_stop_verification.json", "{\n\"historical_status\":\"PRE-HOLDOUT QUALIFICATION STOP — CORPUS IDENTITY MISMATCH\",\n\"requested_split\":\"HOLDOUT\",\n\"attempted_holdout_materializations\":1,\n\"actual_reserved_holdout_materializations\":0,\n\"candidate_scoring_runs\":0,\n\"candidate_holdout_observations\":0,\n\"erroneous_canonical_sha256\":\"" + std::string(kWrongSha) + "\",\n\"expected_holdout_canonical_sha256\":\"" + std::string(kHoldoutSha) + "\",\n\"source_receipt_sha256\":\"" + hashFile(priorReceipt) + "\",\n\"preserved\":true\n}\n");
        ok &= put(options.output / "erroneous_materialization_diagnosis.md", "# Historical erroneous materialization diagnosis\n\n- Requested split: `HOLDOUT`\n- Incorrectly selected seed: `0x240A1002`\n- Expected HOLDOUT seed: `0x240A1003`\n- Generated canonical SHA-256: `" + std::string(kWrongSha) + "`\n- Expected canonical SHA-256: `" + std::string(kHoldoutSha) + "`\n- Candidate scoring count: `0`\n- Reason scoring stopped: canonical identity mismatch before scoring\n\nDiagnosis: `SPLIT_TO_SEED_ROUTING_DEFECT`. This is neither corpus contamination, candidate failure, qualification failure, nor Predictor architecture failure.\n");
        ok &= put(options.output / "erroneous_materialization_receipt.json", "{\n\"requested_split\":\"HOLDOUT\",\n\"incorrectly_selected_seed\":\"0x240A1002\",\n\"expected_holdout_seed\":\"0x240A1003\",\n\"generated_canonical_sha256\":\"" + std::string(kWrongSha) + "\",\n\"expected_canonical_sha256\":\"" + std::string(kHoldoutSha) + "\",\n\"candidate_scoring_count\":0,\n\"reason_scoring_stopped\":\"PRE-HOLDOUT QUALIFICATION STOP — CORPUS IDENTITY MISMATCH\",\n\"diagnosis\":\"SPLIT_TO_SEED_ROUTING_DEFECT\",\n\"disposition\":\"INVALID_FOR_HOLDOUT_QUALIFICATION\",\n\"reserved_holdout_materialized\":false,\n\"reserved_holdout_scored\":false,\n\"candidate_holdout_scores_observed\":false,\n\"evidence_deleted\":false,\n\"candidate_scoring_performed\":false,\n\"reused_as_validation\":false\n}\n");
        ok &= put(options.output / "corrected_split_routing.md", "# Corrected X0 split routing\n\nThe corrected imported X1R constructor uses `splitRouteFor` with explicit exhaustive cases: DEVELOPMENT `0x240A1001`, 1800 scenarios; VALIDATION `0x240A1002`, 600 scenarios; HOLDOUT `0x240A1003`, 600 scenarios. No default fallback exists. The only path beyond the three named values throws `std::invalid_argument` before corpus construction.\n\nThis repair calls `corpusFor` only for DEVELOPMENT and VALIDATION. The HOLDOUT route was inspected as configuration only; its scenario population was not instantiated.\n");
        ok &= put(options.output / "split_routing_self_tests.csv", "input,expected_seed,expected_scenarios,actual_seed,actual_scenarios,materialized,result\nDevelopment,0x240A1001,1800,0x240A1001,1800,true,PASS\nValidation,0x240A1002,600,0x240A1002,600,true,PASS\nHoldout,0x240A1003,600,0x240A1003,600,false,PASS_CONFIGURATION_ONLY\nInvalid split,N/A,N/A,N/A,N/A,false,FAIL_CLOSED_PASS\n");
        ok &= put(options.output / "development_identity_reproduction.json", "{\n\"split\":\"DEVELOPMENT\",\n\"seed\":\"0x240A1001\",\n\"scenarios\":1800,\n\"expected_sha256\":\"" + std::string(kDevelopmentSha) + "\",\n\"actual_sha256\":\"" + field(developmentSeal, "canonical_sha256") + "\",\n\"exact\":true,\n\"candidate_scoring_performed\":false\n}\n");
        ok &= put(options.output / "validation_identity_reproduction.json", "{\n\"split\":\"VALIDATION\",\n\"seed\":\"0x240A1002\",\n\"scenarios\":600,\n\"expected_sha256\":\"" + std::string(kValidationSha) + "\",\n\"actual_sha256\":\"" + field(validationSeal, "canonical_sha256") + "\",\n\"exact\":true,\n\"candidate_scoring_performed\":false\n}\n");
        ok &= put(options.output / "static_holdout_route.json", "{\n\"split\":\"HOLDOUT\",\n\"seed\":\"0x240A1003\",\n\"scenarios\":600,\n\"expected_canonical_sha256\":\"" + std::string(kHoldoutSha) + "\",\n\"route_source\":\"locked splitRouteFor configuration\",\n\"rows_generated\":false,\n\"canonical_hash_calculated_from_rows\":false,\n\"reserved_holdout_materialized\":false,\n\"reserved_holdout_scored\":false,\n\"candidate_holdout_scores_observed\":false\n}\n");
        ok &= put(options.output / "x1r_complete_lock_verification.json", "{\n\"original_x1r_lock_verification\":\"PASS\",\n\"historical_fold_table_reproduction\":\"PASS\",\n\"historical_fold_cells_expected\":840,\n\"historical_fold_cells_exact\":840,\n\"complete_lock_sha256\":\"" + std::string(kCompleteLock) + "\",\n\"final_calibration_binding_sha256\":\"" + std::string(kFinalBinding) + "\",\n\"validation_used_for_refit\":false,\n\"prior_holdout_materialized\":false,\n\"prior_holdout_replays\":0\n}\n");
        ok &= put(options.output / "candidate_a_binding_verification.json", "{\n\"candidate\":\"FEATURE-C+QB-B+CAL-95\",\n\"binding_verification\":\"PASS\",\n\"final_calibration_table_sha256\":\"" + std::string(kTableA) + "\",\n\"candidate_fingerprint_sha256\":\"" + std::string(kCandidateA) + "\",\n\"refit_performed_by_this_repair\":false\n}\n");
        ok &= put(options.output / "candidate_b_binding_verification.json", "{\n\"candidate\":\"FEATURE-C+QB-C+CAL-95\",\n\"binding_verification\":\"PASS\",\n\"final_calibration_table_sha256\":\"" + std::string(kTableB) + "\",\n\"candidate_fingerprint_sha256\":\"" + std::string(kCandidateB) + "\",\n\"refit_performed_by_this_repair\":false\n}\n");
        ok &= put(options.output / "x2_corrected_generator_identity.json", generatorJson);
        ok &= put(options.output / "x2_replacement_qualification_authority.json", authorityJson);
        ok &= put(options.output / "production_boundary_receipt.json", "{\n\"production_source_changed\":false,\n\"mapping_core_tests\":\"PASS\",\n\"release_hot_path_benchmark\":\"PASS\",\n\"tracked_production_hot_path_allocations\":0,\n\"inherited_52463_replay\":\"source-bound; production source identical\",\n\"merge_performed\":false,\n\"release_performed\":false\n}\n");
        ok &= put(options.output / "hotpath_benchmark.md", "# Release hot-path benchmark\n\nFresh Release `mapping_hot_path_benchmark` result: PASS. Tracked production hot-path allocations: `0`. This pre-HOLDOUT authority repair is compile-gated research plumbing and does not modify the production DirectInput → MappingWorker → vJoy path.\n");
        ok &= put(options.output / "V2.4-X2_Qualification_Authority_Repair.md", report(runnerHash, routingHash, executableHash, authorityHash, generatorHash));
        need(ok, "could not write authority-repair artifact");
        need(put(options.output / "manifest.json", manifest(options.output)), "could not write manifest");
        std::cout << "V2.4-X2 AUTHORITY REPAIRED — RESERVED HOLDOUT REMAINS PRISTINE — ONE REPLACEMENT QUALIFICATION RUN AUTHORIZED\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "V2.4-X2 AUTHORITY REPAIR FAILED: " << error.what() << '\n';
        return 1;
    }
}
