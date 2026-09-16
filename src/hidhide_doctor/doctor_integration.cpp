#include "doctor_integration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>

#include <algorithm>

namespace hotas::doctor {
namespace {

QString contextPath(const QString &sessionId)
{
    return QDir(doctorIntegrationDirectory()).filePath(QStringLiteral("context-%1.json").arg(sessionId));
}

QString resultPath(const QString &sessionId)
{
    return QDir(doctorIntegrationDirectory()).filePath(QStringLiteral("result-%1.json").arg(sessionId));
}

bool validText(const QString &value, qsizetype maximum)
{
    if (value.size() > maximum || value.contains(QChar::Null)) return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

bool validPathHint(const QString &path)
{
    if (path.isEmpty()) return true;
    const QFileInfo info(path);
    return info.isAbsolute() && validText(path, 1024);
}

bool secureForCurrentUser(const QString &path)
{
    // The shared folder is per-user Local AppData.  Remove inherited broad
    // permissions where Qt can express them and fail closed when the file
    // cannot be made owner-readable/writable.
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool atomicallyWriteJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not create the local Doctor handoff file.");
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.size() > kDoctorIntegrationMaximumBytes || file.write(payload) != payload.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not commit the bounded local Doctor handoff file.");
        return false;
    }
    if (!secureForCurrentUser(path)) {
        QFile::remove(path);
        if (error) *error = QStringLiteral("Could not apply owner-only permissions to the Doctor handoff file.");
        return false;
    }
    return true;
}

bool validateContext(const DoctorLaunchContext &context, QString *reason)
{
    const QList<QString> values{context.invokingVersion, context.invokingBuildId, context.reason,
        context.profileId, context.profileName, context.deviceRigId, context.deviceRigName,
        context.expectedVirtualOutput, context.isolationIntent};
    if (!isValidDoctorIntegrationSessionId(context.sessionId) || !validPathHint(context.expectedHotasExecutable)
        || std::any_of(values.cbegin(), values.cend(), [](const QString &value) { return !validText(value, 256); })
        || context.expectedPhysicalControllerIds.size() > 16
        || std::any_of(context.expectedPhysicalControllerIds.cbegin(), context.expectedPhysicalControllerIds.cend(),
                       [](const QString &value) { return !validText(value, 256); })) {
        if (reason) *reason = QStringLiteral("The local launch context is malformed or exceeds its safety limits.");
        return false;
    }
    return true;
}

} // namespace

QString doctorIntegrationDirectory()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString directory = QDir(root).filePath(QStringLiteral("HOTAS BF6/HidHide Doctor/IPC"));
    QDir().mkpath(directory);
    return directory;
}

bool isValidDoctorIntegrationSessionId(const QString &sessionId)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{32}$"));
    return expression.match(sessionId).hasMatch();
}

bool writeDoctorLaunchContext(DoctorLaunchContext context, QString *error)
{
    if (!validateContext(context, error)) return false;
    QJsonArray controllers;
    for (const QString &identifier : context.expectedPhysicalControllerIds) controllers.append(identifier);
    return atomicallyWriteJson(contextPath(context.sessionId), QJsonObject{
        {QStringLiteral("schemaVersion"), kDoctorIntegrationProtocolVersion},
        {QStringLiteral("sessionId"), context.sessionId},
        {QStringLiteral("invokingVersion"), context.invokingVersion},
        {QStringLiteral("invokingBuildId"), context.invokingBuildId},
        {QStringLiteral("reason"), context.reason},
        {QStringLiteral("expectedHotasExecutable"), context.expectedHotasExecutable},
        {QStringLiteral("profileId"), context.profileId}, {QStringLiteral("profileName"), context.profileName},
        {QStringLiteral("deviceRigId"), context.deviceRigId}, {QStringLiteral("deviceRigName"), context.deviceRigName},
        {QStringLiteral("expectedVirtualOutput"), context.expectedVirtualOutput},
        {QStringLiteral("isolationIntent"), context.isolationIntent},
        {QStringLiteral("expectedPhysicalControllerIds"), controllers}}, error);
}

