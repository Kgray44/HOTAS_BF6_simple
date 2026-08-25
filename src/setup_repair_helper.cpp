#include "setup_repair_helper.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>

#include <algorithm>

namespace hotas {
namespace {

constexpr int kRepairOperationTimeoutMs = 30000;

QString decodeProcessOutput(const QByteArray &bytes)
{
    if (bytes.size() >= 2 && bytes[0] == '\xff' && bytes[1] == '\xfe') {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2),
                                  (bytes.size() - 2) / 2);
    }
    if (bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes.contains('\0')) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    }
    return QString::fromLocal8Bit(bytes);
}

QJsonObject executeOperation(const QJsonObject &operation, bool rollback)
{
    QJsonObject result;
    result.insert(QStringLiteral("name"), operation.value(QStringLiteral("name")).toString());
    result.insert(QStringLiteral("rollback"), rollback);

    const QString program = operation.value(QStringLiteral("program")).toString();
    const QJsonArray argumentsJson = rollback
        ? operation.value(QStringLiteral("rollbackArguments")).toArray()
        : operation.value(QStringLiteral("arguments")).toArray();
    QStringList arguments;
    arguments.reserve(argumentsJson.size());
    for (const QJsonValue &value : argumentsJson) arguments.append(value.toString());

    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
        result.insert(QStringLiteral("message"), QStringLiteral("Utility was not found: %1").arg(program));
        result.insert(QStringLiteral("started"), false);
        result.insert(QStringLiteral("finished"), false);
        result.insert(QStringLiteral("succeeded"), false);
        result.insert(QStringLiteral("exitCode"), -1);
        return result;
    }

    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    const bool started = process.waitForStarted(std::min(kRepairOperationTimeoutMs, 3000));
    result.insert(QStringLiteral("started"), started);
    if (!started) {
        result.insert(QStringLiteral("finished"), false);
        result.insert(QStringLiteral("succeeded"), false);
        result.insert(QStringLiteral("exitCode"), -1);
        result.insert(QStringLiteral("message"), process.errorString());
        return result;
    }

    const bool finished = process.waitForFinished(kRepairOperationTimeoutMs);
    if (!finished) {
        process.kill();
        process.waitForFinished(1000);
    }
    const QString output = decodeProcessOutput(process.readAllStandardOutput());
    const QString errorOutput = decodeProcessOutput(process.readAllStandardError());
    const bool succeeded = finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    result.insert(QStringLiteral("finished"), finished);
    result.insert(QStringLiteral("succeeded"), succeeded);
    result.insert(QStringLiteral("exitCode"), finished ? process.exitCode() : -1);
    result.insert(QStringLiteral("output"), output);
    result.insert(QStringLiteral("errorOutput"), errorOutput);
    if (!finished) {
        result.insert(QStringLiteral("message"), QStringLiteral("Timed out after %1 ms").arg(kRepairOperationTimeoutMs));
    } else if (process.exitStatus() != QProcess::NormalExit) {
        result.insert(QStringLiteral("message"), QStringLiteral("Utility terminated unexpectedly"));
    } else if (!succeeded) {
        result.insert(QStringLiteral("message"), QStringLiteral("Command exited with code %1").arg(process.exitCode()));
    }
    return result;
}

bool writeResult(const QString &path, const QJsonObject &result)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    return file.commit();
}

} // namespace

std::optional<int> runElevatedRepairTransaction(int argc, char *argv[])
{
    bool requested = false;
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--hotas-repair-transaction")) {
            requested = true;
            break;
        }
    }
    if (!requested) return std::nullopt;

    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("hotas-repair-transaction"), QStringLiteral("Run the approved repair transaction.")});
    parser.addOption({QStringLiteral("request"), QStringLiteral("Path to the approved repair request."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("result"), QStringLiteral("Path for structured repair results."), QStringLiteral("path")});
    parser.process(application);
    const QString requestPath = parser.value(QStringLiteral("request"));
    const QString resultPath = parser.value(QStringLiteral("result"));
    if (requestPath.isEmpty() || resultPath.isEmpty()) return 2;

    QJsonObject response;
    QJsonArray results;
    response.insert(QStringLiteral("schemaVersion"), 1);
    QFile request(requestPath);
    QJsonParseError parseError;
    if (!request.open(QIODevice::ReadOnly)) {
        response.insert(QStringLiteral("success"), false);
        response.insert(QStringLiteral("message"), QStringLiteral("Approved repair request could not be read."));
        response.insert(QStringLiteral("operations"), results);
        writeResult(resultPath, response);
        return 2;
    }
    const QJsonDocument requestDocument = QJsonDocument::fromJson(request.readAll(), &parseError);
    const QJsonArray operations = requestDocument.object().value(QStringLiteral("operations")).toArray();
    if (parseError.error != QJsonParseError::NoError || operations.isEmpty() || operations.size() > 4) {
        response.insert(QStringLiteral("success"), false);
        response.insert(QStringLiteral("message"), QStringLiteral("Approved repair request is invalid."));
        response.insert(QStringLiteral("operations"), results);
        writeResult(resultPath, response);
        return 2;
    }

    QList<QJsonObject> completed;
    bool success = true;
    for (const QJsonValue &value : operations) {
        const QJsonObject operation = value.toObject();
        const QJsonObject result = executeOperation(operation, false);
        results.append(result);
        if (!result.value(QStringLiteral("succeeded")).toBool()) {
            success = false;
            break;
        }
        completed.append(operation);
    }
    if (!success) {
        for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
            if (it->value(QStringLiteral("rollbackArguments")).toArray().isEmpty()) continue;
            results.append(executeOperation(*it, true));
        }
    }

    response.insert(QStringLiteral("success"), success);
    response.insert(QStringLiteral("message"), success
        ? QStringLiteral("Approved repair operations completed.")
        : QStringLiteral("An approved repair operation failed."));
    response.insert(QStringLiteral("operations"), results);
    if (!writeResult(resultPath, response)) return 3;
    return success ? 0 : 1;
}

} // namespace hotas
