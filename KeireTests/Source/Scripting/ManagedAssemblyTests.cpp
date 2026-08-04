#include "Keire/ECS/Component.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ScriptSystem.h"
#include "KeireInternal/Scripting/ManagedSdk.h"

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
    CHECK(decoded->Definition().SchemaVersion == Keire::ManagedAssemblySchemaVersion);

    const auto importer = Keire::CreateManagedAssemblyAssetImporter();
    const auto output = importer.ContextualImport({}, Keire::ManagedAssemblyAsset::Encode(definition));
    CHECK(output.AssetDependencies == definition.References);
}

TEST_CASE("Managed SDK settings preserve project data and resolve a validated custom SDK")
{
    const auto root = std::filesystem::temp_directory_path() / ("Keire-ManagedSdk-" + TestAsset(89).ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    const auto settings = root / "ProjectSettings/Scripting.keiresettings";
    const auto sdkRoot = root / "CustomSdk";
#if defined(_WIN32)
    const auto dotnet = sdkRoot / "dotnet.exe";
#else
    const auto dotnet = sdkRoot / "dotnet";
#endif
    std::filesystem::create_directories(settings.parent_path());
    std::filesystem::create_directories(sdkRoot / "sdk/10.0.100");
    std::ofstream(settings) << R"({"unrelated":17,"sdkSelection":"custom","customSdkExecutable":")"
                            << dotnet.generic_string() << R"("})";
    std::ofstream(dotnet, std::ios::binary) << "fixture";

    const auto loaded = Keire::Detail::ReadManagedSdkConfiguration(root, {});
    CHECK(loaded.Selection == Keire::ManagedSdkSelection::Custom);
    CHECK(loaded.CustomExecutable == dotnet);
    CHECK(Keire::Detail::ResolveDotnet(loaded.CustomExecutable, loaded.Selection, root, {}) ==
          std::filesystem::absolute(dotnet).lexically_normal());

    Keire::Detail::WriteManagedSdkConfiguration(root, {Keire::ManagedSdkSelection::SystemPath, dotnet});
    const auto rewritten = ReadBytes(settings);
    const std::string text(reinterpret_cast<const char*>(rewritten.data()), rewritten.size());
    CHECK(text.find(R"("unrelated": 17)") != std::string::npos);
    CHECK(text.find(R"("sdkSelection": "systemPath")") != std::string::npos);
    CHECK(text.find(dotnet.generic_string()) != std::string::npos);
}

