#include "doctor_approved_package_runtime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUrl>
#include <QVector>

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace hotas::doctor {
namespace {

constexpr qint64 kMaximumApprovedPackageBytes = 64LL * 1024 * 1024;

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

bool canonicalFileName(const QString &value)
{
    return !value.isEmpty() && value == QFileInfo(value).fileName()
        && !value.contains(QStringLiteral("..")) && !value.contains(QLatin1Char('/')) && !value.contains(QLatin1Char('\\'));
}

bool approvedReleaseUrl(const ApprovedPackage &package, QString *reason)
{
    const QUrl url(package.source);
    if (package.sourceKind != ApprovedPackageSourceKind::OfficialSignedRelease || url.scheme() != QStringLiteral("https")
        || url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0
        || !url.path().startsWith(QStringLiteral("/nefarius/HidHide/releases/download/"))
        || !url.path().endsWith(QStringLiteral("/") + package.artifactFileName)) {
        if (reason) *reason = QStringLiteral("Package release URL is not the catalogued official HidHide HTTPS asset.");
        return false;
    }
    return true;
}

bool approvedRedirect(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.scheme() == QStringLiteral("https") && (host == QStringLiteral("github.com")
        || host == QStringLiteral("objects.githubusercontent.com")
        || host == QStringLiteral("release-assets.githubusercontent.com")
        || host == QStringLiteral("github-releases.githubusercontent.com"));
}

QString packagePath(const ApprovedPackage &package)
{
    return QDir(ApprovedPackageRuntime::cacheRoot()).filePath(package.packageId + QStringLiteral("--") + package.artifactFileName);
}

QString metadataPath(const ApprovedPackage &package)
{
    return packagePath(package) + QStringLiteral(".json");
}

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(256 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}

CpuArchitecture executableArchitecture(const QString &path)
{
    DWORD type = 0;
    if (!GetBinaryTypeW(reinterpret_cast<LPCWSTR>(path.utf16()), &type)) return CpuArchitecture::Unknown;
    if (type == SCS_64BIT_BINARY) return CpuArchitecture::X64;
    if (type == SCS_32BIT_BINARY) return CpuArchitecture::X86;
    return CpuArchitecture::Unknown;
}

QString fileVersion(const QString &path)
{
    DWORD unused = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(reinterpret_cast<LPCWSTR>(path.utf16()), &unused);
    if (!bytes) return {};
    QByteArray buffer(static_cast<int>(bytes), Qt::Uninitialized);
    if (!GetFileVersionInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, bytes, buffer.data())) return {};
    struct Translation { WORD language; WORD codePage; };
    Translation *translation = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID *>(&translation), &translationBytes)
        || translationBytes < sizeof(Translation)) return {};
    const QString query = QStringLiteral("\\StringFileInfo\\%1%2\\FileVersion")
        .arg(translation->language, 4, 16, QLatin1Char('0')).arg(translation->codePage, 4, 16, QLatin1Char('0'));
    wchar_t *value = nullptr;
    UINT valueBytes = 0;
    if (!VerQueryValueW(buffer.data(), reinterpret_cast<LPCWSTR>(query.utf16()), reinterpret_cast<LPVOID *>(&value), &valueBytes)
        || !value || valueBytes == 0) return {};
    return QString::fromWCharArray(value, static_cast<int>(valueBytes)).trimmed();
}

