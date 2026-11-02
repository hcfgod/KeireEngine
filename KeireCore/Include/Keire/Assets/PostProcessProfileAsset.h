#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Keire
{
    class Texture2DAsset;

    enum class PostProcessParameter : std::uint8_t
    {
        ExposureStops,
        BloomIntensity,
        BloomThreshold,
        BloomSoftKnee,
        BloomRadius,
        Temperature,
        Tint,
        Contrast,
        Saturation,
        RedGain,
        GreenGain,
        BlueGain,
        VignetteIntensity,
        VignetteSmoothness,
        VignetteRoundness,
        GrainIntensity,
        GrainResponse,
        ChromaticAberration,
        FocusDistance,
        Aperture,
        FocalLength,
        DepthOfFieldStrength,
        MotionBlurStrength,
        Count
    };

    enum class PostProcessEffect : std::uint8_t
    {
        Bloom,
        ColorGrading,
        Vignette,
        FilmGrain,
        ChromaticAberration,
        DepthOfField,
        MotionBlur,
        Count
    };

    struct PostProcessParameterDescription
    {
        std::string_view Key;
        std::string_view Label;
        std::string_view Unit;
        double Default;
        double Minimum;
        double Maximum;
        double Step;
    };

    template <typename T> struct PostProcessOverride
    {
        bool Override = false;
        T Value{};

        [[nodiscard]] bool operator==(const PostProcessOverride&) const noexcept = default;
    };

    /// Per-parameter overrides prevent a local volume from resetting unrelated global effects.
    enum class PostProcessCustomStage : std::uint8_t
    {
        BeforeToneMap,
        AfterToneMap,
        AfterUi
    };

    struct PostProcessCustomEffect
    {
        AssetId Material;
        PostProcessCustomStage Stage = PostProcessCustomStage::AfterToneMap;
        bool Enabled = true;

        [[nodiscard]] bool operator==(const PostProcessCustomEffect&) const noexcept = default;
    };

    inline constexpr std::size_t MaximumPostProcessCustomEffects = 16;

    /// Custom effect order is authored order within each stage. An empty overridden stack clears inheritance.
    struct PostProcessProfileDefinition
    {
        std::uint32_t SchemaVersion = 3;
        std::array<PostProcessOverride<double>, static_cast<std::size_t>(PostProcessParameter::Count)> Parameters;
        std::array<PostProcessOverride<bool>, static_cast<std::size_t>(PostProcessEffect::Count)> Effects;
        /// Null with Override=true explicitly clears an inherited LUT. Unchecked references inherit.
        PostProcessOverride<AssetId> ColorLookup;
        PostProcessOverride<double> ColorLookupWeight{false, 1.0};
        PostProcessOverride<std::vector<PostProcessCustomEffect>> CustomEffects;

        KEIRE_API PostProcessProfileDefinition();

        [[nodiscard]] bool operator==(const PostProcessProfileDefinition&) const noexcept = default;
    };

    [[nodiscard]] KEIRE_API std::span<const PostProcessParameterDescription>
    PostProcessParameterDescriptions() noexcept;
    [[nodiscard]] KEIRE_API std::span<const std::string_view> PostProcessEffectKeys() noexcept;

    /// Validate a linear, non-HDR Data texture with no mipmaps and clamp/linear sampling.
    /// Layout is N*N by N, N=16/32/64: R varies along X within a tile, G along Y, B selects the tile.
    /// RGB values represent normalized display-linear colors; alpha is ignored. Returns N.
    [[nodiscard]] KEIRE_API std::uint32_t ValidatePostProcessColorLookup(const Texture2DAsset& texture);

    class KEIRE_API PostProcessProfileAsset final : public Asset
    {
      public:
        explicit PostProcessProfileAsset(PostProcessProfileDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245505052ULL, 0x4f46494c45000001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }

        [[nodiscard]] std::size_t ResidentBytes() const noexcept override
        {
            return sizeof(*this) + m_Definition.CustomEffects.Value.capacity() * sizeof(PostProcessCustomEffect);
        }

        [[nodiscard]] const PostProcessProfileDefinition& Definition() const noexcept { return m_Definition; }

        static void Validate(const PostProcessProfileDefinition& definition);
        [[nodiscard]] static Ref<PostProcessProfileAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const PostProcessProfileDefinition& definition);

      private:
        PostProcessProfileDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreatePostProcessProfileAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreatePostProcessProfileAssetDecoder();
} // namespace Keire
