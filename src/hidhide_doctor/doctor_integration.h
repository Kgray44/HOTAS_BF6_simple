#pragma once

#include <QString>
#include <QStringList>

namespace hotas::doctor {

// This protocol is intentionally small and unprivileged.  It only conveys the
// reason Doctor was opened and the HOTAS configuration it expects to find; the
// native provider remains the sole authority for system evidence and repairs.
inline constexpr int kDoctorIntegrationProtocolVersion = 1;
inline constexpr qsizetype kDoctorIntegrationMaximumBytes = 16 * 1024;

struct DoctorLaunchContext final {
    QString sessionId;
    QString invokingVersion;
    QString invokingBuildId;
    QString reason;
    QString expectedHotasExecutable;
    QString profileId;
    QString profileName;
    QString deviceRigId;
    QString deviceRigName;
    QString expectedVirtualOutput;
    QString isolationIntent;
    QStringList expectedPhysicalControllerIds;
};

struct DoctorIntegrationReadResult final {
    bool accepted = false;
    DoctorLaunchContext context;
    QString rejection;
};

// Both products resolve this path independently.  A caller supplies only a
// UUID on the command line, never a file or shell path.
QString doctorIntegrationDirectory();
bool writeDoctorLaunchContext(DoctorLaunchContext context, QString *error = nullptr);
DoctorIntegrationReadResult consumeDoctorLaunchContext(const QString &sessionId);

// A result is a low-frequency notification only. It cannot request mapper or
// helper work; the mapper may elect to refresh its normal setup/readiness view.
bool writeDoctorIntegrationResult(const DoctorLaunchContext &context, const QString &state,
                                  const QString &detail, QString *error = nullptr);
// Results are bound to the one-time launch session. A mapper never consumes a
// status message emitted by a different Doctor process.
bool consumeDoctorIntegrationResult(const QString &sessionId, QString *state,
                                    QString *detail, QString *error = nullptr);

bool isValidDoctorIntegrationSessionId(const QString &sessionId);

} // namespace hotas::doctor