bool trustedSignature(const QString &path, QString *signer, QString *reason)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = reinterpret_cast<LPCWSTR>(path.utf16());
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &fileInfo;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    const GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG verify = WinVerifyTrust(nullptr, const_cast<GUID *>(&policy), &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, const_cast<GUID *>(&policy), &trust);
    if (verify != ERROR_SUCCESS) {
        if (reason) *reason = QStringLiteral("Authenticode verification failed (WinVerifyTrust %1).").arg(verify);
        return false;
    }
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    PCCERT_CONTEXT certificate = nullptr;
    DWORD encoding = 0, content = 0, format = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, reinterpret_cast<const void *>(path.utf16()),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
            &encoding, &content, &format, &store, &message, nullptr)) {
        if (reason) *reason = QStringLiteral("Authenticode signer certificate could not be read.");
        return false;
    }
    const auto close = qScopeGuard([&] { if (certificate) CertFreeCertificateContext(certificate); if (message) CryptMsgClose(message); if (store) CertCloseStore(store, 0); });
    DWORD signerBytes = 0;
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerBytes) || signerBytes == 0) {
        if (reason) *reason = QStringLiteral("Authenticode signer information is unavailable.");
        return false;
    }
    QByteArray signerInfo(static_cast<int>(signerBytes), Qt::Uninitialized);
    if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signerInfo.data(), &signerBytes)) return false;
    const auto *info = reinterpret_cast<const CMSG_SIGNER_INFO *>(signerInfo.constData());
    CERT_INFO certInfo{};
    certInfo.Issuer = info->Issuer;
    certInfo.SerialNumber = info->SerialNumber;
    certificate = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
        CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
    if (!certificate) {
        if (reason) *reason = QStringLiteral("Authenticode signer certificate does not match the signed payload.");
        return false;
    }
    const DWORD characters = CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
    if (characters <= 1) {
        if (reason) *reason = QStringLiteral("Authenticode signer subject is empty.");
        return false;
    }
    QVector<wchar_t> subject(static_cast<qsizetype>(characters));
    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject.data(), characters);
    if (signer) *signer = QString::fromWCharArray(subject.constData()).trimmed();
    return true;
}

bool hardenCachePath(const QString &path, QString *reason)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)(A;;FA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) {
        if (reason) *reason = QStringLiteral("Could not create the approved-package cache ACL.");
        return false;
    }
    const auto freeDescriptor = qScopeGuard([&] { LocalFree(descriptor); });
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present || !dacl
        || SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr) != ERROR_SUCCESS) {
        if (reason) *reason = QStringLiteral("Could not harden the approved-package cache ACL.");
        return false;
    }
    return true;
}

bool cacheAclIsRestricted(const QString &path, QString *reason)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    PSID owner = nullptr;
    const DWORD status = GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &descriptor);
    const auto freeDescriptor = qScopeGuard([&] { if (descriptor) LocalFree(descriptor); });
    if (status != ERROR_SUCCESS || !owner || !dacl) {
        if (reason) *reason = QStringLiteral("Approved-package cache ACL could not be inspected.");
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED)) {
        if (reason) *reason = QStringLiteral("Approved-package cache ACL is inheritable or not protected.");
        return false;
    }
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID systemSid = nullptr;
    if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &systemSid)) return false;
    const auto freeSystemSid = qScopeGuard([&] { FreeSid(systemSid); });
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void *ace = nullptr;
        if (!GetAce(dacl, index, &ace) || !ace) return false;
        const auto *header = static_cast<ACE_HEADER *>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            if (reason) *reason = QStringLiteral("Approved-package cache ACL contains a non-allow ACE.");
            return false;
        }
        const auto *allowed = static_cast<const ACCESS_ALLOWED_ACE *>(ace);
        PSID sid = reinterpret_cast<PSID>(const_cast<DWORD *>(&allowed->SidStart));
        if (!EqualSid(sid, owner) && !EqualSid(sid, systemSid)) {
            if (reason) *reason = QStringLiteral("Approved-package cache ACL grants a principal other than the owner or LocalSystem.");
            return false;
        }
    }
    return true;
}

bool readMetadata(const ApprovedPackageArtifact &artifact, QString *reason)
{
    QFile file(artifact.metadataPath);
    if (!file.open(QIODevice::ReadOnly)) { if (reason) *reason = QStringLiteral("Approved-package cache metadata is missing."); return false; }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || object.value(QStringLiteral("packageId")).toString() != artifact.package.packageId
        || object.value(QStringLiteral("source")).toString() != artifact.package.source
        || object.value(QStringLiteral("sha256")).toString().compare(artifact.package.expectedSha256, Qt::CaseInsensitive) != 0
        || object.value(QStringLiteral("fileName")).toString() != artifact.package.artifactFileName) {
        if (reason) *reason = QStringLiteral("Approved-package cache metadata does not match the immutable catalog record.");
        return false;
    }
    return true;
}

