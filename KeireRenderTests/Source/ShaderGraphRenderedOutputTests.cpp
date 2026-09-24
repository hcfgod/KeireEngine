#include "Keire/Application.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;

    [[nodiscard]] std::array<int, 3> MaximumChannelDominance(const std::vector<std::uint8_t>& pixels)
    {
        std::array<int, 3> result{-255, -255, -255};
        for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
            for (std::size_t channel = 0; channel < result.size(); ++channel)
                result[channel] = std::max(result[channel], static_cast<int>(pixels[offset + channel]) -
                                                                std::max(pixels[offset + (channel + 1) % 3],
                                                                         pixels[offset + (channel + 2) % 3]));
        return result;
    }

    [[nodiscard]] bool ContainsDominantChannel(const std::vector<std::uint8_t>& pixels, const std::size_t channel)
    {
        constexpr std::uint8_t minimumDelta = 24;
        for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
        {
            const auto primary = pixels[offset + channel];
            const auto firstOther = pixels[offset + (channel + 1) % 3];
            const auto secondOther = pixels[offset + (channel + 2) % 3];
            if (primary > firstOther + minimumDelta && primary > secondOther + minimumDelta)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool ContainsCyan(const std::vector<std::uint8_t>& pixels)
    {
        constexpr std::uint8_t minimumChannel = 128;
        constexpr std::uint8_t minimumRedDelta = 64;
        for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
        {
            const auto red = pixels[offset];
            const auto green = pixels[offset + 1];
            const auto blue = pixels[offset + 2];
            if (green > minimumChannel && blue > minimumChannel && green > red + minimumRedDelta &&
                blue > red + minimumRedDelta)
                return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<std::byte> ReadAssetBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Could not open render-test asset: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        std::vector<std::byte> result(characters.size());
        std::ranges::transform(characters, result.begin(), [](const char value) { return std::byte(value); });
        return result;
    }

    [[nodiscard]] Keire::ApplicationSpecification RenderTestSpecification()
    {
        const char* backend = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_TEST_BACKEND");
        if (backend && *backend && !SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            throw std::runtime_error("Could not restore the requested GPU backend after SDL shutdown.");
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire live Shader Graph render tests";
        specification.MainWindow.Width = SurfaceSize;
        specification.MainWindow.Height = SurfaceSize;
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
        specification.Render.EnableGpuValidation =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_VALIDATION") != nullptr;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        specification.Input.Mode = Keire::InputMode::Disabled;
        specification.Scenes.Mode = Keire::SceneMode::Disabled;
        specification.ManageLogging = false;
        specification.SuspendWhenMainWindowMinimized = false;
        return specification;
    }

    [[nodiscard]] Keire::RenderEnvironmentSettings ShaderBindingTestEnvironment()
    {
        Keire::RenderEnvironmentSettings environment;
        // Keep binding colors above the low-light tone-mapping region after PBR diffuse normalization.
        environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
        environment.AmbientIntensity = 1.0F;
        environment.SkyVisible = false;
        return environment;
    }

    class LiveShaderGraphFixture final
    {
      public:
        explicit LiveShaderGraphFixture(const Keire::ShaderGraphTarget target = Keire::ShaderGraphTarget::Material,
                                        const std::optional<Keire::ShaderGraphTemplate> preset = {})
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-LiveShaderGraphTests-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
              Target(target)
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto shaderImporter = Keire::CreateShaderAssetImporter();
            const auto materialImporter = Keire::CreateMaterialAssetImporter();
            const auto graphImporter = Keire::CreateShaderGraphAssetImporter();
            Database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
                .ProjectRoot = Root,
                .Importers =
                    std::vector<Keire::AssetImporterRegistration>{shaderImporter, materialImporter, graphImporter}});

            auto graph = preset ? Keire::CreateShaderGraphTemplate(*preset) : Keire::CreateTargetShaderGraph(Target);
            if (!preset)
            {
                auto parameter = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter,
                                                              Keire::ShaderGraphValueType::Color);
                parameter.Name = "BaseColor";
                parameter.Symbol = "BaseColor";
                parameter.Value = Keire::Color{0.0F, 1.0F, 0.0F, 1.0F};
                graph.Nodes.push_back(std::move(parameter));
                const auto output = std::ranges::find(graph.Nodes.back().Pins, "Value", &Keire::ShaderGraphPin::Name);
                const auto input =
                    std::ranges::find(graph.Nodes.front().Pins, "BaseColor", &Keire::ShaderGraphPin::Name);
                if (output == graph.Nodes.back().Pins.end() || input == graph.Nodes.front().Pins.end())
                    throw std::logic_error("The default Shader Graph does not expose a BaseColor input.");
                graph.Connections.push_back({Keire::AssetId::Generate(),
                                             {graph.Nodes.back().Id, output->Id},
                                             {graph.Nodes.front().Id, input->Id}});
            }
            Graph = Database->CreateAsset("Live.keireshadergraph", graphImporter,
                                          Keire::ShaderGraphAsset::EncodeSource(graph));
            const auto record = Database->Find(Graph);
            if (!record || record->SubAssets.empty())
                throw std::runtime_error("The Shader Graph import did not publish shader and material subassets.");
            Shader = record->SubAssets.front();
            if (Target == Keire::ShaderGraphTarget::Material)
                Material = record->SubAssets.back();
            else
            {
                Keire::MaterialAssetDefinition material;
                material.Shader = Shader;
                material.Surface.AlphaMode = Keire::MaterialAlphaMode::Blend;
                Material = Database->CreateAsset("Live.keiremateriallegacy", materialImporter,
                                                 Keire::MaterialAsset::EncodeSource(material));
            }
            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        }

        ~LiveShaderGraphFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        [[nodiscard]] bool PublishRedRevision(Keire::Application& application) const
        {
            auto graph = Keire::CreateTargetShaderGraph(Target);
            auto parameter =
                Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
            parameter.Name = "BaseColor";
            parameter.Symbol = "BaseColor";
            parameter.Value = Keire::Color{1.0F, 0.0F, 0.0F, 1.0F};
            graph.Nodes.push_back(std::move(parameter));
            const auto output = std::ranges::find(graph.Nodes.back().Pins, "Value", &Keire::ShaderGraphPin::Name);
            const auto input = std::ranges::find(graph.Nodes.front().Pins, "BaseColor", &Keire::ShaderGraphPin::Name);
            if (output == graph.Nodes.back().Pins.end() || input == graph.Nodes.front().Pins.end())
                return false;
            graph.Connections.push_back(
                {Keire::AssetId::Generate(), {graph.Nodes.back().Id, output->Id}, {graph.Nodes.front().Id, input->Id}});

            Keire::ShaderGraphCompileOptions options;
            options.GeneratedSource = "Assets/Generated/ShaderGraphLive.hlsl";
            const auto compilation = Keire::CompileShaderGraph(graph, options);
            if (!compilation.Succeeded() || compilation.Variants.size() != 1)
                return false;

            Keire::ShaderImporterSpecification importerSpecification;
#if defined(_WIN32)
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV};
#elif defined(__APPLE__)
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl};
#else
            importerSpecification.Formats = {Keire::ShaderBinaryFormat::SpirV};
