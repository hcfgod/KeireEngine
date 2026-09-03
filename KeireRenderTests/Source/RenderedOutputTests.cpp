#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Application.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireRenderTests/RenderAssetFixture.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using KeireRenderTests::Detail::RenderAssetFixture;

namespace
{
    constexpr std::uint32_t LocalShadowEdgeSurfaceSize = 257;

    enum class CaptureKind : std::uint8_t
    {
        AmbientZero,
        AmbientWhite,
        AmbientRed,
        AmbientGreen,
        AmbientBlue,
        DirectionalDisabled,
        DirectionalEnabled,
        TintRed,
        TintBlue,
        ExposureLow,
        ExposureHigh,
        NormalIdentity,
        NormalTransformed
    };

    constexpr std::array CaptureSequence{
        CaptureKind::AmbientZero,        CaptureKind::AmbientWhite, CaptureKind::AmbientRed,
        CaptureKind::AmbientGreen,       CaptureKind::AmbientBlue,  CaptureKind::DirectionalDisabled,
        CaptureKind::DirectionalEnabled, CaptureKind::TintRed,      CaptureKind::TintBlue,
        CaptureKind::ExposureLow,        CaptureKind::ExposureHigh, CaptureKind::NormalIdentity,
        CaptureKind::NormalTransformed};

    struct CaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        std::vector<std::vector<float>> ShadowDepth;
        std::vector<std::uint64_t> MaterialBindingBuilds;
        std::vector<std::uint64_t> SkinningStaticBuilds;
        std::vector<std::uint64_t> SkinningOutputBuilds;
        std::vector<float> SkinningPreparationMilliseconds;
        Keire::RenderCapabilities Capabilities;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    struct DescriptorRolloverResults final
    {
        std::vector<std::uint8_t> Frame;
        std::uint64_t MaterialDependencyChecks = 0;
    };

    struct VfxChannelSignal final
    {
        float Weight = 0.0F;
        float WeightedX = 0.0F;

        [[nodiscard]] float CentroidX() const noexcept { return Weight > 0.0F ? WeightedX / Weight : 0.0F; }
    };

