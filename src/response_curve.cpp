#include "response_curve.h"

#include <QJsonArray>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hotas {
namespace {

using namespace Qt::StringLiterals;

constexpr std::array<int, 6> kPointDensities{5, 7, 9, 13, 17, 25};
constexpr float kPointSpacing = 0.002F;

float clampUnit(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

float domainMinimum(bool unipolar)
{
    return unipolar ? 0.0F : -1.0F;
}

std::vector<CurvePoint> symmetricPoints(
    std::initializer_list<std::pair<float, float>> positive)
{
    std::vector<CurvePoint> points;
    for (auto iterator = positive.end(); iterator != positive.begin();) {
        --iterator;
        if (iterator->first <= 0.0F) continue;
        points.push_back({-iterator->first, -iterator->second, false});
    }
    for (const auto &[input, output] : positive) points.push_back({input, output, false});
    return points;
}

const std::vector<CurvePresetInfo> kStandardPresets{
    {u"very-light"_qs, u"Very Light"_qs, 0.12F},
    {u"light"_qs, u"Light"_qs, 0.22F},
    {u"medium-light"_qs, u"Medium-Light"_qs, 0.34F},
    {u"medium"_qs, u"Medium"_qs, 0.48F},
    {u"medium-strong"_qs, u"Medium-Strong"_qs, 0.64F},
    {u"strong"_qs, u"Strong"_qs, 0.78F},
    {u"very-strong"_qs, u"Very Strong"_qs, 0.90F},
    {u"maximum"_qs, u"Maximum"_qs, 1.00F},
};

const std::vector<AdvancedCurvePresetInfo> kAdvancedPresets{
    {u"precision-tracking"_qs, u"Precision Tracking"_qs,
     u"Fine tracking of small moving targets"_qs,
     u"Soft center, measured midrange, full edge authority"_qs,
     u"Derived: cubic aim-response practice and HOTAS precision use"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.20F, 0.055F}, {0.50F, 0.26F},
                      {0.80F, 0.68F}, {1.0F, 1.0F}})},
    {u"fine-gun-aim"_qs, u"Fine Gun Aim"_qs,
     u"Small corrections while tracking a firing solution"_qs,
     u"Very soft center with a controlled upper ramp"_qs,
     u"Derived: FPS aim precision and Fitts-law-oriented fine motion"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.15F, 0.025F}, {0.35F, 0.12F},
                      {0.65F, 0.48F}, {0.85F, 0.78F}, {1.0F, 1.0F}})},
    {u"stable-strafe"_qs, u"Stable Strafe"_qs,
     u"Deliberate lateral corrections and formation work"_qs,
     u"Broad calm center, then a steady progressive climb"_qs,
     u"Derived: controller deadzone normalization and steady-state control"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.28F, 0.10F}, {0.52F, 0.31F},
                      {0.76F, 0.64F}, {0.93F, 0.90F}, {1.0F, 1.0F}})},
    {u"high-rate-maneuver"_qs, u"High-Rate Maneuver"_qs,
     u"Rapid turn-rate changes without losing full command"_qs,
     u"Near-linear center, assertive middle and edge"_qs,
     u"Derived: flight-control progressive authority conventions"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.20F, 0.18F}, {0.50F, 0.56F},
                      {0.80F, 0.90F}, {1.0F, 1.0F}})},
    {u"hover-control"_qs, u"Hover Control"_qs,
     u"Helicopter hover and minute attitude corrections"_qs,
     u"Large fine-control region and gentle edge recovery"_qs,
     u"Derived: simulator control-curve practice; evidence-informed"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.30F, 0.08F}, {0.65F, 0.45F},
                      {0.90F, 0.84F}, {1.0F, 1.0F}})},
    {u"fast-acquisition"_qs, u"Fast Acquisition"_qs,
     u"Quickly acquiring a new target or heading"_qs,
     u"Responsive center followed by an aggressive midrange"_qs,
     u"Derived: aim-acquisition gain trade-off; evidence-informed"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.10F, 0.08F}, {0.30F, 0.32F},
                      {0.55F, 0.66F}, {0.80F, 0.90F}, {1.0F, 1.0F}})},
    {u"center-stabilizer"_qs, u"Center Stabilizer"_qs,
     u"Reducing over-correction around a neutral command"_qs,
     u"Central plateau-like region with continuous authority recovery"_qs,
     u"Derived: deadzone/hysteresis stability principles without a hard step"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.25F, 0.06F}, {0.50F, 0.30F},
                      {0.75F, 0.65F}, {0.90F, 0.86F}, {1.0F, 1.0F}})},
    {u"progressive-authority"_qs, u"Progressive Authority"_qs,
     u"Measured transitions into strong end-of-travel command"_qs,
     u"Soft start, deliberately progressive edge authority"_qs,
     u"Derived: HOTAS response-curve convention; evidence-informed"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.20F, 0.13F}, {0.50F, 0.39F},
                      {0.75F, 0.70F}, {0.90F, 0.90F}, {1.0F, 1.0F}})},
    {u"hybrid-precision"_qs, u"Hybrid Precision"_qs,
     u"Precision approach followed by agile correction"_qs,
     u"Soft central window with a brisk but continuous upper ramp"_qs,
     u"Derived: blended pointing-gain and flight-control response"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.20F, 0.075F}, {0.50F, 0.42F},
                      {0.70F, 0.72F}, {0.90F, 0.95F}, {1.0F, 1.0F}})},
    {u"edge-softened"_qs, u"Edge Softened"_qs,
     u"Avoiding abrupt maximum-rate command near full travel"_qs,
     u"Responsive middle that eases into the final authority"_qs,
     u"Derived: bounded response-gain control; evidence-informed"_qs,
     symmetricPoints({{0.0F, 0.0F}, {0.20F, 0.22F}, {0.50F, 0.61F},
                      {0.75F, 0.83F}, {0.90F, 0.94F}, {1.0F, 1.0F}})},
};

