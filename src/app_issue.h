#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace hotas {
using namespace Qt::StringLiterals;

// Control-plane description of a product problem.  This is deliberately a
// value type: it is constructed when configuration/device state changes and
// projected to QML or Diagnostics, never from MappingWorker's report loop.
struct AppIssue {
    QString id;
    QString code;
    QString category;
    QString severity = u"info"_qs;
    QString scopeType;
    QString scopeId;
    QString affectedObjectType;
    QString affectedObjectId;
    QStringList affectedObjectIds;
    QString title;
    QString explanation;
    QString recommendedAction;
    QString recommendedActionLabel;
    QVariantList alternativeActions;
    bool automaticallyFixable = false;
    bool requiresLiveHardware = false;
    int priority = 500;
    QString technicalDetails;
    // A small data target understood by the presentation shell. It keeps the
    // issue producer independent of QML page objects and dialog instances.
    QVariantMap navigationTarget;

    QVariantMap toVariantMap() const;
    static AppIssue fromVariantMap(const QVariantMap &value);
};

// Common outcome returned by user-facing operations that are not an obvious
// direct manipulation.  Existing QML can retain the stable success/title/
// message keys while new surfaces receive scope, severity and next-action
// metadata without inventing page-specific result dictionaries.
struct AppActionResult {
    bool success = false;
    QString severity = u"error"_qs;
    QString title;
    QString message;
    QString affectedObjectType;
    QString affectedObjectId;
    QString nextAction;
    QString nextActionLabel;
    QString technicalDetails;
    bool inProgress = false;
    bool undoAvailable = false;

    QVariantMap toVariantMap() const;
};

} // namespace hotas
