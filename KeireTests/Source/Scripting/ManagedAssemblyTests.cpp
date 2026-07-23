#include "Keire/ECS/Component.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ScriptSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace
{
    [[nodiscard]] Keire::AssetId TestAsset(const std::uint64_t value)
    {
        return Keire::AssetId(0x4b45495245544553ULL, value);
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }
} // namespace

TEST_CASE("Managed assembly definitions round trip and expose dependencies")
{
    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "Gameplay";
    definition.RootNamespace = "KeireGame.Runtime";
    definition.SourceRoots = {"Scripts/Runtime", "Scripts/Shared"};
    definition.References = {TestAsset(2)};

    const auto decoded = Keire::ManagedAssemblyAsset::Decode(Keire::ManagedAssemblyAsset::Encode(definition));
    CHECK(decoded->Definition().Name == "Gameplay");
    CHECK(decoded->Definition().RootNamespace == "KeireGame.Runtime");
    CHECK(decoded->Definition().SourceRoots == definition.SourceRoots);
    CHECK(decoded->Definition().References == definition.References);

    const auto importer = Keire::CreateManagedAssemblyAssetImporter();
    const auto output = importer.ContextualImport({}, Keire::ManagedAssemblyAsset::Encode(definition));
    CHECK(output.AssetDependencies == definition.References);
}

TEST_CASE("Managed builds publish only successful replacements")
{
    const auto dotnet = std::filesystem::absolute("Library/DotnetSdk10/sdk/dotnet.exe");
    if (!std::filesystem::is_regular_file(dotnet))
    {
        MESSAGE("Skipping managed build integration test because the isolated .NET 10 SDK is unavailable.");
        return;
    }

    const auto root = std::filesystem::absolute("Library/ManagedBuildIntegration-" + TestAsset(99).ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    std::filesystem::create_directories(root / "Scripts");
    const auto source = root / "Scripts/Gameplay.cs";
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "namespace Game; public static class Gameplay { public static int Value => 42; }\n";
    }

    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = root;
    specification.DotnetExecutable = dotnet;
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "Gameplay";
    definition.RootNamespace = "Game";
    definition.SourceRoots = {"Scripts"};
    Keire::ManagedBuildRequest request;
    request.Assemblies = {{TestAsset(1), definition}};

    const auto first = scripts->StartBuild(request);
    REQUIRE(scripts->WaitForBuild(first, std::chrono::seconds(60)));
    REQUIRE(scripts->BuildStatus().State == Keire::ManagedBuildState::Succeeded);
    const auto active = scripts->BuildStatus().ActiveAssemblyDirectory / "Gameplay.dll";
    REQUIRE(std::filesystem::is_regular_file(active));
    const auto successfulWrite = std::filesystem::last_write_time(active);

    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "namespace Game; public static class Gameplay { this is not valid C# }\n";
    }
    const auto failed = scripts->StartBuild(std::move(request));
    REQUIRE(scripts->WaitForBuild(failed, std::chrono::seconds(60)));
    CHECK(scripts->BuildStatus().State == Keire::ManagedBuildState::Failed);
    CHECK_FALSE(scripts->BuildStatus().Diagnostics.empty());
    CHECK(std::filesystem::is_regular_file(active));
    CHECK(std::filesystem::last_write_time(active) == successfulWrite);
    scripts->Close();
}

TEST_CASE("Managed assembly definitions reject paths outside the project")
{
    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "Gameplay";
    definition.RootNamespace = "Gameplay";
    definition.SourceRoots = {"../External"};
    CHECK_THROWS_WITH_AS(Keire::ManagedAssemblyAsset::Validate(definition),
                         "Managed assembly source roots must be unique project-relative paths.", std::invalid_argument);
}

TEST_CASE("Third-person sandbox gameplay assembly compiles against Keire.Managed")
{
#if defined(_WIN32)
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet.exe");
#else
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet");
#endif
    REQUIRE(std::filesystem::is_regular_file(dotnet));
    const auto root = std::filesystem::absolute("Samples/KeireSandbox");
    const auto assembly = Keire::ManagedAssemblyAsset::Decode(ReadBytes(root / "Assets/Scripts/Gameplay.keireasm"));
    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = root;
    specification.AssemblyDirectory = "Library/SandboxScriptValidation";
    specification.ManagedApiAssembly = std::filesystem::absolute("Build/Managed/Keire.Managed.dll");
    specification.DotnetExecutable = dotnet;
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
    Keire::ManagedBuildRequest request;
    request.Assemblies = {{TestAsset(102), assembly->Definition()}};
    const auto operation = scripts->StartBuild(std::move(request));
    REQUIRE(scripts->WaitForBuild(operation, std::chrono::seconds(60)));
    const auto status = scripts->BuildStatus();
    const auto diagnostic = status.Diagnostics.empty() ? std::string{} : status.Diagnostics.front().Message;
    INFO(diagnostic);
    CHECK(status.State == Keire::ManagedBuildState::Succeeded);
    scripts->Close();
}

