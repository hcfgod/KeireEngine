#pragma once

#include "KeireClient/Editor/ShaderGraphPreview.h"
#include "KeireClient/Editor/ShaderGraphPreviewRaster.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    struct PreviewMaterialSurface
    {
        Keire::Vector4 BaseColor{1.0F, 1.0F, 1.0F, 1.0F};
        float Metallic = 0.0F;
        float Roughness = 0.5F;
        float Specular = 0.5F;
        float ClearCoat = 0.0F;
        float ClearCoatRoughness = 0.25F;
        Keire::Vector4 SheenColor{0.0F, 0.0F, 0.0F, 1.0F};
        float SheenRoughness = 0.5F;
        Keire::Vector3 Normal{0.0F, 0.0F, 1.0F};
        Keire::Vector4 Emission{0.0F, 0.0F, 0.0F, 1.0F};
        float Occlusion = 1.0F;
        float Opacity = 1.0F;
        Keire::Vector4 SubsurfaceColor{1.0F, 0.35F, 0.25F, 1.0F};
        float Subsurface = 0.0F;
        float Anisotropy = 0.0F;
        Keire::Vector3 Tangent{1.0F, 0.0F, 0.0F};
        float Transmission = 0.0F;
        float IndexOfRefraction = 1.5F;
        float Refraction = 0.0F;
        float Thickness = 1.0F;
    };

    struct PreviewGraphValue
    {
        Keire::Vector4 Data;
        Keire::ShaderGraphValueType Type = Keire::ShaderGraphValueType::Scalar;
        Keire::AssetId Texture;
        Keire::ShaderTextureSemantic TextureSemantic = Keire::ShaderTextureSemantic::Generic;
        std::optional<PreviewMaterialSurface> Surface;
    };

    enum class EvaluationPhase : std::uint8_t
    {
        Begin,
        SelectBranch,
        Execute,
    };

    struct EvaluationTask
    {
        Keire::ShaderGraphEndpoint Endpoint;
        EvaluationPhase Phase = EvaluationPhase::Begin;
    };

    [[nodiscard]] Keire::Vector4 ValueVector(const Keire::ShaderGraphValue& value) noexcept;
    [[nodiscard]] Keire::Vector3 Normalize(Keire::Vector3 value, Keire::Vector3 fallback = {0.0F, 0.0F, 1.0F}) noexcept;
    [[nodiscard]] float Dot(Keire::Vector3 first, Keire::Vector3 second) noexcept;
    [[nodiscard]] Keire::Vector3 Cross(Keire::Vector3 first, Keire::Vector3 second) noexcept;
    [[nodiscard]] std::size_t ComponentCount(Keire::ShaderGraphValueType type) noexcept;
    [[nodiscard]] float Component(Keire::Vector4 value, std::size_t index) noexcept;
    void SetComponent(Keire::Vector4& value, std::size_t index, float component) noexcept;
    [[nodiscard]] PreviewGraphValue
    GraphValue(const Keire::ShaderGraphValue& value, Keire::ShaderGraphValueType type,
               Keire::ShaderTextureSemantic semantic = Keire::ShaderTextureSemantic::Generic) noexcept;
    [[nodiscard]] float SafeDivide(float first, float second) noexcept;
    [[nodiscard]] float Fraction(float value) noexcept;
    [[nodiscard]] float MaterialHash(Keire::Vector2 value) noexcept;
    [[nodiscard]] float MaterialValueNoise(Keire::Vector2 position) noexcept;
    [[nodiscard]] float MaterialNoise(Keire::Vector2 uv, float scale, float detail) noexcept;

    class ShaderGraphPreviewEvaluator final
    {
      public:
        struct Impl;

        explicit ShaderGraphPreviewEvaluator(const ShaderGraphPreviewRequest& request);
        ~ShaderGraphPreviewEvaluator();

        ShaderGraphPreviewEvaluator(const ShaderGraphPreviewEvaluator&) = delete;
        ShaderGraphPreviewEvaluator& operator=(const ShaderGraphPreviewEvaluator&) = delete;

        [[nodiscard]] Detail::PreviewMaterial Resolve(Keire::Vector2 uv, Keire::Vector3 normal,
                                                      Keire::Vector3 position);

      private:
        void SetContext(Keire::Vector2 uv, Keire::Vector3 normal, Keire::Vector3 position);
        [[nodiscard]] std::optional<Keire::Vector4> MasterInput(std::string_view name,
                                                                Keire::ShaderGraphValueType type);
        [[nodiscard]] std::optional<PreviewMaterialSurface> MasterAttributes();

        const ShaderGraphPreviewRequest& m_Request;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor::ShaderGraphPreviewInternal