#endif
            const auto importer = Keire::CreateShaderAssetImporter(std::move(importerSpecification));
            if (!importer.ContextualImport)
                return false;
            const auto& variant = compilation.Variants.front();
            Keire::AssetImportContext context;
            context.Asset = Keire::AssetId::Generate();
            context.ProjectRoot = std::filesystem::current_path();
            context.SourceRoot = context.ProjectRoot / "Assets";
            context.RelativePath = variant.GeneratedSource;
            context.RelativePath.replace_extension(".keireshader");
            context.SourcePath = context.ProjectRoot / context.RelativePath;
            context.MetadataPath = context.SourcePath;
            context.MetadataPath += ".keiremeta";
            context.ReadProjectFile = [generatedSource = variant.GeneratedSource,
                                       generatedBytes = std::vector<std::byte>(
                                           std::as_bytes(std::span(variant.Hlsl.data(), variant.Hlsl.size())).begin(),
                                           std::as_bytes(std::span(variant.Hlsl.data(), variant.Hlsl.size())).end())](
                                          const std::filesystem::path& requested)
            {
                if (requested.lexically_normal() == generatedSource.lexically_normal())
                    return generatedBytes;
                throw std::runtime_error("A live Shader Graph shader dependency is unavailable.");
            };
            const auto manifest = std::as_bytes(std::span(variant.Manifest.data(), variant.Manifest.size()));
            const auto imported = importer.ContextualImport(context, manifest);
            const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
            const auto assets = application.Assets();
            if (!assets || !shader || !assets->PublishDevelopmentAsset(Shader, shader))
                return false;

            Keire::ShaderGraphInstanceDefinition defaults;
            defaults.Parent = Graph;
            const std::array ancestry{defaults};
            const auto resolved = Keire::ResolveShaderGraphInstance(graph, ancestry);
            const auto material = Keire::BakeShaderGraphInstance(
                graph, resolved, [shaderAsset = Shader](const std::span<const std::string>) { return shaderAsset; });
            return assets->PublishDevelopmentAsset(Material, Keire::CreateRef<Keire::MaterialAsset>(material));
        }

        std::filesystem::path Root;
        std::filesystem::path Catalog;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetId Graph;
        Keire::AssetId Shader;
        Keire::AssetId Material;
        Keire::ShaderGraphTarget Target;
    };

    struct LiveShaderGraphResults final
    {
        std::vector<std::uint8_t> Initial;
        std::vector<std::uint8_t> Revised;
        std::vector<std::uint8_t> LastFrame;
        bool RevisionPublished = false;
    };

    struct FullscreenResults final
    {
        std::vector<std::uint8_t> Baseline;
        std::vector<std::uint8_t> Effect;
        std::vector<std::uint8_t> Cleared;
    };

    class FullscreenCaptureLayer final : public Keire::Layer
    {
      public:
        FullscreenCaptureLayer(const Keire::AssetId material, const Keire::RenderPath path,
                               std::shared_ptr<FullscreenResults> results)
            : Layer("Fullscreen spatial effects"), m_Material(material), m_Path(path), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Fullscreen spatial effects"),
                                                     Keire::ComponentRegistry::CreateDefault());
            for (int index = 0; index < 3; ++index)
            {
                auto entity = m_Scene->CreateEntity("White geometry");
                entity.GetComponent<Keire::TransformComponent>()->SetLocalPosition(
                    {static_cast<float>(index - 1) * 0.85F, index == 1 ? 0.4F : -0.3F, 0});
                entity.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.55F, 0.55F, 0.55F});
                auto mesh = entity.AddComponent<Keire::MeshRendererComponent>();
                mesh->SetMesh(Keire::MeshAsset::CubeId());
                mesh->SetTint({1, 1, 1, 1});
            }
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Fullscreen spatial effects";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            m_View = Owner().Renderer()->CreateView(surface);
            m_Camera.View = Keire::Math::LookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
            m_Camera.Projection = Keire::Math::Perspective(55, 1, 0.1F, 100);
            m_Camera.ClearColor = {0, 0, 0, 1};
            m_View->SetCamera(m_Camera);
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
            if (++m_Frames > 200)
            {
                Owner().RequestExit();
                return;
            }
            if (m_Frames > 4)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                if (!pixels.empty() && m_Results->Baseline.empty())
                {
                    m_Results->Baseline = pixels;
                    m_Camera.FullscreenEffects[1] = m_Material;
                    m_View->SetCamera(m_Camera);
                }
                else if (m_Results->Effect.empty() && pixels.size() == m_Results->Baseline.size() &&
                         pixels != m_Results->Baseline)
                {
                    std::size_t changed = 0;
                    for (std::size_t index = 0; index < pixels.size(); ++index)
                        changed += std::abs(static_cast<int>(pixels[index]) - m_Results->Baseline[index]) > 8;
                    if (changed > 60)
                    {
                        m_Results->Effect = pixels;
                        m_Camera.FullscreenEffects[1] = {};
                        m_View->SetCamera(m_Camera);
                        m_ClearFrame = m_Frames;
                    }
                }
                else if (!m_Results->Effect.empty() && m_Frames > m_ClearFrame + 2)
                {
                    m_Results->Cleared = std::move(pixels);
                    Owner().RequestExit();
                    return;
                }
            }
            auto environment = ShaderBindingTestEnvironment();
            environment.RequestedRenderPath = m_Path;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            if (m_Frames == 1)
                REQUIRE(Keire::ResolveRenderFeatureSelection(environment, Owner().Renderer()->FeatureCapabilities())
                            .EffectivePath == m_Path);
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
        }

      private:
        Keire::AssetId m_Material;
        Keire::RenderPath m_Path;
        std::shared_ptr<FullscreenResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::RenderCamera m_Camera;
        unsigned m_Frames = 0;
        unsigned m_ClearFrame = 0;
    };

    class LiveShaderGraphCaptureLayer final : public Keire::Layer
    {
      public:
        LiveShaderGraphCaptureLayer(LiveShaderGraphFixture& fixture, std::shared_ptr<LiveShaderGraphResults> results)
            : Layer("Live Shader Graph capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000007"),
                                                     Keire::SceneAsset::EmptyDefinition("Live Shader Graph tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Live Shader Graph mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(Keire::MeshAsset::CubeId());
            renderer->SetMaterial(m_Fixture.Material);
            renderer->SetTint({0.25F, 0.55F, 1.0F, 1.0F});
            object.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 2.7364445F, 7.5536985F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Live Shader Graph tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({-9.550004F, 9.944860F, 8.224380F}, {-6.672211F, 7.929247F, 7.611357F},
                                              {0.0F, 1.0F, 0.0F});
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
                if (m_Stage == 0 && ContainsDominantChannel(pixels, 1))
                {
                    m_Results->Initial = pixels;
                    m_Results->RevisionPublished = m_Fixture.PublishRedRevision(Owner());
                    m_Stage = 1;
                }
                else if (m_Stage == 1 && ContainsDominantChannel(pixels, 0))
                {
                    m_Results->Revised = std::move(pixels);
                    Owner().RequestExit();
                    return;
                }
                if (!pixels.empty())
                    m_Results->LastFrame = std::move(pixels);
            }
            if (++m_FrameCount > 120)
            {
                Owner().RequestExit();
                return;
            }
            Owner().Renderer()->Submit({m_Scene, m_View, false, ShaderBindingTestEnvironment()});
            m_Submitted = true;
        }

      private:
        LiveShaderGraphFixture& m_Fixture;
        std::shared_ptr<LiveShaderGraphResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_FrameCount = 0;
        std::uint32_t m_Stage = 0;
        bool m_Submitted = false;
    };

    class VfxShaderCaptureLayer final : public Keire::Layer
    {
      public:
        VfxShaderCaptureLayer(LiveShaderGraphFixture& fixture, std::shared_ptr<LiveShaderGraphResults> results,
                              Keire::VfxBackend backend, Keire::VfxRendererType renderer)
            : Layer("Authored particle shader"), m_Fixture(fixture), m_Results(std::move(results)), m_Backend(backend),
              m_Renderer(renderer)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Authored particle shader"),
                                                     Keire::ComponentRegistry::CreateDefault());
            Keire::RenderSurfaceSpecification surface;
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0, 0, 0, 1};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.8F, 0.4F, 2.5F}, {}, {0, 1, 0});
            camera.Projection = Keire::Math::Perspective(55, 1, 0.1F, 100);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            Keire::VfxEffectDefinition effect;
            effect.EmitterId = Keire::AssetId::Generate();
            effect.Name = "Authored particle shader";
            effect.Duration = 10;
            effect.Capacity = 16;
            effect.Modules = {
                {Keire::AssetId::Generate(), true, Keire::VfxBurstModule{0, 8, 1, 0.1F}},
                {Keire::AssetId::Generate(), true, Keire::VfxShapeModule{Keire::VfxShape::Box}},
                {Keire::AssetId::Generate(), true, Keire::VfxInitializeModule{10, 10}},
                {Keire::AssetId::Generate(), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.5F)}},
                {Keire::AssetId::Generate(), true,
                 Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1, 1, 1, 1})}},
                {Keire::AssetId::Generate(), true, Keire::VfxRendererModule{m_Renderer, {}, {}, m_Fixture.Material}}};
            effect = Keire::ConvertVfxEffectToGraph(effect);
            if (m_Renderer == Keire::VfxRendererType::Ribbon)
            {
                effect.Systems.front().DataType = Keire::VfxParticleDataType::ParticleStrip;
                effect.Systems.front().ParticlesPerStrip = 8;
            }
            m_World = Keire::CreateRef<Keire::VfxWorld>(
                Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 16, .Backend = m_Backend});
            if (!m_World->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(std::move(effect))}))
                throw std::runtime_error("Could not activate authored VFX fixture.");
            m_World->Update(0.01F);
        }

        void OnDetach() noexcept override
        {
            m_World.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frames != 0)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                if (m_Results->Initial.empty() && ContainsDominantChannel(pixels, 1))
                {
                    m_Results->Initial = pixels;
                    m_Results->RevisionPublished = m_Fixture.PublishRedRevision(Owner());
                }
                else if (m_Results->RevisionPublished && ContainsDominantChannel(pixels, 0))
                {
                    m_Results->Revised = pixels;
                    Owner().RequestExit();
                }
                m_Results->LastFrame = std::move(pixels);
            }
            if (++m_Frames > 120)
            {
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.SkyVisible = false;
            environment.AmbientColor = {1, 1, 1, 1};
            environment.AmbientIntensity = 1;
            m_World->Update(1.0F / 60.0F);
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment, {}, m_World->CaptureRenderSnapshot()});
        }

      private:
        LiveShaderGraphFixture& m_Fixture;
        std::shared_ptr<LiveShaderGraphResults> m_Results;
        Keire::VfxBackend m_Backend;
        Keire::VfxRendererType m_Renderer;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_World;
        std::uint32_t m_Frames = 0;
    };

    class MaterialPropertyBlockCaptureLayer final : public Keire::Layer
    {
      public:
        MaterialPropertyBlockCaptureLayer(const Keire::AssetId material,
                                          std::shared_ptr<std::vector<std::uint8_t>> pixels)
            : Layer("Material property block capture"), m_Material(material), m_Pixels(std::move(pixels))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Material property block"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Overridden surface");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(Keire::MeshAsset::CubeId());
            renderer->SetMaterial(m_Material);
            renderer->SetMaterialProperty("BaseColor", Keire::Color{1.0F, 0.0F, 0.0F, 1.0F});
            object.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 2.7364445F, 7.5536985F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Material property block";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({-9.550004F, 9.944860F, 8.224380F}, {-6.672211F, 7.929247F, 7.611357F},
                                              {0.0F, 1.0F, 0.0F});
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
                if (!pixels.empty())
                    *m_Pixels = pixels;
                if (ContainsDominantChannel(pixels, 0))
                {
                    *m_Pixels = std::move(pixels);
                    Owner().RequestExit();
                    return;
                }
            }
            if (++m_FrameCount > 120)
            {
                Owner().RequestExit();
                return;
            }
            Owner().Renderer()->Submit({m_Scene, m_View, false, ShaderBindingTestEnvironment()});
            m_Submitted = true;
        }

      private:
        Keire::AssetId m_Material;
        std::shared_ptr<std::vector<std::uint8_t>> m_Pixels;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_FrameCount = 0;
        bool m_Submitted = false;
    };

    class SandboxMaterialGraphFixture final
    {
      public:
        SandboxMaterialGraphFixture()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-SandboxMaterialGraphTests-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto shaderImporter = Keire::CreateShaderAssetImporter();
            const auto materialImporter = Keire::CreateMaterialAssetImporter();
            const auto shaderGraphImporter = Keire::CreateShaderGraphAssetImporter();
            const auto materialGraphImporter = Keire::CreateMaterialGraphAssetImporter();
            Database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
                .ProjectRoot = Root,
                .Importers = std::vector<Keire::AssetImporterRegistration>{
                    shaderImporter, materialImporter, shaderGraphImporter, materialGraphImporter}});

            const auto repository = std::filesystem::current_path();
            const auto shaderSource = repository /
                                      "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/"
                                      "SG_03_NeonPulse.keireshadergraph";
            const auto materialSource =
                repository / "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/"
                             "MG_03_NeonPulse.keirematerial";
            Graph = Database->CreateAsset("Graphs/NeonPulse.keireshadergraph", shaderGraphImporter,
                                          ReadAssetBytes(shaderSource));
            auto definition = Keire::MaterialGraphAsset::DecodeSource(ReadAssetBytes(materialSource));
            definition.Shader.Asset = Graph;
            MaterialGraph = Database->CreateAsset("Materials/NeonPulse.keirematerial", materialGraphImporter,
                                                  Keire::MaterialGraphAsset::EncodeSource(definition));
            const auto record = Database->Find(MaterialGraph);
            if (!record || record->SubAssets.size() < 2)
                throw std::runtime_error("The Sandbox Material Graph did not publish shader and material subassets.");
            Material = record->SubAssets.back();
            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        }

        ~SandboxMaterialGraphFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        std::filesystem::path Root;
        std::filesystem::path Catalog;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetId Graph;
        Keire::AssetId MaterialGraph;
        Keire::AssetId Material;
    };

    class SandboxMaterialGraphCaptureLayer final : public Keire::Layer
    {
      public:
        SandboxMaterialGraphCaptureLayer(const Keire::AssetId material,
                                         std::shared_ptr<std::vector<std::uint8_t>> pixels)
            : Layer("Sandbox Material Graph capture"), m_Material(material), m_Pixels(std::move(pixels))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Sandbox Material Graph"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Neon Pulse material");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(Keire::MeshAsset::CubeId());
            renderer->SetMaterial(m_Material);
            object.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 2.7364445F, 7.5536985F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Sandbox Material Graph";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({-9.550004F, 9.944860F, 8.224380F}, {-6.672211F, 7.929247F, 7.611357F},
                                              {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
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
                if (!pixels.empty())
                    *m_Pixels = pixels;
                if (ContainsCyan(pixels))
                {
                    *m_Pixels = std::move(pixels);
                    Owner().RequestExit();
                    return;
                }
            }
            if (++m_FrameCount > 120)
            {
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.08F, 0.09F, 0.12F, 1.0F};
            environment.AmbientIntensity = 0.45F;
            environment.SkyVisible = false;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        Keire::AssetId m_Material;
        std::shared_ptr<std::vector<std::uint8_t>> m_Pixels;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_FrameCount = 0;
        bool m_Submitted = false;
    };
} // namespace

