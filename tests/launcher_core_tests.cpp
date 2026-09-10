#include "launcher_core.h"
#include "update_transaction.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int gFailures = 0;

void expect(bool condition, const char *message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++gFailures;
}

std::string validManifest(std::string version = "1.6.1")
{
    return R"({"schema":1,"channel":"stable","version":")" + version
        + R"(","tag":"v)" + version
        + R"(","installer":"HOTAS-BF6-Setup-v)" + version
        + R"(.exe","installer_url":"https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v)" + version
        + R"(/HOTAS-BF6-Setup-v)" + version
        + R"(.exe","sha256":"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824","minimum_launcher_version":"1.0.0","published_utc":"2026-08-22T00:00:00Z","release_notes_url":"https://github.com/Kgray44/HOTAS_BF6_simple/releases/tag/v)"
        + version + R"("})";
}

void versionTests()
{
    using namespace hotas::launcher;
    SemanticVersion version{};
    expect(parseSemanticVersion("1.6.0", version), "parse normal version");
    expect(!parseSemanticVersion("1.6", version), "reject missing patch");
    expect(!parseSemanticVersion("1.06.0", version), "reject leading zero");
    expect(!parseSemanticVersion("1.6.x", version), "reject malformed version");
    SemanticVersion newer{};
    SemanticVersion older{};
    parseSemanticVersion("1.6.10", newer);
    parseSemanticVersion("1.6.9", older);
    expect(compareSemanticVersions(newer, older) > 0, "compare numeric patch version");
    parseSemanticVersion("2.0.0", newer);
    parseSemanticVersion("1.99.99", older);
    expect(compareSemanticVersions(newer, older) > 0, "compare major version");
    parseSemanticVersion("1.10.0", newer);
    parseSemanticVersion("1.9.0", older);
    expect(compareSemanticVersions(newer, older) > 0, "compare numeric minor version");
    expect(hotas::launcher::updateManifestUrl()
               == "https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json",
           "expose the shared stable update manifest URL");
}

void manifestTests()
{
    using namespace hotas::launcher;
    SemanticVersion local{};
    parseSemanticVersion("1.6.0", local);
    UpdateManifest manifest;
    expect(parseAndValidateManifest(validManifest(), manifest), "accept valid manifest");
    expect(decideUpdate(false, {}, local) == UpdateAction::LaunchCurrent, "fetch failure launches current");
    expect(decideUpdate(true, validManifest("1.6.0"), local) == UpdateAction::LaunchCurrent,
           "equal version launches current");
    expect(decideUpdate(true, validManifest("1.5.9"), local) == UpdateAction::LaunchCurrent,
           "older version never downgrades");
    expect(decideUpdate(true, validManifest("1.6.1"), local) == UpdateAction::InstallUpdate,
           "newer patch selects update");
    expect(decideUpdate(true, validManifest("1.7.0"), local) == UpdateAction::InstallUpdate,
           "newer minor selects update");
    expect(decideUpdate(true, validManifest("2.0.0"), local) == UpdateAction::InstallUpdate,
           "newer major selects update");
    expect(!parseAndValidateManifest("{\"schema\":2}", manifest), "reject unsupported schema");
    expect(!parseAndValidateManifest("{\"schema\":1}", manifest), "reject missing fields");
    std::string http = validManifest();
    http.replace(http.find("https://"), 8, "http://");
    expect(!parseAndValidateManifest(http, manifest), "reject non-HTTPS installer URL");
    std::string beta = validManifest();
    beta.replace(beta.find("stable"), 6, "beta");
    expect(!parseAndValidateManifest(beta, manifest), "reject non-stable channel");
    std::string badHash = validManifest();
    badHash.replace(badHash.find("2cf24dba"), 64, "not-a-sha256");
    expect(!parseAndValidateManifest(badHash, manifest), "reject invalid SHA-256");
}

void hashTests()
{
    using namespace hotas::launcher;
    const auto path = std::filesystem::temp_directory_path() / "hotas-launcher-sha256-test.txt";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "hello";
    }
    constexpr auto helloHash = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    expect(verifyFileSha256(path.wstring(), helloHash), "accept expected SHA-256");
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << "!";
    }
    expect(!verifyFileSha256(path.wstring(), helloHash), "reject changed file SHA-256");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

bool writeTextFile(const std::filesystem::path &path, const std::string &contents)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    return static_cast<bool>(output);
}

