#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "Keire/Scripting/ScriptSystem.h"
#include "KeireInternal/Scripting/ManagedBuildWorkspace.h"
#include "KeireTests/AssetTestProject.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

namespace
{
    Keire::ManagedAssemblyGraphEntry Assembly(const std::string& name)
    {
        Keire::ManagedAssemblyDefinition definition;
        definition.Name = name;
        definition.RootNamespace = name;
        return {Keire::AssetId::Generate(), definition, "Assets/" + name + "/" + name + ".keireasm"};
    }
    const Keire::ManagedAssemblyGraphEntry& Named(const std::vector<Keire::ManagedAssemblyGraphEntry>& graph,
                                                  std::string_view name)
    {
        const auto found =
            std::ranges::find_if(graph, [&](const auto& entry) { return entry.Definition.Name == name; });
        REQUIRE(found != graph.end());
        return *found;
    }
    void WriteReference(const KeireTests::TemporaryAssetProject& project, const std::filesystem::path& path,
                        const std::string& target)
    {
        const auto bytes = Keire::ManagedAssemblyReferenceAsset::Encode(target);
        project.Write(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
} // namespace

TEST_CASE("Assembly reference assets assign other folders and nested boundaries exactly once")
{
    KeireTests::TemporaryAssetProject project;
    auto shared = Assembly("Shared"), child = Assembly("Child");
    child.DefinitionPath = "Assets/Elsewhere/Nested/Child.keireasm";
    const std::array definitions{shared, child};
    project.Write("Shared/Base.cs", "class Base {}");
    project.Write("Elsewhere/Extra.cs", "class Extra {}");
    project.Write("Elsewhere/Nested/Child.cs", "class Child {}");
    WriteReference(project, "Elsewhere/Shared.asmref", "GUID:" + shared.Asset.ToString());
    auto graph = Keire::ResolveProjectManagedAssemblies(project.Root, definitions);
    CHECK(Named(graph, "Shared").SourceFiles->size() == 2);
    CHECK(Named(graph, "Child").SourceFiles->size() == 1);
    CHECK(Named(graph, "Assembly-CSharp").SourceFiles->empty());
    WriteReference(project, "Elsewhere/Shared.asmref", "Shared");
    CHECK_NOTHROW((void)Keire::ResolveProjectManagedAssemblies(project.Root, definitions));
    SUBCASE("Duplicate boundary is rejected")
    {
        WriteReference(project, "Elsewhere/Duplicate.asmref", "Shared");
        CHECK_THROWS_AS((void)Keire::ResolveProjectManagedAssemblies(project.Root, definitions), std::invalid_argument);
    }
    SUBCASE("Missing and predefined targets are rejected")
    {
        for (const auto name : {"Gone", "Assembly-CSharp", ""})
        {
            WriteReference(project, "Elsewhere/Shared.asmref", name);
            CHECK_THROWS_AS((void)Keire::ResolveProjectManagedAssemblies(project.Root, definitions),
                            std::invalid_argument);
        }
    }
    SUBCASE("Importer retains GUID dependency and rejects malformed input")
    {
        const auto importer = Keire::CreateManagedAssemblyReferenceAssetImporter();
        const auto output = importer.ContextualImport(
            {}, Keire::ManagedAssemblyReferenceAsset::Encode("GUID:" + shared.Asset.ToString()));
        CHECK(output.AssetDependencies == std::vector<Keire::AssetId>{shared.Asset});
        CHECK_THROWS((void)Keire::ManagedAssemblyReferenceAsset::Encode("GUID:invalid"));
    }
}

TEST_CASE("Assembly platform exclusions preserve ownership and reject dangling references")
{
    KeireTests::TemporaryAssetProject project;
    auto feature = Assembly("Feature"), consumer = Assembly("Consumer");
    feature.Definition.IncludePlatforms = {"Windows"};
    project.Write("Feature/OnlyWindows.cs", "invalid on other platforms");
    project.Write("Other/Always.cs", "class Always {}");
    WriteReference(project, "More/Feature.asmref", "Feature");
    project.Write("More/OnlyWindows.cs", "also excluded");
    const std::array definitions{feature};
    const auto excluded = Keire::ResolveProjectManagedAssemblies(project.Root, definitions, {.Platform = "Linux"});
    REQUIRE(excluded.size() == 1);
    CHECK(*excluded.front().SourceFiles == std::vector<std::filesystem::path>{"Assets/Other/Always.cs"});
    CHECK(excluded.front().Definition.References.empty());
    const auto included = Keire::ResolveProjectManagedAssemblies(project.Root, definitions, {.Platform = "Windows"});
    CHECK(Named(included, "Feature").SourceFiles->size() == 2);
    consumer.Definition.References = {feature.Asset};
    CHECK_THROWS_AS((void)Keire::ResolveProjectManagedAssemblies(project.Root, std::array{feature, consumer},
                                                                 {.Platform = "Linux"}),
                    std::invalid_argument);
    feature.Definition.ExcludePlatforms = {"Linux"};
    CHECK_THROWS_AS(Keire::ManagedAssemblyAsset::Validate(feature.Definition), std::invalid_argument);
    feature.Definition.IncludePlatforms.clear();
    feature.Definition.ExcludePlatforms = {"Unknown"};
    CHECK_THROWS_AS(Keire::ManagedAssemblyAsset::Validate(feature.Definition), std::invalid_argument);
}

TEST_CASE("Editor-only platform filters isolate player assemblies")
{
    KeireTests::TemporaryAssetProject project;
    auto editor = Assembly("Tools");
    editor.Definition.IncludePlatforms = {"Editor"};
    project.Write("Tools/Tool.cs", "editor code");
    const auto graph = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{editor});
    CHECK(Named(graph, "Tools").Definition.Classification == Keire::ManagedAssemblyClassification::Editor);
    CHECK(Named(graph, "Assembly-CSharp").Definition.References.empty());
    const auto player = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{editor}, {.IsEditor = false});
    REQUIRE(player.size() == 1);
    CHECK(player.front().SourceFiles->empty());
}

