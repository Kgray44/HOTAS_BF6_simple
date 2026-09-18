#pragma once

#include "doctor_environment.h"

#include <QHash>
#include <QSet>

namespace hotas::doctor {

enum class CatalogImplementationState { Deferred, Implemented, Qualified, RepairLinked, Unsupported };

struct DoctorCheckDefinition final {
    DoctorCheckId id;
    QString title;
    DoctorPhase phase = DoctorPhase::SystemEnvironment;
    QList<DoctorCheckId> prerequisites;
    WorkWeight weight{1};
    qint64 timeoutMs = 0;
    CatalogImplementationState implementation = CatalogImplementationState::Deferred;
    bool applicable(const DoctorEnvironment &environment) const;
};

struct CatalogCoverageReport final {
    int defined = 0;
    int registered = 0;
    int implemented = 0;
    int qualified = 0;
    int repairLinked = 0;
    int unsupported = 0;
    QStringList unregisteredIds;
};

class DoctorCatalog final {
public:
    static QStringList v11DefinedCheckIds();
    bool registerCheck(DoctorCheckDefinition definition, QString *reason = nullptr);
    bool contains(const DoctorCheckId &id) const;
    const DoctorCheckDefinition *find(const DoctorCheckId &id) const;
    CatalogCoverageReport coverage() const;
    QList<DoctorCheckDefinition> registeredChecks() const;

private:
    QHash<QString, DoctorCheckDefinition> m_definitions;
};

} // namespace hotas::doctor