DoctorIntegrationReadResult consumeDoctorLaunchContext(const QString &sessionId)
{
    DoctorIntegrationReadResult result;
    if (!isValidDoctorIntegrationSessionId(sessionId)) {
        result.rejection = QStringLiteral("The Doctor launch token is invalid.");
        return result;
    }
    const QString path = contextPath(sessionId);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.rejection = QStringLiteral("The requested local Doctor context is unavailable.");
        return result;
    }
    const QByteArray payload = file.read(kDoctorIntegrationMaximumBytes + 1);
    file.close();
    QFile::remove(path); // one-time intent; no context remains after Doctor has read it
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (payload.size() > kDoctorIntegrationMaximumBytes || parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.rejection = QStringLiteral("The Doctor launch context is not a bounded valid JSON object.");
        return result;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != kDoctorIntegrationProtocolVersion
        || object.value(QStringLiteral("sessionId")).toString() != sessionId) {
        result.rejection = QStringLiteral("The Doctor launch context uses an unsupported protocol version.");
        return result;
    }
    DoctorLaunchContext context;
    context.sessionId = sessionId;
    context.invokingVersion = object.value(QStringLiteral("invokingVersion")).toString();
    context.invokingBuildId = object.value(QStringLiteral("invokingBuildId")).toString();
    context.reason = object.value(QStringLiteral("reason")).toString();
    context.expectedHotasExecutable = object.value(QStringLiteral("expectedHotasExecutable")).toString();
    context.profileId = object.value(QStringLiteral("profileId")).toString();
    context.profileName = object.value(QStringLiteral("profileName")).toString();
    context.deviceRigId = object.value(QStringLiteral("deviceRigId")).toString();
    context.deviceRigName = object.value(QStringLiteral("deviceRigName")).toString();
    context.expectedVirtualOutput = object.value(QStringLiteral("expectedVirtualOutput")).toString();
    context.isolationIntent = object.value(QStringLiteral("isolationIntent")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("expectedPhysicalControllerIds")).toArray()) context.expectedPhysicalControllerIds.append(value.toString());
    if (!validateContext(context, &result.rejection)) return result;
    result.accepted = true;
    result.context = std::move(context);
    return result;
}

bool writeDoctorIntegrationResult(const DoctorLaunchContext &context, const QString &state,
                                  const QString &detail, QString *error)
{
    if (!validateContext(context, error) || !validText(state, 80) || !validText(detail, 1024)) {
        if (error && error->isEmpty()) *error = QStringLiteral("The Doctor result exceeds its safety limits.");
        return false;
    }
    return atomicallyWriteJson(resultPath(context.sessionId), QJsonObject{{QStringLiteral("schemaVersion"), kDoctorIntegrationProtocolVersion},
        {QStringLiteral("sessionId"), context.sessionId}, {QStringLiteral("state"), state},
        {QStringLiteral("detail"), detail}}, error);
}

bool consumeDoctorIntegrationResult(const QString &sessionId, QString *state, QString *detail, QString *error)
{
    if (!isValidDoctorIntegrationSessionId(sessionId)) {
        if (error) *error = QStringLiteral("Doctor result session token is invalid.");
        return false;
    }
    QFile file(resultPath(sessionId));
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("Doctor result could not be read."); return false; }
    const QByteArray payload = file.read(kDoctorIntegrationMaximumBytes + 1);
    file.close();
    QFile::remove(file.fileName());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonObject object = document.object();
    const QString parsedState = object.value(QStringLiteral("state")).toString();
    const QString parsedDetail = object.value(QStringLiteral("detail")).toString();
    if (payload.size() > kDoctorIntegrationMaximumBytes || parseError.error != QJsonParseError::NoError || !document.isObject()
        || object.value(QStringLiteral("schemaVersion")).toInt(-1) != kDoctorIntegrationProtocolVersion
        || object.value(QStringLiteral("sessionId")).toString() != sessionId
        || !validText(parsedState, 80) || !validText(parsedDetail, 1024)) {
        if (error) *error = QStringLiteral("Doctor result was rejected as malformed.");
        return false;
    }
    if (state) *state = parsedState;
    if (detail) *detail = parsedDetail;
    return true;
}

} // namespace hotas::doctor