    struct VfxGraphCaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        std::vector<std::uint32_t> FrameUploadSubmissions;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    [[nodiscard]] VfxChannelSignal MeasureChannelSignal(const std::vector<std::uint8_t>& pixels,
                                                        const std::size_t channel)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        REQUIRE(channel < 3);
        VfxChannelSignal result;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(SurfaceSize) + x) * 4U;
                const auto firstOther = (channel + 1) % 3;
                const auto secondOther = (channel + 2) % 3;
                const auto value = static_cast<float>(pixels[offset + channel]);
                const auto other =
                    static_cast<float>(std::max(pixels[offset + firstOther], pixels[offset + secondOther]));
                const auto weight = std::max(value - other, 0.0F) / 255.0F;
                result.Weight += weight;
                result.WeightedX += weight * static_cast<float>(x);
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Keire::AssetId RenderVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x4750555658464752ULL, value);
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> RenderedGraphEffect(const bool customBeforeForce)
    {
        const auto base = customBeforeForce ? 100ULL : 200ULL;
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(base);
        definition.Name = customBeforeForce ? "GPU graph custom before force" : "GPU graph custom after force";
        definition.Duration = 2.0F;
        definition.Capacity = 4;
        definition.Modules = {
            {RenderVfxId(base + 1), true, Keire::VfxBurstModule{0.0F, 1, 1, 0.1F}},
            {RenderVfxId(base + 2), true, Keire::VfxShapeModule{}},
            {RenderVfxId(base + 3), true, Keire::VfxInitializeModule{5.0F, 5.0F, {}, {}, {}, {}}},
            {RenderVfxId(base + 4), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.8F)}},
            {RenderVfxId(base + 5), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {RenderVfxId(base + 6), true, Keire::VfxForceModule{{4.0F, 0.0F, 0.0F}, 0.0F}},
            {RenderVfxId(base + 7), true, Keire::VfxRendererModule{}},
        };
        definition.Blackboard = {
            {RenderVfxId(base + 20), "Tint Override", Keire::VfxValueType::Color, Keire::Color{0.0F, 0.0F, 1.0F, 1.0F},
             true},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);

        auto& system = definition.Systems.front();
        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        if (update == system.Nodes.end())
            throw std::logic_error("Converted GPU VFX graph is missing its Update Context.");
        const auto force = std::ranges::find(update->Blocks, RenderVfxId(base + 6), &Keire::VfxGraphBlock::Reference);
        if (force == update->Blocks.end())
            throw std::logic_error("Converted GPU VFX graph is missing its Force Block.");

        const auto parameter = std::ranges::find(system.Nodes, RenderVfxId(base + 20), &Keire::VfxGraphNode::Reference);
        if (parameter == system.Nodes.end() || parameter->Pins.empty())
            throw std::logic_error("Converted GPU VFX graph is missing its Blackboard parameter.");
        const auto parameterNode = parameter->Id;
        const auto parameterOutputPin = parameter->Pins.front().Id;

        const auto customBlock = RenderVfxId(base + 30);
        const auto customTintInput = RenderVfxId(base + 32);
        auto custom = Keire::CreateVfxGraphPortableHlslBlock("Velocity = float3(0.0, 0.0, 0.0);\nTint = TintOverride;");
        custom.Id = customBlock;
        custom.Pins.push_back(
            {customTintInput, "Tint Override", Keire::VfxValueType::Color, true, "TintOverride", std::nullopt});
        update->Blocks.insert(customBeforeForce ? force : std::next(force), std::move(custom));

        Keire::VfxGraphConnection tintConnection;
        tintConnection.Id = RenderVfxId(base + 35);
        tintConnection.OutputNode = parameterNode;
        tintConnection.OutputPin = parameterOutputPin;
        tintConnection.InputNode = update->Id;
        tintConnection.InputPin = customTintInput;
        tintConnection.InputBlock = customBlock;
        system.Connections.push_back(tintConnection);

        Keire::ValidateVfxEffect(definition);
        const auto compiled = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        if (!compiled.Valid)
            throw std::logic_error("Rendered schema-4 GPU VFX graph did not compile.");
        const auto persisted = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(definition));
        if (!persisted)
            throw std::logic_error("Rendered schema-4 GPU VFX graph did not survive persistence.");
        return persisted;
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset>
    RenderedResourceOutputEffect(const Keire::VfxRendererType rendererType, const Keire::AssetId resource,
                                 const Keire::AssetId material = {})
    {
        Keire::VfxRendererModule renderer;
        renderer.Type = rendererType;
        renderer.Material = material;
        if (rendererType == Keire::VfxRendererType::Mesh)
            renderer.Mesh = resource;
        else
            renderer.Sprite = resource;
        Keire::VfxShapeModule shape;
        if (rendererType == Keire::VfxRendererType::Ribbon)
        {
            shape.Shape = Keire::VfxShape::Box;
            shape.BoxHalfExtent = {0.45F, 0.25F, 0.01F};
        }

        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(rendererType == Keire::VfxRendererType::Mesh ? 300 : 400);
        definition.Name = rendererType == Keire::VfxRendererType::Mesh         ? "GPU mesh output"
                          : rendererType == Keire::VfxRendererType::Ribbon     ? "GPU ribbon output"
                          : rendererType == Keire::VfxRendererType::Volumetric ? "GPU volumetric output"
                                                                               : "Textured sprite output";
        const auto animatedOutput =
            rendererType == Keire::VfxRendererType::Ribbon || rendererType == Keire::VfxRendererType::Volumetric;
        definition.Duration = animatedOutput ? 4.0F : 1.0F;
        definition.Capacity = 4;
        definition.Modules = {
            {RenderVfxId(301), true,
             Keire::VfxBurstModule{0.0F, rendererType == Keire::VfxRendererType::Ribbon ? 4U : 1U, 1, 0.1F}},
            {RenderVfxId(302), true, shape},
            {RenderVfxId(303), true,
             Keire::VfxInitializeModule{animatedOutput ? 4.0F : 1.0F, animatedOutput ? 4.0F : 1.0F, {}, {}, {}, {}}},
            {RenderVfxId(304), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(1.4F)}},
            {RenderVfxId(305), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 1.0F, 1.0F, 1.0F})}},
            {RenderVfxId(306), true, renderer},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        if (rendererType == Keire::VfxRendererType::Ribbon)
        {
            definition.Systems.front().DataType = Keire::VfxParticleDataType::ParticleStrip;
            definition.Systems.front().ParticlesPerStrip = 4;
        }
        Keire::ValidateVfxEffect(definition);
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> RenderedShapeSamplingEffect(const Keire::VfxShape shape,
                                                                                const Keire::AssetId resource)
    {
        Keire::VfxShapeModule shapeModule;
        shapeModule.Shape = shape;
        if (shape == Keire::VfxShape::Mesh)
            shapeModule.Mesh = resource;
        else
            shapeModule.Volume = resource;

        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(shape == Keire::VfxShape::Mesh ? 500 : 600);
        definition.Name = shape == Keire::VfxShape::Mesh ? "GPU mesh spawn sampling" : "GPU volume spawn sampling";
        definition.Duration = 1.0F;
        definition.Capacity = 64;
        definition.Modules = {
            {RenderVfxId(501), true, Keire::VfxBurstModule{0.0F, 32, 1, 0.1F}},
            {RenderVfxId(502), true, shapeModule},
            {RenderVfxId(503), true, Keire::VfxInitializeModule{1.0F, 1.0F, {}, {}, {}, {}}},
            {RenderVfxId(504), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.12F)}},
            {RenderVfxId(505), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.3F, 0.05F, 1.0F})}},
            {RenderVfxId(506), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        Keire::ValidateVfxEffect(definition);
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> RenderedGpuDepthCollisionEffect(const bool enabled)
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(700);
        definition.Name = "GPU depth collision";
        definition.Duration = 4.0F;
        definition.Capacity = 4;
        definition.Modules = {
            {RenderVfxId(701), true, Keire::VfxBurstModule{0.0F, 1, 1, 0.1F}},
            {RenderVfxId(702), true, Keire::VfxShapeModule{}},
            {RenderVfxId(703), true,
             Keire::VfxInitializeModule{4.0F, 4.0F, {0.0F, 0.0F, -1.5F}, {0.0F, 0.0F, -1.5F}, {}, {}}},
            {RenderVfxId(704), true,
             Keire::VfxCollisionModule{enabled ? Keire::VfxCollisionMode::GpuDepth : Keire::VfxCollisionMode::None,
                                       0.0F, true}},
            {RenderVfxId(705), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.2F)}},
            {RenderVfxId(706), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {RenderVfxId(707), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        Keire::ValidateVfxEffect(definition);
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    class RenderCaptureLayer final : public Keire::Layer
    {
      public:
        explicit RenderCaptureLayer(std::shared_ptr<CaptureResults> results, const bool deferredIrradyn = false)
            : Layer("Rendered output capture"), m_Results(std::move(results)), m_DeferredIrradyn(deferredIrradyn)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000001"),
                                                     Keire::SceneAsset::EmptyDefinition("Rendered output tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Rendered cube");
            m_Transform = object.GetComponent<Keire::TransformComponent>();
            m_Renderer = object.AddComponent<Keire::MeshRendererComponent>();

            auto lightEntity = m_Scene->CreateEntity("Directional light");
            m_LightTransform = lightEntity.GetComponent<Keire::TransformComponent>();
            m_Light = lightEntity.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetLightColor({1.0F, 1.0F, 1.0F, 1.0F});
            m_Light->SetIntensity(1.0F);
            m_LightTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Rendered output tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = m_DeferredIrradyn ? Keire::RenderSampleCount::One : Keire::RenderSampleCount::Four;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Capabilities = Owner().Renderer()->Capabilities();
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_LightTransform.Reset();
            m_Renderer.Reset();
            m_Transform.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));

            if (m_NextCapture == CaptureSequence.size())
            {
                Owner().RequestExit();
                return;
            }

            Configure(CaptureSequence[m_NextCapture]);
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
            ++m_NextCapture;
            m_Submitted = true;
        }

      private:
        void Configure(const CaptureKind kind)
        {
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.Exposure = 1.0F;
            m_Environment.RequestedRenderPath =
                m_DeferredIrradyn ? Keire::RenderPath::DeferredHybrid : Keire::RenderPath::Automatic;
            m_Environment.RequestedGlobalIllumination =
                m_DeferredIrradyn ? Keire::GlobalIlluminationMode::Irradyn : Keire::GlobalIlluminationMode::Disabled;
            m_Renderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
            m_Light->SetEnabled(false);
            m_Transform->SetLocalRotation({});
            m_Transform->SetLocalScale({1.0F, 1.0F, 1.0F});

            switch (kind)
            {
            case CaptureKind::AmbientZero:
            case CaptureKind::DirectionalDisabled:
                m_Environment.AmbientIntensity = 0.0F;
                break;
            case CaptureKind::AmbientWhite:
                break;
            case CaptureKind::AmbientRed:
                m_Environment.AmbientColor = {1.0F, 0.0F, 0.0F, 1.0F};
                break;
            case CaptureKind::AmbientGreen:
                m_Environment.AmbientColor = {0.0F, 1.0F, 0.0F, 1.0F};
                break;
            case CaptureKind::AmbientBlue:
                m_Environment.AmbientColor = {0.0F, 0.0F, 1.0F, 1.0F};
                break;
            case CaptureKind::DirectionalEnabled:
            case CaptureKind::NormalIdentity:
                m_Environment.AmbientIntensity = 0.0F;
                m_Light->SetEnabled(true);
                break;
            case CaptureKind::TintRed:
                m_Renderer->SetTint({1.0F, 0.0F, 0.0F, 1.0F});
                break;
            case CaptureKind::TintBlue:
                m_Renderer->SetTint({0.0F, 0.0F, 1.0F, 1.0F});
                break;
            case CaptureKind::ExposureLow:
                m_Environment.Exposure = 0.25F;
                break;
            case CaptureKind::ExposureHigh:
                m_Environment.Exposure = 1.0F;
                break;
            case CaptureKind::NormalTransformed:
                m_Environment.AmbientIntensity = 0.0F;
                m_Light->SetEnabled(true);
                m_Transform->SetLocalEulerAngles({25.0F, 55.0F, 0.0F});
                m_Transform->SetLocalScale({1.0F, 1.5F, 0.65F});
                break;
            }
        }

        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::TransformComponent> m_Transform;
        Keire::Ref<Keire::MeshRendererComponent> m_Renderer;
        Keire::Ref<Keire::TransformComponent> m_LightTransform;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::RenderEnvironmentSettings m_Environment;
        std::size_t m_NextCapture = 0;
        bool m_Submitted = false;
        bool m_DeferredIrradyn = false;
    };

    class DescriptorRolloverCaptureLayer final : public Keire::Layer
    {
      public:
        DescriptorRolloverCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                       const std::array<Keire::AssetId, 2> textures,
                                       std::shared_ptr<DescriptorRolloverResults> results)
            : Layer("Descriptor rollover rendered output capture"), m_Mesh(mesh), m_Material(material),
              m_Textures(textures), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000012"),
                                                     Keire::SceneAsset::EmptyDefinition("Descriptor rollover tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            constexpr std::size_t rendererCount = 160U;
            for (std::size_t index = 0; index < rendererCount; ++index)
            {
                auto object = m_Scene->CreateEntity("Rendered triangle " + std::to_string(index));
                const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
                renderer->SetMesh(m_Mesh);
                renderer->SetMaterial(m_Material);
                renderer->SetMaterialProperty("MainTexture", m_Textures[index % m_Textures.size()]);
                renderer->SetCastShadows(false);
            }

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Descriptor rollover test";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                m_Results->Frame =
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                m_Results->MaterialDependencyChecks =
                    Keire::RenderSystemInternalAccess::MaterialDependencyCheckCount(*Owner().Renderer());
                Owner().RequestExit();
                return;
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.SkyVisible = false;
            environment.RequestedRenderPath = Keire::RenderPath::ForwardPlus;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::Msaa4;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::array<Keire::AssetId, 2> m_Textures;
        std::shared_ptr<DescriptorRolloverResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Submitted = false;
    };

    class VfxGraphGpuCaptureLayer final : public Keire::Layer
    {
      public:
        explicit VfxGraphGpuCaptureLayer(std::shared_ptr<VfxGraphCaptureResults> results)
            : Layer("Schema-v3 GPU VFX graph capture"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            Owner().Renderer()->RequestGpuVfxPipelineWarmup();
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000014"),
                                                     Keire::SceneAsset::EmptyDefinition("GPU VFX graph tests"),
                                                     Keire::ComponentRegistry::CreateDefault());

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "GPU VFX graph tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.SkyVisible = false;
            m_Environment.AmbientIntensity = 0.0F;

            StartVariant(false);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
                CaptureStatistics();
            m_World.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!Owner().Renderer()->Statistics().VfxPipelinesReady)
                return;
            if (!m_Submitted)
            {
                Submit();
                m_Submitted = true;
                return;
            }

            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
            CaptureStatistics();
            switch (m_Phase)
            {
            case Phase::AfterForceSpawn:
                m_World->Update(0.5F);
                m_Phase = Phase::AfterForceSimulated;
                Submit();
                break;
            case Phase::AfterForceSimulated:
                StartVariant(true);
                m_Phase = Phase::BeforeForceSpawn;
                Submit();
                break;
            case Phase::BeforeForceSpawn:
                m_World->Update(0.5F);
                m_Phase = Phase::BeforeForceSimulated;
                Submit();
                break;
            case Phase::BeforeForceSimulated:
                Owner().RequestExit();
                break;
            }
        }

      private:
        enum class Phase : std::uint8_t
        {
            AfterForceSpawn,
            AfterForceSimulated,
            BeforeForceSpawn,
            BeforeForceSimulated
        };

        void StartVariant(const bool customBeforeForce)
        {
            m_World = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1, .MaximumParticles = 4, .Backend = Keire::VfxBackend::Gpu});
            const auto effect = RenderedGraphEffect(customBeforeForce);
            const auto handle = m_World->Activate(
                {effect,
                 1,
                 {},
                 {},
                 0,
                 {{RenderVfxId((customBeforeForce ? 100ULL : 200ULL) + 20), Keire::Color{0.0F, 1.0F, 0.0F, 1.0F}}}});
            if (!handle)
                throw std::logic_error("Could not activate the rendered schema-v3 GPU VFX graph.");
            m_World->Update(0.01F);
        }

        void Submit()
        {
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment, {}, m_World->CaptureRenderSnapshot()});
        }
        void CaptureStatistics() noexcept
        {
            const auto statistics = Owner().Renderer()->Statistics();
            m_Results->Statistics.VfxComputeDispatches =
                std::max(m_Results->Statistics.VfxComputeDispatches, statistics.VfxComputeDispatches);
            m_Results->Statistics.VfxIndirectDraws =
                std::max(m_Results->Statistics.VfxIndirectDraws, statistics.VfxIndirectDraws);
            m_Results->Statistics.VfxGpuWorlds = std::max(m_Results->Statistics.VfxGpuWorlds, statistics.VfxGpuWorlds);
            m_Results->Statistics.VfxPipelinesReady =
                m_Results->Statistics.VfxPipelinesReady || statistics.VfxPipelinesReady;
            m_Results->Statistics.VfxPipelineWarmupMilliseconds =
                std::max(m_Results->Statistics.VfxPipelineWarmupMilliseconds, statistics.VfxPipelineWarmupMilliseconds);
            m_Results->HasStatistics = true;
        }

        std::shared_ptr<VfxGraphCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_World;
        Keire::RenderEnvironmentSettings m_Environment;
        Phase m_Phase = Phase::AfterForceSpawn;
        bool m_Submitted = false;
    };

    class VfxResourceOutputCaptureLayer final : public Keire::Layer
    {
      public:
        VfxResourceOutputCaptureLayer(const Keire::VfxBackend backend, Keire::Ref<Keire::VfxEffectAsset> effect,
                                      std::shared_ptr<VfxGraphCaptureResults> results,
                                      const bool advanceSimulation = false, const Keire::AssetId collisionMesh = {},
                                      const Keire::AssetId collisionMaterial = {},
                                      const Keire::Vector3 emitterPosition = {})
            : Layer("VFX resource output capture"), m_Backend(backend), m_Effect(std::move(effect)),
              m_Results(std::move(results)), m_CollisionMesh(collisionMesh), m_CollisionMaterial(collisionMaterial),
              m_EmitterPosition(emitterPosition), m_AdvanceSimulation(advanceSimulation)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("VFX resource output"),
                                                     Keire::ComponentRegistry::CreateDefault());
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "VFX resource output";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.SkyVisible = false;
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            if (m_CollisionMesh)
            {
                auto collider = m_Scene->CreateEntity("GPU VFX depth collider");
                m_CollisionTransform = collider.GetComponent<Keire::TransformComponent>();
                auto renderer = collider.AddComponent<Keire::MeshRendererComponent>();
                renderer->SetMesh(m_CollisionMesh);
                renderer->SetMaterial(m_CollisionMaterial);
            }

            const auto compiled = Keire::CompileVfxEffect(m_Effect->Definition(), m_Backend);
            if (!compiled.Valid)
                throw std::logic_error("Rendered VFX resource output did not compile for its requested backend.");
            const auto maximumParticles = std::max<std::uint32_t>(4U, m_Effect->Definition().Capacity);
            m_World = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1, .MaximumParticles = maximumParticles, .Backend = m_Backend});
            const auto handle = m_World->Activate({m_Effect});
            if (!handle)
                throw std::logic_error("Could not activate the rendered VFX resource output.");
            m_World->SetTransform(handle, m_EmitterPosition, {});
            m_World->Update(0.01F);
        }

        void OnDetach() noexcept override
        {
            m_World.Reset();
            m_Effect.Reset();
            m_CollisionTransform.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!m_Submitted)
            {
                Owner().Renderer()->Submit(
                    {m_Scene, m_View, false, m_Environment, {}, m_World->CaptureRenderSnapshot()});
                m_Submitted = true;
                return;
            }
            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
            const auto statistics = Owner().Renderer()->Statistics();
            m_Results->FrameUploadSubmissions.push_back(statistics.FrameUploadSubmissions);
            m_Results->Statistics.VfxIndirectDraws =
                std::max(m_Results->Statistics.VfxIndirectDraws, statistics.VfxIndirectDraws);
            m_Results->Statistics.VfxGpuWorlds = std::max(m_Results->Statistics.VfxGpuWorlds, statistics.VfxGpuWorlds);
            m_Results->HasStatistics = true;
            if (m_Results->Frames.size() >= 60)
            {
                Owner().RequestExit();
                return;
            }
            if (m_AdvanceSimulation)
                m_World->Update(1.0F / 60.0F);
            if (m_CollisionTransform && m_Results->Frames.size() == 35)
                m_CollisionTransform->SetLocalPosition({3.0F, 0.0F, 0.0F});
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment, {}, m_World->CaptureRenderSnapshot()});
        }

      private:
        Keire::VfxBackend m_Backend;
        Keire::Ref<Keire::VfxEffectAsset> m_Effect;
        std::shared_ptr<VfxGraphCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_World;
        Keire::Ref<Keire::TransformComponent> m_CollisionTransform;
        Keire::RenderEnvironmentSettings m_Environment;
        Keire::AssetId m_CollisionMesh;
        Keire::AssetId m_CollisionMaterial;
        Keire::Vector3 m_EmitterPosition;
        bool m_Submitted = false;
        bool m_AdvanceSimulation = false;
    };

    class AssetMeshCaptureLayer final : public Keire::Layer
    {
      public:
        AssetMeshCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                              std::shared_ptr<CaptureResults> results,
                              const Keire::RenderSampleCount sampleCount = Keire::RenderSampleCount::One)
            : Layer("Asset mesh capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results)),
              m_SampleCount(sampleCount)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000002"),
                                                     Keire::SceneAsset::EmptyDefinition("Asset mesh tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Asset mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterial(m_Material);
            renderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
            auto sharedObject = m_Scene->CreateEntity("Shared asset mesh");
            const auto sharedRenderer = sharedObject.AddComponent<Keire::MeshRendererComponent>();
            sharedRenderer->SetMesh(m_Mesh);
            sharedRenderer->SetMaterial(m_Material);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Asset mesh tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = m_SampleCount;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            bool exitAfterSubmit = false;
            if (m_Submitted)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->MaterialBindingBuilds.push_back(
                    Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer()));
                const auto statistics = MeasureCenter(m_Results->Frames.back());
                const auto bindingCountIsStable =
                    m_Results->MaterialBindingBuilds.size() >= 2 &&
                    m_Results->MaterialBindingBuilds[m_Results->MaterialBindingBuilds.size() - 2] ==
                        m_Results->MaterialBindingBuilds.back();
                if (m_Results->Frames.size() >= 3 && bindingCountIsStable &&
                    statistics.Green > statistics.Red + MinimumBehaviorDelta &&
                    statistics.Green > statistics.Blue + MinimumBehaviorDelta)
                    exitAfterSubmit = true;
                if (m_Results->Frames.size() >= 120)
                    exitAfterSubmit = true;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
            if (exitAfterSubmit)
                Owner().RequestExit();
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::RenderSampleCount m_SampleCount = Keire::RenderSampleCount::One;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Submitted = false;
    };

    class SkinnedMeshCaptureLayer final : public Keire::Layer
    {
      public:
        SkinnedMeshCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material, const Keire::AssetId skeleton,
                                const Keire::AssetId skin, std::shared_ptr<CaptureResults> results)
            : Layer("Skinned mesh capture"), m_Mesh(mesh), m_Material(material), m_Skeleton(skeleton), m_Skin(skin),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000009"),
                                                     Keire::SceneAsset::EmptyDefinition("Skinned mesh tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Skinned asset mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterial(m_Material);
            m_Animator = object.AddComponent<Keire::AnimatorComponent>();
            m_Animator->SetSkeleton(m_Skeleton);
            m_Animator->SetSkinnedMesh(m_Skin);
            SetPaletteTranslation(-0.65F);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Skinned mesh tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_Animator.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            bool complete = false;
            if (m_Submitted)
            {
                Owner().Renderer()->Flush();
                m_Results->SkinningStaticBuilds.push_back(
                    Keire::RenderSystemInternalAccess::SkinningStaticBuildCount(*Owner().Renderer()));
                m_Results->SkinningOutputBuilds.push_back(
                    Keire::RenderSystemInternalAccess::SkinningOutputBuildCount(*Owner().Renderer()));
                m_Results->SkinningPreparationMilliseconds.push_back(
                    Owner().Renderer()->Statistics().SkinningPreparationMilliseconds);
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto left = GreenDominance(pixels, true);
                const auto right = GreenDominance(pixels, false);
                if (m_Results->Frames.empty() && right > left + MinimumBehaviorDelta)
                {
                    m_Results->Frames.push_back(std::move(pixels));
                    SetPaletteTranslation(0.65F);
                }
                else if (m_Results->Frames.size() == 1 && left > right + MinimumBehaviorDelta)
                {
                    m_Results->Frames.push_back(std::move(pixels));
                    m_DeformationCaptured = true;
                }
            }

            if (m_DeformationCaptured && m_Frames >= 8)
                complete = true;
            if (++m_Frames >= 120)
                complete = true;
            if (complete)
            {
                Owner().RequestExit();
                return;
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.SkyVisible = false;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        void SetPaletteTranslation(const float translation)
        {
            const std::array palette{Keire::Math::ComposeTransform({translation, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F})};
            m_Animator->SetRuntimePose("Test", 0.0F, true, palette);
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        Keire::AssetId m_Skeleton;
        Keire::AssetId m_Skin;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::AnimatorComponent> m_Animator;
        std::uint32_t m_Frames = 0;
        bool m_Submitted = false;
        bool m_DeformationCaptured = false;
    };

    class ShadowCaptureLayer final : public Keire::Layer
    {
      public:
        ShadowCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                           std::shared_ptr<CaptureResults> results)
            : Layer("Shadow capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000004"),
                                                     Keire::SceneAsset::EmptyDefinition("Shadow tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto floor = m_Scene->CreateEntity("Shadow receiver");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.75F, 0.0F});
            floor.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 0.15F, 4.0F});
            const auto floorRenderer = floor.AddComponent<Keire::MeshRendererComponent>();
            floorRenderer->SetMesh(m_Mesh);
            floorRenderer->SetMaterial(m_Material);
            floorRenderer->SetCastShadows(false);

            auto caster = m_Scene->CreateEntity("Shadow caster");
            caster.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
            caster.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.65F, 0.65F, 0.65F});
            const auto casterRenderer = caster.AddComponent<Keire::MeshRendererComponent>();
            casterRenderer->SetMesh(m_Mesh);
            casterRenderer->SetMaterial(m_Material);
            casterRenderer->SetReceiveShadows(false);
            casterRenderer->SetCastShadows(false);
            m_Caster = casterRenderer;

            auto sun = m_Scene->CreateEntity("Sun");
            sun.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Light = sun.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetIntensity(4.0F);
            m_Light->SetShadows(Keire::ShadowQuality::Disabled);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Shadow tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.02F, 0.02F, 0.02F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({3.0F, 3.0F, 5.0F}, {0.0F, -0.25F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_Caster.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame == 120)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Light->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 144)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->ShadowDepth.push_back(Keire::RenderSystemInternalAccess::ReadbackDirectionalShadow(
                    *Owner().Renderer(), *m_View->Surface(), 0));
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 168)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->ShadowDepth.push_back(Keire::RenderSystemInternalAccess::ReadbackDirectionalShadow(
                    *Owner().Renderer(), *m_View->Surface(), 0));
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.08F, 0.08F, 0.08F, 1.0F};
            environment.AmbientIntensity = 0.3F;
            environment.DirectionalShadowCascadeCount = 2;
            environment.DirectionalShadowResolution = 1024;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        std::uint32_t m_Frame = 0;
    };

    class LocalShadowCaptureLayer final : public Keire::Layer
    {
      public:
        LocalShadowCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                std::shared_ptr<CaptureResults> results)
            : Layer("Local shadow capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000005"),
                                                     Keire::SceneAsset::EmptyDefinition("Local shadow tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto floor = m_Scene->CreateEntity("Shadow receiver");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.75F, 0.0F});
            floor.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 0.15F, 4.0F});
            const auto floorRenderer = floor.AddComponent<Keire::MeshRendererComponent>();
            floorRenderer->SetMesh(m_Mesh);
            floorRenderer->SetMaterial(m_Material);
            floorRenderer->SetCastShadows(false);

            auto caster = m_Scene->CreateEntity("Shadow caster");
            caster.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
            caster.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.65F, 0.65F, 0.65F});
            const auto casterRenderer = caster.AddComponent<Keire::MeshRendererComponent>();
            casterRenderer->SetMesh(m_Mesh);
            casterRenderer->SetMaterial(m_Material);
            casterRenderer->SetReceiveShadows(false);
            casterRenderer->SetCastShadows(false);
            m_Caster = casterRenderer;

            auto pointEntity = m_Scene->CreateEntity("Point shadow light");
            pointEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            m_Point = pointEntity.AddComponent<Keire::PointLightComponent>();
            m_Point->SetIntensity(16.0F);
            m_Point->SetRange(10.0F);
            m_Point->SetShadows(Keire::ShadowQuality::Disabled);

            auto spotEntity = m_Scene->CreateEntity("Spot shadow light");
            spotEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            spotEntity.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Spot = spotEntity.AddComponent<Keire::SpotLightComponent>();
            m_Spot->SetIntensity(20.0F);
            m_Spot->SetRange(10.0F);
            m_Spot->SetConeAngles(35.0F, 55.0F);
            m_Spot->SetShadows(Keire::ShadowQuality::Disabled);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Local shadow tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.02F, 0.02F, 0.02F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({3.0F, 3.0F, 5.0F}, {0.0F, -0.25F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Point.Reset();
            m_Spot.Reset();
            m_Caster.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            // NOLINTBEGIN(bugprone-branch-clone) -- distinct shadow states require identical capture transitions.
            if (m_Frame == 120)
            {
                Capture();
                m_Point->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 144)
            {
                Capture();
                CaptureShadow(0);
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 168)
            {
                Capture();
                CaptureShadow(0);
                m_Point->SetShadows(Keire::ShadowQuality::Disabled);
                m_Caster->SetCastShadows(false);
                m_Spot->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 192)
            {
                Capture();
                CaptureShadow(0);
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 216)
            {
                Capture();
                CaptureShadow(0);
                Owner().RequestExit();
                return;
            }
            // NOLINTEND(bugprone-branch-clone)
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.05F, 0.05F, 0.05F, 1.0F};
            environment.AmbientIntensity = 0.2F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        void Capture()
        {
            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
        }

        void CaptureShadow(const std::uint32_t layer)
        {
            m_Results->ShadowDepth.push_back(
                Keire::RenderSystemInternalAccess::ReadbackLocalShadow(*Owner().Renderer(), *m_View->Surface(), layer));
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::PointLightComponent> m_Point;
        Keire::Ref<Keire::SpotLightComponent> m_Spot;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        std::uint32_t m_Frame = 0;
    };

    class LocalShadowAtlasEdgeCaptureLayer final : public Keire::Layer
    {
      public:
        LocalShadowAtlasEdgeCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                         std::shared_ptr<CaptureResults> results)
            : Layer("Local shadow atlas edge capture"), m_Mesh(mesh), m_Material(material),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000015"),
                                                     Keire::SceneAsset::EmptyDefinition("Local shadow atlas edge"),
                                                     Keire::ComponentRegistry::CreateDefault());

            constexpr Keire::Vector3 receiverPosition{4.008F, 0.0F, -4.0F};
            auto receiver = m_Scene->CreateEntity("Point face seam receiver");
            receiver.GetComponent<Keire::TransformComponent>()->SetLocalPosition(receiverPosition);
            receiver.GetComponent<Keire::TransformComponent>()->SetLocalScale({3.0F, 0.1F, 3.0F});
            const auto receiverRenderer = receiver.AddComponent<Keire::MeshRendererComponent>();
            receiverRenderer->SetMesh(m_Mesh);
            receiverRenderer->SetMaterial(m_Material);
            receiverRenderer->SetCastShadows(false);

            auto caster = m_Scene->CreateEntity("Opposite point face caster");
            m_CasterTransform = caster.GetComponent<Keire::TransformComponent>();
            m_CasterTransform->SetLocalPosition({-2.004F, 1.525F, -2.0F});
            m_CasterTransform->SetLocalScale({0.8F, 0.8F, 0.8F});
            m_Caster = caster.AddComponent<Keire::MeshRendererComponent>();
            m_Caster->SetMesh(m_Mesh);
            m_Caster->SetMaterial(m_Material);
            m_Caster->SetReceiveShadows(false);
            m_Caster->SetCastShadows(false);

            auto pointEntity = m_Scene->CreateEntity("Point face seam light");
            pointEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 3.0F, 0.0F});
            m_Point = pointEntity.AddComponent<Keire::PointLightComponent>();
            m_Point->SetIntensity(40.0F);
            m_Point->SetRange(15.0F);
            m_Point->SetShadowResolution(Keire::ShadowResolutionHint::Low);
            m_Point->SetShadows(Keire::ShadowQuality::Soft);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Local shadow atlas edge";
            surface.Width = LocalShadowEdgeSurfaceSize;
            surface.Height = LocalShadowEdgeSurfaceSize;
            surface.ClearColor = {0.02F, 0.02F, 0.02F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({receiverPosition.X, 8.0F, receiverPosition.Z},
                                              {receiverPosition.X, 0.05F, receiverPosition.Z}, {0.0F, 0.0F, -1.0F});
            camera.Projection = Keire::Math::Perspective(30.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Point.Reset();
            m_Caster.Reset();
            m_CasterTransform.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame == 120)
            {
                Capture();
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 144)
            {
                Capture();
                m_Results->ShadowDepth.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackLocalShadow(*Owner().Renderer(), *m_View->Surface(), 0));
                // Move the caster onto the receiver ray for a positive-control shadow capture.
                m_CasterTransform->SetLocalPosition({2.004F, 1.525F, -2.0F});
            }
            else if (m_Frame == 168)
            {
                Capture();
                Owner().RequestExit();
                return;
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.03F, 0.03F, 0.03F, 1.0F};
            environment.AmbientIntensity = 0.1F;
            environment.SkyVisible = false;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        void Capture()
        {
            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::PointLightComponent> m_Point;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        Keire::Ref<Keire::TransformComponent> m_CasterTransform;
        std::uint32_t m_Frame = 0;
    };

    struct ReloadCaptureResults final
    {
        std::vector<std::uint8_t> Green;
        std::vector<std::uint8_t> Red;
        std::vector<std::uint8_t> DimRed;
        std::vector<std::uint8_t> ShaderGreen;
        std::vector<std::uint8_t> AfterFailure;
        bool TextureReloadQueued = false;
        bool MaterialReloadQueued = false;
        bool ShaderReloadQueued = false;
        bool InvalidReloadQueued = false;
        std::uint64_t PenultimateFailureBuilds = 0;
        std::uint64_t SettledFailureBuilds = 0;
    };

    class AssetRevisionCaptureLayer final : public Keire::Layer
    {
      public:
        AssetRevisionCaptureLayer(RenderAssetFixture& fixture, std::shared_ptr<ReloadCaptureResults> results)
            : Layer("Asset revision capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000003"),
                                                     Keire::SceneAsset::EmptyDefinition("Texture reload tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Reloaded texture");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Fixture.Mesh);
            renderer->SetMaterial(m_Fixture.Material);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Texture reload tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto statistics = MeasureCenter(pixels);
                if (m_Stage == 0 && statistics.Green > statistics.Red + MinimumBehaviorDelta)
                {
                    m_Results->Green = pixels;
                    const auto red = RenderAssetFixture::SolidTexture(255, 0, 0);
                    m_Results->TextureReloadQueued = m_Fixture.ReplaceTexture(Owner(), red);
                    m_Stage = 1;
                }
                else if (m_Stage == 1 && statistics.Red > statistics.Green + MinimumBehaviorDelta)
                {
                    m_Results->Red = pixels;
                    m_Results->MaterialReloadQueued = m_Fixture.ReplaceMaterialTint(Owner(), {0.25F, 1.0F, 1.0F, 1.0F});
                    m_Stage = 2;
                }
                else if (m_Stage == 2 && statistics.Red > statistics.Green + MinimumBehaviorDelta &&
                         statistics.Red < MeasureCenter(m_Results->Red).Red - MinimumBehaviorDelta)
                {
                    m_Results->DimRed = pixels;
                    m_Results->ShaderReloadQueued = m_Fixture.ReplaceShaderOutputSwizzle(Owner());
                    m_Stage = 3;
                }
                else if (m_Stage == 3 && statistics.Green > statistics.Red + MinimumBehaviorDelta)
                {
                    m_Results->ShaderGreen = pixels;
                    constexpr std::array invalid{std::byte{0x4b}, std::byte{0x45}, std::byte{0x49}};
                    m_Results->InvalidReloadQueued = m_Fixture.ReplaceTexture(Owner(), invalid);
                    m_Stage = 4;
                }
                else if (m_Stage == 4 && ++m_FramesAfterFailure == 8)
                {
                    m_Results->AfterFailure = std::move(pixels);
                    m_Results->PenultimateFailureBuilds = m_PreviousFailureBuilds;
                    m_Results->SettledFailureBuilds =
                        Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer());
                    Owner().RequestExit();
                    return;
                }
                if (m_Stage == 4)
                {
                    m_PreviousFailureBuilds =
                        Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer());
                }
            }
            if (++m_FrameCount > 120)
            {
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        RenderAssetFixture& m_Fixture;
        std::shared_ptr<ReloadCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::size_t m_FrameCount = 0;
        std::size_t m_FramesAfterFailure = 0;
        std::uint64_t m_PreviousFailureBuilds = 0;
        int m_Stage = 0;
        bool m_Submitted = false;
    };
    struct MaterialSemanticResults final
    {
        std::array<std::vector<std::uint8_t>, 15> Frames;
        bool ReloadsSucceeded = true;
    };
    class MaterialSemanticCaptureLayer final : public Keire::Layer
    {
      public:
        MaterialSemanticCaptureLayer(RenderAssetFixture& fixture, std::shared_ptr<MaterialSemanticResults> results)
            : Layer("Material semantic capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000004"),
                                                     Keire::SceneAsset::EmptyDefinition("Material semantic tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("PBR triangle");
            m_ObjectTransform = object.GetComponent<Keire::TransformComponent>();
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Fixture.Mesh);
            renderer->SetMaterial(m_Fixture.Material);
            m_Material = Owner().Assets()->Load<Keire::MaterialAsset>(m_Fixture.Material);
            auto lightEntity = m_Scene->CreateEntity("Directional light");
            m_LightTransform = lightEntity.GetComponent<Keire::TransformComponent>();
            m_LightTransform->SetLocalEulerAngles({30.0F, 180.0F, 0.0F});
            m_Light = lightEntity.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetIntensity(4.0F);
            auto pointEntity = m_Scene->CreateEntity("Point light");
            m_PointTransform = pointEntity.GetComponent<Keire::TransformComponent>();
            m_PointTransform->SetLocalPosition({0.0F, 0.0F, 1.5F});
            m_PointLight = pointEntity.AddComponent<Keire::PointLightComponent>();
            m_PointLight->SetIntensity(8.0F);
            m_PointLight->SetRange(4.0F);
            m_PointLight->SetEnabled(false);
            auto spotEntity = m_Scene->CreateEntity("Spot light");
            m_SpotTransform = spotEntity.GetComponent<Keire::TransformComponent>();
            m_SpotTransform->SetLocalPosition({0.0F, 0.0F, 1.5F});
            m_SpotTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});
            m_SpotLight = spotEntity.AddComponent<Keire::SpotLightComponent>();
            m_SpotLight->SetIntensity(8.0F);
            m_SpotLight->SetRange(4.0F);
            m_SpotLight->SetConeAngles(20.0F, 35.0F);
            m_SpotLight->SetEnabled(false);
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Material semantic tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 0.05F;
        }
        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_LightTransform.Reset();
            m_PointLight.Reset();
            m_PointTransform.Reset();
            m_SpotLight.Reset();
            m_SpotTransform.Reset();
            m_ObjectTransform.Reset();
            m_Material = {};
            m_View.Reset();
            m_Scene.Reset();
        }
        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted && (m_Material.Revision() < m_ExpectedMaterialRevision ||
                                (m_Stage == 0 && Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(
                                                     *Owner().Renderer()) == 0)))
            {
                m_SettledFrames = 0;
                if (++m_ReloadWaitFrames > 120)
                {
                    m_Results->ReloadsSucceeded = false;
                    Owner().RequestExit();
                    return;
                }
            }
            else if (m_Submitted && ++m_SettledFrames >= 8)
            {
                m_ReloadWaitFrames = 0;
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto statistics = MeasureCenter(pixels);
                if (m_Stage == 0 && statistics.Green <= statistics.Red + MinimumBehaviorDelta)
                {
                    m_SettledFrames = 0;
                    if (++m_StartupWaits >= 15)
                    {
                        Owner().RequestExit();
                        return;
                    }
                    Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                    return;
                }
                m_Results->Frames[m_Stage] = std::move(pixels);
                if (m_Stage + 1 == m_Results->Frames.size())
                {
                    Owner().RequestExit();
                    return;
                }
                ++m_Stage;
                const auto previousRevision = m_Material.Revision();
                const bool reloadQueued = ConfigureStage(m_Stage);
                m_Results->ReloadsSucceeded = reloadQueued && m_Results->ReloadsSucceeded;
                m_ExpectedMaterialRevision = previousRevision + (reloadQueued ? 1U : 0U);
                m_SettledFrames = 0;
            }
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
            m_Submitted = true;
        }

      private:
        [[nodiscard]] std::string CommonProperties(const Keire::AssetId texture = {}) const
        {
            const auto baseColorTexture = texture ? texture : m_Fixture.Texture;
            return "\"Tint\":[1,1,1,1],\"MainTexture\":\"" + baseColorTexture.ToString() +
                   "\",\"MetallicFactor\":0,\"RoughnessFactor\":1,\"NormalScale\":1,"
                   "\"OcclusionStrength\":1,\"EmissiveFactor\":[1,1,1,1]";
        }
        [[nodiscard]] bool ConfigureStage(const std::size_t stage)
        {
            auto properties = CommonProperties();
            if (stage == 1)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.NeutralNormal.ToString() +
                              "\",\"MetallicRoughnessTexture\":\"" + m_Fixture.NeutralOrm.ToString() +
                              "\",\"OcclusionTexture\":\"" + m_Fixture.NeutralOrm.ToString() +
                              "\",\"EmissiveTexture\":\"" + m_Fixture.BlackEmissive.ToString() + "\"";
            }
            else if (stage == 2)
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
            else if (stage == 3)
            {
                properties += ",\"MetallicFactor\":1,\"MetallicRoughnessTexture\":\"" +
                              m_Fixture.MetallicSmoothOrm.ToString() + "\"";
            }
            else if (stage == 4)
            {
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.35F;
            }
            else if (stage == 5)
            {
                m_Light->SetEnabled(false);
                properties += ",\"OcclusionTexture\":\"" + m_Fixture.OccludedOrm.ToString() + "\"";
            }
            else if (stage == 6)
            {
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.0F;
                properties += ",\"EmissiveTexture\":\"" + m_Fixture.RedEmissive.ToString() + "\"";
            }
            else if (stage == 7)
            {
                m_Light->SetEnabled(true);
                m_Environment.AmbientIntensity = 0.05F;
                properties += ",\"MetallicFactor\":1,\"MetallicTexture\":\"" + m_Fixture.MetallicMap.ToString() +
                              "\",\"RoughnessTexture\":\"" + m_Fixture.RoughnessMap.ToString() + "\"";
            }
            else if (stage == 8)
            {
                m_Light->SetEnabled(true);
                m_Environment.AmbientIntensity = 0.05F;
                properties = CommonProperties(m_Fixture.TransparentTexture);
            }
            else if (stage == 9)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalScale({1.0F, 1.0F, 0.5F});
            }
            else if (stage == 10)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalEulerAngles({70.0F, 0.0F, 0.0F});
            }
            else if (stage == 11)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalEulerAngles({});
                m_ObjectTransform->SetLocalScale({-1.0F, 1.0F, 1.0F});
            }
            else if (stage == 12)
            {
                m_ObjectTransform->SetLocalScale({1.0F, 1.0F, 1.0F});
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.0F;
                m_PointLight->SetEnabled(true);
            }
            else if (stage == 13)
            {
                m_PointLight->SetEnabled(false);
                m_SpotLight->SetEnabled(true);
                m_SpotTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});
            }
            else if (stage == 14)
                m_SpotTransform->SetLocalEulerAngles({});
            return m_Fixture.ReplaceMaterialProperties(Owner(), properties);
        }
        RenderAssetFixture& m_Fixture;
        std::shared_ptr<MaterialSemanticResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::TransformComponent> m_LightTransform;
        Keire::Ref<Keire::TransformComponent> m_PointTransform;
        Keire::Ref<Keire::TransformComponent> m_SpotTransform;
        Keire::Ref<Keire::TransformComponent> m_ObjectTransform;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::Ref<Keire::PointLightComponent> m_PointLight;
        Keire::Ref<Keire::SpotLightComponent> m_SpotLight;
        Keire::AssetHandle<Keire::MaterialAsset> m_Material;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint64_t m_ExpectedMaterialRevision = 0;
        std::size_t m_Stage = 0;
        std::size_t m_SettledFrames = 0;
        std::size_t m_StartupWaits = 0;
        std::size_t m_ReloadWaitFrames = 0;
        bool m_Submitted = false;
    };

} // namespace
namespace KeireRenderTests
{
    bool ProbeRenderedOutput(std::string& diagnostic) noexcept
    {
        try
        {
            const auto results = std::make_shared<CaptureResults>();
            Keire::Application application(RenderTestSpecification());
            (void)application.PushLayer(std::make_unique<RenderCaptureLayer>(results));
            if (application.Run() != 0 || results->Frames.size() != CaptureSequence.size())
            {
                diagnostic = "capture sequence did not complete";
                return false;
            }
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
        catch (...)
        {
            diagnostic = "unknown render failure";
            return false;
        }
    }
} // namespace KeireRenderTests
TEST_CASE("rendered lighting output preserves observable color contracts")
{
    const auto results = std::make_shared<CaptureResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<RenderCaptureLayer>(results));
        REQUIRE(application.Run() == 0);
    }
    REQUIRE(results->Frames.size() == CaptureSequence.size());
    const auto defaultSky = MeasureSkyCorner(results->Frames.front());
    CHECK(defaultSky.Luminance() > MinimumBehaviorDelta);
    CHECK(defaultSky.Blue > defaultSky.Red + ColorTolerance);
    std::vector<PixelStatistics> captures;
    captures.reserve(results->Frames.size());
    for (const auto& pixels : results->Frames)
        captures.push_back(MeasureCenter(pixels));
    const auto at = [&captures](const CaptureKind kind) -> const PixelStatistics&
    {
        const auto found = std::ranges::find(CaptureSequence, kind);
        REQUIRE(found != CaptureSequence.end());
        return captures[static_cast<std::size_t>(std::distance(CaptureSequence.begin(), found))];
    };
    CHECK(at(CaptureKind::AmbientWhite).Luminance() > at(CaptureKind::AmbientZero).Luminance() + MinimumBehaviorDelta);
    CHECK(std::abs(at(CaptureKind::AmbientZero).Luminance() - at(CaptureKind::DirectionalDisabled).Luminance()) <=
          ColorTolerance);

    CHECK(at(CaptureKind::AmbientRed).Red > at(CaptureKind::AmbientRed).Green + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientRed).Red > at(CaptureKind::AmbientRed).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientGreen).Green > at(CaptureKind::AmbientGreen).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientGreen).Green > at(CaptureKind::AmbientGreen).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientBlue).Blue > at(CaptureKind::AmbientBlue).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientBlue).Blue > at(CaptureKind::AmbientBlue).Green + MinimumBehaviorDelta);

    CHECK(at(CaptureKind::DirectionalEnabled).Luminance() >
          at(CaptureKind::DirectionalDisabled).Luminance() + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::TintRed).Red > at(CaptureKind::TintRed).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::TintBlue).Blue > at(CaptureKind::TintBlue).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::ExposureHigh).Luminance() > at(CaptureKind::ExposureLow).Luminance() + MinimumBehaviorDelta);
    CHECK(std::abs(at(CaptureKind::NormalIdentity).Luminance() - at(CaptureKind::NormalTransformed).Luminance()) >
          ColorTolerance);
}

