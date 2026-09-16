#pragma once

#include "doctor_repair_contract.h"

#include <QJsonObject>

namespace hotas::doctor {

// The only request shape the elevated helper accepts.  This deliberately has
// no program path, registry path, service name, command line, or file action.
// The pipe transport has a separately generated name; the nonce binds this
// short-lived payload to that one launch.
struct RepairHelperRequest final {
    static constexpr int protocolVersion = 2;
    static constexpr qsizetype maximumMessageBytes = 64 * 1024;

    int version = protocolVersion;
    QString doctorBuildId;
    QString helperBuildId;
    RepairTransactionId transactionId;
    RepairPlan plan;
    QString nonce;
    QDateTime expiresAt;
    // This probes the elevated helper's authenticated input path only.  The
    // helper repeats its read-only validation then returns before any SET.
    bool connectivityOnly = false;
    QString requestDigest;
};

class RepairHelperProtocol final {
public:
    static QString seal(const RepairHelperRequest &request);
    static QJsonObject serialize(const RepairHelperRequest &request);
    static std::optional<RepairHelperRequest> parse(const QByteArray &payload, QString *reason = nullptr);
    static bool validate(const RepairHelperRequest &request, const DoctorEnvironment &environment,
        const QString &expectedDoctorBuildId, const QString &expectedNonce, QString *reason = nullptr);

    // Kept separate from generic plan validation because the helper must make
    // its own target-scope decision after it receives an untrusted frame.
    static bool targetIsAllowed(const RepairOperation &operation, QString *reason = nullptr);
};

} // namespace hotas::doctor
