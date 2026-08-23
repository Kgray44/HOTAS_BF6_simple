#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hotas::launcher {

struct SemanticVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

bool parseSemanticVersion(std::string_view text, SemanticVersion &version,
                          std::string *error = nullptr);
int compareSemanticVersions(const SemanticVersion &left, const SemanticVersion &right);

struct UpdateManifest {
    SemanticVersion version;
    SemanticVersion minimumLauncherVersion;
    std::string versionText;
    std::string tag;
    std::string installer;
    std::string installerUrl;
    std::string sha256;
    std::string publishedUtc;
    std::string releaseNotesUrl;
};

// Parses only the deliberately small, flat release manifest schema.  Any
// unexpected JSON value shape, duplicate field, or unsafe URL fails closed.
bool parseAndValidateManifest(std::string_view json, UpdateManifest &manifest,
                              std::string *error = nullptr);

enum class UpdateAction {
    LaunchCurrent,
    InstallUpdate,
};

// Fetch and parse failures are intentionally failure-open: the installed
// mapper remains launchable when GitHub or the network is unavailable.
UpdateAction decideUpdate(bool fetchSucceeded, std::string_view manifestJson,
                          const SemanticVersion &localVersion,
                          UpdateManifest *manifest = nullptr,
                          std::string *reason = nullptr);

// Uses Windows CNG SHA-256.  It accepts only a canonical 64-hex digest.
bool verifyFileSha256(const std::wstring &path, std::string_view expectedHash,
                      std::string *error = nullptr);

} // namespace hotas::launcher