TEST_CASE("Deferred Hybrid Irradyn preserves observable rendered lighting contracts")
{
    const auto results = std::make_shared<CaptureResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<RenderCaptureLayer>(results, true));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->HasStatistics);
    CHECK(results->Capabilities.DeferredHybrid);
    CHECK(results->Capabilities.IrradynGlobalIllumination);
    REQUIRE(results->Frames.size() == CaptureSequence.size());

    std::vector<PixelStatistics> captures;
    captures.reserve(results->Frames.size());
    for (const auto& pixels : results->Frames)
        captures.push_back(MeasureCenter(pixels));
    const auto at = [&captures](const CaptureKind kind) -> const PixelStatistics&
    {
        const auto found = std::ranges::find(CaptureSequence, kind);
        REQUIRE(found != CaptureSequence.end());
        return captures[static_cast<std::size_t>(std::distance(CaptureSequence.begin(), found))];
    };

    CHECK(at(CaptureKind::AmbientWhite).Luminance() > at(CaptureKind::AmbientZero).Luminance() + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::DirectionalEnabled).Luminance() >
          at(CaptureKind::DirectionalDisabled).Luminance() + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::TintRed).Red > at(CaptureKind::TintRed).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::ExposureHigh).Luminance() > at(CaptureKind::ExposureLow).Luminance() + MinimumBehaviorDelta);
    CHECK(results->Statistics.ExecutedFrameGraphPasses == results->Statistics.PlannedFrameGraphPasses);
}

