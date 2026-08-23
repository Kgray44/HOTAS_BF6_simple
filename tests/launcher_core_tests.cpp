#include "launcher_core.h"

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

} // namespace

int main()
{
    versionTests();
    manifestTests();
    hashTests();
    if (gFailures == 0) std::cout << "launcher_core_tests passed\n";
    return gFailures == 0 ? 0 : 1;
}
