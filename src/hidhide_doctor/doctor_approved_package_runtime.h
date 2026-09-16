#pragma once

#include "doctor_deep_repair.h"

#include <atomic>
#include <functional>

namespace hotas::doctor {

// A cache record is derived exclusively from an ApprovedPackageCatalog entry.
// It never accepts a caller-supplied executable path, URL, hash, signer, or
// command line. The elevated helper revalidates it before every use.
struct ApprovedPackageArtifact final {
    ApprovedPackage package;
    QString filePath;
    QString metadataPath;
    QString sha256;
    QString signerIdentity;
    QString artifactVersion;
    CpuArchitecture architecture = CpuArchitecture::Unknown;
    quint64 size = 0;
};

struct PackageAcquisitionResult final {
    bool acquired = false;
    bool cancelled = false;
    QString detail;
    ApprovedPackageArtifact artifact;
};

using PackageProgress = std::function<void(qint64 received, qint64 total)>;

class ApprovedPackageRuntime final {
public:
    static QString cacheRoot();
    static PackageAcquisitionResult acquire(const QString &packageId, const DoctorEnvironment &environment,
        std::atomic_bool *cancelled = nullptr, PackageProgress progress = {});

    // Performs source metadata, size, final-path ACL, SHA-256, Authenticode,
    // signer, PE architecture, and file-version checks again. It is intended
    // for the immediately-pre-install boundary as well as cache reuse.
    static PackageValidationResult revalidate(const ApprovedPackageArtifact &artifact,
        const DoctorEnvironment &environment);
};

} // namespace hotas::doctor