TEST_CASE("schema-4 Block order Blackboard overrides and Portable HLSL drive rendered GPU VFX particles")
{
    const auto results = std::make_shared<VfxGraphCaptureResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<VfxGraphGpuCaptureLayer>(results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 4);
    const auto afterSpawnRed = MeasureChannelSignal(results->Frames[0], 0);
    const auto afterSpawnGreen = MeasureChannelSignal(results->Frames[0], 1);
    const auto afterSimulatedRed = MeasureChannelSignal(results->Frames[1], 0);
    const auto afterSimulatedGreen = MeasureChannelSignal(results->Frames[1], 1);
    const auto beforeSpawnRed = MeasureChannelSignal(results->Frames[2], 0);
    const auto beforeSpawnGreen = MeasureChannelSignal(results->Frames[2], 1);
    const auto beforeSimulatedRed = MeasureChannelSignal(results->Frames[3], 0);
    const auto beforeSimulatedGreen = MeasureChannelSignal(results->Frames[3], 1);

    REQUIRE(afterSpawnRed.Weight > 10.0F);
    REQUIRE(beforeSpawnRed.Weight > 10.0F);
    CHECK(afterSpawnRed.Weight > afterSpawnGreen.Weight + 10.0F);
    CHECK(beforeSpawnRed.Weight > beforeSpawnGreen.Weight + 10.0F);

    REQUIRE(afterSimulatedGreen.Weight > 10.0F);
    REQUIRE(beforeSimulatedGreen.Weight > 10.0F);
    CHECK(afterSimulatedGreen.Weight > afterSimulatedRed.Weight + 10.0F);
    CHECK(beforeSimulatedGreen.Weight > beforeSimulatedRed.Weight + 10.0F);

    CHECK(std::abs(afterSimulatedGreen.CentroidX() - afterSpawnRed.CentroidX()) < 4.0F);
    CHECK(std::abs(beforeSimulatedGreen.CentroidX() - beforeSpawnRed.CentroidX()) > 20.0F);
    CHECK(std::abs(beforeSimulatedGreen.CentroidX() - afterSimulatedGreen.CentroidX()) > 20.0F);

    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.VfxGpuWorlds > 0);
    CHECK(results->Statistics.VfxPipelinesReady);
    CHECK(results->Statistics.VfxPipelineWarmupMilliseconds > 0.0F);
}
TEST_CASE("CPU and GPU textured material Sprite plus GPU Mesh VFX outputs survive render readback")
{
    RenderAssetFixture assets;
    SUBCASE("CPU and GPU textured material Sprite")
    {
        for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
        {
            CAPTURE(backend);
            const auto results = std::make_shared<VfxGraphCaptureResults>();
            auto specification = RenderTestSpecification();
            specification.Assets.Mode = Keire::AssetMode::Development;
            specification.Assets.DevelopmentCatalog = assets.Catalog;
            {
                Keire::Application application(std::move(specification));
                (void)application.PushLayer(std::make_unique<VfxResourceOutputCaptureLayer>(
                    backend,
                    RenderedResourceOutputEffect(Keire::VfxRendererType::Sprite, assets.Texture, assets.Material),
                    results));
                REQUIRE(application.Run() == 0);
            }
            REQUIRE(results->Frames.size() == 60);
            float greenDominance = 0.0F;
            for (const auto& frame : results->Frames)
            {
                const auto green = MeasureChannelSignal(frame, 1);
                const auto red = MeasureChannelSignal(frame, 0);
                greenDominance = std::max(greenDominance, green.Weight - red.Weight);
            }
            CHECK(greenDominance > 10.0F);
            REQUIRE(!results->FrameUploadSubmissions.empty());
            CHECK(results->FrameUploadSubmissions.back() == 0);
        }
    }
    SUBCASE("GPU Mesh")
    {
        const auto results = std::make_shared<VfxGraphCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<VfxResourceOutputCaptureLayer>(
                Keire::VfxBackend::Gpu,
                RenderedResourceOutputEffect(Keire::VfxRendererType::Mesh, assets.Mesh, assets.Material), results));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE(results->Frames.size() == 60);
        float greenDominance = 0.0F;
        for (const auto& frame : results->Frames)
        {
            const auto green = MeasureChannelSignal(frame, 1);
            const auto red = MeasureChannelSignal(frame, 0);
            greenDominance = std::max(greenDominance, green.Weight - red.Weight);
        }
        CHECK(greenDominance > 10.0F);
    }
}

