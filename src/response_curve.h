#pragma once

#include "mapping_types.h"

#include <QJsonObject>

#include <array>
#include <memory>
#include <vector>

namespace hotas {

constexpr int kResponseCurveLutSamples = 4097;

struct CurvePresetInfo {
    QString id;
    QString name;
    float strength = 0.0F;
};

struct AdvancedCurvePresetInfo {
    QString id;
    QString name;
    QString category;
    QString bestFor;
    QString behavior;
    QString provenance;
    std::vector<CurvePoint> centeredPoints;
};

struct CurveAnalysis {
    bool valid = true;
    bool monotonic = true;
    bool continuous = true;
    bool fullAuthority = true;
    bool noOvershoot = true;
    float centerGain = 1.0F;
    float quarterGain = 1.0F;
    float halfGain = 1.0F;
    float threeQuarterGain = 1.0F;
    float peakGain = 1.0F;
    float largestGainTransition = 0.0F;
    float neutralOffset = 0.0F;
    bool neutralMapsToNeutral = true;
};

// The immutable form consumed by the mapping worker. It is intentionally a
// fixed-size cache so lookup has no allocation, point walk, or Qt access.
struct CompiledResponseCurve {
    bool unipolar = false;
    std::array<float, kResponseCurveLutSamples> samples{};
};

const std::vector<CurvePresetInfo> &standardCurvePresets();
const std::vector<AdvancedCurvePresetInfo> &advancedCurvePresets();
const AdvancedCurvePresetInfo *advancedCurvePreset(const QString &id);
bool supportedCurvePointDensity(int density);

QString curveFamilyLabel(CurveFamily family);
QString curveInterpolationLabel(CurveInterpolation interpolation);
QString curveDefinitionSummary(const CurveDefinition &definition);
QString curveDefinitionSourceSummary(const CurveDefinition &definition);

CurveDefinition linearCurveDefinition();
CurveDefinition standardCurveDefinition(CurveFamily family, const QString &presetId);
CurveDefinition standardCurveDefinition(CurveFamily family, float strength);
CurveDefinition advancedCurveDefinition(const QString &presetId);

// Clamp and repair a persisted curve. It is safe to call after deserialization
// as well as after every point operation.
void normalizeCurveDefinition(CurveDefinition &definition, bool unipolar);
bool curveDefinitionIsValid(const CurveDefinition &definition, bool unipolar);
CurveDefinition materializeCurveDefinition(const CurveDefinition &definition, bool unipolar,
                                           int density = 0);
// Converts explicit points by preserving the meaningful half of the source
// response. Centered -> one-sided starts at the former 0 origin; no -1..1
// curve is compressed into a fake 0..1 center. Generated families remain
// parameterized and are evaluated in their selected domain.
CurveDefinition convertCurveDefinitionDomain(const CurveDefinition &definition,
                                             bool sourceUnipolar, bool targetUnipolar);
bool updateCurvePoint(CurveDefinition &definition, bool unipolar, int index,
                      float input, float output);
bool setCurvePointLocked(CurveDefinition &definition, bool unipolar, int index, bool locked);
bool addCurvePoint(CurveDefinition &definition, bool unipolar, float input, float output,
                   int *selectedIndex = nullptr);
bool removeCurvePoint(CurveDefinition &definition, bool unipolar, int index);
CurveDefinition resampleCurveDefinition(const CurveDefinition &definition, bool unipolar,
                                        int density);

// Input/output for this function use the curve's display domain: -1…+1 for a
// centered control, 0…1 for a unipolar control.
float evaluateCurveDefinition(float domainInput, const CurveDefinition &definition,
                              bool unipolar);
float evaluateCurveGain(float domainInput, const CurveDefinition &definition, bool unipolar);
CurveAnalysis analyzeCurveDefinition(const CurveDefinition &definition, bool unipolar);
std::shared_ptr<const CompiledResponseCurve> compileResponseCurve(
    const CurveDefinition &definition, bool unipolar);
// Test/benchmark instrumentation. Compilation never occurs in the report loop.
std::uint64_t responseCurveCompileCount();
float evaluateCompiledResponseCurve(float normalizedInput,
                                    const std::shared_ptr<const CompiledResponseCurve> &curve);

QJsonObject curveDefinitionToJson(const CurveDefinition &definition);
CurveDefinition curveDefinitionFromJson(const QJsonObject &json, bool unipolar);
QJsonObject personalCurvePresetToJson(const PersonalCurvePreset &preset);
bool personalCurvePresetFromJson(const QJsonObject &json, PersonalCurvePreset *preset);
bool personalCurvePresetNameAvailable(const std::vector<PersonalCurvePreset> &presets,
                                      const QString &name, const QString &exceptId = {});

} // namespace hotas