TEST_CASE("Managed IDE workspace mirrors assembly source roots and references")
{
    const auto root = std::filesystem::temp_directory_path() / ("Keire-ManagedIde-" + TestAsset(90).ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup() { std::filesystem::remove_all(Root); }
    } cleanup{root};
    std::filesystem::create_directories(root / "Assets/Scripts/Gameplay");

    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = root;
    specification.AssemblyDirectory = "Library/ScriptAssemblies";
    std::filesystem::create_directories(root / "Library/Managed");
    std::ofstream(root / "Library/Managed/Keire.Managed.dll", std::ios::binary) << "managed-api";
    specification.ManagedApiAssembly = root / "Library/Managed/Keire.Managed.dll";
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);

    Keire::ManagedAssemblyDefinition gameplay;
    gameplay.Name = "Gameplay";
    gameplay.RootNamespace = "Game";
    gameplay.SourceRoots = {"Assets/Scripts/Gameplay"};
    Keire::ManagedBuildRequest request;
    request.Assemblies.push_back({TestAsset(91), gameplay});
    const auto workspace = scripts->GenerateIdeWorkspace(request, "My Game");

    CHECK(workspace.Solution == root / "My_Game.sln");
    REQUIRE(workspace.Projects.size() == 1);
    CHECK(workspace.Projects.front() == root / "Gameplay.csproj");
    CHECK(std::filesystem::is_regular_file(workspace.Solution));
    CHECK(std::filesystem::is_regular_file(workspace.Projects.front()));
    const auto project = ReadBytes(workspace.Projects.front());
    const std::string projectText(reinterpret_cast<const char*>(project.data()), project.size());
    CHECK(projectText.starts_with("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
    CHECK(projectText.find("Assets/Scripts/Gameplay") != std::string::npos);
    CHECK(projectText.find("<RootNamespace>Game</RootNamespace>") != std::string::npos);
    CHECK(projectText.find("<Reference Include=\"Keire.Managed\">") != std::string::npos);
    CHECK(projectText.find("<TargetFramework>net8.0</TargetFramework>") != std::string::npos);
    CHECK(projectText.find("<LangVersion>12.0</LangVersion>") != std::string::npos);
    CHECK(projectText.find(
              "<WarningsNotAsErrors>$(WarningsNotAsErrors);CS0168;CS0169;CS0219;CS0414</WarningsNotAsErrors>") !=
          std::string::npos);
    CHECK(projectText.find("Library/ScriptAssemblies/References/Keire.Managed.dll") != std::string::npos);
    CHECK(std::filesystem::is_regular_file(root / "Library/ScriptAssemblies/References/Keire.Managed.dll"));
    CHECK(projectText.find(root.generic_string()) == std::string::npos);
}

TEST_CASE("Managed IDE workspace references the engine API project in source checkouts")
{
    const auto root = std::filesystem::temp_directory_path() / ("Keire-ManagedIdeSource-" + TestAsset(92).ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup() { std::filesystem::remove_all(Root); }
    } cleanup{root};
    const auto projectRoot = root / "Game";
    const auto managedApiProject = root / "KeireManaged/Keire.Managed.csproj";
    std::filesystem::create_directories(projectRoot / "Assets/Scripts/Gameplay");
    std::filesystem::create_directories(projectRoot / "Library/Managed");
    std::filesystem::create_directories(managedApiProject.parent_path());
    std::ofstream(managedApiProject) << "<Project Sdk=\"Microsoft.NET.Sdk\" />\n";
    std::ofstream(projectRoot / "Library/Managed/Keire.Managed.dll", std::ios::binary) << "managed-api";

    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = projectRoot;
    specification.AssemblyDirectory = "Library/ScriptAssemblies";
    specification.ManagedApiAssembly = projectRoot / "Library/Managed/Keire.Managed.dll";
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);

    Keire::ManagedAssemblyDefinition gameplay;
    gameplay.Name = "Gameplay";
    gameplay.RootNamespace = "Game";
    gameplay.SourceRoots = {"Assets/Scripts/Gameplay"};
    Keire::ManagedBuildRequest request;
    request.Assemblies.push_back({TestAsset(93), gameplay});
    const auto workspace = scripts->GenerateIdeWorkspace(request, "Source Game");

    const auto project = ReadBytes(workspace.Projects.front());
    const std::string projectText(reinterpret_cast<const char*>(project.data()), project.size());
    CHECK(
        projectText.find(
            "<ProjectReference Include=\"Library/ScriptAssemblies/References/Keire.Managed.VisualStudio.csproj\" />") !=
        std::string::npos);
    CHECK(projectText.find("<Reference Include=\"Keire.Managed\">") == std::string::npos);
    CHECK(projectText.find("<TargetFramework>net8.0</TargetFramework>") != std::string::npos);
    CHECK(projectText.find("<LangVersion>12.0</LangVersion>") != std::string::npos);

    const auto solution = ReadBytes(workspace.Solution);
    const std::string solutionText(reinterpret_cast<const char*>(solution.data()), solution.size());
    CHECK(solutionText.find(
              "\"Keire.Managed\", \"Library/ScriptAssemblies/References/Keire.Managed.VisualStudio.csproj\"") !=
          std::string::npos);
    CHECK(solutionText.find("{4B454952-4D41-4E41-4745-440000000001}.Debug|Any CPU.Build.0") != std::string::npos);

    const auto designTimeProject =
        projectRoot / "Library/ScriptAssemblies/References/Keire.Managed.VisualStudio.csproj";
    REQUIRE(std::filesystem::is_regular_file(designTimeProject));
    const auto designTimeProjectBytes = ReadBytes(designTimeProject);
    const std::string designTimeProjectText(reinterpret_cast<const char*>(designTimeProjectBytes.data()),
                                            designTimeProjectBytes.size());
    CHECK(designTimeProjectText.find("<TargetFramework>net8.0</TargetFramework>") != std::string::npos);
    CHECK(designTimeProjectText.find("<LangVersion>12.0</LangVersion>") != std::string::npos);
    CHECK(designTimeProjectText.find("KeireManaged/**/*.cs") != std::string::npos);
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
                  "[StableComponentId(\"73616e64-626f-4078-8000-000000000097\")] "
                  "public sealed class PlayerDependency : Behaviour { } "
                  "[StableComponentId(\"73616e64-626f-4078-8000-000000000099\")] "
                  "[RequireComponent(typeof(PlayerDependency))] "
                  "[ExecutionOrder(-50)] public sealed class Player : Behaviour { "
                  "[SerializeField, StableFieldId(\"73616e64-626f-4078-8000-000000000098\")] "
                  "public float Speed = 7.5f; "
                  "[SerializeField] public float ConsumedSpeed = -1.0f; "
                  "[SerializeField] public bool SeekFailureObserved = false; "
                  "[SerializeField] public bool ClipFailureObserved = false; "
                  "[SerializeField] public bool ClipDidNotAddSource = false; "
                  "[SerializeField] public bool Utf8BusLimitObserved = false; "
                  "[SerializeField] public bool Utf8BusBoundaryAccepted = false; "
                  "[SerializeField] public bool RunAudioScalarValidation = false; "
                  "[SerializeField] public bool VolumeValidationObserved = false; "
                  "[SerializeField] public bool PitchValidationObserved = false; "
                  "[SerializeField] public bool DisableThroughProperty = false; "
                  "[SerializeField] public bool DisableObserved = false; "
                  "[SerializeField] public bool AnimatorIkObserved = false; "
                  "[SerializeField] public float AnimatorIkWeight = -1.0f; "
                  "protected override void Awake() { Speed += 1.0f; } "
                  "protected override void FixedUpdate() { ConsumedSpeed = Speed; "
                  "if (DisableThroughProperty) { DisableThroughProperty = false; Enabled = false; } "
                  "try { var source = Entity.AudioSource; source.Time = 0.5f; } "
                  "catch (System.InvalidOperationException) { SeekFailureObserved = true; } "
                  "if (RunAudioScalarValidation) ValidateAudioScalars(); "
                  "else { ValidateMissingAudioSource(); ValidateAudioBusNames(); } } "
                  "private void ValidateMissingAudioSource() { "
                  "try { var source = Entity.AudioSource; "
                  "source.Clip = new AssetReference<AudioClip>(new AssetId(1, 2)); } "
                  "catch (System.InvalidOperationException) { ClipFailureObserved = true; } "
                  "ClipDidNotAddSource = !Entity.HasComponent<AudioSourceComponent>(); } "
                  "private void ValidateAudioBusNames() { var clip = new AssetId(1, 2); "
                  "try { Audio.Play(Entity, clip, new AudioPlaybackOptions { "
                  "Bus = new string('\\u00e9', 65) }); } "
                  "catch (System.ArgumentException) { Utf8BusLimitObserved = true; } "
                  "try { _ = Audio.Play(Entity, clip, new AudioPlaybackOptions { "
                  "Bus = new string('\\u00e9', 64) }); Utf8BusBoundaryAccepted = true; } "
                  "catch (System.ArgumentException) { } } "
                  "private void ValidateAudioScalars() { var source = Entity.AudioSource; "
                  "source.Volume = 16.0f; var volumeFailures = 0; "
                  "foreach (var value in new float[] { float.NaN, -0.01f, 16.01f }) { "
                  "try { source.Volume = value; } "
                  "catch (System.ArgumentOutOfRangeException) { volumeFailures++; } } "
                  "VolumeValidationObserved = volumeFailures == 3 && source.Volume == 16.0f; "
                  "source.Pitch = 8.0f; var pitchFailures = 0; "
                  "foreach (var value in new float[] { float.PositiveInfinity, 0.01f, 8.01f }) { "
                  "try { source.Pitch = value; } "
                  "catch (System.ArgumentOutOfRangeException) { pitchFailures++; } } "
                  "PitchValidationObserved = pitchFailures == 3 && source.Pitch == 8.0f; } "
                  "protected override void OnDisable() { DisableObserved = !Enabled; } "
                  "protected override void OnAnimatorIk(AnimationIkContext context) { "
                  "AnimatorIkObserved = true; AnimatorIkWeight = context.LayerWeight; } "
                  "protected override void OnBeforeReload() { Speed += 1.0f; } "
                  "protected override void OnAfterReload() { Speed += 1.0f; } } "
                  "[CreateAssetMenu(\"Gameplay/Player Tuning\", \"PlayerTuning\")] "
                  "[StableAssetTypeId(\"73616e64-626f-4078-8000-000000000190\")] "
                  "public sealed class PlayerTuning : ScriptableObject { "
                  "[StableFieldId(\"73616e64-626f-4078-8000-000000000191\"), Range(0.0, 100.0)] "
                  "public float Speed = 7.5f; "
                  "[StableFieldId(\"73616e64-626f-4078-8000-000000000192\")] "
                  "public AssetReference<PlayerTuning> Parent; } "
                  "[StableAssetTypeId(\"73616e64-626f-4078-8000-000000000193\")] "
                  "public sealed class InvalidTuning : ScriptableObject { public float MissingStableId = 1.0f; }\n";
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
    const auto prepared = scripts->PrepareReload(request);
    INFO(scripts->ReloadStatus().Diagnostic);
    REQUIRE(prepared);
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Prepared);
    CHECK(scripts->ReloadStatus().AvailableTypes == std::vector<std::string>{"Game.Player", "Game.PlayerDependency"});
    CHECK(scripts->ReloadStatus().RetainedState == request.State);
    scripts->CommitReload();
    CHECK(scripts->ReloadStatus().State == Keire::ManagedReloadState::Active);
    CHECK(scripts->ReloadStatus().Generation == 1);
    const auto managedAssetTypes = scripts->ManagedAssetTypes();
    const auto playerTuning = std::ranges::find(managedAssetTypes, std::string("Game.PlayerTuning"),
                                                &Keire::ManagedAssetTypeDescriptor::FullName);
    REQUIRE(playerTuning != managedAssetTypes.end());
    CHECK(playerTuning->MenuPath == "Gameplay/Player Tuning");
    CHECK(playerTuning->Properties.size() == 2);
    CHECK(playerTuning->Properties[0].Kind == Keire::ManagedAssetPropertyKind::Scalar);
    CHECK(playerTuning->Properties[0].Minimum == 0.0);
    CHECK(playerTuning->Properties[0].Maximum == 100.0);
    CHECK(playerTuning->Properties[1].Kind == Keire::ManagedAssetPropertyKind::AssetReference);
    REQUIRE(playerTuning->Properties[1].ExpectedAssetType);
    CHECK(*playerTuning->Properties[1].ExpectedAssetType == Keire::ManagedDataAsset::StaticType());
    REQUIRE(playerTuning->Properties[1].ExpectedManagedType);
    CHECK(playerTuning->Properties[1].ExpectedManagedType->Value() ==
          Keire::AssetId::Parse("73616e64-626f-4078-8000-000000000190"));
    const auto assetDiagnostics = scripts->ManagedAssetTypeDiagnostics();
    CHECK(std::ranges::any_of(assetDiagnostics, [](const Keire::ManagedAssetTypeDiagnostic& diagnostic)
                              { return diagnostic.TypeName == "Game.InvalidTuning"; }));
    const auto componentType = Keire::ComponentTypeId::Parse("73616e64-626f-4078-8000-000000000099");
    const auto dependencyType = Keire::ComponentTypeId::Parse("73616e64-626f-4078-8000-000000000097");
    auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registryRevision = registry->Revision();
    scripts->InstallManagedComponents(registry);
    CHECK(registry->Contains(componentType));
    CHECK(registry->Revision() == registryRevision + 1);
    const auto registration = registry->Find(componentType);
    REQUIRE(registration);
    CHECK(registration->RequiredComponents == std::vector<Keire::ComponentTypeId>{dependencyType});

    const std::string duplicateState =
        R"({"Version":1,"Fields":[{"StableId":"","Name":"Speed","Type":"System.Single","Aliases":[],"Value":3.0},)"
        R"({"StableId":"73616e64-626f-4078-8000-000000000098","Name":"Speed","Type":"System.Single",)"
        R"("Aliases":[],"Value":11.0}]})";
    const auto duplicateComponent = registration->Factory();
    REQUIRE(duplicateComponent);
    registration->Deserialize(*duplicateComponent, {{"managedState", duplicateState}}, registration->SchemaVersion);
    const auto duplicateProjection = registration->Serialize(*duplicateComponent);
    REQUIRE(duplicateProjection.contains("Speed"));
    CHECK(std::get<double>(duplicateProjection.at("Speed")) == doctest::Approx(11.0));

    const std::string legacyState =
        R"({"Version":1,"Fields":[{"StableId":"","Name":"Speed","Type":"System.Single","Aliases":[],"Value":5.0},)"
        R"({"StableId":"","Name":"LegacyOnly","Type":"System.Int32","Aliases":[],"Value":41}]})";
    auto sceneDefinition = Keire::SceneAsset::EmptyDefinition("Managed Lifecycle");
    auto editingScene = Keire::CreateRef<Keire::Scene>(TestAsset(103), std::move(sceneDefinition), registry);
    auto scriptedEntity = editingScene->CreateEntity("Player");
    const auto editingComponent = scriptedEntity.AddComponent(componentType);
    REQUIRE(editingComponent);
    CHECK(scriptedEntity.HasComponent(dependencyType));
    registration->Deserialize(*editingComponent, {{"managedState", legacyState}}, registration->SchemaVersion);
    const auto editingValuesBeforePlay = registration->Serialize(*editingComponent);
    REQUIRE(editingValuesBeforePlay.contains("managedState"));
    const auto editingStateBeforePlay = std::get<std::string>(editingValuesBeforePlay.at("managedState"));
    editingScene->MarkSaved();

    auto play = Keire::CreateRef<Keire::SceneRuntimeSession>(editingScene);
    CHECK_NOTHROW(play->Play());
    INFO(play->Diagnostic().Message);
    REQUIRE(play->State() == Keire::ScenePlayState::Playing);
    REQUIRE(play->RuntimeScene());
    auto runtimeEntity = play->RuntimeScene()->FindEntity(scriptedEntity.Id());
    REQUIRE(runtimeEntity);
    const auto runtimeComponent = runtimeEntity.GetComponent(componentType);
    REQUIRE(runtimeComponent);
    CHECK_THROWS_AS(static_cast<void>(runtimeEntity.RemoveComponent(dependencyType)), std::logic_error);

    auto runtimeValues = registration->Serialize(*runtimeComponent);
    REQUIRE(runtimeValues.contains("managedState"));
    const auto& canonicalState = std::get<std::string>(runtimeValues.at("managedState"));
    const std::string stableSpeed = R"("StableId":"73616e64-626f-4078-8000-000000000098","Name":"Speed")";
    const std::string legacySpeed = R"("StableId":"","Name":"Speed")";
    const std::string retainedUnknown = R"("Name":"LegacyOnly","Type":"System.Int32","Aliases":[],"Value":41)";
    const auto stableSpeedPosition = canonicalState.find(stableSpeed);
    REQUIRE(stableSpeedPosition != std::string::npos);
    CHECK(canonicalState.find(stableSpeed, stableSpeedPosition + stableSpeed.size()) == std::string::npos);
    CHECK(canonicalState.find(legacySpeed) == std::string::npos);
    CHECK(canonicalState.find(retainedUnknown) != std::string::npos);

    runtimeValues.insert_or_assign("Speed", 12.25);
    registration->Deserialize(*runtimeComponent, runtimeValues, registration->SchemaVersion);
    CHECK_NOTHROW(play->FixedUpdate(1.0F / 60.0F));
    const auto consumedValues = registration->Serialize(*runtimeComponent);
    REQUIRE(consumedValues.contains("Speed"));
    REQUIRE(consumedValues.contains("ConsumedSpeed"));
    REQUIRE(consumedValues.contains("SeekFailureObserved"));
    REQUIRE(consumedValues.contains("ClipFailureObserved"));
    REQUIRE(consumedValues.contains("ClipDidNotAddSource"));
    REQUIRE(consumedValues.contains("Utf8BusLimitObserved"));
    REQUIRE(consumedValues.contains("Utf8BusBoundaryAccepted"));
    CHECK(std::get<double>(consumedValues.at("Speed")) == doctest::Approx(12.25));
    CHECK(std::get<double>(consumedValues.at("ConsumedSpeed")) == doctest::Approx(12.25));
    CHECK(std::get<bool>(consumedValues.at("SeekFailureObserved")));
    CHECK(std::get<bool>(consumedValues.at("ClipFailureObserved")));
    CHECK(std::get<bool>(consumedValues.at("ClipDidNotAddSource")));
    CHECK(std::get<bool>(consumedValues.at("Utf8BusLimitObserved")));
    CHECK(std::get<bool>(consumedValues.at("Utf8BusBoundaryAccepted")));
    CHECK_FALSE(runtimeEntity.HasComponent<Keire::AudioSourceComponent>());

    auto disableValues = consumedValues;
    disableValues.insert_or_assign("DisableThroughProperty", true);
    registration->Deserialize(*runtimeComponent, disableValues, registration->SchemaVersion);
    CHECK_NOTHROW(play->FixedUpdate(1.0F / 60.0F));
    CHECK_FALSE(runtimeComponent->Enabled());
    const auto disabledResults = registration->Serialize(*runtimeComponent);
    REQUIRE(disabledResults.contains("DisableObserved"));
    CHECK(std::get<bool>(disabledResults.at("DisableObserved")));
    runtimeComponent->SetEnabled(true);

    CHECK_NOTHROW(play->RuntimeScene()->DispatchAnimatorIk(scriptedEntity.Id(), {.LayerWeight = 0.625F}));
    const auto ikResults = registration->Serialize(*runtimeComponent);
    REQUIRE(ikResults.contains("AnimatorIkObserved"));
    REQUIRE(ikResults.contains("AnimatorIkWeight"));
    CHECK(std::get<bool>(ikResults.at("AnimatorIkObserved")));
    CHECK(std::get<double>(ikResults.at("AnimatorIkWeight")) == doctest::Approx(0.625));

    REQUIRE(runtimeEntity.AddComponent<Keire::AudioSourceComponent>());
    auto scalarValidationValues = consumedValues;
    scalarValidationValues.insert_or_assign("RunAudioScalarValidation", true);
    registration->Deserialize(*runtimeComponent, scalarValidationValues, registration->SchemaVersion);
    CHECK_NOTHROW(play->FixedUpdate(1.0F / 60.0F));
    const auto scalarResults = registration->Serialize(*runtimeComponent);
    REQUIRE(scalarResults.contains("VolumeValidationObserved"));
    REQUIRE(scalarResults.contains("PitchValidationObserved"));
    CHECK(std::get<bool>(scalarResults.at("VolumeValidationObserved")));
    CHECK(std::get<bool>(scalarResults.at("PitchValidationObserved")));

    const auto editingValues = registration->Serialize(*editingComponent);
    REQUIRE(editingValues.contains("managedState"));
    REQUIRE(editingValues.contains("Speed"));
    CHECK(std::get<std::string>(editingValues.at("managedState")) == editingStateBeforePlay);
    CHECK(std::get<double>(editingValues.at("Speed")) == doctest::Approx(5.0));
    CHECK_FALSE(scriptedEntity.HasComponent<Keire::AudioSourceComponent>());
    CHECK_FALSE(editingScene->Dirty());
    play->Stop();
    CHECK(std::get<double>(registration->Serialize(*editingComponent).at("Speed")) == doctest::Approx(5.0));
    editingScene->Close();

    const auto instance =
        scripts->CreateBehaviour("Game.Player", 7, Keire::AssetId::Parse("00000000-0000-0000-0000-000000000042"));
    REQUIRE(instance);
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Awake));
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Enable));
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Start));
    const auto replayCheckpoint = scripts->CaptureReplayCheckpoint();
    REQUIRE(replayCheckpoint.size() == 1U);
    CHECK(replayCheckpoint.front().Enabled);
    REQUIRE(scripts->SetBehaviourEnabled(instance, false));
    REQUIRE_FALSE(scripts->CaptureReplayCheckpoint().front().Enabled);
    CHECK_NOTHROW(scripts->RestoreReplayCheckpoint(replayCheckpoint));
    CHECK(scripts->CaptureReplayCheckpoint().front().Enabled);
    auto incompatibleReplayCheckpoint = replayCheckpoint;
    incompatibleReplayCheckpoint.front().Entity = Keire::AssetId::Generate();
    CHECK_THROWS_AS(scripts->RestoreReplayCheckpoint(incompatibleReplayCheckpoint), std::runtime_error);
    CHECK(scripts->CaptureReplayCheckpoint().front().Enabled);
    const auto callbackMetrics = scripts->Metrics();
    CHECK(callbackMetrics.CallbackInvocations >= 3);
    CHECK(callbackMetrics.ManagedInteropCalls >= 3);
    CHECK(callbackMetrics.CallbackMilliseconds >= 0.0);
    const auto callbackBreakdown = scripts->CallbackMetrics();
    CHECK_FALSE(callbackBreakdown.Truncated);
    const auto awakeMetric = std::ranges::find_if(
        callbackBreakdown.Entries, [](const Keire::ManagedCallbackMetric& metric)
        { return metric.TypeName == "Game.Player" && metric.Callback == Keire::ManagedBehaviourCallback::Awake; });
    REQUIRE(awakeMetric != callbackBreakdown.Entries.end());
    CHECK(awakeMetric->InstanceCount == 1);
    CHECK(awakeMetric->Invocations == 1);
    CHECK(awakeMetric->SkippedInvocations == 0);
    CHECK(awakeMetric->Milliseconds >= 0.0);

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