bool writeMetadata(const ApprovedPackageArtifact &artifact, QString *reason)
{
    QSaveFile file(artifact.metadataPath);
    if (!file.open(QIODevice::WriteOnly)) { if (reason) *reason = QStringLiteral("Approved-package cache metadata could not be created."); return false; }
    const QJsonObject metadata{{QStringLiteral("packageId"), artifact.package.packageId},
        {QStringLiteral("source"), artifact.package.source}, {QStringLiteral("sha256"), artifact.sha256},
        {QStringLiteral("signer"), artifact.signerIdentity}, {QStringLiteral("version"), artifact.artifactVersion},
        {QStringLiteral("architecture"), displayName(artifact.architecture)},
        {QStringLiteral("size"), static_cast<double>(artifact.size)}, {QStringLiteral("fileName"), artifact.package.artifactFileName}};
    if (file.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        if (reason) *reason = QStringLiteral("Approved-package cache metadata could not be committed.");
        return false;
    }
    return hardenCachePath(artifact.metadataPath, reason);
}

ApprovedPackageArtifact artifactFor(const ApprovedPackage &package)
{
    ApprovedPackageArtifact artifact;
    artifact.package = package;
    artifact.filePath = packagePath(package);
    artifact.metadataPath = metadataPath(package);
    return artifact;
}

} // namespace

QString ApprovedPackageRuntime::cacheRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(appData).filePath(QStringLiteral("approved-packages"));
}

PackageValidationResult ApprovedPackageRuntime::revalidate(const ApprovedPackageArtifact &artifact,
    const DoctorEnvironment &environment)
{
    PackageValidationResult result;
    QString reason;
    if (!canonicalFileName(artifact.package.artifactFileName) || !approvedReleaseUrl(artifact.package, &reason)
        || QFileInfo(artifact.filePath).absoluteFilePath() != QFileInfo(packagePath(artifact.package)).absoluteFilePath()
        || !QFileInfo::exists(artifact.filePath) || !readMetadata(artifact, &reason)
        || !cacheAclIsRestricted(artifact.filePath, &reason) || !cacheAclIsRestricted(artifact.metadataPath, &reason)) {
        result.reason = reason.isEmpty() ? QStringLiteral("Approved-package cache path is not valid.") : reason;
        return result;
    }
    const QFileInfo info(artifact.filePath);
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaximumApprovedPackageBytes
        || (artifact.package.expectedSize && static_cast<quint64>(info.size()) != artifact.package.expectedSize)) {
        result.reason = QStringLiteral("Approved package file size is not the catalogued bounded asset size.");
        return result;
    }
    const QString sha = hashFile(artifact.filePath);
    QString signer;
    if (sha.isEmpty() || !trustedSignature(artifact.filePath, &signer, &reason)) {
        result.reason = reason.isEmpty() ? QStringLiteral("Approved package could not be hashed or signature-verified.") : reason;
        return result;
    }
    const QString version = fileVersion(artifact.filePath);
    const CpuArchitecture architecture = executableArchitecture(artifact.filePath);
    result = ApprovedPackageCatalog::validate(artifact.package, environment, sha, signer, version, architecture, artifact.package.source);
    if (!result.valid) return result;
    result.checks.append(QStringLiteral("final cache ACL restricted"));
    result.checks.append(QStringLiteral("cache metadata exact"));
    result.checks.append(QStringLiteral("bounded file size exact"));
    return result;
}

