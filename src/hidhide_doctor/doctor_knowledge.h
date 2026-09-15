#pragma once

#include "doctor_session.h"

#include <QHash>

namespace hotas::doctor {

struct ReadOnlyDiagnosticSnapshot;

// Phase 2's knowledge layer consumes immutable Phase 1 observations.  It has
// no provider, repair, process, registry, or driver-control dependency.
struct KnowledgeAnalysis final {
    QString engineVersion;
    int findingRuleCount = 0;
    int diagnosisRuleCount = 0;
    QHash<QString, DoctorCheckResult> catalogResults;
    QStringList contradictions;
};

class DoctorFindingEngine final {
public:
    QList<Finding> evaluate(const DoctorSession &session,
        const ReadOnlyDiagnosticSnapshot &snapshot) const;
    static int ruleCount();
};

class DoctorKnowledgeEngine final {
public:
    static QString version();
    static int ruleCount();

    // Appends only derived findings, diagnoses, bounded activity events, and
    // Phase-2 correlation/knowledge check results. It never edits evidence.
    KnowledgeAnalysis analyze(DoctorSession &session,
        const ReadOnlyDiagnosticSnapshot &snapshot) const;
};

} // namespace hotas::doctor