TEST_CASE("live Shader Graph shader and parameter revisions update assigned scene meshes")
{
    LiveShaderGraphFixture assets;
    const auto results = std::make_shared<LiveShaderGraphResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<LiveShaderGraphCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    const auto dominance = MaximumChannelDominance(results->LastFrame);
    INFO("Last frame channel dominance R/G/B: ", dominance[0], "/", dominance[1], "/", dominance[2]);
    CHECK(results->RevisionPublished);
    REQUIRE_FALSE(results->Initial.empty());
    REQUIRE_FALSE(results->Revised.empty());
    CHECK(ContainsDominantChannel(results->Initial, 1));
    CHECK(ContainsDominantChannel(results->Revised, 0));
}

TEST_CASE("authored VFX shaders render and hot reload on CPU and GPU billboards and ribbons")
{
    LiveShaderGraphFixture assets(Keire::ShaderGraphTarget::Vfx);
    for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
        for (const auto renderer : {Keire::VfxRendererType::Sprite, Keire::VfxRendererType::Ribbon})
        {
            CAPTURE(backend);
            CAPTURE(renderer);
            const auto results = std::make_shared<LiveShaderGraphResults>();
            auto specification = RenderTestSpecification();
            specification.Assets.Mode = Keire::AssetMode::Development;
            specification.Assets.DevelopmentCatalog = assets.Catalog;
            {
                Keire::Application application(std::move(specification));
                (void)application.PushLayer(
                    std::make_unique<VfxShaderCaptureLayer>(assets, results, backend, renderer));
                REQUIRE(application.Run() == 0);
            }
            const auto dominance = MaximumChannelDominance(results->LastFrame);
            INFO("Last frame channel dominance: ", dominance[0], "/", dominance[1], "/", dominance[2]);
            CHECK(results->RevisionPublished);
            CHECK_FALSE(results->Initial.empty());
            CHECK_FALSE(results->Revised.empty());
        }
}