const CurvePresetInfo *standardPreset(const QString &id)
{
    const auto found = std::find_if(kStandardPresets.cbegin(), kStandardPresets.cend(),
        [&id](const CurvePresetInfo &preset) { return preset.id == id; });
    return found == kStandardPresets.cend() ? nullptr : &*found;
}

float signedPower(float value, float exponent)
{
    return std::copysign(std::pow(std::abs(value), exponent), value);
}

float smoothStep(float value)
{
    return value * value * (3.0F - 2.0F * value);
}

float evaluateGenerated(float domainInput, const CurveDefinition &definition, bool unipolar)
{
    const float minimum = domainMinimum(unipolar);
    const float input = std::clamp(domainInput, minimum, 1.0F);
    if (definition.family == CurveFamily::Linear) return input;

    const float strength = std::clamp(definition.strength, 0.0F, 1.0F);
    if (definition.family == CurveFamily::JCurve) {
        const float exponent = 1.0F + 1.80F * strength;
        return unipolar ? std::pow(input, exponent) : signedPower(input, exponent);
    }
    if (definition.family == CurveFamily::SCurve) {
        if (unipolar) return (1.0F - strength) * input + strength * smoothStep(input);
        const float magnitude = std::abs(input);
        return std::copysign((1.0F - strength) * magnitude + strength * smoothStep(magnitude), input);
    }
    return input;
}

