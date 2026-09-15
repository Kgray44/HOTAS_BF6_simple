#include "doctor_repair_helper_protocol.h"

#include "doctor_repair_engine.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace hotas::doctor {
namespace {

QString sha256(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

QJsonObject operationJson(const RepairOperation &operation)
{
    QJsonObject object{{QStringLiteral("id"), operation.id.value()},
        {QStringLiteral("kind"), static_cast<int>(operation.kind)},
        {QStringLiteral("targetKind"), static_cast<int>(operation.targetKind)},
        {QStringLiteral("target"), operation.targetIdentity},
        {QStringLiteral("value"), operation.requestedValue}};
    QJsonArray preconditions;
    for (const RepairPrecondition &precondition : operation.preconditions)
        preconditions.append(QJsonObject{{QStringLiteral("key"), precondition.stableKey}, {QStringLiteral("expected"), precondition.expectedValue}});
    object.insert(QStringLiteral("preconditions"), preconditions);
    return object;
}

std::optional<RepairOperation> operationFromJson(const QJsonObject &object, QString *reason)
{
    RepairOperation operation;
    operation.id = DoctorOperationId(object.value(QStringLiteral("id")).toString());
    operation.kind = static_cast<RepairOperationKind>(object.value(QStringLiteral("kind")).toInt());
    operation.targetKind = static_cast<RepairTargetKind>(object.value(QStringLiteral("targetKind")).toInt());
    operation.targetIdentity = object.value(QStringLiteral("target")).toString();
    operation.requestedValue = object.value(QStringLiteral("value")).toString();
    const QJsonArray preconditions = object.value(QStringLiteral("preconditions")).toArray();
    if (preconditions.size() > 16) { if (reason) *reason = QStringLiteral("Helper operation has too many preconditions."); return std::nullopt; }
    for (const QJsonValue &value : preconditions) {
        const QJsonObject condition = value.toObject();
        operation.preconditions.append({condition.value(QStringLiteral("key")).toString(), condition.value(QStringLiteral("expected")).toString()});
    }
    if (!operation.isWellFormed(reason)) return std::nullopt;
    return operation;
}

QJsonObject planJson(const RepairPlan &plan)
{
    QJsonArray operations;
    for (const RepairOperation &operation : plan.operations) operations.append(operationJson(operation));
    QJsonArray preconditions;
    for (const RepairPrecondition &precondition : plan.preconditions)
        preconditions.append(QJsonObject{{QStringLiteral("key"), precondition.stableKey}, {QStringLiteral("expected"), precondition.expectedValue}});
    return {{QStringLiteral("id"), plan.id.value()}, {QStringLiteral("sessionId"), plan.sessionId.value()},
        {QStringLiteral("recipeId"), plan.recipeId.value()}, {QStringLiteral("recipeVersion"), plan.recipeVersion},
        {QStringLiteral("riskClass"), static_cast<int>(plan.riskClass)}, {QStringLiteral("qualification"), static_cast<int>(plan.qualification)},
        {QStringLiteral("authorization"), static_cast<int>(plan.authorization)}, {QStringLiteral("preconditions"), preconditions},
        {QStringLiteral("preconditionFingerprint"), plan.preconditionFingerprint}, {QStringLiteral("expectedPreState"), plan.expectedPreState},
        {QStringLiteral("expectedPostState"), plan.expectedPostState}, {QStringLiteral("expectedPostFingerprint"), plan.expectedPostFingerprint},
        {QStringLiteral("operations"), operations}, {QStringLiteral("planDigest"), plan.integrityDigest}};
}

std::optional<RepairPlan> planFromJson(const QJsonObject &object, QString *reason)
{
    RepairPlan plan;
    plan.id = RepairPlanId(object.value(QStringLiteral("id")).toString());
    plan.sessionId = DoctorSessionId(object.value(QStringLiteral("sessionId")).toString());
    plan.recipeId = RepairRecipeId(object.value(QStringLiteral("recipeId")).toString());
    plan.recipeVersion = object.value(QStringLiteral("recipeVersion")).toString();
    plan.riskClass = static_cast<RepairRiskClass>(object.value(QStringLiteral("riskClass")).toInt());
    plan.qualification = static_cast<RepairQualificationLevel>(object.value(QStringLiteral("qualification")).toInt());
    plan.authorization = static_cast<RepairAuthorization>(object.value(QStringLiteral("authorization")).toInt());
    plan.preconditionFingerprint = object.value(QStringLiteral("preconditionFingerprint")).toString();
    plan.expectedPreState = object.value(QStringLiteral("expectedPreState")).toString();
    plan.expectedPostState = object.value(QStringLiteral("expectedPostState")).toString();
    plan.expectedPostFingerprint = object.value(QStringLiteral("expectedPostFingerprint")).toString();
    plan.integrityDigest = object.value(QStringLiteral("planDigest")).toString();
    const QJsonArray preconditions = object.value(QStringLiteral("preconditions")).toArray();
    const QJsonArray operations = object.value(QStringLiteral("operations")).toArray();
    if (preconditions.size() > 32 || operations.isEmpty() || operations.size() > 6) {
        if (reason) *reason = QStringLiteral("Helper plan exceeds the bounded Phase 3 R1 request shape.");
        return std::nullopt;
    }
    for (const QJsonValue &value : preconditions) {
        const QJsonObject condition = value.toObject();
        plan.preconditions.append({condition.value(QStringLiteral("key")).toString(), condition.value(QStringLiteral("expected")).toString()});
    }
    for (const QJsonValue &value : operations) {
        std::optional<RepairOperation> operation = operationFromJson(value.toObject(), reason);
        if (!operation) return std::nullopt;
        plan.operations.append(*operation);
    }
    return plan;
}

bool validNonce(const QString &nonce)
{
    return QRegularExpression(QStringLiteral("^[A-F0-9]{32,128}$")).match(nonce).hasMatch();
}

bool boolValue(const QString &value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0;
}

std::optional<QJsonObject> configurationState(const QString &serialized, QString *reason)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(serialized.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (reason) *reason = QStringLiteral("Bound helper plan has no valid serialized configuration state.");
        return std::nullopt;
    }
    const QJsonObject state = document.object();
    if (!state.value(QStringLiteral("whitelist")).isArray() || !state.value(QStringLiteral("blacklist")).isArray()
        || !state.value(QStringLiteral("active")).isBool() || !state.value(QStringLiteral("inverse")).isBool()) {
        if (reason) *reason = QStringLiteral("Bound helper plan lacks complete HidHide configuration state.");
        return std::nullopt;
    }
    return state;
}

QStringList strings(const QJsonArray &array)
{
    QStringList result;
    result.reserve(array.size());
    for (const QJsonValue &value : array) result.append(value.toString());
    return result;
}

QStringList exactDifference(const QStringList &before, const QStringList &after)
{
    QStringList result;
    for (const QString &entry : before) {
        const bool retained = std::any_of(after.cbegin(), after.cend(), [&](const QString &candidate) {
            return ConfigurationDeltaEngine::semanticKey(candidate) == ConfigurationDeltaEngine::semanticKey(entry);
        });
        if (!retained) result.append(entry);
    }
    return result;
}

bool containsOperation(const QList<RepairOperation> &operations, RepairOperationKind kind, const QString &target)
{
    return std::any_of(operations.cbegin(), operations.cend(), [&](const RepairOperation &operation) {
        return operation.kind == kind && ConfigurationDeltaEngine::semanticKey(operation.targetIdentity) == ConfigurationDeltaEngine::semanticKey(target)
            && operation.requestedValue == target;
    });
}

bool planDeltaMatches(const RepairPlan &plan, QString *reason)
{
    const std::optional<QJsonObject> before = configurationState(plan.expectedPreState, reason);
    const std::optional<QJsonObject> after = configurationState(plan.expectedPostState, reason);
    if (!before || !after) return false;
    const auto exactListDelta = [&](const QString &key, RepairOperationKind add, RepairOperationKind remove) {
        const QStringList beforeValues = strings(before->value(key).toArray());
        const QStringList afterValues = strings(after->value(key).toArray());
        const QStringList additions = exactDifference(afterValues, beforeValues);
        const QStringList removals = exactDifference(beforeValues, afterValues);
        for (const QString &entry : additions) if (!containsOperation(plan.operations, add, entry)) return false;
        for (const QString &entry : removals) if (!containsOperation(plan.operations, remove, entry)) return false;
        const int actual = std::count_if(plan.operations.cbegin(), plan.operations.cend(), [&](const RepairOperation &operation) {
            return operation.kind == add || operation.kind == remove;
        });
        return actual == additions.size() + removals.size();
    };
    if (!exactListDelta(QStringLiteral("whitelist"), RepairOperationKind::AddWhitelistEntry, RepairOperationKind::RemoveWhitelistEntry)
        || !exactListDelta(QStringLiteral("blacklist"), RepairOperationKind::AddBlacklistEntry, RepairOperationKind::RemoveBlacklistEntry)) {
        if (reason) *reason = QStringLiteral("Helper plan operations do not exactly match its bound configuration delta.");
        return false;
    }
    const auto exactBooleanDelta = [&](const QString &key, RepairOperationKind kind, const QString &target) {
        const bool beforeValue = before->value(key).toBool();
        const bool afterValue = after->value(key).toBool();
        const int count = std::count_if(plan.operations.cbegin(), plan.operations.cend(), [&](const RepairOperation &operation) { return operation.kind == kind; });
        if (beforeValue == afterValue) return count == 0;
        return count == 1 && std::any_of(plan.operations.cbegin(), plan.operations.cend(), [&](const RepairOperation &operation) {
            return operation.kind == kind && operation.targetIdentity == target
                && operation.requestedValue == (afterValue ? QStringLiteral("true") : QStringLiteral("false"));
        });
    };
    if (!exactBooleanDelta(QStringLiteral("active"), RepairOperationKind::SetHidHideActive, QStringLiteral("active"))
        || !exactBooleanDelta(QStringLiteral("inverse"), RepairOperationKind::SetHidHideInverse, QStringLiteral("inverse"))) {
        if (reason) *reason = QStringLiteral("Helper plan boolean operation does not exactly match its bound configuration delta.");
        return false;
    }
    return true;
}

} // namespace