TEST_CASE("CPU and GPU Ribbon and Volumetric VFX outputs survive render readback")
{
    RenderAssetFixture assets;
    for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
    {
        for (const auto renderer : {Keire::VfxRendererType::Ribbon, Keire::VfxRendererType::Volumetric})
        {
            CAPTURE(backend);
            CAPTURE(renderer);
            const auto results = std::make_shared<VfxGraphCaptureResults>();
            auto specification = RenderTestSpecification();
            specification.Assets.Mode = Keire::AssetMode::Development;
            specification.Assets.DevelopmentCatalog = assets.Catalog;
            {
                Keire::Application application(std::move(specification));
                (void)application.PushLayer(std::make_unique<VfxResourceOutputCaptureLayer>(
                    backend, RenderedResourceOutputEffect(renderer, {}, assets.Material), results, true));
                REQUIRE(application.Run() == 0);
            }

            REQUIRE(results->Frames.size() == 60);
            float maximumLuminance = 0.0F;
            float maximumGreenDominance = 0.0F;
            for (const auto& frame : results->Frames)
            {
                maximumLuminance = std::max(maximumLuminance, MeasureCenter(frame).Luminance());
                maximumGreenDominance = std::max(maximumGreenDominance, MeasureChannelSignal(frame, 1).Weight -
                                                                            MeasureChannelSignal(frame, 0).Weight);
            }
            CHECK(maximumLuminance > MinimumShadowDepthDelta);
            CHECK(maximumGreenDominance > 10.0F);
            REQUIRE(results->HasStatistics);
            if (backend == Keire::VfxBackend::Gpu)
                CHECK(results->Statistics.VfxGpuWorlds > 0);
        }
    }
}