float evaluateMonotonePoints(float input, const std::vector<CurvePoint> &points,
                             CurveInterpolation interpolation)
{
    if (points.empty()) return input;
    if (input <= points.front().input) return points.front().output;
    if (input >= points.back().input) return points.back().output;
    const auto upper = std::upper_bound(points.cbegin(), points.cend(), input,
        [](float value, const CurvePoint &point) { return value < point.input; });
    const size_t right = static_cast<size_t>(std::distance(points.cbegin(), upper));
    const size_t left = right - 1;
    const CurvePoint &a = points[left];
    const CurvePoint &b = points[right];
    const float h = b.input - a.input;
    const float t = std::clamp((input - a.input) / h, 0.0F, 1.0F);
    if (interpolation == CurveInterpolation::Linear || points.size() < 3) {
        return a.output + (b.output - a.output) * t;
    }

    const size_t count = points.size();
    std::vector<float> secants(count - 1);
    for (size_t index = 0; index + 1 < count; ++index) {
        secants[index] = (points[index + 1].output - points[index].output)
            / (points[index + 1].input - points[index].input);
    }
    std::vector<float> slopes(count);
    const auto endpointSlope = [](float h0, float h1, float d0, float d1) {
        float slope = ((2.0F * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (slope * d0 <= 0.0F) return 0.0F;
        if (d0 * d1 < 0.0F && std::abs(slope) > std::abs(3.0F * d0)) return 3.0F * d0;
        return slope;
    };
    const float firstH = points[1].input - points[0].input;
    const float secondH = points[2].input - points[1].input;
    slopes.front() = endpointSlope(firstH, secondH, secants[0], secants[1]);
    for (size_t index = 1; index + 1 < count; ++index) {
        const float previous = secants[index - 1];
        const float next = secants[index];
        if (previous <= 0.0F || next <= 0.0F) {
            slopes[index] = 0.0F;
            continue;
        }
        const float previousH = points[index].input - points[index - 1].input;
        const float nextH = points[index + 1].input - points[index].input;
        const float w1 = 2.0F * nextH + previousH;
        const float w2 = nextH + 2.0F * previousH;
        slopes[index] = (w1 + w2) / (w1 / previous + w2 / next);
    }
    const float lastH = points[count - 1].input - points[count - 2].input;
    const float penultimateH = points[count - 2].input - points[count - 3].input;
    slopes.back() = endpointSlope(lastH, penultimateH, secants.back(), secants[count - 3]);

    const float t2 = t * t;
    const float t3 = t2 * t;
    const float result = (2.0F * t3 - 3.0F * t2 + 1.0F) * a.output
        + (t3 - 2.0F * t2 + t) * h * slopes[left]
        + (-2.0F * t3 + 3.0F * t2) * b.output
        + (t3 - t2) * h * slopes[right];
    return std::clamp(result, a.output, b.output);
}

std::vector<CurvePoint> identityPoints(bool unipolar, int density)
{
    const float minimum = domainMinimum(unipolar);
    std::vector<CurvePoint> points;
    points.reserve(static_cast<size_t>(density));
    for (int index = 0; index < density; ++index) {
        const float value = minimum + (1.0F - minimum) * static_cast<float>(index)
            / static_cast<float>(density - 1);
        points.push_back({value, value, index == 0 || index == density - 1});
    }
    return points;
}

void normalizePoints(CurveDefinition &definition, bool unipolar)
{
    const int density = supportedCurvePointDensity(definition.pointDensity)
        ? definition.pointDensity : 9;
    definition.pointDensity = density;
    const float minimum = domainMinimum(unipolar);
    if (definition.points.size() < 3 || definition.points.size() > 25
        || (definition.symmetry && !unipolar && definition.points.size() % 2 == 0)) {
        definition.points = identityPoints(unipolar, density);
    }
    const int pointCount = static_cast<int>(definition.points.size());
    std::sort(definition.points.begin(), definition.points.end(),
              [](const CurvePoint &left, const CurvePoint &right) { return left.input < right.input; });
    if (definition.symmetry && !unipolar) {
        const int middle = static_cast<int>(definition.points.size()) / 2;
        definition.points[middle] = {0.0F, 0.0F, true};
        float lastInput = 0.0F;
        float lastOutput = 0.0F;
        for (int offset = 1; offset <= middle; ++offset) {
            const int positive = middle + offset;
            const float maximum = offset == middle ? 1.0F : 1.0F - (middle - offset) * kPointSpacing;
            const float input = std::clamp(std::abs(definition.points[positive].input),
                                           lastInput + kPointSpacing, maximum);
            const float output = std::clamp(std::abs(definition.points[positive].output),
                                            lastOutput, 1.0F);
            const bool locked = definition.points[positive].locked
                || definition.points[middle - offset].locked;
            definition.points[positive] = {input, output, locked};
            definition.points[middle - offset] = {-input, -output, locked};
            lastInput = input;
            lastOutput = output;
        }
        definition.points.front() = {-1.0F, -1.0F, true};
        definition.points.back() = {1.0F, 1.0F, true};
        return;
    }

    definition.points.front() = {minimum, minimum, true};
    definition.points.back() = {1.0F, 1.0F, true};
    float previousInput = minimum;
    float previousOutput = minimum;
    for (int index = 1; index + 1 < pointCount; ++index) {
        const float maximumInput = 1.0F - (pointCount - 1 - index) * kPointSpacing;
        CurvePoint &point = definition.points[static_cast<size_t>(index)];
        point.input = std::clamp(point.input, previousInput + kPointSpacing, maximumInput);
        point.output = std::clamp(point.output, previousOutput, 1.0F);
        previousInput = point.input;
        previousOutput = point.output;
    }
}

CurveFamily curveFamilyFromString(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == u"j"_qs) return CurveFamily::JCurve;
    if (normalized == u"s"_qs) return CurveFamily::SCurve;
    if (normalized == u"advanced"_qs) return CurveFamily::Advanced;
    if (normalized == u"personal"_qs) return CurveFamily::Personal;
    if (normalized == u"custom"_qs) return CurveFamily::Custom;
    return CurveFamily::Linear;
}

QString curveFamilyKey(CurveFamily family)
{
    switch (family) {
    case CurveFamily::JCurve: return u"j"_qs;
    case CurveFamily::SCurve: return u"s"_qs;
    case CurveFamily::Advanced: return u"advanced"_qs;
    case CurveFamily::Personal: return u"personal"_qs;
    case CurveFamily::Custom: return u"custom"_qs;
    case CurveFamily::Linear: return u"linear"_qs;
    }
    return u"linear"_qs;
}

} // namespace

const std::vector<CurvePresetInfo> &standardCurvePresets()
{
    return kStandardPresets;
}

const std::vector<AdvancedCurvePresetInfo> &advancedCurvePresets()
{
    return kAdvancedPresets;
}

