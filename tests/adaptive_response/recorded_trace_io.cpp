#include "verification_harness.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace hotas::verification {

bool writeRecordedHotasTraceCsv(const QString &path, const RecordedHotasTrace &trace, QString *error)
{
    if (trace.timestampsUs.size() != trace.axes.size()) {
        if (error) *error = "Timestamp and axis row counts differ.";
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream output(&file);
    output << "timestamp_us,axis_x,axis_y,axis_z,axis_rx,axis_ry,axis_rz,slider0,slider1\n";
    for (size_t row = 0; row < trace.timestampsUs.size(); ++row) {
        output << trace.timestampsUs[row];
        for (const float value : trace.axes[row]) output << ',' << value;
        output << '\n';
    }
    return output.status() == QTextStream::Ok;
}

bool readRecordedHotasTraceCsv(const QString &path, RecordedHotasTrace *trace, QString *error)
{
    if (!trace) {
        if (error) *error = "Trace destination is null.";
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream input(&file);
    const QString expected = "timestamp_us,axis_x,axis_y,axis_z,axis_rx,axis_ry,axis_rz,slider0,slider1";
    if (input.readLine().trimmed() != expected) {
        if (error) *error = "Unsupported HOTAS trace CSV header.";
        return false;
    }
    RecordedHotasTrace parsed;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList fields = line.split(',');
        bool timestampOk = false;
        if (fields.size() != 9) {
            if (error) *error = "Trace row does not contain timestamp plus eight axes.";
            return false;
        }
        const std::uint64_t timestamp = fields[0].toULongLong(&timestampOk);
        if (!timestampOk || (!parsed.timestampsUs.empty() && timestamp <= parsed.timestampsUs.back())) {
            if (error) *error = "Trace timestamps must be strictly increasing unsigned microseconds.";
            return false;
        }
        std::array<float, 8> axes{};
        for (int axis = 0; axis < 8; ++axis) {
            bool axisOk = false;
            axes[static_cast<size_t>(axis)] = fields[axis + 1].toFloat(&axisOk);
            if (!axisOk || !std::isfinite(axes[static_cast<size_t>(axis)]) || std::abs(axes[static_cast<size_t>(axis)]) > 1.0001F) {
                if (error) *error = "Trace axis values must be finite normalized values in [-1, 1].";
                return false;
            }
        }
        parsed.timestampsUs.push_back(timestamp);
        parsed.axes.push_back(axes);
    }
    if (parsed.timestampsUs.size() < 2) {
        if (error) *error = "A trace needs at least two timestamped samples.";
        return false;
    }
    *trace = std::move(parsed);
    return true;
}

ScenarioDefinition replayScenarioFromRecordedAxis(const RecordedHotasTrace &trace, int axis,
                                                  const std::string &id, bool unipolar)
{
    ScenarioDefinition scenario;
    scenario.id = id;
    scenario.trajectoryId = id;
    scenario.family = "recorded-trace";
    scenario.unipolar = unipolar;
    const int selectedAxis = std::clamp(axis, 0, 7);
    const std::uint64_t origin = trace.timestampsUs.empty() ? 0 : trace.timestampsUs.front();
    for (size_t row = 0; row < trace.timestampsUs.size(); ++row) {
        scenario.points.push_back({static_cast<float>(trace.timestampsUs[row] - origin) / 1000000.0F,
            trace.axes[row][static_cast<size_t>(selectedAxis)]});
    }
    scenario.durationSeconds = scenario.points.empty() ? 0.0F : scenario.points.back().timeSeconds;
    return scenario;
}

} // namespace hotas::verification
