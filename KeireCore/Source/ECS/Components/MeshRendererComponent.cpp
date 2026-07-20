#include "Keire/ECS/Components/MeshRendererComponent.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadMeshProperty(const ComponentPropertyBag& values, const std::string_view key,
                                         const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Mesh Renderer property has an incompatible type.");
        }

        [[nodiscard]] bool ValidTint(const Color color) noexcept
        {
            return Math::IsFinite(color) && color.Red >= 0.0F && color.Red <= 1.0F && color.Green >= 0.0F &&
                   color.Green <= 1.0F && color.Blue >= 0.0F && color.Blue <= 1.0F && color.Alpha >= 0.0F &&
                   color.Alpha <= 1.0F;
        }
    } // namespace

    MeshRendererComponent::MeshRendererComponent() : Component(StaticType()) {}

    void MeshRendererComponent::SetMesh(const AssetId mesh)
    {
        m_Mesh = mesh;
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterial(const AssetId material)
    {
        m_Material = material;
        NotifyChanged();
    }

    void MeshRendererComponent::SetTint(const Color tint)
    {
        if (!ValidTint(tint))
            throw std::invalid_argument("Mesh Renderer tint must contain finite values in 0..1.");
        m_Tint = tint;
        NotifyChanged();
    }

    void MeshRendererComponent::SetVisible(const bool visible)
    {
        m_Visible = visible;
        NotifyChanged();
    }

    void MeshRendererComponent::Reset()
    {
        m_Mesh = {};
        m_Material = {};
        m_Tint = {0.25F, 0.55F, 1.0F, 1.0F};
        m_Visible = true;
        NotifyChanged();
    }

    ComponentRegistration CreateMeshRendererComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = MeshRendererComponent::StaticType();
        result.Name = "Mesh Renderer";
        result.Category = "Rendering";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"mesh", "Mesh", "Rendering", ComponentPropertyKind::Asset, false, {}, {}, 0.1, MeshAsset::StaticType()},
            {"material",
             "Material",
             "Rendering",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             MaterialAsset::StaticType()},
            {"tint", "Tint", "Rendering", ComponentPropertyKind::Color},
            {"visible", "Visible", "Rendering", ComponentPropertyKind::Boolean}};
        result.Factory = [] { return Ref<Component>(CreateRef<MeshRendererComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& renderer = dynamic_cast<const MeshRendererComponent&>(component);
            return ComponentPropertyBag{{"mesh", renderer.m_Mesh},
                                        {"material", renderer.m_Material},
                                        {"tint", renderer.m_Tint},
                                        {"visible", renderer.m_Visible}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Mesh Renderer component schema version.");
            auto& renderer = dynamic_cast<MeshRendererComponent&>(component);
            renderer.SetMesh(ReadMeshProperty(values, "mesh", AssetId{}));
            renderer.SetMaterial(ReadMeshProperty(values, "material", AssetId{}));
            renderer.SetTint(ReadMeshProperty(values, "tint", Color{0.25F, 0.55F, 1.0F, 1.0F}));
            renderer.SetVisible(ReadMeshProperty(values, "visible", true));
        };
        return result;
    }
} // namespace Keire
