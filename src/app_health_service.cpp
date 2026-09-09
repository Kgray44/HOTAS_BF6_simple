#include "app_health_service.h"

#include <QtGlobal>

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

bool isBlockingSeverity(const QString &severity)
{
    return severity == u"critical"_qs || severity == u"error"_qs
        || severity == u"warning"_qs || severity == u"setup-needed"_qs
        || severity == u"offline"_qs;
}

} // namespace

QVariantMap AppHealthService::summarize(const QVariantList &issues)
{
    QVariantMap primary;
    int blockingCount = 0;
    int noteCount = 0;
    for (const QVariant &entry : issues) {
        const QVariantMap issue = entry.toMap();
        const QString severity = issue.value(u"severity"_qs).toString();
        if (severity == u"note"_qs || severity == u"info"_qs || severity == u"waiting"_qs) {
            ++noteCount;
            continue;
        }
        if (isBlockingSeverity(severity)) ++blockingCount;
        if (primary.isEmpty() || issue.value(u"priority"_qs, 500).toInt()
                                 < primary.value(u"priority"_qs, 500).toInt()) {
            primary = issue;
        }
    }

    const bool ready = blockingCount == 0;
    const QString label = ready ? u"READY"_qs
        : blockingCount == 1 ? u"1 NEEDS ATTENTION"_qs
                             : QString(u"%1 NEED ATTENTION"_qs).arg(blockingCount);
    return {{u"ready"_qs, ready}, {u"label"_qs, label},
            {u"blockingCount"_qs, blockingCount}, {u"noteCount"_qs, noteCount},
            {u"issueCount"_qs, issues.size()}, {u"primaryIssue"_qs, primary}};
}

} // namespace hotas
