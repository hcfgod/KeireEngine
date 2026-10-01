#include "Keire/Core.h"
#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"
#include "KeireRuntimeInternal/RuntimeShutdown.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    class ShutdownServices final : public Keire::Detail::ManagedRuntimeSceneServices
    {
      public:
        Keire::Ref<Keire::SceneRuntimeWorld> World;
        std::vector<std::string> Messages;
        void WriteManagedLog(Keire::ManagedLogLevel, std::string_view message) noexcept override
        {
            try
            {
                Messages.emplace_back(message);
            }
            catch (...)
            {
            }
        }
        float ManagedDeltaTime() const noexcept override { return 0.0F; }
        Keire::Vector2 ReadManagedInput(std::string_view) noexcept override { return {}; }

      protected:
        Keire::Ref<Keire::Scene> ManagedRuntimeScene(Keire::AssetId) const noexcept override { return {}; }
        Keire::Ref<Keire::AssetSystem> ManagedRuntimeAssets() const noexcept override { return {}; }
        Keire::Ref<Keire::Scene> ManagedRuntimeSceneForWorld(std::uint64_t world,
                                                             Keire::AssetId) const noexcept override
        {
            return World ? World->FindWorld(world) : Keire::Ref<Keire::Scene>{};
        }
    };
} // namespace

TEST_CASE("runtime shutdown preserves managed disable destroy logging and scene access")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-Shutdown-" + Keire::AssetId::Generate().ToString());
    struct Cleanup
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    std::filesystem::create_directories(root / "Scripts");
    {
        std::ofstream source(root / "Scripts/ShutdownProbe.cs");
        source << R"(using Keire;
[StableComponentId("1e5e6483-cd2d-4408-a1fe-d4a260990791")]
public sealed class ShutdownProbe : Behaviour
{
    protected override void OnDisable() { Debug.Log("disable:" + Entity.Name + ":" + Transform.Position.X); }
    protected override void OnDestroy() { Debug.Log("destroy:" + Entity.Name + ":" + Transform.Position.X); }
})";
    }
    ShutdownServices services;
    const auto configuration =
        std::string(KEIRE_BUILD_CONFIGURATION) == "Release" || std::string(KEIRE_BUILD_CONFIGURATION) == "Dist"
            ? "Release"
            : "Debug";
    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = root;
    specification.RuntimeHostDirectory =
        std::filesystem::absolute(std::string("Build/Dependencies/coral/Build/") + configuration);
    specification.RuntimeRootDirectory = std::filesystem::absolute("Build/Dependencies/dotnet-sdk");
    specification.ManagedApiAssembly = std::filesystem::absolute("Build/Managed/Keire.Managed.dll");
#if defined(_WIN32)
    specification.DotnetExecutable = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet.exe");
#else
    specification.DotnetExecutable = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet");
#endif
    specification.RuntimeServices = &services;
    const auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
    REQUIRE(scripts->RuntimeHostAvailable());
    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "ShutdownGameplay";
    definition.RootNamespace = "ShutdownGameplay";
    definition.SourceRoots = {"Scripts"};
    Keire::ManagedBuildRequest request;
    request.Assemblies = {{Keire::AssetId::Generate(), definition}};
    const auto build = scripts->StartBuild(std::move(request));
    REQUIRE(scripts->WaitForBuild(build, std::chrono::seconds(60)));
    const auto status = scripts->BuildStatus();
    for (const auto& diagnostic : status.Diagnostics)
        INFO(diagnostic.Message);
    REQUIRE(status.State == Keire::ManagedBuildState::Succeeded);
    Keire::ManagedReloadRequest reload;
    reload.Assemblies = {status.ActiveAssemblyDirectory / "ShutdownGameplay.dll"};
    reload.ManagedApiAssembly = status.ManagedApiAssembly;
    REQUIRE(scripts->PrepareReload(reload));
    scripts->CommitReload();
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    scripts->InstallManagedComponents(registry);
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(Keire::AssetSystemSpecification{});
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    services.World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    const auto authored = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                         Keire::SceneAsset::EmptyDefinition("Shutdown"), registry);
    auto entity = authored->CreateEntity("Probe");
    REQUIRE(entity.AddComponent(Keire::ComponentTypeId::Parse("1e5e6483-cd2d-4408-a1fe-d4a260990791")));
    const auto session =
        Keire::CreateRef<Keire::SceneRuntimeSession>(authored, assets, Keire::Ref<Keire::AudioSystem>{});
    REQUIRE(services.World->Adopt(session));
    session->Play();
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    KeireRuntime::StopRuntimeSessions(services.World, session);
    CHECK(services.Messages == std::vector<std::string>{"disable:Probe:0", "destroy:Probe:0"});
    CHECK(services.World->IsOpen());
    CHECK(session->State() == Keire::ScenePlayState::Stopped);
    KeireRuntime::StopRuntimeSessions(services.World, session);
    CHECK(services.Messages.size() == 2);
    scripts->SetRuntimeServices(nullptr);
    services.World->Close();
    services.World->Close();
    CHECK(services.Messages.size() == 2);
    authored->Close();
    scripts->Close();
    scenes->Close();
    assets->Close();
}