const AdvancedCurvePresetInfo *advancedCurvePreset(const QString &id)
{
    const auto found = std::find_if(kAdvancedPresets.cbegin(), kAdvancedPresets.cend(),
        [&id](const AdvancedCurvePresetInfo &preset) { return preset.id == id; });
    return found == kAdvancedPresets.cend() ? nullptr : &*found;
}

bool supportedCurvePointDensity(int density)
{
    return std::find(kPointDensities.cbegin(), kPointDensities.cend(), density) != kPointDensities.cend();
}

QString curveFamilyLabel(CurveFamily family)
{
    switch (family) {
    case CurveFamily::JCurve: return u"J-Curve"_qs;
    case CurveFamily::SCurve: return u"S-Curve"_qs;
    case CurveFamily::Advanced: return u"Advanced"_qs;
    case CurveFamily::Personal: return u"Personal"_qs;
    case CurveFamily::Custom: return u"Custom"_qs;
    case CurveFamily::Linear: return u"Linear"_qs;
    }
    return u"Linear"_qs;
}

QString curveInterpolationLabel(CurveInterpolation interpolation)
{
    return interpolation == CurveInterpolation::Linear ? u"Linear"_qs : u"Smooth"_qs;
}

QString curveDefinitionSummary(const CurveDefinition &definition)
{
    if (definition.family == CurveFamily::Custom) {
        return definition.baseLabel.isEmpty() ? u"Custom"_qs
            : u"Custom · Based on "_qs + definition.baseLabel;
    }
    if (definition.family == CurveFamily::Linear) return u"Linear"_qs;
    if (!definition.presetId.isEmpty()) return curveFamilyLabel(definition.family)
        + u" · "_qs + definition.baseLabel;
    return curveFamilyLabel(definition.family);
}

QString curveDefinitionSourceSummary(const CurveDefinition &definition)
{
    return definition.family == CurveFamily::Custom ? definition.baseLabel : curveDefinitionSummary(definition);
}

CurveDefinition linearCurveDefinition()
{
    return {};
}

CurveDefinition standardCurveDefinition(CurveFamily family, const QString &presetId)
{
    if (family != CurveFamily::JCurve && family != CurveFamily::SCurve) return linearCurveDefinition();
    const CurvePresetInfo *preset = standardPreset(presetId);
    if (!preset) preset = standardPreset(u"medium"_qs);
    CurveDefinition definition;
    definition.family = family;
    definition.sourceFamily = family;
    definition.strength = preset->strength;
    definition.presetId = preset->id;
    definition.baseLabel = preset->name;
    definition.sourcePresetId = preset->id;
    return definition;
}

CurveDefinition advancedCurveDefinition(const QString &presetId)
{
    const AdvancedCurvePresetInfo *preset = advancedCurvePreset(presetId);
    if (!preset) preset = &kAdvancedPresets.front();
    CurveDefinition definition;
    definition.family = CurveFamily::Advanced;
    definition.sourceFamily = CurveFamily::Advanced;
    definition.presetId = preset->id;
    definition.baseLabel = preset->name;
    definition.sourcePresetId = preset->id;
    return definition;
}

void normalizeCurveDefinition(CurveDefinition &definition, bool unipolar)
{
    if (static_cast<int>(definition.family) < static_cast<int>(CurveFamily::Linear)
        || static_cast<int>(definition.family) > static_cast<int>(CurveFamily::Custom)) {
        definition = linearCurveDefinition();
    }
    if (static_cast<int>(definition.sourceFamily) < static_cast<int>(CurveFamily::Linear)
        || static_cast<int>(definition.sourceFamily) > static_cast<int>(CurveFamily::Custom)) {
        definition.sourceFamily = definition.family;
    }
    definition.strength = std::clamp(definition.strength, 0.0F, 1.0F);
    if (!supportedCurvePointDensity(definition.pointDensity)) definition.pointDensity = 9;
    if (unipolar) definition.symmetry = false;
    if ((definition.family == CurveFamily::JCurve || definition.family == CurveFamily::SCurve)
        && !standardPreset(definition.presetId)) {
        definition = standardCurveDefinition(definition.family, u"medium"_qs);
    }
    if (definition.family == CurveFamily::Advanced && !advancedCurvePreset(definition.presetId)) {
        definition = advancedCurveDefinition(kAdvancedPresets.front().id);
    }
    if (definition.baseLabel.trimmed().isEmpty()) definition.baseLabel = curveFamilyLabel(definition.family);
    if (definition.pointEditing || definition.family == CurveFamily::Custom
        || (definition.family == CurveFamily::Personal && !definition.points.empty())) {
        normalizePoints(definition, unipolar);
    }
    if (!definition.pointEditing && definition.family != CurveFamily::Custom
        && definition.family != CurveFamily::Personal) definition.points.clear();
}

