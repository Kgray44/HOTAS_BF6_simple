#include "doctor_catalog.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace hotas::doctor {
namespace {

QString resourcePath()
{
    return QStringLiteral(":/hidhide-doctor/HOTAS_BF6_HidHide_Doctor_Diagnostic_Catalog_v1.1.md");
}

} // namespace

bool DoctorCheckDefinition::applicable(const DoctorEnvironment &environment) const
{
    // Phase 0 has no real probes.  Applicability is intentionally based on
    // structured capability/environment data, never locale or a friendly
    // executable-version string.  Per-check refinements arrive with Phase 1.
    if (id.value().startsWith(QStringLiteral("HD-API-"))) return environment.hidhide.present;
    return true;
}

QStringList DoctorCatalog::v11DefinedCheckIds()
{
    Q_INIT_RESOURCE(hidhide_doctor_governance_resources);
    QFile source(resourcePath());
    if (!source.open(QIODevice::ReadOnly)) return {};
    const QString document = QString::fromUtf8(source.readAll());
    const QRegularExpression identifier(QStringLiteral("\\b(HD-(?:SYS|INST|PKG|DRV|API|CFG|DEV|ISO|WIN|X|KB|PORT)-[0-9]{3})\\b"));
    QSet<QString> seen;
    QStringList ids;
    QRegularExpressionMatchIterator matches = identifier.globalMatch(document);
    while (matches.hasNext()) {
        const QString id = matches.next().captured(1);
        if (!seen.contains(id)) {
            seen.insert(id);
            ids.append(id);
        }
    }
    return ids;
}

QString DoctorCatalog::v11CheckTitle(const DoctorCheckId &id)
{
    Q_INIT_RESOURCE(hidhide_doctor_governance_resources);
    QFile source(resourcePath());
    if (!source.open(QIODevice::ReadOnly)) return id.value();
    const QString escaped = QRegularExpression::escape(id.value());
    // Catalog rows have an ID and the human-facing check title in the first
    // two cells.  Keep the parser deliberately line-scoped so prose mentions
    // cannot accidentally become a title.
    const QRegularExpression row(QStringLiteral("^\\s*\\|\\s*`%1`\\s*\\|\\s*([^|]+?)\\s*\\|")
        .arg(escaped), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = row.match(QString::fromUtf8(source.readAll()));
    return match.hasMatch() ? match.captured(1).trimmed() : id.value();
}

bool DoctorCatalog::registerCheck(DoctorCheckDefinition definition, QString *reason)
{
    if (!definition.id.isValid()) {
        if (reason) *reason = QStringLiteral("Check ID is invalid.");
        return false;
    }
    if (!definition.weight.isValid()) {
        if (reason) *reason = QStringLiteral("Check work weight must be positive.");
        return false;
    }
    const QString id = definition.id.value();
    if (m_definitions.contains(id)) {
        if (reason) *reason = QStringLiteral("Duplicate CheckId: %1").arg(id);
        return false;
    }
    for (const DoctorCheckId &prerequisite : definition.prerequisites) {
        if (!prerequisite.isValid() || !m_definitions.contains(prerequisite.value())) {
            if (reason) *reason = QStringLiteral("Unknown prerequisite: %1").arg(prerequisite.value());
            return false;
        }
    }
    m_definitions.insert(id, std::move(definition));
    return true;
}

bool DoctorCatalog::contains(const DoctorCheckId &id) const
{
    return m_definitions.contains(id.value());
}

const DoctorCheckDefinition *DoctorCatalog::find(const DoctorCheckId &id) const
{
    const auto iterator = m_definitions.constFind(id.value());
    return iterator == m_definitions.cend() ? nullptr : &iterator.value();
}

CatalogCoverageReport DoctorCatalog::coverage() const
{
    CatalogCoverageReport report;
    const QStringList defined = v11DefinedCheckIds();
    report.defined = defined.size();
    for (const QString &id : defined) {
        const auto iterator = m_definitions.constFind(id);
        if (iterator == m_definitions.cend()) {
            report.unregisteredIds.append(id);
            continue;
        }
        ++report.registered;
        switch (iterator->implementation) {
        case CatalogImplementationState::Deferred: break;
        case CatalogImplementationState::Conditional: ++report.conditional; break;
        case CatalogImplementationState::Implemented: ++report.implemented; break;
        case CatalogImplementationState::Qualified: ++report.implemented; ++report.qualified; break;
        case CatalogImplementationState::RepairLinked: ++report.implemented; ++report.repairLinked; break;
        case CatalogImplementationState::Unsupported: ++report.unsupported; break;
        }
    }
    return report;
}

QList<DoctorCheckDefinition> DoctorCatalog::registeredChecks() const
{
    QList<DoctorCheckDefinition> result = m_definitions.values();
    std::sort(result.begin(), result.end(), [](const DoctorCheckDefinition &left, const DoctorCheckDefinition &right) {
        return left.id.value() < right.id.value();
    });
    return result;
}

} // namespace hotas::doctor
