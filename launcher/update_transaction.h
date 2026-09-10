#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace hotas::launcher {

struct UpdateTransactionCallbacks {
    std::function<bool()> runInstaller;
    std::function<bool()> startCandidate;
    std::function<bool()> startPrevious;
};

struct UpdateTransactionResult {
    bool committed = false;
    bool rollbackAttempted = false;
    bool rollbackRestored = false;
    bool previousStartupHealthy = false;
    std::string previousVersion;
    std::string detail;
};

// Verify the package that can actually be launched.  The updater uses this
// both before making a backup and before allowing a candidate to commit.
bool verifyInstalledPackage(const std::filesystem::path &root, const std::string &expectedVersion,
                            std::string *actualVersion = nullptr, std::string *error = nullptr);

// The backup directory must be outside the live installation directory.  A
// restore only overwrites verified package files; it never deletes an unknown
// live path while recovering from an interrupted installer.
UpdateTransactionResult runUpdateTransaction(const std::filesystem::path &installedRoot,
                                             const std::filesystem::path &backupRoot,
                                             const std::string &candidateVersion,
                                             const UpdateTransactionCallbacks &callbacks);

} // namespace hotas::launcher