bool curveDefinitionIsValid(const CurveDefinition &definition, bool unipolar)
{
    const float minimum = domainMinimum(unipolar);
    if (definition.pointEditing || definition.family == CurveFamily::Custom
        || (definition.family == CurveFamily::Personal && !definition.points.empty())) {
        if (!supportedCurvePointDensity(definition.pointDensity)
            || definition.points.size() < 3 || definition.points.size() > 25
            || (definition.symmetry && !unipolar && definition.points.size() % 2 == 0)) return false;
        if (std::abs(definition.points.front().input - minimum) > 0.0001F
            || std::abs(definition.points.front().output - minimum) > 0.0001F
            || std::abs(definition.points.back().input - 1.0F) > 0.0001F
            || std::abs(definition.points.back().output - 1.0F) > 0.0001F) return false;
        for (size_t index = 1; index < definition.points.size(); ++index) {
            if (definition.points[index].input <= definition.points[index - 1].input
                || definition.points[index].output < definition.points[index - 1].output) return false;
        }
    }
    return true;
}

float evaluateCurveDefinition(float domainInput, const CurveDefinition &definition, bool unipolar)
{
    const float input = std::clamp(domainInput, domainMinimum(unipolar), 1.0F);
    if (definition.family == CurveFamily::Advanced) {
        const AdvancedCurvePresetInfo *preset = advancedCurvePreset(definition.presetId);
        if (!preset) return input;
        if (!unipolar) return evaluateMonotonePoints(input, preset->centeredPoints, CurveInterpolation::Smooth);
        const float centeredInput = input * 2.0F - 1.0F;
        return (evaluateMonotonePoints(centeredInput, preset->centeredPoints,
                                       CurveInterpolation::Smooth) + 1.0F) * 0.5F;
    }
    if (definition.pointEditing || definition.family == CurveFamily::Custom
        || definition.family == CurveFamily::Personal) {
        return evaluateMonotonePoints(input, definition.points, definition.interpolation);
    }
    return evaluateGenerated(input, definition, unipolar);
}

CurveDefinition materializeCurveDefinition(const CurveDefinition &definition, bool unipolar, int density)
{
    const int requestedDensity = supportedCurvePointDensity(density) ? density
        : (supportedCurvePointDensity(definition.pointDensity) ? definition.pointDensity : 9);
    CurveDefinition editable = definition;
    editable.baseLabel = curveDefinitionSourceSummary(definition);
    editable.sourcePresetId = definition.presetId;
    editable.sourceFamily = definition.family == CurveFamily::Custom
        ? definition.sourceFamily : definition.family;
    editable.family = CurveFamily::Custom;
    editable.presetId = u"custom"_qs;
    editable.pointEditing = true;
    editable.symmetry = !unipolar && definition.symmetry;
    editable.pointDensity = requestedDensity;
    const float minimum = domainMinimum(unipolar);
    editable.points.clear();
    editable.points.reserve(static_cast<size_t>(requestedDensity));
    for (int index = 0; index < requestedDensity; ++index) {
        const float input = minimum + (1.0F - minimum) * static_cast<float>(index)
            / static_cast<float>(requestedDensity - 1);
        editable.points.push_back({input, evaluateCurveDefinition(input, definition, unipolar), false});
    }
    normalizeCurveDefinition(editable, unipolar);
    return editable;
}

bool updateCurvePoint(CurveDefinition &definition, bool unipolar, int index,
                      float input, float output)
{
    if (!definition.pointEditing || index <= 0
        || index >= static_cast<int>(definition.points.size()) - 1) return false;
    if (definition.points[static_cast<size_t>(index)].locked) return false;
    if (definition.symmetry && !unipolar) {
        const int middle = static_cast<int>(definition.points.size()) / 2;
        if (index == middle) return false;
        const int positive = index > middle ? index : static_cast<int>(definition.points.size()) - 1 - index;
        const int mirror = static_cast<int>(definition.points.size()) - 1 - positive;
        if (definition.points[positive].locked || definition.points[mirror].locked) return false;
        const float lower = definition.points[positive - 1].input + kPointSpacing;
        const float upper = definition.points[positive + 1].input - kPointSpacing;
        const float x = std::clamp(std::abs(input), lower, upper);
        const float y = std::clamp(std::abs(output), definition.points[positive - 1].output,
                                   definition.points[positive + 1].output);
        const bool locked = definition.points[positive].locked;
        definition.points[positive] = {x, y, locked};
        definition.points[mirror] = {-x, -y, locked};
    } else {
        CurvePoint &point = definition.points[static_cast<size_t>(index)];
        if (point.locked) return false;
        point.input = std::clamp(input, definition.points[index - 1].input + kPointSpacing,
                                 definition.points[index + 1].input - kPointSpacing);
        point.output = std::clamp(output, definition.points[index - 1].output,
                                  definition.points[index + 1].output);
    }
    definition.family = CurveFamily::Custom;
    definition.presetId = u"custom"_qs;
    normalizeCurveDefinition(definition, unipolar);
    return true;
}

