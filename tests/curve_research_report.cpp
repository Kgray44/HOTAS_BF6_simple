#include "response_curve.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string pointsText(const std::vector<hotas::CurvePoint> &points)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(3);
    for (size_t index = 0; index < points.size(); ++index) {
        if (index != 0) output << ' ';
        output << '(' << points[index].input << ',' << points[index].output << ')';
    }
    return output.str();
}

void printMetric(float value)
{
    std::cout << std::fixed << std::setprecision(3) << value;
}

} // namespace

int main()
{
    // This report deliberately calls the same evaluator and health analysis
    // used by the Curve Studio. It keeps the documentation's numeric record
    // derived from the actual production curve definitions rather than from
    // hand-maintained presentation values.
    std::cout << "# Generated v1.4 Advanced Curve Metrics\n\n";
    std::cout << "All values use the production centered-domain evaluator at 100% Response Strength. "
                 "Control points are the normalized source definition; gains are local derivatives.\n\n";
    std::cout << "| Preset | Category | Normalized control points | Center output | Center gain | 25% gain | "
                 "50% gain | 75% gain | Peak gain |\n";
    std::cout << "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n";

    for (const hotas::AdvancedCurvePresetInfo &preset : hotas::advancedCurvePresets()) {
        const hotas::CurveDefinition definition = hotas::advancedCurveDefinition(preset.id);
        const hotas::CurveAnalysis analysis = hotas::analyzeCurveDefinition(definition, false);
        std::cout << "| " << preset.name.toStdString()
                  << " | " << preset.category.toStdString()
                  << " | `" << pointsText(preset.centeredPoints) << "` | ";
        printMetric(hotas::evaluateCurveDefinition(0.0F, definition, false));
        std::cout << " | ";
        printMetric(analysis.centerGain);
        std::cout << " | ";
        printMetric(analysis.quarterGain);
        std::cout << " | ";
        printMetric(analysis.halfGain);
        std::cout << " | ";
        printMetric(analysis.threeQuarterGain);
        std::cout << " | ";
        printMetric(analysis.peakGain);
        std::cout << " |\n";
    }
}
