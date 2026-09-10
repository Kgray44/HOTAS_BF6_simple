#include "update_transaction.h"

#include <array>
#include <cctype>
#include <fstream>
#include <optional>

namespace hotas::launcher {
namespace {

void setError(std::string *error, const std::string &message)
{
    if (error) *error = message;
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool regularFile(const std::filesystem::path &path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool copyPackage(const std::filesystem::path &source, const std::filesystem::path &destination,
                 std::string *error)
{
    std::error_code filesystemError;
    std::filesystem::create_directories(destination, filesystemError);
    if (filesystemError) {
        setError(error, "could not create package destination: " + filesystemError.message());
        return false;
    }
    std::filesystem::copy(source, destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        filesystemError);
    if (filesystemError) {
        setError(error, "could not copy package: " + filesystemError.message());
        return false;
    }
    return true;
}

} // namespace

bool verifyInstalledPackage(const std::filesystem::path &root, const std::string &expectedVersion,
                            std::string *actualVersion, std::string *error)
{
    if (root.empty()) {
        setError(error, "installation directory is empty");
        return false;
    }
    constexpr std::array<const wchar_t *, 7> requiredFiles{
        L"HOTAS BF6 Launcher.exe", L"HOTAS BF6.exe", L"VERSION", L"Qt6Core.dll", L"Qt6Gui.dll",
        L"Qt6Qml.dll", L"Qt6Quick.dll",
    };
    for (const wchar_t *file : requiredFiles) {
        if (!regularFile(root / file)) {
            setError(error, "required installed file is missing: " + (root / file).string());
            return false;
        }
    }
    if (!regularFile(root / L"Qt6QuickControls2.dll")) {
        setError(error, "required Qt Quick Controls runtime is missing");
        return false;
    }
    std::error_code filesystemError;
    const auto qmlRoot = root / L"qml";
    if (!std::filesystem::is_directory(qmlRoot, filesystemError) || filesystemError
        || std::filesystem::is_empty(qmlRoot, filesystemError) || filesystemError) {
        setError(error, "required QML runtime is missing or empty");
        return false;
    }
    std::ifstream versionFile(root / L"VERSION", std::ios::binary);
    std::string version((std::istreambuf_iterator<char>(versionFile)), std::istreambuf_iterator<char>());
    version = trim(std::move(version));
    if (version.empty()) {
        setError(error, "installed VERSION is empty");
        return false;
    }
    if (actualVersion) *actualVersion = version;
    if (!expectedVersion.empty() && version != expectedVersion) {
        setError(error, "installed VERSION '" + version + "' does not match expected '" + expectedVersion + "'");
        return false;
    }
    return true;
}

UpdateTransactionResult runUpdateTransaction(const std::filesystem::path &installedRoot,
                                             const std::filesystem::path &backupRoot,
                                             const std::string &candidateVersion,
                                             const UpdateTransactionCallbacks &callbacks)
{
    UpdateTransactionResult result;
    std::string error;
    if (candidateVersion.empty()) {
        result.detail = "candidate version is empty";
        return result;
    }
    if (!verifyInstalledPackage(installedRoot, {}, &result.previousVersion, &error)) {
        result.detail = "current installation preflight failed: " + error;
        return result;
    }
    std::error_code filesystemError;
    if (backupRoot.empty() || backupRoot == installedRoot || std::filesystem::exists(backupRoot, filesystemError)) {
        result.detail = "rollback destination is unavailable";
        return result;
    }
    if (!copyPackage(installedRoot, backupRoot, &error)
        || !verifyInstalledPackage(backupRoot, result.previousVersion, nullptr, &error)) {
        result.detail = "could not create verified rollback backup: " + error;
        return result;
    }

    const auto rollback = [&](const std::string &reason) {
        result.rollbackAttempted = true;
        std::string rollbackError;
        if (!copyPackage(backupRoot, installedRoot, &rollbackError)
            || !verifyInstalledPackage(installedRoot, result.previousVersion, nullptr, &rollbackError)) {
            result.detail = reason + "; rollback restore failed: " + rollbackError;
            return;
        }
        result.rollbackRestored = true;
        result.previousStartupHealthy = callbacks.startPrevious && callbacks.startPrevious();
        result.detail = result.previousStartupHealthy
            ? reason + "; previous installation restored and passed startup health"
            : reason + "; previous installation was restored but did not pass startup health";
    };

    if (!callbacks.runInstaller || !callbacks.runInstaller()) {
        rollback("installer did not complete successfully");
        return result;
    }
    if (!verifyInstalledPackage(installedRoot, candidateVersion, nullptr, &error)) {
        rollback("candidate post-install verification failed: " + error);
        return result;
    }
    if (!callbacks.startCandidate || !callbacks.startCandidate()) {
        rollback("candidate launcher or mapper startup health failed");
        return result;
    }
    result.committed = true;
    result.detail = "candidate package and startup health verified";
    return result;
}

} // namespace hotas::launcher