bool setCurvePointLocked(CurveDefinition &definition, bool unipolar, int index, bool locked)
{
    if (!definition.pointEditing || index < 0
        || index >= static_cast<int>(definition.points.size())) return false;
    const int last = static_cast<int>(definition.points.size()) - 1;
    if (index == 0 || index == last) return false;
    if (definition.symmetry && !unipolar) {
        const int middle = static_cast<int>(definition.points.size()) / 2;
        if (index == middle) return false;
        const int mirror = last - index;
        definition.points[index].locked = locked;
        definition.points[mirror].locked = locked;
    } else {
        definition.points[index].locked = locked;
    }
    normalizeCurveDefinition(definition, unipolar);
    return true;
}

bool addCurvePoint(CurveDefinition &definition, bool unipolar, float input, float output,
                   int *selectedIndex)
{
    if (!definition.pointEditing || definition.points.size() >= 25) return false;
    const float minimum = domainMinimum(unipolar);
    if (definition.symmetry && !unipolar) {
        if (definition.points.size() > 23) return false;
        const float x = std::clamp(std::abs(input), kPointSpacing, 1.0F - kPointSpacing);
        const float y = std::clamp(std::abs(output), 0.0F, 1.0F);
        for (const CurvePoint &point : definition.points) {
            if (std::abs(std::abs(point.input) - x) < kPointSpacing) return false;
        }
        definition.points.push_back({x, y, false});
        definition.points.push_back({-x, -y, false});
        normalizeCurveDefinition(definition, unipolar);
        const auto found = std::find_if(definition.points.cbegin(), definition.points.cend(),
            [x](const CurvePoint &point) { return std::abs(point.input - x) < kPointSpacing * 2.0F; });
        if (selectedIndex && found != definition.points.cend()) {
            *selectedIndex = static_cast<int>(std::distance(definition.points.cbegin(), found));
        }
    } else {
        const float x = std::clamp(input, minimum + kPointSpacing, 1.0F - kPointSpacing);
        for (const CurvePoint &point : definition.points) {
            if (std::abs(point.input - x) < kPointSpacing) return false;
        }
        definition.points.push_back({x, std::clamp(output, minimum, 1.0F), false});
        normalizeCurveDefinition(definition, unipolar);
        const auto found = std::find_if(definition.points.cbegin(), definition.points.cend(),
            [x](const CurvePoint &point) { return std::abs(point.input - x) < kPointSpacing * 2.0F; });
        if (selectedIndex && found != definition.points.cend()) {
            *selectedIndex = static_cast<int>(std::distance(definition.points.cbegin(), found));
        }
    }
    definition.family = CurveFamily::Custom;
    definition.presetId = u"custom"_qs;
    return true;
}

bool removeCurvePoint(CurveDefinition &definition, bool unipolar, int index)
{
    if (!definition.pointEditing || index <= 0
        || index >= static_cast<int>(definition.points.size()) - 1
        || definition.points[static_cast<size_t>(index)].locked) return false;
    if (definition.symmetry && !unipolar) {
        if (definition.points.size() <= 5) return false;
        const int last = static_cast<int>(definition.points.size()) - 1;
        const int middle = last / 2;
        if (index == middle) return false;
        const int positive = index > middle ? index : last - index;
        const int mirror = last - positive;
        if (definition.points[static_cast<size_t>(positive)].locked
            || definition.points[static_cast<size_t>(mirror)].locked) return false;
        definition.points.erase(definition.points.begin() + positive);
        definition.points.erase(definition.points.begin() + mirror);
    } else {
        if (definition.points.size() <= 3) return false;
        definition.points.erase(definition.points.begin() + index);
    }
    normalizeCurveDefinition(definition, unipolar);
    definition.family = CurveFamily::Custom;
    definition.presetId = u"custom"_qs;
    return true;
}

CurveDefinition resampleCurveDefinition(const CurveDefinition &definition, bool unipolar, int density)
{
    return materializeCurveDefinition(definition, unipolar, density);
}