TEST_CASE("GPU Mesh and Volume spawn shapes survive weighted-sampling render readback")
{
    RenderAssetFixture assets;
    for (const auto shape : {Keire::VfxShape::Mesh, Keire::VfxShape::Volume})
    {
        CAPTURE(shape);
        const auto results = std::make_shared<VfxGraphCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        const auto resource = shape == Keire::VfxShape::Mesh ? assets.Mesh : assets.Volume;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<VfxResourceOutputCaptureLayer>(
                Keire::VfxBackend::Gpu, RenderedShapeSamplingEffect(shape, resource), results));
            REQUIRE(application.Run() == 0);
        }

        REQUIRE(results->Frames.size() == 60);
        VfxChannelSignal strongestRed;
        for (const auto& frame : results->Frames)
        {
            const auto red = MeasureChannelSignal(frame, 0);
            if (red.Weight > strongestRed.Weight)
                strongestRed = red;
        }
        CHECK(strongestRed.Weight > 3.0F);
        if (shape == Keire::VfxShape::Volume)
            CHECK(strongestRed.CentroidX() < static_cast<float>(SurfaceSize) * 0.5F);
        REQUIRE(results->HasStatistics);
        CHECK(results->Statistics.VfxGpuWorlds > 0);
    }
}

TEST_CASE("GPU depth collision kills particles against sampled scene geometry")
{
    RenderAssetFixture assets;
    std::array<float, 2> lateRed{};
    for (const bool collisionEnabled : {false, true})
    {
        CAPTURE(collisionEnabled);
        const auto results = std::make_shared<VfxGraphCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<VfxResourceOutputCaptureLayer>(
                Keire::VfxBackend::Gpu, RenderedGpuDepthCollisionEffect(collisionEnabled), results, true, assets.Mesh,
                assets.Material, Keire::Vector3{0.0F, 0.0F, 0.75F}));
            REQUIRE(application.Run() == 0);
        }

        REQUIRE(results->Frames.size() == 60);
        float earlyRed = 0.0F;
        for (std::size_t index = 0; index < results->Frames.size(); ++index)
        {
            const auto red = MeasureChannelSignal(results->Frames[index], 0);
            if (index < 20)
            {
                earlyRed = std::max(earlyRed, red.Weight);
            }
            if (index >= 45)
            {
                const auto resultIndex = collisionEnabled ? 1U : 0U;
                lateRed[resultIndex] = std::max(lateRed[resultIndex], red.Weight);
            }
        }
        CHECK(earlyRed > 3.0F);
    }
    CHECK(lateRed[0] > 3.0F);
    CHECK(lateRed[1] < lateRed[0] * 0.2F);
}

