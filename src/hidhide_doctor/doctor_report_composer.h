#pragma once

#include "doctor_session.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace hotas::doctor {

// Reporting is deliberately a structured projection of DoctorSession. It is
// independent of the currently visible QML pane, selection, or presentation
// density, so copied and exported evidence cannot be truncated by the UI.
enum class DoctorReportFormat { Markdown, Json, PlainText, DiagnosticBundle };
enum class DoctorReportDetail { Summary, Detailed, Forensic };
enum class DoctorReportPrivacy { SafeToShare, LocalUnredacted };

struct DoctorReportRequest final {
    QString scope = QStringLiteral("Entire Session");
    // Optional stable evidence selection for selected-item copy.  The report
    // composer remains independent of the QML item that initiated it.
    QString selectedEvidenceId;
    DoctorReportFormat format = DoctorReportFormat::Markdown;
    DoctorReportDetail detail = DoctorReportDetail::Detailed;
    DoctorReportPrivacy privacy = DoctorReportPrivacy::SafeToShare;
};

struct DoctorReportDocument final {
    QByteArray markdown;
    QByteArray json;
    QByteArray plainText;
    QByteArray timelineJson;
    QByteArray evidenceJson;
    QStringList included;
    QStringList redacted;
    QStringList excluded;
};

class DoctorReportComposer final {
public:
    static DoctorReportDocument compose(const DoctorSession &session, const QString &buildIdentity,
                                        const DoctorReportRequest &request = {});
    static bool write(const DoctorReportDocument &document, const QString &destination,
                      DoctorReportFormat format, QString *error = nullptr);
    static QString sectionMarkdown(const DoctorSession &session, const QString &buildIdentity,
                                   const QString &scope, DoctorReportPrivacy privacy = DoctorReportPrivacy::SafeToShare);
};

DoctorReportFormat doctorReportFormatFromString(const QString &value);
DoctorReportDetail doctorReportDetailFromString(const QString &value);
DoctorReportPrivacy doctorReportPrivacyFromString(const QString &value);

} // namespace hotas::doctor