float evaluateCurveGain(float domainInput, const CurveDefinition &definition, bool unipolar)
{
    const float input = std::clamp(domainInput, domainMinimum(unipolar), 1.0F);
    const float strength = std::clamp(definition.strength, 0.0F, 1.0F);
    if (definition.family == CurveFamily::Linear) return 1.0F;
    if (definition.family == CurveFamily::JCurve) {
        const float exponent = 1.0F + 1.80F * strength;
        const float magnitude = std::abs(input);
        if (magnitude < 0.000001F && exponent > 1.0F) return 0.0F;
        return exponent * std::pow(magnitude, exponent - 1.0F);
    }
    if (definition.family == CurveFamily::SCurve) {
        const float magnitude = unipolar ? input : std::abs(input);
        return (1.0F - strength) + strength * 6.0F * magnitude * (1.0F - magnitude);
    }
    // Point and Advanced curves use the same authoritative evaluator. A
    // centered finite difference is stable and avoids deriving separate UI
    // math for every PCHIP branch.
    const float h = 0.0001F;
    const float minimum = domainMinimum(unipolar);
    const float left = std::max(minimum, input - h);
    const float right = std::min(1.0F, input + h);
    if (right <= left) return 0.0F;
    const float gain = (evaluateCurveDefinition(right, definition, unipolar)
        - evaluateCurveDefinition(left, definition, unipolar)) / (right - left);
    return std::isfinite(gain) ? std::max(0.0F, gain) : 0.0F;
}

CurveAnalysis analyzeCurveDefinition(const CurveDefinition &definition, bool unipolar)
{
    CurveAnalysis analysis;
    analysis.peakGain = 0.0F;
    analysis.valid = curveDefinitionIsValid(definition, unipolar);
    analysis.monotonic = analysis.valid;
    analysis.continuous = analysis.valid;
    analysis.noOvershoot = analysis.valid;
    const float minimum = domainMinimum(unipolar);
    const float start = evaluateCurveDefinition(minimum, definition, unipolar);
    const float end = evaluateCurveDefinition(1.0F, definition, unipolar);
    analysis.fullAuthority = std::abs(start - minimum) < 0.002F && std::abs(end - 1.0F) < 0.002F;
    const float center = unipolar ? 0.0F : 0.0F;
    analysis.centerGain = evaluateCurveGain(center, definition, unipolar);
    analysis.quarterGain = evaluateCurveGain(unipolar ? 0.25F : 0.25F, definition, unipolar);
    analysis.halfGain = evaluateCurveGain(unipolar ? 0.50F : 0.50F, definition, unipolar);
    analysis.threeQuarterGain = evaluateCurveGain(unipolar ? 0.75F : 0.75F, definition, unipolar);
    float previousGain = 0.0F;
    for (int index = 0; index <= 128; ++index) {
        const float input = minimum + (1.0F - minimum) * static_cast<float>(index) / 128.0F;
        const float gain = evaluateCurveGain(input, definition, unipolar);
        analysis.peakGain = std::max(analysis.peakGain, gain);
        if (index > 0) analysis.largestGainTransition = std::max(analysis.largestGainTransition,
            std::abs(gain - previousGain));
        previousGain = gain;
        const float output = evaluateCurveDefinition(input, definition, unipolar);
        if (!std::isfinite(output) || output < minimum - 0.0001F || output > 1.0001F) {
            analysis.valid = false;
            analysis.noOvershoot = false;
        }
    }
    return analysis;
}

std::shared_ptr<const CompiledResponseCurve> compileResponseCurve(
    const CurveDefinition &definition, bool unipolar)
{
    CurveDefinition safe = definition;
    normalizeCurveDefinition(safe, unipolar);
    auto compiled = std::make_shared<CompiledResponseCurve>();
    compiled->unipolar = unipolar;
    for (int index = 0; index < kResponseCurveLutSamples; ++index) {
        const float normalizedInput = -1.0F + 2.0F * static_cast<float>(index)
            / static_cast<float>(kResponseCurveLutSamples - 1);
        const float domainInput = unipolar ? (normalizedInput + 1.0F) * 0.5F : normalizedInput;
        const float domainOutput = evaluateCurveDefinition(domainInput, safe, unipolar);
        compiled->samples[static_cast<size_t>(index)] = clampUnit(
            unipolar ? domainOutput * 2.0F - 1.0F : domainOutput);
    }
    return compiled;
}

float evaluateCompiledResponseCurve(float normalizedInput,
                                    const std::shared_ptr<const CompiledResponseCurve> &curve)
{
    if (!curve) return clampUnit(normalizedInput);
    const float position = std::clamp((normalizedInput + 1.0F) * 0.5F, 0.0F, 1.0F)
        * static_cast<float>(kResponseCurveLutSamples - 1);
    const int left = static_cast<int>(position);
    const int right = std::min(left + 1, kResponseCurveLutSamples - 1);
    const float fraction = position - static_cast<float>(left);
    return curve->samples[static_cast<size_t>(left)]
        + (curve->samples[static_cast<size_t>(right)] - curve->samples[static_cast<size_t>(left)]) * fraction;
}

