#pragma once

#include "Keire/Authoring/GraphAuthoring.h"

#include <nlohmann/json_fwd.hpp>

namespace Keire::Detail
{
    [[nodiscard]] nlohmann::json EncodeGraphAuthoringMetadata(const GraphAuthoringMetadata& metadata);
    [[nodiscard]] GraphAuthoringMetadata DecodeGraphAuthoringMetadata(const nlohmann::json& source,
                                                                      std::span<const AssetId> validNodes);
} // namespace Keire::Detail