TEST_CASE("Assembly define constraints and semantic version ranges determine compilation")
{
    KeireTests::TemporaryAssetProject project;
    auto feature = Assembly("Feature");
    feature.Definition.VersionDefines = {{"example.package", "[1.2.0,2.0.0)", "HAS_PACKAGE"}};
    feature.Definition.DefineConstraints = {"HAS_PACKAGE || MANUAL", "!DISABLE", "KEIRE_LINUX"};
    Keire::ManagedAssemblyBuildContext context;
    context.Platform = "Linux";
    context.ResourceVersions = {{"example.package", "1.2.0"}};
    auto resolve = [&] { return Keire::ResolveProjectManagedAssemblies(project.Root, std::array{feature}, context); };
    CHECK(resolve().size() == 2);
    const auto graph = resolve();
    CHECK(std::ranges::find(Named(graph, "Feature").Definition.DefineSymbols, "HAS_PACKAGE") !=
          Named(graph, "Feature").Definition.DefineSymbols.end());
    for (const auto version : {"1.1.9", "1.2.0-preview.1", "2.0.0"})
    {
        context.ResourceVersions[0].Version = version;
        CHECK(resolve().size() == 1);
    }
    context.ResourceVersions[0].Version = "1.9.0+build.3";
    CHECK(resolve().size() == 2);
    context.DefineSymbols = {"DISABLE"};
    CHECK(resolve().size() == 1);
    context.ResourceVersions.clear();
    context.DefineSymbols = {"MANUAL"};
    CHECK(resolve().size() == 2);
    context.DefineSymbols.clear();
    CHECK(resolve().size() == 1);
    feature.Definition.Packages = {{"example.package", "1.5.0"}};
    CHECK(resolve().size() == 2);
    for (const auto expression : {"[1.5.0]", "(1.4,1.6]", "[1.5,)", "(,2.0)", "1.5", ""})
    {
        feature.Definition.VersionDefines[0].Expression = expression;
        CHECK(resolve().size() == 2);
    }
    for (const auto expression : {"[2,1]", "(1,1)", "[broken]", "[1,2", "[1,2,3]"})
    {
        feature.Definition.VersionDefines[0].Expression = expression;
        CHECK_THROWS(Keire::ManagedAssemblyAsset::Validate(feature.Definition));
    }
    feature.Definition.VersionDefines.clear();
    for (const auto condition : {"", "A && B", "A ||", "!!A", "A;B"})
    {
        feature.Definition.DefineConstraints = {condition};
        CHECK_THROWS(Keire::ManagedAssemblyAsset::Validate(feature.Definition));
    }
}