PackageAcquisitionResult ApprovedPackageRuntime::acquire(const QString &packageId, const DoctorEnvironment &environment,
    std::atomic_bool *cancelled, PackageProgress progress)
{
    PackageAcquisitionResult result;
    const std::optional<ApprovedPackage> package = ApprovedPackageCatalog::find(packageId);
    if (!package) { result.detail = QStringLiteral("Requested package is absent from the approved catalog."); return result; }
    if (!canonicalFileName(package->artifactFileName) || package->expectedSha256.size() != 64 || package->expectedSize == 0
        || package->expectedSize > kMaximumApprovedPackageBytes) {
        result.detail = QStringLiteral("Approved package record is incomplete or exceeds the package-size bound.");
        return result;
    }
    QString reason;
    if (!approvedReleaseUrl(*package, &reason)) { result.detail = reason; return result; }
    ApprovedPackageArtifact artifact = artifactFor(*package);
    if (QFileInfo::exists(artifact.filePath) || QFileInfo::exists(artifact.metadataPath)) {
        const PackageValidationResult validation = revalidate(artifact, environment);
        if (!validation.valid) { result.detail = QStringLiteral("Existing approved-package cache entry was rejected: ") + validation.reason; return result; }
        artifact.sha256 = package->expectedSha256;
        artifact.signerIdentity = package->signerIdentity;
        artifact.artifactVersion = package->artifactVersion;
        artifact.architecture = package->architecture;
        artifact.size = package->expectedSize;
        result.acquired = true;
        result.detail = QStringLiteral("Existing approved package cache entry was revalidated.");
        result.artifact = artifact;
        return result;
    }
    if (cancelled && cancelled->load()) { result.cancelled = true; result.detail = QStringLiteral("Approved package acquisition was cancelled before download."); return result; }
    QDir root(cacheRoot());
    if (!root.mkpath(QStringLiteral(".")) || !hardenCachePath(root.absolutePath(), &reason)) { result.detail = reason.isEmpty() ? QStringLiteral("Approved-package cache directory could not be created.") : reason; return result; }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(package->source));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HOTAS-BF6-HidHide-Doctor/4"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QSaveFile staged(artifact.filePath);
    if (!staged.open(QIODevice::WriteOnly)) { result.detail = QStringLiteral("Approved package could not be staged in the cache."); return result; }
    bool exceededBound = false;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] {
        const QByteArray block = reply->readAll();
        if (!block.isEmpty() && staged.write(block) != block.size()) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (progress) progress(received, total);
        if ((total > package->expectedSize) || received > package->expectedSize || (cancelled && cancelled->load())) {
            exceededBound = total > package->expectedSize || received > package->expectedSize;
            reply->abort();
        }
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const auto cleanup = qScopeGuard([&] { reply->deleteLater(); });
    if (cancelled && cancelled->load()) { staged.cancelWriting(); result.cancelled = true; result.detail = QStringLiteral("Approved package acquisition was cancelled — no package was committed."); return result; }
    if (reply->error() != QNetworkReply::NoError || exceededBound || !approvedRedirect(reply->url())) {
        staged.cancelWriting();
        result.detail = exceededBound ? QStringLiteral("Approved package download exceeded its catalogued size bound.")
            : !approvedRedirect(reply->url()) ? QStringLiteral("Approved package download redirected outside the approved HTTPS hosts.")
            : QStringLiteral("Approved package download failed without committing a cache entry.");
        return result;
    }
    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty() && staged.write(tail) != tail.size()) { staged.cancelWriting(); result.detail = QStringLiteral("Approved package staging write failed."); return result; }
    if (!staged.commit()) { result.detail = QStringLiteral("Approved package staging could not be committed."); return result; }
    if (!hardenCachePath(artifact.filePath, &reason)) { result.detail = reason; return result; }
    artifact.sha256 = package->expectedSha256;
    artifact.signerIdentity = package->signerIdentity;
    artifact.artifactVersion = package->artifactVersion;
    artifact.architecture = package->architecture;
    artifact.size = package->expectedSize;
    if (!writeMetadata(artifact, &reason)) { result.detail = reason; return result; }
    const PackageValidationResult validation = revalidate(artifact, environment);
    if (!validation.valid) { result.detail = QStringLiteral("Downloaded package was rejected before use: ") + validation.reason; return result; }
    result.acquired = true;
    result.detail = QStringLiteral("Approved package was acquired, cache-hardened, and independently revalidated.");
    result.artifact = artifact;
    return result;
}

} // namespace hotas::doctor