TEST_CASE("fullscreen spatial presets modify camera images and clearing restores both render paths")
{
    for (const auto preset :
         {Keire::ShaderGraphTemplate::FullscreenBlur, Keire::ShaderGraphTemplate::FullscreenChromaticAberration,
          Keire::ShaderGraphTemplate::FullscreenDistortion, Keire::ShaderGraphTemplate::FullscreenVignette})
    {
        CAPTURE(preset);
        LiveShaderGraphFixture assets(Keire::ShaderGraphTarget::Fullscreen, preset);
        for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
        {
            CAPTURE(path);
            const auto results = std::make_shared<FullscreenResults>();
            auto specification = RenderTestSpecification();
            specification.Assets.Mode = Keire::AssetMode::Development;
            specification.Assets.DevelopmentCatalog = assets.Catalog;
            {
                Keire::Application application(std::move(specification));
                (void)application.PushLayer(std::make_unique<FullscreenCaptureLayer>(assets.Material, path, results));
                REQUIRE(application.Run() == 0);
            }
            REQUIRE(results->Baseline.size() == SurfaceSize * SurfaceSize * 4U);
            REQUIRE(results->Effect.size() == results->Baseline.size());
            CHECK((results->Effect != results->Baseline));
            CHECK((results->Cleared == results->Baseline));
            std::uint64_t baselineEnergy = 0;
            std::uint64_t effectEnergy = 0;
            for (std::size_t index = 0; index < results->Baseline.size(); ++index)
                if (index % 4 != 3)
                {
                    baselineEnergy += results->Baseline[index];
                    effectEnergy += results->Effect[index];
                }
            CHECK(effectEnergy > baselineEnergy / 4);
            CHECK(effectEnergy < baselineEnergy * 2);
        }
    }
}

TEST_CASE("per-renderer material property blocks reach Shader Graph GPU bindings")
{
    LiveShaderGraphFixture assets;
    const auto pixels = std::make_shared<std::vector<std::uint8_t>>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<MaterialPropertyBlockCaptureLayer>(assets.Material, pixels));
        REQUIRE(application.Run() == 0);
    }

    const auto dominance = MaximumChannelDominance(*pixels);
    INFO("Last frame channel dominance R/G/B: ", dominance[0], "/", dominance[1], "/", dominance[2]);
    REQUIRE_FALSE(pixels->empty());
    CHECK(ContainsDominantChannel(*pixels, 0));
}

TEST_CASE("Sandbox unlit Material Graph creates a native GPU pipeline")
{
    SandboxMaterialGraphFixture assets;
    const auto pixels = std::make_shared<std::vector<std::uint8_t>>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<SandboxMaterialGraphCaptureLayer>(assets.Material, pixels));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(pixels->empty());
    CHECK(ContainsCyan(*pixels));
}
