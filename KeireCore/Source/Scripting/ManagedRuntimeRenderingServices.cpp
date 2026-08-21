#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Scenes/Scene.h"

#include <algorithm>
#include <utility>

namespace Keire::Detail
{
    class ManagedMaterialParameterStore::Impl final
    {
      public:
        static constexpr std::size_t MaximumGlobalMaterialParameters = 256;

        struct CollectionEntry final
        {
            AssetHandle<MaterialParameterCollectionAsset> Handle;
            Ref<MaterialParameterCollectionState> State;
            std::uint64_t Revision = 0;
        };

        static void Refresh(CollectionEntry& entry)
        {
            const auto loaded = entry.Handle.TryGetLoaded();
            const auto revision = entry.Handle.Revision();
            if (!loaded || entry.Handle.UsingFallback() || revision == 0 || revision == entry.Revision)
                return;
            const auto previous = entry.State ? entry.State->Snapshot() : std::map<AssetId, MaterialPropertyValue>{};
            auto replacement = CreateRef<MaterialParameterCollectionState>(loaded->Definition());
            for (const auto& parameter : loaded->Definition().Parameters)
            {
                const auto value = previous.find(parameter.Id);
                if (value == previous.end())
                    continue;
                try
                {
                    replacement->Set(parameter.Id, value->second);
                }
                catch (...)
                {
                    // Hot reload deliberately drops an override whose stable parameter changed to an incompatible type.
                }
            }
            if (entry.State)
                entry.State->Close();
            entry.State = std::move(replacement);
            entry.Revision = revision;
        }

        std::map<AssetId, CollectionEntry> Collections;
    };

    ManagedMaterialParameterStore::ManagedMaterialParameterStore() : m_Impl(std::make_unique<Impl>()) {}

    ManagedMaterialParameterStore::~ManagedMaterialParameterStore() { Close(); }

    bool ManagedMaterialParameterStore::Ready(const Ref<AssetSystem>& assets, const AssetId collection)
    {
        if (!assets || !collection)
            return false;
        auto [entry, inserted] = m_Impl->Collections.try_emplace(collection);
        if (inserted)
            entry->second.Handle = assets->Load<MaterialParameterCollectionAsset>(collection, AssetPriority::High);
        Impl::Refresh(entry->second);
        return static_cast<bool>(entry->second.State);
    }

    bool ManagedMaterialParameterStore::Set(const Ref<AssetSystem>& assets, const AssetId collection,
                                            const std::string_view name, MaterialPropertyValue value)
    {
        if (!Ready(assets, collection))
            return false;
        auto& entry = m_Impl->Collections.at(collection);
        const auto definition = entry.State->Definition();
        const auto parameter =
            std::ranges::find(definition.Parameters, name, &MaterialParameterCollectionParameter::Name);
        if (parameter == definition.Parameters.end())
            return false;
        entry.State->Set(parameter->Id, value);
        return true;
    }

    bool ManagedMaterialParameterStore::Reset(const Ref<AssetSystem>& assets, const AssetId collection,
                                              const std::string_view name)
    {
        if (!Ready(assets, collection))
            return false;
        auto& entry = m_Impl->Collections.at(collection);
        const auto definition = entry.State->Definition();
        const auto parameter =
            std::ranges::find(definition.Parameters, name, &MaterialParameterCollectionParameter::Name);
        return parameter != definition.Parameters.end() && entry.State->Reset(parameter->Id);
    }

    bool ManagedMaterialParameterStore::Clear(const Ref<AssetSystem>& assets, const AssetId collection)
    {
        if (!Ready(assets, collection))
            return false;
        auto& entry = m_Impl->Collections.at(collection);
        for (const auto& parameter : entry.State->Definition().Parameters)
            (void)entry.State->Reset(parameter.Id);
        return true;
    }

    std::map<std::string, MaterialPropertyValue, std::less<>> ManagedMaterialParameterStore::Snapshot()
    {
        std::map<std::string, MaterialPropertyValue, std::less<>> result;
        for (auto& [asset, entry] : m_Impl->Collections)
        {
            (void)asset;
            Impl::Refresh(entry);
            if (!entry.State)
                continue;
            const auto values = entry.State->Snapshot();
            for (const auto& parameter : entry.State->Definition().Parameters)
            {
                const auto value = values.find(parameter.Id);
                if (value != values.end() &&
                    (result.contains(parameter.Name) || result.size() < Impl::MaximumGlobalMaterialParameters))
                    result.insert_or_assign(parameter.Name, value->second);
            }
        }
        return result;
    }

