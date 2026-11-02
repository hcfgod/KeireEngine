#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Math/Vector3d.h"

#include <cstdint>

namespace Keire
{
    enum class PostProcessVolumeShape : std::uint8_t
    {
        Box,
        Sphere
    };

    struct PostProcessVolumeSettings
    {
        AssetId Profile;
        bool Global = true;
        PostProcessVolumeShape Shape = PostProcessVolumeShape::Box;
        Vector3d HalfExtents{5.0, 5.0, 5.0};
        double Radius = 5.0;
        double BlendDistance = 1.0;
        double Weight = 1.0;
        std::int32_t Priority = 0;

        [[nodiscard]] bool operator==(const PostProcessVolumeSettings&) const noexcept = default;
    };

    /// Authored volume geometry is local to its Transform. Profiles contain effect settings.
    class KEIRE_API PostProcessVolumeComponent final : public Component
    {
      public:
        PostProcessVolumeComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245505056ULL, 0x4f4c554d45000001ULL));
        }

        [[nodiscard]] const PostProcessVolumeSettings& Settings() const noexcept { return m_Settings; }

        /// Validates the complete replacement before changing authored state or notifying observers.
        void Configure(const PostProcessVolumeSettings& settings);
        void Reset();
        static void Validate(const PostProcessVolumeSettings& settings);

      private:
        PostProcessVolumeSettings m_Settings;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreatePostProcessVolumeComponentRegistration();
} // namespace Keire