TEST_CASE("large material scenes roll descriptor pressure across ordered command buffers")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<DescriptorRolloverResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<DescriptorRolloverCaptureLayer>(
            assets.Mesh, assets.Material, std::array{assets.Texture, assets.RedEmissive}, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frame.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
    CHECK(MeasureCenter(results->Frame).Luminance() > MinimumBehaviorDelta);
    CHECK(results->MaterialDependencyChecks == 1);
}

TEST_CASE("renderer replaces the deterministic error mesh with an asset-backed indexed mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<AssetMeshCaptureLayer>(assets.Mesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() >= 2);
    REQUIRE(results->Frames.size() <= 120);
    const auto first = MeasureCenter(results->Frames.front());
    const auto last = MeasureCenter(results->Frames.back());
    CHECK(first.Red > first.Green + MinimumBehaviorDelta);
    CHECK(first.Blue > first.Green + MinimumBehaviorDelta);
    CHECK(last.Green > last.Red + MinimumBehaviorDelta);
    CHECK(last.Green > last.Blue + MinimumBehaviorDelta);
    REQUIRE(results->MaterialBindingBuilds.size() == results->Frames.size());
    CHECK(results->MaterialBindingBuilds.back() == 1);
    CHECK(results->MaterialBindingBuilds[results->MaterialBindingBuilds.size() - 2] ==
          results->MaterialBindingBuilds.back());
    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.ExecutedFrameGraphPasses == results->Statistics.PlannedFrameGraphPasses);
    CHECK(results->Statistics.FrameGraphTransitions > 0);
    CHECK(results->Statistics.TransientResourceAllocations > 0);
    CHECK(results->Statistics.RendererQueueHighWaterMark > 0);
    // ABI-v3 spatial selection is draw-owned, so identical draws remain separate until selections become per-instance.
    CHECK(results->Statistics.InstanceBatches == 0);
    CHECK(results->Statistics.FrameUploadSubmissions == 0);
    CHECK(results->Statistics.AllowedFramesInFlight == 1);
    CHECK(results->Statistics.DrawPreparationMilliseconds > 0.0F);
    CHECK(results->Statistics.DepthPassMilliseconds >= 0.0F);
    CHECK(results->Statistics.CommandRecordingUnattributedMilliseconds >= 0.0F);
    CHECK(results->Statistics.DrawCalls < 25);
    CHECK(results->Statistics.CpuPreparationP95Milliseconds >= 0.0F);
    CHECK(results->Statistics.RendererLatencyMilliseconds >= 0.0F);
}

TEST_CASE("generated Shader Graph shaders create a graphics pipeline with a dense resource layout")
{
    RenderAssetFixture assets(true);
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<AssetMeshCaptureLayer>(Keire::MeshAsset::CubeId(), assets.ShaderGraphMaterial, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() >= 2);
    REQUIRE(results->Frames.size() <= 120);
    const auto last = MeasureCenter(results->Frames.back());
    CHECK(last.Green > last.Red + MinimumBehaviorDelta);
    CHECK(last.Green > last.Blue + MinimumBehaviorDelta);
}

TEST_CASE("procedural vertex displacement graphs create a graphics pipeline without phantom uniforms")
{
    for (const bool parameterDriven : {false, true})
    {
        CAPTURE(parameterDriven);
        RenderAssetFixture assets(true, true, parameterDriven);
        const auto results = std::make_shared<CaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<AssetMeshCaptureLayer>(
                assets.Mesh, assets.ShaderGraphMaterial, results, Keire::RenderSampleCount::Four));
            REQUIRE(application.Run() == 0);
        }

        REQUIRE(results->Frames.size() >= 2);
        REQUIRE(results->Frames.size() <= 120);
        const auto last = MeasureCenter(results->Frames.back());
        CHECK(last.Green > last.Red + MinimumBehaviorDelta);
        CHECK(last.Green > last.Blue + MinimumBehaviorDelta);
    }
}