    void ManagedMaterialParameterStore::Close() noexcept
    {
        for (auto& [asset, entry] : m_Impl->Collections)
        {
            (void)asset;
            if (entry.State)
                entry.State->Close();
        }
        m_Impl->Collections.clear();
    }

    namespace
    {
        template <typename T> [[nodiscard]] Ref<T> Find(const Ref<Scene>& scene, const AssetId entity)
        {
            const auto target = scene ? scene->FindEntity(EntityId(entity)) : Entity{};
            return target ? target.GetComponent<T>() : Ref<T>{};
        }

        template <typename T>
        [[nodiscard]] std::optional<float> ReadCommonLightScalar(const Ref<T>& light,
                                                                 const ManagedRenderingScalarProperty property)
        {
            if (!light)
                return std::nullopt;
            switch (property)
            {
            case ManagedRenderingScalarProperty::Intensity:
                return light->Intensity();
            case ManagedRenderingScalarProperty::ShadowStrength:
                return light->ShadowStrength();
            case ManagedRenderingScalarProperty::ShadowBias:
                return light->ShadowBias();
            case ManagedRenderingScalarProperty::IndirectMultiplier:
                return light->IndirectMultiplier();
            default:
                return std::nullopt;
            }
        }

        template <typename T>
        [[nodiscard]] bool SetCommonLightScalar(const Ref<T>& light, const ManagedRenderingScalarProperty property,
                                                const float value)
        {
            if (!light)
                return false;
            switch (property)
            {
            case ManagedRenderingScalarProperty::Intensity:
                light->SetIntensity(value);
                return true;
            case ManagedRenderingScalarProperty::ShadowStrength:
                light->SetShadowStrength(value);
                return true;
            case ManagedRenderingScalarProperty::ShadowBias:
                light->SetShadowBias(value);
                return true;
            case ManagedRenderingScalarProperty::IndirectMultiplier:
                light->SetIndirectMultiplier(value);
                return true;
            default:
                return false;
            }
        }

        template <typename T>
        [[nodiscard]] std::optional<std::int32_t> ReadCommonLightInteger(const Ref<T>& light,
                                                                         const ManagedRenderingIntegerProperty property)
        {
            if (!light)
                return std::nullopt;
            switch (property)
            {
            case ManagedRenderingIntegerProperty::Shadows:
                return static_cast<std::int32_t>(light->Shadows());
            case ManagedRenderingIntegerProperty::BakeMode:
                return static_cast<std::int32_t>(light->BakeMode());
            case ManagedRenderingIntegerProperty::ShadowResolution:
                return static_cast<std::int32_t>(light->ShadowResolution());
            default:
                return std::nullopt;
            }
        }

        template <typename T>
        [[nodiscard]] bool SetCommonLightInteger(const Ref<T>& light, const ManagedRenderingIntegerProperty property,
                                                 const std::int32_t value)
        {
            if (!light)
                return false;
            switch (property)
            {
            case ManagedRenderingIntegerProperty::Shadows:
                light->SetShadows(static_cast<ShadowQuality>(value));
                return true;
            case ManagedRenderingIntegerProperty::BakeMode:
                light->SetBakeMode(static_cast<LightBakeMode>(value));
                return true;
            case ManagedRenderingIntegerProperty::ShadowResolution:
                light->SetShadowResolution(static_cast<ShadowResolutionHint>(value));
                return true;
            default:
                return false;
            }
        }
    } // namespace

