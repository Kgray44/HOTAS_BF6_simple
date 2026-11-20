#include "physical_trace_io.h"

#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace hotas::verification {
using namespace Qt::StringLiterals;
namespace {

constexpr auto kHeaderV1 = "timestamp_us,raw_normalized,calibrated_normalized,source_reported,source_changed,elapsed_us,effective_cadence_hz";
constexpr auto kHeaderV2 = "timestamp_us,raw_normalized,calibrated_normalized,source_reported,source_changed,elapsed_us,effective_cadence_hz,raw_direct_input";
constexpr float kMeaningfulChange = 0.00015F;

bool parseFlag(const QString &field, bool *value)
{
    if (!value || (field != u"0"_qs && field != u"1"_qs)) return false;
    *value = field == u"1"_qs;
    return true;
}

bool validNormalized(float value)
{
    return std::isfinite(value) && std::abs(value) <= 1.0001F;
}

int directionOf(float value)
{
    return value > kMeaningfulChange ? 1 : value < -kMeaningfulChange ? -1 : 0;
}

} // namespace

bool writePhysicalAxisTraceCsv(const QString &path, const PhysicalAxisTrace &trace, QString *error)
{
    if (trace.selectedAxis < 0 || trace.selectedAxis >= kPhysicalAxisCount) {
        if (error) *error = "Selected physical axis is invalid.";
        return false;
    }
    if (trace.samples.size() < 2) {
        if (error) *error = "A physical trace needs at least two samples.";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream output(&file);
    output.setRealNumberNotation(QTextStream::FixedNotation);
    output.setRealNumberPrecision(6);
    const QString captureLabel = QString(trace.captureLabel).replace('\n', ' ').replace('\r', ' ');
    const QString axisLabel = QString(trace.selectedAxisLabel).replace('\n', ' ').replace('\r', ' ');
    output << "# physical_trace_schema=V2.3.T-physical-axis-v2\n";
    output << "# capture_label=" << captureLabel << '\n';
    output << "# selected_axis_index=" << trace.selectedAxis << '\n';
    output << "# selected_axis_label=" << axisLabel << '\n';
    output << "# calibration_enabled=" << (trace.calibrationEnabled ? 1 : 0) << '\n';
    output << "# direct_input_range_min=" << trace.directInputRangeMinimum << '\n';
    output << "# direct_input_range_max=" << trace.directInputRangeMaximum << '\n';
    output << "# raw_normalization_min=" << trace.rawNormalizationMinimum << '\n';
    output << "# raw_normalization_max=" << trace.rawNormalizationMaximum << '\n';
    output << kHeaderV2 << '\n';
    std::uint64_t previousTimestamp = 0;
    for (const PhysicalAxisTraceSample &sample : trace.samples) {
        if ((previousTimestamp != 0 && sample.timestampUs <= previousTimestamp)
            || !validNormalized(sample.rawNormalized) || !validNormalized(sample.calibratedNormalized)) {
            if (error) *error = "Physical trace contains invalid timestamps or normalized axis values.";
            return false;
        }
        previousTimestamp = sample.timestampUs;
        output << sample.timestampUs << ',' << sample.rawNormalized << ',' << sample.calibratedNormalized << ','
               << (sample.sourceReported ? 1 : 0) << ',' << (sample.sourceChanged ? 1 : 0) << ','
               << sample.elapsedUs << ',' << sample.effectiveCadenceHz << ',' << sample.rawDirectInput << '\n';
    }
    if (output.status() != QTextStream::Ok || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool readPhysicalAxisTraceCsv(const QString &path, PhysicalAxisTrace *trace, QString *error)
{
    if (!trace) {
        if (error) *error = "Physical trace destination is null.";
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    PhysicalAxisTrace parsed;
    QTextStream input(&file);
    bool headerSeen = false;
    bool legacyV1 = false;
    std::uint64_t previousTimestamp = 0;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            const int separator = line.indexOf('=');
            if (separator <= 2) continue;
            const QString key = line.mid(2, separator - 2);
            const QString value = line.mid(separator + 1);
            if (key == u"capture_label"_qs) parsed.captureLabel = value;
            else if (key == u"selected_axis_index"_qs) {
                bool ok = false;
                parsed.selectedAxis = value.toInt(&ok);
                if (!ok) {
                    if (error) *error = "Selected axis metadata is invalid.";
                    return false;
                }
            } else if (key == u"selected_axis_label"_qs) parsed.selectedAxisLabel = value;
            else if (key == u"calibration_enabled"_qs) {
                if (!parseFlag(value, &parsed.calibrationEnabled)) {
                    if (error) *error = "Calibration metadata is invalid.";
                    return false;
                }
            } else if (key == u"direct_input_range_min"_qs) {
                bool ok = false;
                parsed.directInputRangeMinimum = value.toInt(&ok);
                if (!ok) {
                    if (error) *error = "DirectInput minimum range metadata is invalid.";
                    return false;
                }
            } else if (key == u"direct_input_range_max"_qs) {
                bool ok = false;
                parsed.directInputRangeMaximum = value.toInt(&ok);
                if (!ok) {
                    if (error) *error = "DirectInput maximum range metadata is invalid.";
                    return false;
                }
            } else if (key == u"raw_normalization_min"_qs) {
                bool ok = false;
                parsed.rawNormalizationMinimum = value.toInt(&ok);
                if (!ok) {
                    if (error) *error = "Raw-normalization minimum metadata is invalid.";
                    return false;
                }
            } else if (key == u"raw_normalization_max"_qs) {
                bool ok = false;
                parsed.rawNormalizationMaximum = value.toInt(&ok);
                if (!ok) {
                    if (error) *error = "Raw-normalization maximum metadata is invalid.";
                    return false;
                }
            }
            continue;
        }
        if (!headerSeen) {
            if (line == QLatin1String(kHeaderV1)) legacyV1 = true;
            else if (line != QLatin1String(kHeaderV2)) {
                if (error) *error = "Unsupported physical trace CSV header.";
                return false;
            }
            headerSeen = true;
            continue;
        }
        const QStringList fields = line.split(',');
        if (fields.size() != (legacyV1 ? 7 : 8)) {
            if (error) *error = legacyV1 ? "Physical trace v1 row does not contain seven fields."
                                        : "Physical trace v2 row does not contain eight fields.";
            return false;
        }
        PhysicalAxisTraceSample sample;
        bool timestampOk = false;
        bool rawOk = false;
        bool calibratedOk = false;
        bool elapsedOk = false;
        bool cadenceOk = false;
        bool rawDirectInputOk = legacyV1;
        sample.timestampUs = fields[0].toULongLong(&timestampOk);
        sample.rawNormalized = fields[1].toFloat(&rawOk);
        sample.calibratedNormalized = fields[2].toFloat(&calibratedOk);
        sample.elapsedUs = fields[5].toULongLong(&elapsedOk);
        sample.effectiveCadenceHz = fields[6].toDouble(&cadenceOk);
        if (!legacyV1) sample.rawDirectInput = fields[7].toInt(&rawDirectInputOk);
        if (!timestampOk || !rawOk || !calibratedOk || !elapsedOk || !cadenceOk || !rawDirectInputOk
            || !parseFlag(fields[3], &sample.sourceReported)
            || !parseFlag(fields[4], &sample.sourceChanged)
            || (previousTimestamp != 0 && sample.timestampUs <= previousTimestamp)
            || !validNormalized(sample.rawNormalized) || !validNormalized(sample.calibratedNormalized)
            || !std::isfinite(sample.effectiveCadenceHz) || sample.effectiveCadenceHz < 0.0) {
            if (error) *error = "Physical trace has invalid sample data.";
            return false;
        }
        if (previousTimestamp != 0 && sample.elapsedUs != sample.timestampUs - previousTimestamp) {
            if (error) *error = "Physical trace elapsed interval does not match adjacent timestamps.";
            return false;
        }
        previousTimestamp = sample.timestampUs;
        parsed.samples.push_back(sample);
    }
    if (!headerSeen || parsed.selectedAxis < 0 || parsed.selectedAxis >= kPhysicalAxisCount
        || parsed.directInputRangeMinimum >= parsed.directInputRangeMaximum
        || parsed.rawNormalizationMinimum >= parsed.rawNormalizationMaximum
        || parsed.samples.size() < 2) {
        if (error) *error = "Physical trace metadata or sample count is incomplete.";
        return false;
    }
    *trace = std::move(parsed);
    return true;
}

std::vector<TraceSample> traceSamplesFromPhysicalAxisTrace(const PhysicalAxisTrace &trace)
{
    std::vector<TraceSample> result;
    if (trace.samples.empty()) return result;
    result.reserve(trace.samples.size());
    const std::uint64_t origin = trace.samples.front().timestampUs;
    int previousDirection = 0;
    bool previousMoving = false;
    for (size_t index = 0; index < trace.samples.size(); ++index) {
        const PhysicalAxisTraceSample &current = trace.samples[index];
        const float time = static_cast<float>(current.timestampUs - origin) / 1000000.0F;
        const float dt = index + 1U < trace.samples.size()
            ? static_cast<float>(trace.samples[index + 1U].timestampUs - current.timestampUs) / 1000000.0F
            : 0.0F;
        float velocity = 0.0F;
        if (index > 0U && current.elapsedUs > 0U) {
            velocity = (current.calibratedNormalized - trace.samples[index - 1U].calibratedNormalized)
                / (static_cast<float>(current.elapsedUs) / 1000000.0F);
        }
        const int direction = directionOf(velocity);
        const bool moving = direction != 0;
        TraceSample sample;
        sample.timeSeconds = time;
        sample.intended = current.calibratedNormalized;
        sample.humanIntentVelocity = velocity;
        sample.humanIntentDirection = direction;
        sample.humanIntentReversal = direction != 0 && previousDirection != 0 && direction != previousDirection;
        sample.humanIntentStop = !moving && previousMoving;
        sample.physical = current.calibratedNormalized;
        sample.velocity = velocity;
        sample.dtSeconds = dt;
        sample.intendedMoving = moving;
        sample.sourceSampleUpdated = current.sourceReported;
        sample.sourceSampleTimeSeconds = time;
        sample.targetArrival = sample.humanIntentStop;
        sample.physicalStop = sample.humanIntentStop;
        sample.trueReversal = sample.humanIntentReversal;
        result.push_back(sample);
        if (direction != 0) previousDirection = direction;
        previousMoving = moving;
    }
    return result;
}

} // namespace hotas::verification