TEST_CASE("skinned asset vertices follow bounded palette deformation")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    specification.Render.MaximumFramesInFlight = 3;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<SkinnedMeshCaptureLayer>(assets.Mesh, assets.Material,
                                                                              assets.Skeleton, assets.Skin, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 2);
    CHECK(GreenDominance(results->Frames[0], false) > GreenDominance(results->Frames[0], true) + MinimumBehaviorDelta);
    CHECK(GreenDominance(results->Frames[1], true) > GreenDominance(results->Frames[1], false) + MinimumBehaviorDelta);
    REQUIRE(results->SkinningStaticBuilds.size() >= 8);
    const auto firstStaticBuild =
        std::ranges::find_if(results->SkinningStaticBuilds, [](const std::uint64_t count) { return count != 0; });
    REQUIRE(firstStaticBuild != results->SkinningStaticBuilds.end());
    CHECK(std::ranges::all_of(firstStaticBuild, results->SkinningStaticBuilds.end(),
                              [](const std::uint64_t count) { return count == 1; }));
    REQUIRE(results->SkinningOutputBuilds.size() == results->SkinningStaticBuilds.size());
    CHECK(results->SkinningOutputBuilds.back() == 3);
    CHECK(results->SkinningOutputBuilds[results->SkinningOutputBuilds.size() - 2] ==
          results->SkinningOutputBuilds.back());
    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.AllowedFramesInFlight == 3);
    CHECK(std::ranges::all_of(results->SkinningPreparationMilliseconds, [](const float milliseconds)
                              { return std::isfinite(milliseconds) && milliseconds >= 0.0F; }));
    CHECK(results->Statistics.CommandRecordingUnattributedMilliseconds >= 0.0F);
}

TEST_CASE("directional shadow maps occlude a separate receiving mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<ShadowCaptureLayer>(assets.CubeMesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 3);
    const auto unshadowed = MeasureCenter(results->Frames[0]);
    const auto withoutCaster = MeasureCenter(results->Frames[1]);
    CHECK(std::abs(unshadowed.Red - withoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - withoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - withoutCaster.Blue) <= ColorTolerance);
    REQUIRE(results->ShadowDepth.size() == 2);
    CHECK(MaximumDifference(results->ShadowDepth[0], results->ShadowDepth[1]) >= MinimumShadowDepthDelta);
    CHECK(MaximumDarkening(results->Frames[1], results->Frames[2]) >= MinimumShadowDelta);
}

TEST_CASE("point and spot shadow maps occlude a separate receiving mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<LocalShadowCaptureLayer>(assets.CubeMesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 5);
    REQUIRE(results->ShadowDepth.size() == 4);
    CHECK(MaximumDifference(results->ShadowDepth[0], results->ShadowDepth[1]) >= MinimumShadowDepthDelta);
    CHECK(MaximumDifference(results->ShadowDepth[2], results->ShadowDepth[3]) >= MinimumShadowDepthDelta);
    const auto unshadowed = MeasureCenter(results->Frames[0]);
    const auto pointWithoutCaster = MeasureCenter(results->Frames[1]);
    const auto spotWithoutCaster = MeasureCenter(results->Frames[3]);
    CHECK(std::abs(unshadowed.Red - pointWithoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - pointWithoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - pointWithoutCaster.Blue) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Red - spotWithoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - spotWithoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - spotWithoutCaster.Blue) <= ColorTolerance);
    CHECK(MaximumDarkening(results->Frames[1], results->Frames[2]) >= MinimumShadowDelta);
    CHECK(MaximumDarkening(results->Frames[3], results->Frames[4]) >= MinimumShadowDelta);
}

TEST_CASE("soft point-shadow PCF does not bleed between adjacent atlas faces")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<LocalShadowAtlasEdgeCaptureLayer>(assets.CubeMesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 3);
    constexpr auto expectedBytes =
        static_cast<std::size_t>(LocalShadowEdgeSurfaceSize) * LocalShadowEdgeSurfaceSize * 4U;
    for (const auto& frame : results->Frames)
        REQUIRE(frame.size() == expectedBytes);

    const auto centerLuminance = [](const std::vector<std::uint8_t>& pixels)
    {
        constexpr auto center = LocalShadowEdgeSurfaceSize / 2U;
        const auto offset = (static_cast<std::size_t>(center) * LocalShadowEdgeSurfaceSize + center) * 4U;
        return (0.2126F * static_cast<float>(pixels[offset]) + 0.7152F * static_cast<float>(pixels[offset + 1]) +
                0.0722F * static_cast<float>(pixels[offset + 2])) /
               255.0F;
    };
    const auto unoccluded = centerLuminance(results->Frames[0]);
    const auto adjacentFaceCaster = centerLuminance(results->Frames[1]);
    const auto inFrontCaster = centerLuminance(results->Frames[2]);
    CHECK(unoccluded > MinimumBehaviorDelta);
    CHECK(unoccluded - adjacentFaceCaster <= 0.01F);
    CHECK(unoccluded > inFrontCaster + MinimumShadowDelta);

    REQUIRE(results->ShadowDepth.size() == 1);
    const auto& atlas = results->ShadowDepth.front();
    constexpr auto atlasSize = Keire::RenderBackend::LocalShadowResolution;
    REQUIRE(atlas.size() == static_cast<std::size_t>(atlasSize) * atlasSize);
    constexpr std::uint32_t faceSize = 256;
    constexpr auto guard = static_cast<std::uint32_t>(Keire::Detail::ShadowAtlasGuardTexels);
    bool adjacentFaceContainsCaster = false;
    bool adjacentFaceGuardIsClear = true;
    for (std::uint32_t y = guard; y < faceSize - guard; ++y)
    {
        for (std::uint32_t x = faceSize + guard; x < faceSize + guard + 8U; ++x)
            adjacentFaceContainsCaster |= atlas[static_cast<std::size_t>(y) * atlasSize + x] < 0.999F;
        for (std::uint32_t x = faceSize; x < faceSize + guard; ++x)
            adjacentFaceGuardIsClear &= atlas[static_cast<std::size_t>(y) * atlasSize + x] == doctest::Approx(1.0F);
    }
    CHECK(adjacentFaceContainsCaster);
    CHECK(adjacentFaceGuardIsClear);
}

TEST_CASE("PBR material semantics produce stable behavioral pixel deltas")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<MaterialSemanticResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<MaterialSemanticCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->ReloadsSucceeded);
    std::array<PixelStatistics, 15> captures;
    for (std::size_t index = 0; index < captures.size(); ++index)
    {
        REQUIRE_FALSE(results->Frames[index].empty());
        captures[index] = MeasureCenter(results->Frames[index]);
    }
    CHECK(std::abs(captures[0].Red - captures[1].Red) <= ColorTolerance);
    CHECK(std::abs(captures[0].Green - captures[1].Green) <= ColorTolerance);
    CHECK(std::abs(captures[0].Blue - captures[1].Blue) <= ColorTolerance);
    CHECK(std::abs(captures[1].Luminance() - captures[2].Luminance()) > MinimumNormalResponseDelta);
    CHECK(std::abs(captures[1].Luminance() - captures[3].Luminance()) > MinimumBehaviorDelta);
    CHECK(captures[4].Luminance() > captures[5].Luminance() + MinimumBehaviorDelta);
    CHECK(captures[6].Red > captures[6].Green + MinimumBehaviorDelta);
    CHECK(captures[6].Red > captures[6].Blue + MinimumBehaviorDelta);
    CHECK(std::abs(captures[3].Red - captures[7].Red) <= ColorTolerance);
    CHECK(std::abs(captures[3].Green - captures[7].Green) <= ColorTolerance);
    CHECK(std::abs(captures[3].Blue - captures[7].Blue) <= ColorTolerance);
    CHECK(captures[8].Alpha >= 1.0F - ColorTolerance);
    CHECK(std::abs(captures[2].Red - captures[9].Red) <= ColorTolerance);
    CHECK(std::abs(captures[2].Green - captures[9].Green) <= ColorTolerance);
    CHECK(std::abs(captures[2].Blue - captures[9].Blue) <= ColorTolerance);
    CHECK(std::abs(captures[9].Luminance() - captures[10].Luminance()) > MinimumBehaviorDelta);
    CHECK(std::abs(captures[2].Red - captures[11].Red) <= ColorTolerance);
    CHECK(std::abs(captures[2].Green - captures[11].Green) <= ColorTolerance);
    CHECK(std::abs(captures[2].Blue - captures[11].Blue) <= ColorTolerance);
    CHECK(captures[12].Luminance() > MinimumBehaviorDelta);
    CHECK(captures[13].Luminance() > captures[14].Luminance() + MinimumBehaviorDelta);
}

TEST_CASE("render asset revisions swap atomically and failed reloads preserve last-good output")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<ReloadCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<AssetRevisionCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->TextureReloadQueued);
    REQUIRE(results->MaterialReloadQueued);
    REQUIRE(results->ShaderReloadQueued);
    REQUIRE(results->InvalidReloadQueued);
    REQUIRE_FALSE(results->Green.empty());
    REQUIRE_FALSE(results->Red.empty());
    REQUIRE_FALSE(results->DimRed.empty());
    REQUIRE_FALSE(results->ShaderGreen.empty());
    REQUIRE_FALSE(results->AfterFailure.empty());
    const auto green = MeasureCenter(results->Green);
    const auto red = MeasureCenter(results->Red);
    const auto dimRed = MeasureCenter(results->DimRed);
    const auto shaderGreen = MeasureCenter(results->ShaderGreen);
    const auto afterFailure = MeasureCenter(results->AfterFailure);
    CHECK(green.Green > green.Red + MinimumBehaviorDelta);
    CHECK(red.Red > red.Green + MinimumBehaviorDelta);
    CHECK(dimRed.Red < red.Red - MinimumBehaviorDelta);
    CHECK(shaderGreen.Green > shaderGreen.Red + MinimumBehaviorDelta);
    CHECK(std::abs(afterFailure.Red - shaderGreen.Red) <= ColorTolerance);
    CHECK(std::abs(afterFailure.Green - shaderGreen.Green) <= ColorTolerance);
    CHECK(std::abs(afterFailure.Blue - shaderGreen.Blue) <= ColorTolerance);
    CHECK(results->PenultimateFailureBuilds == results->SettledFailureBuilds);
}