QString RepairHelperProtocol::seal(const RepairHelperRequest &request)
{
    QJsonObject object = serialize(request);
    object.remove(QStringLiteral("requestDigest"));
    return sha256(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject RepairHelperProtocol::serialize(const RepairHelperRequest &request)
{
    return {{QStringLiteral("protocolVersion"), request.version}, {QStringLiteral("doctorBuildId"), request.doctorBuildId},
        {QStringLiteral("helperBuildId"), request.helperBuildId}, {QStringLiteral("transactionId"), request.transactionId.value()},
        {QStringLiteral("plan"), planJson(request.plan)}, {QStringLiteral("nonce"), request.nonce},
        {QStringLiteral("expiresAt"), request.expiresAt.toUTC().toString(Qt::ISODateWithMs)}, {QStringLiteral("requestDigest"), request.requestDigest}};
}

std::optional<RepairHelperRequest> RepairHelperProtocol::parse(const QByteArray &payload, QString *reason)
{
    if (payload.isEmpty() || payload.size() > RepairHelperRequest::maximumMessageBytes) {
        if (reason) *reason = QStringLiteral("Helper frame is empty or exceeds the 64 KiB limit.");
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (reason) *reason = QStringLiteral("Helper frame is malformed.");
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    RepairHelperRequest request;
    request.version = object.value(QStringLiteral("protocolVersion")).toInt();
    request.doctorBuildId = object.value(QStringLiteral("doctorBuildId")).toString();
    request.helperBuildId = object.value(QStringLiteral("helperBuildId")).toString();
    request.transactionId = RepairTransactionId(object.value(QStringLiteral("transactionId")).toString());
    request.nonce = object.value(QStringLiteral("nonce")).toString();
    request.expiresAt = QDateTime::fromString(object.value(QStringLiteral("expiresAt")).toString(), Qt::ISODateWithMs);
    request.requestDigest = object.value(QStringLiteral("requestDigest")).toString();
    const std::optional<RepairPlan> plan = planFromJson(object.value(QStringLiteral("plan")).toObject(), reason);
    if (!plan) return std::nullopt;
    request.plan = *plan;
    return request;
}

bool RepairHelperProtocol::targetIsAllowed(const RepairOperation &operation, QString *reason)
{
    if (!operation.isWellFormed(reason)) return false;
    switch (operation.kind) {
    case RepairOperationKind::AddWhitelistEntry:
    case RepairOperationKind::RemoveWhitelistEntry:
        if (!QFileInfo(operation.targetIdentity).isAbsolute() || !operation.targetIdentity.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            if (reason) *reason = QStringLiteral("Whitelist target is not an absolute executable identity.");
            return false;
        }
        return true;
    case RepairOperationKind::AddBlacklistEntry:
    case RepairOperationKind::RemoveBlacklistEntry:
        if (!QRegularExpression(QStringLiteral("^(HID|ROOT)\\\\[A-Z0-9_&\\\\-]+"), QRegularExpression::CaseInsensitiveOption).match(operation.targetIdentity).hasMatch()) {
            if (reason) *reason = QStringLiteral("Blacklist target is outside the HidHide device-instance scope.");
            return false;
        }
        return true;
    case RepairOperationKind::SetHidHideActive:
        if (operation.targetIdentity != QStringLiteral("active") || !boolValue(operation.requestedValue)) {
            if (reason) *reason = QStringLiteral("Active-state operation is not a typed HidHide boolean.");
            return false;
        }
        return true;
    case RepairOperationKind::SetHidHideInverse:
        if (operation.targetIdentity != QStringLiteral("inverse") || !boolValue(operation.requestedValue)) {
            if (reason) *reason = QStringLiteral("Inverse-state operation is not a typed HidHide boolean.");
            return false;
        }
        return true;
    default:
        if (reason) *reason = QStringLiteral("Operation is outside the Phase 3 R1 helper allow-list.");
        return false;
    }
}

bool RepairHelperProtocol::validate(const RepairHelperRequest &request, const DoctorEnvironment &environment,
    const QString &expectedDoctorBuildId, const QString &expectedNonce, QString *reason)
{
    if (request.version != RepairHelperRequest::protocolVersion || request.doctorBuildId != expectedDoctorBuildId
        || request.helperBuildId.isEmpty() || !request.transactionId.isValid() || !request.plan.id.isValid()) {
        if (reason) *reason = QStringLiteral("Helper protocol version, build identity, or transaction binding is invalid.");
        return false;
    }
    if (!validNonce(request.nonce) || request.nonce != expectedNonce) {
        if (reason) *reason = QStringLiteral("Helper request nonce is invalid, unexpected, or replayed.");
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!request.expiresAt.isValid() || request.expiresAt.toUTC() <= now || request.expiresAt.toUTC() > now.addSecs(10 * 60)) {
        if (reason) *reason = QStringLiteral("Helper request is expired or outside its short-lived authorization window.");
        return false;
    }
    if (request.requestDigest != seal(request) || request.plan.integrityDigest != RepairHelperContract::seal(request.plan)) {
        if (reason) *reason = QStringLiteral("Helper request or bound repair plan digest does not match.");
        return false;
    }
    if (!RepairHelperContract::validate(request.plan, environment, true, reason)) return false;
    if (!planDeltaMatches(request.plan, reason)) return false;
    for (const RepairOperation &operation : request.plan.operations) if (!targetIsAllowed(operation, reason)) return false;
    return true;
}

} // namespace hotas::doctor