TEST_CASE("Assembly policy schema round trips and rejects lossy legacy encoding")
{
    auto definition = Assembly("Feature").Definition;
    definition.IncludePlatforms = {"Windows", "Linux"};
    definition.DefineConstraints = {"A || !B"};
    definition.VersionDefines = {{"Keire", "[1.0,)", "MODERN"}};
    definition.OverrideReferences = true;
    definition.PrecompiledReferences = {"Assets/Plugins/Math.dll"};
    const auto decoded =
        Keire::ManagedAssemblyAsset::Decode(Keire::ManagedAssemblyAsset::Encode(definition))->Definition();
    CHECK(decoded.IncludePlatforms == definition.IncludePlatforms);
    CHECK(decoded.DefineConstraints == definition.DefineConstraints);
    CHECK(decoded.VersionDefines == definition.VersionDefines);
    CHECK(decoded.OverrideReferences);
    CHECK(decoded.PrecompiledReferences == definition.PrecompiledReferences);
    definition.SchemaVersion = 3;
    CHECK_THROWS_AS((void)Keire::ManagedAssemblyAsset::Encode(definition), std::invalid_argument);
    definition.SchemaVersion = 4;
    for (const auto path : {"../Library/Escape.dll", "Assets/../Escape.dll", "Assets/Plugins/Native.so"})
    {
        definition.PrecompiledReferences = {path};
        CHECK_THROWS(Keire::ManagedAssemblyAsset::Validate(definition));
    }
}

TEST_CASE("Managed runtime hosts can reopen after complete shutdown")
{
    KeireTests::TemporaryAssetProject project;
    Keire::ScriptSystemSpecification specification;
    specification.Mode = Keire::ScriptMode::Enabled;
    specification.ProjectRoot = project.Root;
    const std::string configuration =
        std::string(KEIRE_BUILD_CONFIGURATION) == "Release" || std::string(KEIRE_BUILD_CONFIGURATION) == "Dist"
            ? "Release"
            : "Debug";
    specification.RuntimeHostDirectory = std::filesystem::absolute("Build/Dependencies/coral/Build/" + configuration);
    specification.RuntimeRootDirectory = std::filesystem::absolute("Build/Dependencies/dotnet-sdk");
    specification.ManagedApiAssembly = std::filesystem::absolute("Build/Managed/Keire.Managed.dll");
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        INFO(cycle);
        auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
        REQUIRE(scripts->RuntimeHostAvailable());
        scripts->Close();
        CHECK_FALSE(scripts->IsOpen());
        scripts->Close();
    }
}

