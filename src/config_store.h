#pragma once

#include "mapping_types.h"

#include <QJsonObject>

namespace hotas {

class ConfigStore final {
public:
    static MapperConfiguration load();
    static bool save(const MapperConfiguration &configuration);

    static QJsonObject toJson(const MapperConfiguration &configuration);
    static MapperConfiguration fromJson(const QJsonObject &json, bool *valid = nullptr);
};

} // namespace hotas