QJsonObject curveDefinitionToJson(const CurveDefinition &definition)
{
    QJsonArray points;
    for (const CurvePoint &point : definition.points) {
        points.append(QJsonObject{{u"input"_qs, point.input}, {u"output"_qs, point.output},
                                  {u"locked"_qs, point.locked}});
    }
    return {
        {u"family"_qs, curveFamilyKey(definition.family)},
        {u"sourceFamily"_qs, curveFamilyKey(definition.sourceFamily)},
        {u"strength"_qs, definition.strength},
        {u"presetId"_qs, definition.presetId},
        {u"baseLabel"_qs, definition.baseLabel},
        {u"sourcePresetId"_qs, definition.sourcePresetId},
        {u"pointEditing"_qs, definition.pointEditing},
        {u"symmetry"_qs, definition.symmetry},
        {u"interpolation"_qs, definition.interpolation == CurveInterpolation::Linear ? u"linear"_qs : u"smooth"_qs},
        {u"pointDensity"_qs, definition.pointDensity},
        {u"points"_qs, points},
    };
}

CurveDefinition curveDefinitionFromJson(const QJsonObject &json, bool unipolar)
{
    CurveDefinition definition;
    if (json.isEmpty()) return definition;
    definition.family = curveFamilyFromString(json.value(u"family"_qs).toString());
    definition.sourceFamily = curveFamilyFromString(json.value(u"sourceFamily"_qs).toString());
    if (!json.contains(u"sourceFamily"_qs)) definition.sourceFamily = definition.family;
    definition.strength = static_cast<float>(json.value(u"strength"_qs).toDouble(0.0));
    definition.presetId = json.value(u"presetId"_qs).toString();
    definition.baseLabel = json.value(u"baseLabel"_qs).toString();
    definition.sourcePresetId = json.value(u"sourcePresetId"_qs).toString();
    definition.pointEditing = json.value(u"pointEditing"_qs).toBool(false);
    definition.symmetry = json.value(u"symmetry"_qs).toBool(!unipolar);
    definition.interpolation = json.value(u"interpolation"_qs).toString().compare(
        u"linear"_qs, Qt::CaseInsensitive) == 0 ? CurveInterpolation::Linear : CurveInterpolation::Smooth;
    definition.pointDensity = json.value(u"pointDensity"_qs).toInt(9);
    const QJsonArray points = json.value(u"points"_qs).toArray();
    definition.points.reserve(static_cast<size_t>(points.size()));
    for (const QJsonValue &value : points) {
        const QJsonObject point = value.toObject();
        definition.points.push_back({static_cast<float>(point.value(u"input"_qs).toDouble()),
                                     static_cast<float>(point.value(u"output"_qs).toDouble()),
                                     point.value(u"locked"_qs).toBool(false)});
    }
    normalizeCurveDefinition(definition, unipolar);
    return definition;
}

QJsonObject personalCurvePresetToJson(const PersonalCurvePreset &preset)
{
    return {
        {u"id"_qs, preset.id},
        {u"name"_qs, preset.name},
        {u"description"_qs, preset.description},
        {u"unipolar"_qs, preset.unipolar},
        {u"curve"_qs, curveDefinitionToJson(preset.definition)},
    };
}

bool personalCurvePresetFromJson(const QJsonObject &json, PersonalCurvePreset *preset)
{
    if (!preset) return false;
    PersonalCurvePreset restored;
    restored.id = json.value(u"id"_qs).toString().trimmed();
    restored.name = json.value(u"name"_qs).toString().trimmed();
    restored.description = json.value(u"description"_qs).toString().trimmed().left(160);
    restored.unipolar = json.value(u"unipolar"_qs).toBool(false);
    if (restored.id.isEmpty() || restored.id.size() > 96 || restored.name.isEmpty()
        || restored.name.size() > 48) return false;
    restored.definition = curveDefinitionFromJson(json.value(u"curve"_qs).toObject(), restored.unipolar);
    if (!curveDefinitionIsValid(restored.definition, restored.unipolar)) return false;
    *preset = std::move(restored);
    return true;
}

bool personalCurvePresetNameAvailable(const std::vector<PersonalCurvePreset> &presets,
                                      const QString &name, const QString &exceptId)
{
    const QString candidate = name.trimmed();
    if (candidate.isEmpty() || candidate.size() > 48) return false;
    return std::none_of(presets.cbegin(), presets.cend(), [&](const PersonalCurvePreset &preset) {
        return preset.id != exceptId && preset.name.compare(candidate, Qt::CaseInsensitive) == 0;
    });
}

RuntimeMappingConfiguration compileActiveProfile(const MapperConfiguration &configuration)
{
    const ControllerProfile &profile = activeProfile(configuration);
    RuntimeMappingConfiguration runtime;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        runtime.axes[index].profile = profile.axes[index];
        runtime.axes[index].calibration = configuration.calibration[index];
        runtime.axes[index].responseCurve = compileResponseCurve(profile.axes[index].curve,
            isUnipolarAxis(static_cast<PhysicalAxis>(index)));
    }
    runtime.buttons = profile.buttons;
    return runtime;
}

} // namespace hotas