TEST_CASE("Managed precompiled DLL references compile copy dependencies and enforce overrides")
{
    KeireTests::TemporaryAssetProject project;
#if defined(_WIN32)
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet.exe");
#else
    const auto dotnet = std::filesystem::absolute("Build/Dependencies/dotnet-sdk/dotnet");
#endif
    REQUIRE(std::filesystem::is_regular_file(dotnet));
    Keire::ScriptSystemSpecification spec;
    spec.Mode = Keire::ScriptMode::Enabled;
    spec.ProjectRoot = project.Root;
    spec.SdkSelection = Keire::ManagedSdkSelection::Custom;
    spec.DotnetExecutable = dotnet;
    spec.RuntimeRootDirectory = dotnet.parent_path();
    spec.ManagedApiAssembly = std::filesystem::absolute("Build/Managed/Keire.Managed.dll");
    const std::string configuration =
        std::string(KEIRE_BUILD_CONFIGURATION) == "Release" || std::string(KEIRE_BUILD_CONFIGURATION) == "Dist"
            ? "Release"
            : "Debug";
    spec.RuntimeHostDirectory = std::filesystem::absolute("Build/Dependencies/coral/Build/" + configuration);
    auto scripts = Keire::CreateRef<Keire::ScriptSystem>(spec);
    project.Write("Vendor/Math.cs", "namespace Vendor; public abstract class Base : Keire.Behaviour {} public static "
                                    "class Math { public static int Value => 42; }");
    auto vendor = Assembly("Vendor");
    Keire::ManagedBuildRequest request;
    request.Assemblies = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{vendor});
    auto operation = scripts->StartBuild(request);
    REQUIRE(scripts->WaitForBuild(operation, std::chrono::seconds(90)));
    auto status = scripts->BuildStatus();
    for (const auto& diagnostic : status.Diagnostics)
        INFO(diagnostic.Message);
    REQUIRE(status.State == Keire::ManagedBuildState::Succeeded);
    std::filesystem::create_directories(project.Root / "Assets/Plugins");
    std::filesystem::copy_file(status.ActiveAssemblyDirectory / "Vendor.dll",
                               project.Root / "Assets/Plugins/Vendor.dll");
    std::filesystem::remove(project.Root / "Assets/Vendor/Math.cs");
    project.Write("Game/Use.cs", "[Keire.StableComponentId(\"4e866523-e93c-4db0-99f8-a28c11820cde\")] public sealed "
                                 "class Use : Vendor.Base { public int Value => Vendor.Math.Value; protected override "
                                 "void Start() { if (Value != 42) throw new System.Exception(); } }");
    project.Write("Plugins/Native.dll", "not a managed assembly");
    auto game = Assembly("Game");
    request.Assemblies = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{game});
    CHECK(Named(request.Assemblies, "Game").PrecompiledFiles ==
          std::vector<std::filesystem::path>{"Assets/Plugins/Vendor.dll"});
    operation = scripts->StartBuild(request);
    REQUIRE(scripts->WaitForBuild(operation, std::chrono::seconds(90)));
    status = scripts->BuildStatus();
    for (const auto& diagnostic : status.Diagnostics)
        INFO(diagnostic.Message);
    REQUIRE(status.State == Keire::ManagedBuildState::Succeeded);
    CHECK(std::filesystem::is_regular_file(status.ActiveAssemblyDirectory / "Vendor.dll"));
    Keire::ManagedReloadRequest reload;
    reload.ManagedApiAssembly = status.ManagedApiAssembly;
    reload.Assemblies = status.RuntimeAssemblies;
    const bool prepared = scripts->PrepareReload(reload);
    INFO(scripts->ReloadStatus().Diagnostic);
    REQUIRE(prepared);
    scripts->CommitReload();
    const auto instance = scripts->CreateBehaviour("Use", 1, Keire::AssetId::Generate());
    CHECK_NOTHROW(scripts->InvokeBehaviour(instance, Keire::ManagedBehaviourCallback::Start));
    CHECK(scripts->DestroyBehaviour(instance));
    const auto workspace = scripts->GenerateIdeWorkspace(request, "DLLs");
    CHECK_FALSE(workspace.Projects.empty());
    game.Definition.OverrideReferences = true;
    request.Assemblies = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{game});
    CHECK(Named(request.Assemblies, "Game").PrecompiledFiles.empty());
    operation = scripts->StartBuild(request);
    REQUIRE(scripts->WaitForBuild(operation, std::chrono::seconds(90)));
    CHECK(scripts->BuildStatus().State == Keire::ManagedBuildState::Failed);
    game.Definition.PrecompiledReferences = {"Assets/Plugins/Vendor.dll"};
    request.Assemblies = Keire::ResolveProjectManagedAssemblies(project.Root, std::array{game});
    operation = scripts->StartBuild(request);
    REQUIRE(scripts->WaitForBuild(operation, std::chrono::seconds(90)));
    CHECK(scripts->BuildStatus().State == Keire::ManagedBuildState::Succeeded);
    game.Definition.PrecompiledReferences = {"Assets/Plugins/Native.dll"};
    CHECK_THROWS_AS((void)Keire::ResolveProjectManagedAssemblies(project.Root, std::array{game}),
                    std::invalid_argument);
    scripts->Close();
}