    std::optional<float> ReadManagedRenderingScalar(const Ref<Scene>& scene, const AssetId entity,
                                                    const ManagedRenderingComponent component,
                                                    const ManagedRenderingScalarProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera)
                    return std::nullopt;
                switch (property)
                {
                case ManagedRenderingScalarProperty::VerticalFieldOfView:
                    return camera->VerticalFieldOfViewDegrees();
                case ManagedRenderingScalarProperty::OrthographicSize:
                    return camera->OrthographicSize();
                case ManagedRenderingScalarProperty::NearPlane:
                    return camera->NearPlane();
                case ManagedRenderingScalarProperty::FarPlane:
                    return camera->FarPlane();
                default:
                    return std::nullopt;
                }
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                return renderer && property == ManagedRenderingScalarProperty::LightmapScale
                           ? std::optional<float>(renderer->LightmapScale())
                           : std::nullopt;
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (property == ManagedRenderingScalarProperty::ColorTemperature)
                    return light ? std::optional<float>(light->ColorTemperatureKelvin()) : std::nullopt;
                if (property == ManagedRenderingScalarProperty::CookieRotation)
                    return light ? std::optional<float>(light->CookieRotationDegrees()) : std::nullopt;
                return ReadCommonLightScalar(light, property);
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                if (property == ManagedRenderingScalarProperty::Range)
                    return light ? std::optional<float>(light->Range()) : std::nullopt;
                return ReadCommonLightScalar(light, property);
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            if (!light)
                return std::nullopt;
            if (property == ManagedRenderingScalarProperty::Range)
                return light->Range();
            if (property == ManagedRenderingScalarProperty::InnerAngle)
                return light->InnerAngleDegrees();
            if (property == ManagedRenderingScalarProperty::OuterAngle)
                return light->OuterAngleDegrees();
            if (property == ManagedRenderingScalarProperty::CookieRotation)
                return light->CookieRotationDegrees();
            return ReadCommonLightScalar(light, property);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRenderingScalar(const Ref<Scene>& scene, const AssetId entity,
                                   const ManagedRenderingComponent component,
                                   const ManagedRenderingScalarProperty property, const float value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera)
                    return false;
                switch (property)
                {
                case ManagedRenderingScalarProperty::VerticalFieldOfView:
                    camera->SetVerticalFieldOfViewDegrees(value);
                    return true;
                case ManagedRenderingScalarProperty::OrthographicSize:
                    camera->SetOrthographicSize(value);
                    return true;
                case ManagedRenderingScalarProperty::NearPlane:
                    camera->SetClipPlanes(value, camera->FarPlane());
                    return true;
                case ManagedRenderingScalarProperty::FarPlane:
                    camera->SetClipPlanes(camera->NearPlane(), value);
                    return true;
                default:
                    return false;
                }
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer || property != ManagedRenderingScalarProperty::LightmapScale)
                    return false;
                renderer->SetLightmapScale(value);
                return true;
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return false;
                if (property == ManagedRenderingScalarProperty::ColorTemperature)
                {
                    light->SetColorTemperatureKelvin(value);
                    return true;
                }
                if (property == ManagedRenderingScalarProperty::CookieRotation)
                {
                    light->SetCookieTransform(light->CookieScale(), light->CookieOffset(), value);
                    return true;
                }
                return SetCommonLightScalar(light, property, value);
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                if (light && property == ManagedRenderingScalarProperty::Range)
                {
                    light->SetRange(value);
                    return true;
                }
                return SetCommonLightScalar(light, property, value);
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            if (!light)
                return false;
            if (property == ManagedRenderingScalarProperty::Range)
            {
                light->SetRange(value);
                return true;
            }
            if (property == ManagedRenderingScalarProperty::InnerAngle)
            {
                light->SetConeAngles(value, light->OuterAngleDegrees());
                return true;
            }
            if (property == ManagedRenderingScalarProperty::OuterAngle)
            {
                light->SetConeAngles(light->InnerAngleDegrees(), value);
                return true;
            }
            if (property == ManagedRenderingScalarProperty::CookieRotation)
            {
                light->SetCookieTransform(light->CookieScale(), light->CookieOffset(), value);
                return true;
            }
            return SetCommonLightScalar(light, property, value);
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<std::int32_t> ReadManagedRenderingInteger(const Ref<Scene>& scene, const AssetId entity,
                                                            const ManagedRenderingComponent component,
                                                            const ManagedRenderingIntegerProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera)
                    return std::nullopt;
                if (property == ManagedRenderingIntegerProperty::Priority)
                    return camera->Priority();
                if (property == ManagedRenderingIntegerProperty::Projection)
                    return static_cast<std::int32_t>(camera->Projection());
                if (property == ManagedRenderingIntegerProperty::ClearMode)
                    return static_cast<std::int32_t>(camera->ClearMode());
                return std::nullopt;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                return renderer && property == ManagedRenderingIntegerProperty::GIReceive
                           ? std::optional<std::int32_t>(static_cast<std::int32_t>(renderer->GIReceive()))
                           : std::nullopt;
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
                return ReadCommonLightInteger(Find<DirectionalLightComponent>(scene, entity), property);
            if (component == ManagedRenderingComponent::PointLight)
                return ReadCommonLightInteger(Find<PointLightComponent>(scene, entity), property);
            return ReadCommonLightInteger(Find<SpotLightComponent>(scene, entity), property);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRenderingInteger(const Ref<Scene>& scene, const AssetId entity,
                                    const ManagedRenderingComponent component,
                                    const ManagedRenderingIntegerProperty property, const std::int32_t value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera)
                    return false;
                if (property == ManagedRenderingIntegerProperty::Priority)
                    camera->SetPriority(value);
                else if (property == ManagedRenderingIntegerProperty::Projection)
                    camera->SetProjection(static_cast<CameraProjection>(value));
                else if (property == ManagedRenderingIntegerProperty::ClearMode)
                    camera->SetClearMode(static_cast<CameraClearMode>(value));
                else
                    return false;
                return true;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer || property != ManagedRenderingIntegerProperty::GIReceive)
                    return false;
                renderer->SetGIReceive(static_cast<GIReceiveMode>(value));
                return true;
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
                return SetCommonLightInteger(Find<DirectionalLightComponent>(scene, entity), property, value);
            if (component == ManagedRenderingComponent::PointLight)
                return SetCommonLightInteger(Find<PointLightComponent>(scene, entity), property, value);
            return SetCommonLightInteger(Find<SpotLightComponent>(scene, entity), property, value);
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<bool> ReadManagedRenderingFlag(const Ref<Scene>& scene, const AssetId entity,
                                                 const ManagedRenderingComponent component,
                                                 const ManagedRenderingFlagProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                return camera && property == ManagedRenderingFlagProperty::Primary
                           ? std::optional<bool>(camera->Primary())
                           : std::nullopt;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer)
                    return std::nullopt;
                switch (property)
                {
                case ManagedRenderingFlagProperty::Visible:
                    return renderer->Visible();
                case ManagedRenderingFlagProperty::CastShadows:
                    return renderer->CastShadows();
                case ManagedRenderingFlagProperty::ReceiveShadows:
                    return renderer->ReceiveShadows();
                case ManagedRenderingFlagProperty::StaticLighting:
                    return renderer->StaticLighting();
                case ManagedRenderingFlagProperty::PreserveLightmapUVs:
                    return renderer->PreserveLightmapUVs();
                default:
                    return std::nullopt;
                }
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return std::nullopt;
                if (property == ManagedRenderingFlagProperty::UseColorTemperature)
                    return light->UseColorTemperature();
                if (property == ManagedRenderingFlagProperty::ContactShadows)
                    return light->ContactShadows();
                return std::nullopt;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                return light && property == ManagedRenderingFlagProperty::ContactShadows
                           ? std::optional<bool>(light->ContactShadows())
                           : std::nullopt;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            return light && property == ManagedRenderingFlagProperty::ContactShadows
                       ? std::optional<bool>(light->ContactShadows())
                       : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRenderingFlag(const Ref<Scene>& scene, const AssetId entity,
                                 const ManagedRenderingComponent component, const ManagedRenderingFlagProperty property,
                                 const bool value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera || property != ManagedRenderingFlagProperty::Primary)
                    return false;
                camera->SetPrimary(value);
                return true;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer)
                    return false;
                switch (property)
                {
                case ManagedRenderingFlagProperty::Visible:
                    renderer->SetVisible(value);
                    return true;
                case ManagedRenderingFlagProperty::CastShadows:
                    renderer->SetCastShadows(value);
                    return true;
                case ManagedRenderingFlagProperty::ReceiveShadows:
                    renderer->SetReceiveShadows(value);
                    return true;
                case ManagedRenderingFlagProperty::StaticLighting:
                    renderer->SetStaticLighting(value);
                    return true;
                case ManagedRenderingFlagProperty::PreserveLightmapUVs:
                    renderer->SetPreserveLightmapUVs(value);
                    return true;
                default:
                    return false;
                }
            }
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return false;
                if (property == ManagedRenderingFlagProperty::UseColorTemperature)
                    light->SetUseColorTemperature(value);
                else if (property == ManagedRenderingFlagProperty::ContactShadows)
                    light->SetContactShadows(value);
                else
                    return false;
                return true;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                if (!light || property != ManagedRenderingFlagProperty::ContactShadows)
                    return false;
                light->SetContactShadows(value);
                return true;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            if (!light || property != ManagedRenderingFlagProperty::ContactShadows)
                return false;
            light->SetContactShadows(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<Vector2> ReadManagedRenderingVector(const Ref<Scene>& scene, const AssetId entity,
                                                      const ManagedRenderingComponent component,
                                                      const ManagedRenderingVectorProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return std::nullopt;
                return property == ManagedRenderingVectorProperty::CookieScale ? light->CookieScale()
                                                                               : light->CookieOffset();
            }
            if (component == ManagedRenderingComponent::SpotLight)
            {
                const auto light = Find<SpotLightComponent>(scene, entity);
                if (!light)
                    return std::nullopt;
                return property == ManagedRenderingVectorProperty::CookieScale ? light->CookieScale()
                                                                               : light->CookieOffset();
            }
        }
        catch (...)
        {
        }
        return std::nullopt;
    }

    bool SetManagedRenderingVector(const Ref<Scene>& scene, const AssetId entity,
                                   const ManagedRenderingComponent component,
                                   const ManagedRenderingVectorProperty property, const Vector2 value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetCookieTransform(
                    property == ManagedRenderingVectorProperty::CookieScale ? value : light->CookieScale(),
                    property == ManagedRenderingVectorProperty::CookieOffset ? value : light->CookieOffset(),
                    light->CookieRotationDegrees());
                return true;
            }
            if (component == ManagedRenderingComponent::SpotLight)
            {
                const auto light = Find<SpotLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetCookieTransform(
                    property == ManagedRenderingVectorProperty::CookieScale ? value : light->CookieScale(),
                    property == ManagedRenderingVectorProperty::CookieOffset ? value : light->CookieOffset(),
                    light->CookieRotationDegrees());
                return true;
            }
        }
        catch (...)
        {
        }
        return false;
    }

    std::optional<Color> ReadManagedRenderingColor(const Ref<Scene>& scene, const AssetId entity,
                                                   const ManagedRenderingComponent component,
                                                   const ManagedRenderingColorProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                return camera && property == ManagedRenderingColorProperty::ClearColor
                           ? std::optional<Color>(camera->ClearColor())
                           : std::nullopt;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                return renderer && property == ManagedRenderingColorProperty::Tint
                           ? std::optional<Color>(renderer->Tint())
                           : std::nullopt;
            }
            if (property != ManagedRenderingColorProperty::LightColor)
                return std::nullopt;
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                return light ? std::optional<Color>(light->LightColor()) : std::nullopt;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                return light ? std::optional<Color>(light->LightColor()) : std::nullopt;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            return light ? std::optional<Color>(light->LightColor()) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRenderingColor(const Ref<Scene>& scene, const AssetId entity,
                                  const ManagedRenderingComponent component,
                                  const ManagedRenderingColorProperty property, const Color value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::Camera)
            {
                const auto camera = Find<CameraComponent>(scene, entity);
                if (!camera || property != ManagedRenderingColorProperty::ClearColor)
                    return false;
                camera->SetClearColor(value);
                return true;
            }
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer || property != ManagedRenderingColorProperty::Tint)
                    return false;
                renderer->SetTint(value);
                return true;
            }
            if (property != ManagedRenderingColorProperty::LightColor)
                return false;
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetLightColor(value);
                return true;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetLightColor(value);
                return true;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            if (!light)
                return false;
            light->SetLightColor(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<AssetId> ReadManagedRenderingAsset(const Ref<Scene>& scene, const AssetId entity,
                                                     const ManagedRenderingComponent component,
                                                     const ManagedRenderingAssetProperty property) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                return renderer && property == ManagedRenderingAssetProperty::Mesh
                           ? std::optional<AssetId>(renderer->Mesh())
                           : std::nullopt;
            }
            if (property != ManagedRenderingAssetProperty::Cookie)
                return std::nullopt;
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                return light ? std::optional<AssetId>(light->Cookie()) : std::nullopt;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                return light ? std::optional<AssetId>(light->Cookie()) : std::nullopt;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            return light ? std::optional<AssetId>(light->Cookie()) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRenderingAsset(const Ref<Scene>& scene, const AssetId entity,
                                  const ManagedRenderingComponent component,
                                  const ManagedRenderingAssetProperty property, const AssetId value) noexcept
    {
        try
        {
            if (component == ManagedRenderingComponent::MeshRenderer)
            {
                const auto renderer = Find<MeshRendererComponent>(scene, entity);
                if (!renderer || property != ManagedRenderingAssetProperty::Mesh)
                    return false;
                renderer->SetMesh(value);
                return true;
            }
            if (property != ManagedRenderingAssetProperty::Cookie)
                return false;
            if (component == ManagedRenderingComponent::DirectionalLight)
            {
                const auto light = Find<DirectionalLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetCookie(value);
                return true;
            }
            if (component == ManagedRenderingComponent::PointLight)
            {
                const auto light = Find<PointLightComponent>(scene, entity);
                if (!light)
                    return false;
                light->SetCookie(value);
                return true;
            }
            const auto light = Find<SpotLightComponent>(scene, entity);
            if (!light)
                return false;
            light->SetCookie(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<std::vector<AssetId>> ReadManagedRendererMaterials(const Ref<Scene>& scene,
                                                                     const AssetId entity) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            return renderer ? std::optional<std::vector<AssetId>>(std::in_place, renderer->Materials().begin(),
                                                                  renderer->Materials().end())
                            : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedRendererMaterials(const Ref<Scene>& scene, const AssetId entity,
                                     const std::span<const AssetId> materials) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            if (!renderer)
                return false;
            renderer->SetMaterials(materials);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SetManagedMaterialProperty(const Ref<Scene>& scene, const AssetId entity, const std::string_view name,
                                    MaterialPropertyValue value) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            if (!renderer)
                return false;
            renderer->SetMaterialProperty(std::string(name), value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ResetManagedMaterialProperty(const Ref<Scene>& scene, const AssetId entity,
                                      const std::string_view name) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            return renderer && renderer->ResetMaterialProperty(name);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ClearManagedMaterialProperties(const Ref<Scene>& scene, const AssetId entity) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            if (!renderer)
                return false;
            renderer->ClearMaterialProperties();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SetManagedMaterialInstanceProperty(const Ref<Scene>& scene, const AssetId entity, const std::size_t slot,
                                            const std::string_view name, MaterialPropertyValue value) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            if (!renderer)
                return false;
            renderer->SetMaterialInstanceProperty(slot, std::string(name), value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ResetManagedMaterialInstanceProperty(const Ref<Scene>& scene, const AssetId entity, const std::size_t slot,
                                              const std::string_view name) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            return renderer && renderer->ResetMaterialInstanceProperty(slot, name);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ClearManagedMaterialInstanceProperties(const Ref<Scene>& scene, const AssetId entity,
                                                const std::size_t slot) noexcept
    {
        try
        {
            const auto renderer = Find<MeshRendererComponent>(scene, entity);
            if (!renderer)
                return false;
            renderer->ClearMaterialInstanceProperties(slot);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<float>
    ManagedRuntimeSceneServices::ReadManagedRenderingScalar(const AssetId entity,
                                                            const ManagedRenderingComponent component,
                                                            const ManagedRenderingScalarProperty property) noexcept
    {
        return Detail::ReadManagedRenderingScalar(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingScalar(const AssetId entity,
                                                                const ManagedRenderingComponent component,
                                                                const ManagedRenderingScalarProperty property,
                                                                const float value) noexcept
    {
        return Detail::SetManagedRenderingScalar(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<std::int32_t>
    ManagedRuntimeSceneServices::ReadManagedRenderingInteger(const AssetId entity,
                                                             const ManagedRenderingComponent component,
                                                             const ManagedRenderingIntegerProperty property) noexcept
    {
        return Detail::ReadManagedRenderingInteger(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingInteger(const AssetId entity,
                                                                 const ManagedRenderingComponent component,
                                                                 const ManagedRenderingIntegerProperty property,
                                                                 const std::int32_t value) noexcept
    {
        return Detail::SetManagedRenderingInteger(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<bool>
    ManagedRuntimeSceneServices::ReadManagedRenderingFlag(const AssetId entity,
                                                          const ManagedRenderingComponent component,
                                                          const ManagedRenderingFlagProperty property) noexcept
    {
        return Detail::ReadManagedRenderingFlag(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingFlag(const AssetId entity,
                                                              const ManagedRenderingComponent component,
                                                              const ManagedRenderingFlagProperty property,
                                                              const bool value) noexcept
    {
        return Detail::SetManagedRenderingFlag(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<Vector2>
    ManagedRuntimeSceneServices::ReadManagedRenderingVector(const AssetId entity,
                                                            const ManagedRenderingComponent component,
                                                            const ManagedRenderingVectorProperty property) noexcept
    {
        return Detail::ReadManagedRenderingVector(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingVector(const AssetId entity,
                                                                const ManagedRenderingComponent component,
                                                                const ManagedRenderingVectorProperty property,
                                                                const Vector2 value) noexcept
    {
        return Detail::SetManagedRenderingVector(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<Color>
    ManagedRuntimeSceneServices::ReadManagedRenderingColor(const AssetId entity,
                                                           const ManagedRenderingComponent component,
                                                           const ManagedRenderingColorProperty property) noexcept
    {
        return Detail::ReadManagedRenderingColor(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingColor(const AssetId entity,
                                                               const ManagedRenderingComponent component,
                                                               const ManagedRenderingColorProperty property,
                                                               const Color value) noexcept
    {
        return Detail::SetManagedRenderingColor(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<AssetId>
    ManagedRuntimeSceneServices::ReadManagedRenderingAsset(const AssetId entity,
                                                           const ManagedRenderingComponent component,
                                                           const ManagedRenderingAssetProperty property) noexcept
    {
        return Detail::ReadManagedRenderingAsset(ManagedRuntimeScene(entity), entity, component, property);
    }

    bool ManagedRuntimeSceneServices::SetManagedRenderingAsset(const AssetId entity,
                                                               const ManagedRenderingComponent component,
                                                               const ManagedRenderingAssetProperty property,
                                                               const AssetId value) noexcept
    {
        return Detail::SetManagedRenderingAsset(ManagedRuntimeScene(entity), entity, component, property, value);
    }

    std::optional<std::vector<AssetId>>
    ManagedRuntimeSceneServices::ReadManagedRendererMaterials(const AssetId entity) noexcept
    {
        return Detail::ReadManagedRendererMaterials(ManagedRuntimeScene(entity), entity);
    }

    bool ManagedRuntimeSceneServices::SetManagedRendererMaterials(const AssetId entity,
                                                                  const std::span<const AssetId> materials) noexcept
    {
        return Detail::SetManagedRendererMaterials(ManagedRuntimeScene(entity), entity, materials);
    }

    bool ManagedRuntimeSceneServices::SetManagedMaterialProperty(const AssetId entity, const std::string_view name,
                                                                 MaterialPropertyValue value) noexcept
    {
        return Detail::SetManagedMaterialProperty(ManagedRuntimeScene(entity), entity, name, value);
    }

    bool ManagedRuntimeSceneServices::ResetManagedMaterialProperty(const AssetId entity,
                                                                   const std::string_view name) noexcept
    {
        return Detail::ResetManagedMaterialProperty(ManagedRuntimeScene(entity), entity, name);
    }

    bool ManagedRuntimeSceneServices::ClearManagedMaterialProperties(const AssetId entity) noexcept
    {
        return Detail::ClearManagedMaterialProperties(ManagedRuntimeScene(entity), entity);
    }

    bool ManagedRuntimeSceneServices::SetManagedMaterialInstanceProperty(const AssetId entity, const std::size_t slot,
                                                                         const std::string_view name,
                                                                         MaterialPropertyValue value) noexcept
    {
        return Detail::SetManagedMaterialInstanceProperty(ManagedRuntimeScene(entity), entity, slot, name, value);
    }

    bool ManagedRuntimeSceneServices::ResetManagedMaterialInstanceProperty(const AssetId entity, const std::size_t slot,
                                                                           const std::string_view name) noexcept
    {
        return Detail::ResetManagedMaterialInstanceProperty(ManagedRuntimeScene(entity), entity, slot, name);
    }

    bool ManagedRuntimeSceneServices::ClearManagedMaterialInstanceProperties(const AssetId entity,
                                                                             const std::size_t slot) noexcept
    {
        return Detail::ClearManagedMaterialInstanceProperties(ManagedRuntimeScene(entity), entity, slot);
    }
} // namespace Keire::Detail
