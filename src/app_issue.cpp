#include "app_issue.h"

namespace hotas {
using namespace Qt::StringLiterals;

QVariantMap AppIssue::toVariantMap() const
{
    return {{u"id"_qs, id}, {u"code"_qs, code}, {u"category"_qs, category},
            {u"severity"_qs, severity}, {u"scopeType"_qs, scopeType},
            {u"scopeId"_qs, scopeId}, {u"affectedObjectType"_qs, affectedObjectType},
            {u"affectedObjectId"_qs, affectedObjectId}, {u"title"_qs, title},
            {u"explanation"_qs, explanation}, {u"recommendedAction"_qs, recommendedAction},
            {u"recommendedActionLabel"_qs, recommendedActionLabel},
            {u"alternativeActions"_qs, alternativeActions},
            {u"automaticallyFixable"_qs, automaticallyFixable},
            {u"requiresLiveHardware"_qs, requiresLiveHardware}, {u"priority"_qs, priority},
            {u"technicalDetails"_qs, technicalDetails}, {u"navigationTarget"_qs, navigationTarget}};
}

AppIssue AppIssue::fromVariantMap(const QVariantMap &value)
{
    AppIssue issue;
    issue.id = value.value(u"id"_qs).toString();
    issue.code = value.value(u"code"_qs).toString();
    issue.category = value.value(u"category"_qs).toString();
    issue.severity = value.value(u"severity"_qs, u"info"_qs).toString();
    issue.scopeType = value.value(u"scopeType"_qs).toString();
    issue.scopeId = value.value(u"scopeId"_qs).toString();
    issue.affectedObjectType = value.value(u"affectedObjectType"_qs).toString();
    issue.affectedObjectId = value.value(u"affectedObjectId"_qs).toString();
    issue.title = value.value(u"title"_qs).toString();
    issue.explanation = value.value(u"explanation"_qs).toString();
    issue.recommendedAction = value.value(u"recommendedAction"_qs).toString();
    issue.recommendedActionLabel = value.value(u"recommendedActionLabel"_qs).toString();
    issue.alternativeActions = value.value(u"alternativeActions"_qs).toList();
    issue.automaticallyFixable = value.value(u"automaticallyFixable"_qs).toBool();
    issue.requiresLiveHardware = value.value(u"requiresLiveHardware"_qs).toBool();
    issue.priority = value.value(u"priority"_qs, 500).toInt();
    issue.technicalDetails = value.value(u"technicalDetails"_qs).toString();
    issue.navigationTarget = value.value(u"navigationTarget"_qs).toMap();
    return issue;
}

QVariantMap AppActionResult::toVariantMap() const
{
    return {{u"success"_qs, success}, {u"severity"_qs, severity}, {u"title"_qs, title},
            {u"message"_qs, message}, {u"affectedObjectType"_qs, affectedObjectType},
            {u"affectedObjectId"_qs, affectedObjectId},
            // objectId/nextStep remain for V2.4 page compatibility.
            {u"objectId"_qs, affectedObjectId}, {u"nextAction"_qs, nextAction},
            {u"nextStep"_qs, nextAction}, {u"nextActionLabel"_qs, nextActionLabel},
            {u"technicalDetails"_qs, technicalDetails}, {u"inProgress"_qs, inProgress},
            {u"undoAvailable"_qs, undoAvailable}};
}

} // namespace hotas