bool createPackage(const std::filesystem::path &root, const std::string &version,
                   bool launcher = true, bool mapper = true, bool versionFile = true)
{
    std::error_code error;
    std::filesystem::create_directories(root / "qml" / "QtQuick", error);
    if (error) return false;
    if (launcher && !writeTextFile(root / "HOTAS BF6 Launcher.exe", "launcher-" + version)) return false;
    if (mapper && !writeTextFile(root / "HOTAS BF6.exe", "mapper-" + version)) return false;
    if (versionFile && !writeTextFile(root / "VERSION", version + "\n")) return false;
    return writeTextFile(root / "Qt6Core.dll", "core")
        && writeTextFile(root / "Qt6Gui.dll", "gui")
        && writeTextFile(root / "Qt6Qml.dll", "qml")
        && writeTextFile(root / "Qt6Quick.dll", "quick")
        && writeTextFile(root / "Qt6QuickControls2.dll", "controls")
        && writeTextFile(root / "qml" / "QtQuick" / "qmldir", "module QtQuick");
}

bool packageHasVersion(const std::filesystem::path &root, const std::string &version)
{
    std::string error;
    return hotas::launcher::verifyInstalledPackage(root, version, nullptr, &error);
}

void updateTransactionTests()
{
    using namespace hotas::launcher;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto work = std::filesystem::temp_directory_path() / ("hotas-update-transaction-" + unique);
    const auto installed = work / "installed";
    const auto backup = work / "backup";
    expect(createPackage(installed, "2.5.0"), "create prior update fixture");
    expect(writeTextFile(work / "user-configuration-sentinel.txt", "must survive"),
           "create external configuration sentinel");

    const auto reset = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        std::filesystem::remove_all(backup, ignored);
        return createPackage(installed, "2.5.0");
    };
    const auto configurationIntact = [&] {
        std::ifstream input(work / "user-configuration-sentinel.txt", std::ios::binary);
        std::string value;
        input >> value;
        return value == "must";
    };
    const auto checkRollback = [&](const UpdateTransactionResult &result, const char *label) {
        expect(!result.committed, label);
        expect(result.rollbackAttempted && result.rollbackRestored && result.previousStartupHealthy,
               "failed update must restore and launch the previous package");
        expect(packageHasVersion(installed, "2.5.0"), "rollback must restore the complete previous package");
        expect(configurationIntact(), "rollback must not modify external user configuration");
    };
    const auto runFailure = [&](const char *label, const std::function<bool()> &installer,
                                const std::function<bool()> &candidateStartup = [] { return true; }) {
        expect(reset(), "reset update fixture");
        UpdateTransactionCallbacks callbacks;
        callbacks.runInstaller = installer;
        callbacks.startCandidate = candidateStartup;
        callbacks.startPrevious = [&] { return packageHasVersion(installed, "2.5.0"); };
        checkRollback(runUpdateTransaction(installed, backup, "2.5.1", callbacks), label);
    };

    runFailure("failure before installer launch must not commit", [] { return false; });
    runFailure("non-zero installer exit must not commit", [] { return false; });
    runFailure("interrupted replacement must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return false;
    });
    runFailure("missing launcher must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1", false);
    });
    runFailure("missing mapper must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1", true, false);
    });
    runFailure("missing VERSION must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1", true, true, false);
    });
    runFailure("incorrect VERSION must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "9.9.9");
    });
    runFailure("updated launcher startup failure must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1");
    }, [] { return false; });
    runFailure("updated mapper startup failure must not commit", [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1");
    }, [] { return false; });

    expect(reset(), "reset successful update fixture");
    UpdateTransactionCallbacks success;
    success.runInstaller = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(installed, ignored);
        return createPackage(installed, "2.5.1");
    };
    success.startCandidate = [&] { return packageHasVersion(installed, "2.5.1"); };
    success.startPrevious = [] { return false; };
    const UpdateTransactionResult committed = runUpdateTransaction(installed, backup, "2.5.1", success);
    expect(committed.committed && !committed.rollbackAttempted, "complete candidate must commit only after startup health");
    expect(packageHasVersion(installed, "2.5.1"), "committed update must retain candidate package");
    expect(configurationIntact(), "committed update must preserve external user configuration");

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}

} // namespace

int main()
{
    versionTests();
    manifestTests();
    hashTests();
    updateTransactionTests();
    if (gFailures == 0) std::cout << "launcher_core_tests passed\n";
    return gFailures == 0 ? 0 : 1;
}
