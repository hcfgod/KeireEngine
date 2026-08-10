#include "Keire/Application.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;

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

    [[nodiscard]] Keire::ApplicationSpecification RenderTestSpecification()
    {
        const char* backend = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_TEST_BACKEND");
        if (backend && *backend && !SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            throw std::runtime_error("Could not restore the requested GPU backend after SDL shutdown.");
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire live Material Graph render tests";
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

    class LiveMaterialGraphFixture final
    {
      public:
        LiveMaterialGraphFixture()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-LiveMaterialGraphTests-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto shaderImporter = Keire::CreateShaderAssetImporter();
            const auto materialImporter = Keire::CreateMaterialAssetImporter();
            const auto graphImporter = Keire::CreateMaterialGraphAssetImporter();
            Database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
                .ProjectRoot = Root,
                .Importers =
                    std::vector<Keire::AssetImporterRegistration>{shaderImporter, materialImporter, graphImporter}});

            auto graph = Keire::CreateDefaultMaterialGraph();
            const auto baseColor =
                std::ranges::find(graph.Nodes.front().Pins, "BaseColor", &Keire::MaterialGraphPin::Name);
            if (baseColor == graph.Nodes.front().Pins.end())
                throw std::logic_error("The default Material Graph does not expose a BaseColor input.");
            baseColor->DefaultValue = Keire::Color{0.0F, 1.0F, 0.0F, 1.0F};
            Graph = Database->CreateAsset("Live.keirematerialgraph", graphImporter,
                                          Keire::MaterialGraphAsset::EncodeSource(graph));
            const auto record = Database->Find(Graph);
            if (!record || record->SubAssets.size() < 2)
                throw std::runtime_error("The Material Graph import did not publish shader and material subassets.");
            Shader = record->SubAssets.front();
            Material = record->SubAssets.back();
            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        }

        ~LiveMaterialGraphFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        [[nodiscard]] bool PublishRedRevision(Keire::Application& application) const
        {
            auto graph = Keire::CreateDefaultMaterialGraph();
            auto parameter = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Parameter,
                                                            Keire::MaterialGraphValueType::Color);
            parameter.Name = "BaseColor";
            parameter.Symbol = "BaseColor";
            parameter.Value = Keire::Color{1.0F, 0.0F, 0.0F, 1.0F};
            graph.Nodes.push_back(std::move(parameter));
            const auto output = std::ranges::find(graph.Nodes.back().Pins, "Value", &Keire::MaterialGraphPin::Name);
            const auto input = std::ranges::find(graph.Nodes.front().Pins, "BaseColor", &Keire::MaterialGraphPin::Name);
            if (output == graph.Nodes.back().Pins.end() || input == graph.Nodes.front().Pins.end())
                return false;
            graph.Connections.push_back(
                {Keire::AssetId::Generate(), {graph.Nodes.back().Id, output->Id}, {graph.Nodes.front().Id, input->Id}});

            Keire::MaterialGraphCompileOptions options;
            options.GeneratedSource = "Assets/Generated/MaterialGraphLive.hlsl";
            const auto compilation = Keire::CompileMaterialGraph(graph, options);
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
                throw std::runtime_error("A live Material Graph shader dependency is unavailable.");
            };
            const auto manifest = std::as_bytes(std::span(variant.Manifest.data(), variant.Manifest.size()));
            const auto imported = importer.ContextualImport(context, manifest);
            const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
            const auto assets = application.Assets();
            if (!assets || !shader || !assets->PublishDevelopmentAsset(Shader, shader))
                return false;

            Keire::MaterialGraphInstanceDefinition defaults;
            defaults.Parent = Graph;
            const std::array ancestry{defaults};
            const auto resolved = Keire::ResolveMaterialGraphInstance(graph, ancestry);
            const auto material = Keire::BakeMaterialGraphInstance(
                graph, resolved, [shaderAsset = Shader](const std::span<const std::string>) { return shaderAsset; });
            return assets->PublishDevelopmentAsset(Material, Keire::CreateRef<Keire::MaterialAsset>(material));
        }

        std::filesystem::path Root;
        std::filesystem::path Catalog;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetId Graph;
        Keire::AssetId Shader;
        Keire::AssetId Material;
    };

    struct LiveMaterialGraphResults final
    {
        std::vector<std::uint8_t> Initial;
        std::vector<std::uint8_t> Revised;
        bool RevisionPublished = false;
    };

    class LiveMaterialGraphCaptureLayer final : public Keire::Layer
    {
      public:
        LiveMaterialGraphCaptureLayer(LiveMaterialGraphFixture& fixture,
                                      std::shared_ptr<LiveMaterialGraphResults> results)
            : Layer("Live Material Graph capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000007"),
                                                     Keire::SceneAsset::EmptyDefinition("Live Material Graph tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Live Material Graph mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(Keire::MeshAsset::CubeId());
            renderer->SetMaterial(m_Fixture.Material);
            renderer->SetTint({0.25F, 0.55F, 1.0F, 1.0F});
            object.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 2.7364445F, 7.5536985F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Live Material Graph tests";
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
        LiveMaterialGraphFixture& m_Fixture;
        std::shared_ptr<LiveMaterialGraphResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::uint32_t m_FrameCount = 0;
        std::uint32_t m_Stage = 0;
        bool m_Submitted = false;
    };
} // namespace

TEST_CASE("live Material Graph shader and parameter revisions update assigned scene meshes")
{
    LiveMaterialGraphFixture assets;
    const auto results = std::make_shared<LiveMaterialGraphResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<LiveMaterialGraphCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    CHECK(results->RevisionPublished);
    REQUIRE_FALSE(results->Initial.empty());
    REQUIRE_FALSE(results->Revised.empty());
    CHECK(ContainsDominantChannel(results->Initial, 1));
    CHECK(ContainsDominantChannel(results->Revised, 0));
}