TEST_CASE("Managed assembly schema one remains readable and assembly classifications are isolated")
{
    const std::string legacy =
        R"({"schemaVersion":1,"name":"Legacy","rootNamespace":"Legacy","classification":"runtime",)"
        R"("sourceRoots":["Scripts"],"references":[]})";
    const auto bytes = std::as_bytes(std::span(legacy.data(), legacy.size()));
    const auto decoded = Keire::ManagedAssemblyAsset::Decode(bytes);
    CHECK(decoded->Definition().SchemaVersion == 1);

    Keire::ManagedAssemblyDefinition runtime;
    runtime.Name = "Runtime";
    runtime.RootNamespace = "Game.Runtime";
    runtime.SourceRoots = {"Scripts/Runtime"};
    runtime.References = {TestAsset(202)};
    Keire::ManagedAssemblyDefinition editor;
    editor.Name = "Editor";
    editor.RootNamespace = "Game.Editor";
    editor.Classification = Keire::ManagedAssemblyClassification::Editor;
    editor.SourceRoots = {"Scripts/Editor"};
    const std::array graph{Keire::ManagedAssemblyGraphEntry{TestAsset(201), runtime},
                           Keire::ManagedAssemblyGraphEntry{TestAsset(202), editor}};
    CHECK_THROWS_WITH_AS(Keire::ValidateManagedAssemblyGraph(graph),
                         "Managed assembly reference violates runtime/editor/test isolation.", std::invalid_argument);
}
