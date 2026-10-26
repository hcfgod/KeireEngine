#pragma once

#include <string>

namespace Keire
{
    struct ProjectAuthoringSettings;
    struct RenderEnvironmentSettings;

    namespace Detail
    {
        // Validate and prepare exact persisted bytes without touching the filesystem.
        [[nodiscard]] std::string SerializeProjectAuthoringSettings(const ProjectAuthoringSettings& settings);

        [[nodiscard]] std::string SerializeRenderEnvironmentSettings(const RenderEnvironmentSettings& settings);
    } // namespace Detail
} // namespace Keire