TEST_CASE("Managed runtime reload is transactional and preserves retained state")
{
    const std::string configuration =
        std::string(KEIRE_BUILD_CONFIGURATION) == "Release" || std::string(KEIRE_BUILD_CONFIGURATION) == "Dist"
            ? "Release"
            : "Debug";
    const auto host = std::filesystem::absolute("Build/Dependencies/coral-patched/Build/" + configuration);
#if defined(_WIN32)
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet.exe");
#else
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet");
#endif
    REQUIRE(std::filesystem::is_regular_file(dotnet));
    const auto root = std::filesystem::absolute("Library/ManagedReloadIntegration-" + TestAsset(100).ToString());
    struct ReloadCleanup final
    {
        std::filesystem::path Root;
        ~ReloadCleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    std::filesystem::create_directories(root / "Scripts");
    {
        std::ofstream stream(root / "Scripts/Player.cs", std::ios::binary | std::ios::trunc);
        stream << "using Keire; namespace Game; "
                  "[StableComponentId(\"73616e64-626f-4078-8000-000000000099\")] "
                  "[ExecutionOrder(-50)] public sealed class Player : Behaviour { "
                  "[SerializeField] public float Speed = 7.5f; "
                  "protected override void Awake() { Speed += 1.0f; } "
                  "protected override void FixedUpdate() { Speed += 0.5f; } "
                  "protected override void OnBeforeReload() { Speed += 1.0f; } "
                  "protected override void OnAfterReload() { Speed += 1.0f; } }\n";
    }

    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = root;
    specification.RuntimeHostDirectory = host;
    specification.RuntimeRootDirectory = std::filesystem::absolute("Build/Dependencies/dotnet-sdk");
    specification.ManagedApiAssembly = std::filesystem::absolute("Build/Managed/Keire.Managed.dll");
    specification.DotnetExecutable = dotnet;
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
    REQUIRE(scripts->RuntimeHostAvailable());

    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "ReloadGameplay";
    definition.RootNamespace = "Game";
    definition.SourceRoots = {"Scripts"};
    Keire::ManagedBuildRequest buildRequest;
    buildRequest.Assemblies = {{TestAsset(101), definition}};
    const auto build = scripts->StartBuild(std::move(buildRequest));
    REQUIRE(scripts->WaitForBuild(build, std::chrono::seconds(60)));
    REQUIRE(scripts->BuildStatus().State == Keire::ManagedBuildState::Succeeded);
    const auto assembly = scripts->BuildStatus().ActiveAssemblyDirectory / "ReloadGameplay.dll";
    REQUIRE(std::filesystem::is_regular_file(assembly));

    Keire::ManagedReloadRequest request;
    request.Assemblies = {assembly};
    request.State = {{42, "Game.Player", {{"speed", "7.5"}, {"target", "entity:123"}}}};
    REQUIRE(scripts->PrepareReload(request));
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Prepared);
    CHECK(scripts->ReloadStatus().AvailableTypes == std::vector<std::string>{"Game.Player"});
    CHECK(scripts->ReloadStatus().RetainedState == request.State);
    scripts->CommitReload();
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Active);
    CHECK(scripts->ReloadStatus().Generation == 1);
    const auto componentType = Keire::ComponentTypeId::Parse("73616e64-626f-4078-8000-000000000099");
    auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registryRevision = registry->Revision();
    scripts->InstallManagedComponents(registry);
    CHECK(registry->Contains(componentType));
    CHECK(registry->Revision() == registryRevision + 1);
    auto sceneDefinition = Keire::SceneAsset::EmptyDefinition("Managed Lifecycle");
    auto scene = Keire::CreateRef<Keire::Scene>(TestAsset(103), std::move(sceneDefinition), registry);
    auto scriptedEntity = scene->CreateEntity("Player");
    REQUIRE(scriptedEntity.AddComponent(componentType));
    CHECK_NOTHROW(scene->BeginPlay());
    CHECK_NOTHROW(scene->FixedUpdate(1.0F / 60.0F));
    CHECK_NOTHROW(scene->Update(1.0F / 60.0F));
    CHECK_NOTHROW(scene->EndPlay());
    scene->Close();

    const auto instance =
        scripts->CreateBehaviour("Game.Player", 7, Keire::AssetId::Parse("00000000-0000-0000-0000-000000000042"));
    REQUIRE(instance);
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Awake));
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Enable));
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Start));

    request.Assemblies = {host / "Missing.dll"};
    CHECK_FALSE(scripts->PrepareReload(request));
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Failed);
    CHECK(scripts->ReloadStatus().Generation == 1);
    CHECK_FALSE(scripts->ReloadStatus().Diagnostic.empty());

    request.Assemblies = {assembly};
    REQUIRE(scripts->PrepareReload(request));
    std::string rejection;
    std::thread wrongThread(
        [&]
        {
            try
            {
                scripts->CancelReload();
            }
            catch (const std::exception& error)
            {
                rejection = error.what();
            }
        });
    wrongThread.join();
    CHECK(rejection == "ScriptSystem operation must run on the owner thread.");
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Prepared);
    CHECK_NOTHROW(scripts->CommitReload());
    CHECK(scripts->ReloadStatus().Generation == 2);
    CHECK(scripts->DestroyBehaviour(instance));
    CHECK_FALSE(scripts->DestroyBehaviour(instance));
    REQUIRE(scripts->PrepareReload(request));
    scripts->CancelReload();
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Cancelled);
    scripts->Close();
    CHECK_FALSE(scripts->RuntimeHostAvailable());
    CHECK_NOTHROW(scripts->Close());
}

TEST_CASE("Managed assembly graphs are acyclic and complete")
{
    Keire::ManagedAssemblyDefinition first;
    first.Name = "First";
    first.RootNamespace = "Game.First";
    first.SourceRoots = {"Scripts/First"};
    first.References = {TestAsset(2)};

    Keire::ManagedAssemblyDefinition second;
    second.Name = "Second";
    second.RootNamespace = "Game.Second";
    second.SourceRoots = {"Scripts/Second"};

    std::array graph{Keire::ManagedAssemblyGraphEntry{TestAsset(1), first},
                     Keire::ManagedAssemblyGraphEntry{TestAsset(2), second}};
    CHECK_NOTHROW(Keire::ValidateManagedAssemblyGraph(graph));

    graph[1].Definition.References = {TestAsset(1)};
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedAssemblyGraph(graph), "Managed assembly references contain a cycle.",
                         std::invalid_argument);
}
