#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/TemplateManager.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <ranges>

using namespace KeireHub;

namespace
{
    constexpr auto FixedId = "11111111-1111-4111-8111-111111111111";
    constexpr auto OtherId = "22222222-2222-4222-8222-222222222222";
    constexpr auto FixedTimestamp = "2026-08-06T12:34:56Z";
    constexpr auto HelloSha256 = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] TemplateManagerServices Services(const std::string& id = FixedId,
                                                   const std::uint64_t available = 1024ULL * 1024ULL * 1024ULL)
    {
        return {.GenerateProjectId = [id] { return HubResult<std::string>::Success(id); },
                .CurrentUtcTimestamp = [] { return HubResult<std::string>::Success(FixedTimestamp); },
                .AvailableSpace = [available](const std::filesystem::path&)
                { return HubResult<std::uint64_t>::Success(available); }};
    }

    [[nodiscard]] std::filesystem::path BuiltInTemplates()
    {
        return std::filesystem::current_path() / "KeireHubContent" / "Templates";
    }

    [[nodiscard]] TemplateCreateRequest Request(const std::filesystem::path& destination,
                                                std::string templateId = "keire.empty")
    {
        return {.TemplateId = std::move(templateId),
                .ProjectName = "Test Project",
                .Destination = destination,
                .EditorVersion = Version("0.1.0"),
                .EditorMinimumProjectSchema = 1,
                .EditorMaximumProjectSchema = 3,
                .PlatformTarget = "desktop",
                .HostPlatform = "windows",
                .HostArchitecture = "x86_64"};
    }

    [[nodiscard]] TemplateCreateRequest SandboxRequest(const std::filesystem::path& destination)
    {
        auto request = Request(destination, "keire.sandbox");
        request.EditorVersion = Version("0.3.0");
        return request;
    }

    void WriteFixtureCatalog(const std::filesystem::path& root, const std::uint64_t size = 5,
                             const std::string_view sha256 = HelloSha256,
                             const std::string_view payloadPath = "README.md",
                             const std::string_view requiredPackages = "[]")
    {
        std::filesystem::create_directories(root / "Payload");
        const auto document = std::string(R"({
          "schemaVersion":1,
          "templates":[{
            "id":"test.template","version":"1.0.0","displayName":"Test Template",
            "description":"A deterministic test template.","category":"core","tags":[],
            "thumbnail":"thumbnail.svg","screenshots":[],"compatibleEditors":"^1.0.0",
            "projectSchema":3,"platformTarget":"desktop","estimatedSizeBytes":65536,
            "payloadRoot":"Payload","payloadFiles":[{"path":")") +
                              std::string(payloadPath) + R"(","sizeBytes":)" + std::to_string(size) + R"(,"sha256":")" +
                              std::string(sha256) +
                              R"("}],"defaultProjectConfiguration":{},"starterContent":[],"requiredPackages":)" +
                              std::string(requiredPackages) +
                              R"(,"recommendedPackages":[],"licenses":[],"featured":false
          }]
        })";
        KeireHubTests::WriteText(root / "catalog.json", document);
    }

    [[nodiscard]] std::map<std::string, std::string, std::less<>> ReadTree(const std::filesystem::path& root)
    {
        std::map<std::string, std::string, std::less<>> result;
        for (std::filesystem::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator)
        {
            if (iterator->is_regular_file())
            {
                result.emplace(iterator->path().lexically_relative(root).generic_string(),
                               KeireHubTests::ReadText(iterator->path()));
            }
        }
        return result;
    }

    [[nodiscard]] bool HasStagingDirectory(const std::filesystem::path& parent)
    {
        return std::ranges::any_of(std::filesystem::directory_iterator(parent), [](const auto& entry)
                                   { return entry.path().filename().string().starts_with(".keire-stage-"); });
    }

    [[nodiscard]] PackageManifest RequiredPackage()
    {
        return {.Id = "template.dependency",
                .Version = Version("1.2.0"),
                .Kind = PackageKind::Template,
                .DisplayName = "Template Dependency",
                .Channel = "stable",
                .Platform = "windows",
                .Architecture = "x86_64",
                .ArtifactSizeBytes = 1,
                .ArtifactSha256 = KeireHubTests::Digest(),
                .InstalledSizeBytes = 1,
                .Files = {{"payload.bin", 1, KeireHubTests::Digest('b')}},
                .SignatureKeyId = "release-key"};
    }
} // namespace

TEST_CASE("Project-name validation matches the creation preflight contract")
{
    CHECK(IsValidProjectName("Project"));
    CHECK(IsValidProjectName("Kéire Sandbox"));
    CHECK_FALSE(IsValidProjectName(""));
    CHECK_FALSE(IsValidProjectName("."));
    CHECK_FALSE(IsValidProjectName(".."));
    CHECK_FALSE(IsValidProjectName(" Project"));
    CHECK_FALSE(IsValidProjectName("Project "));
    CHECK_FALSE(IsValidProjectName("Project\tName"));
    CHECK_FALSE(IsValidProjectName("Project/Name"));
    CHECK_FALSE(IsValidProjectName(std::string(129, 'a')));
}

TEST_CASE("Template manager loads the three manifest-driven built-in templates")
{
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    REQUIRE(manager.Snapshot()->size() == 3);
    CHECK(std::ranges::find(*manager.Snapshot(), "keire.empty", &HubTemplateManifest::Id) != manager.Snapshot()->end());
    CHECK(std::ranges::find(*manager.Snapshot(), "keire.3d-starter", &HubTemplateManifest::Id) !=
          manager.Snapshot()->end());
    CHECK(std::ranges::find(*manager.Snapshot(), "keire.sandbox", &HubTemplateManifest::Id) !=
          manager.Snapshot()->end());
}

TEST_CASE("Project creation is byte-reproducible with deterministic identity and time providers")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager first(BuiltInTemplates(), Services());
    TemplateManager second(BuiltInTemplates(), Services());
    REQUIRE(first.Load());
    REQUIRE(second.Load());
    auto firstRequest = Request(temporary.Path() / "First", "keire.3d-starter");
    auto secondRequest = Request(temporary.Path() / "Second", "keire.3d-starter");
    REQUIRE(first.CreateProject(firstRequest));
    REQUIRE(second.CreateProject(secondRequest));
    CHECK(ReadTree(firstRequest.Destination) == ReadTree(secondRequest.Destination));
    CHECK(std::filesystem::is_regular_file(firstRequest.Destination / "Assets/Scenes/StarterScene.keirescene"));
    CHECK(
        std::filesystem::is_regular_file(firstRequest.Destination / "Assets/Scenes/StarterScene.keirescene.keiremeta"));
    const auto descriptor = nlohmann::json::parse(
        KeireHubTests::ReadText(firstRequest.Destination / "ProjectSettings/Project.keireproject"));
    CHECK(descriptor.at("startupScene") == "10000000-0000-4000-8000-000000000001");
}

TEST_CASE("Created projects receive schema-three identity and template provenance")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    auto request = Request(temporary.Path() / "Project", "keire.empty");
    bool validatedBeforePublish = false;
    request.ValidateStagedProject = [&](const std::filesystem::path& staging)
    {
        validatedBeforePublish =
            std::filesystem::is_regular_file(staging / "ProjectSettings" / "Project.keireproject") &&
            !std::filesystem::exists(request.Destination);
        return HubStatus::Success();
    };
    auto result = manager.CreateProject(request);
    REQUIRE(result);
    CHECK(validatedBeforePublish);
    CHECK(result.Value().ProjectId == FixedId);
    const auto descriptor = nlohmann::json::parse(
        KeireHubTests::ReadText(request.Destination / "ProjectSettings" / "Project.keireproject"));
    CHECK(descriptor.at("schemaVersion") == 3);
    CHECK(descriptor.at("id") == FixedId);
    CHECK(descriptor.at("name") == "Test Project");
    CHECK(descriptor.at("createdAt") == FixedTimestamp);
    CHECK(descriptor.at("createdWithEngineVersion") == "0.1.0");
    CHECK(descriptor.at("lastSavedWithEngineVersion") == "0.1.0");
    CHECK(descriptor.at("template").at("id") == "keire.empty");
    CHECK(descriptor.at("template").at("version") == "1.0.0");
}

TEST_CASE("Sandbox creation copies packaged clean content and never mutates its source")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto nextId = std::make_shared<int>(0);
    auto services = Services();
    services.GenerateProjectId = [nextId]
    { return HubResult<std::string>::Success((*nextId)++ == 0 ? FixedId : OtherId); };
    TemplateManager manager(BuiltInTemplates(), std::move(services));
    REQUIRE(manager.Load());
    const auto source = BuiltInTemplates() / "Payloads" / "Sandbox";
    const auto sourceBefore = ReadTree(source);
    auto firstRequest = SandboxRequest(temporary.Path() / "SandboxOne");
    auto secondRequest = SandboxRequest(temporary.Path() / "SandboxTwo");
    auto first = manager.CreateProject(firstRequest);
    auto second = manager.CreateProject(secondRequest);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.Value().ProjectId != second.Value().ProjectId);
    CHECK(ReadTree(source) == sourceBefore);
    CHECK_FALSE(std::filesystem::exists(source / "ProjectSettings" / "Project.keireproject"));
    CHECK(std::filesystem::exists(first.Value().Root / "Assets/Scenes/ShaderMaterialShowcase.keirescene"));
    CHECK(std::filesystem::exists(first.Value().Root / "Assets/Scenes/SampleScene.keirescene"));
    CHECK(std::filesystem::exists(first.Value().Root /
                                  "Assets/Materials/MaterialGraphs/01_BasicPaint_Shader.keireshadergraph"));
    CHECK(std::filesystem::exists(first.Value().Root /
                                  "Assets/Materials/MaterialGraphs/09_HolographicVoronoi.keirematerialgraph"));
    CHECK(std::filesystem::exists(first.Value().Root / "Assets/Vfx/ArcaneNova.keirevfx"));
    CHECK(std::filesystem::exists(first.Value().Root / "Assets/Scripts/Runtime/FirstPersonCamera.cs"));
    CHECK(std::filesystem::exists(first.Value().Root / "Assets/Audio/InterfaceConfirm.wav"));
    CHECK_FALSE(std::filesystem::exists(first.Value().Root / "Assets/Generated"));
    const auto descriptor =
        nlohmann::json::parse(KeireHubTests::ReadText(first.Value().Root / "ProjectSettings/Project.keireproject"));
    CHECK(descriptor.at("startupScene") == "a1aa0000-0000-4000-8000-000000000001");
    CHECK(descriptor.at("defaultInput") == "97b38693-6dc3-4f06-a228-44ba5786e8d1");
    CHECK(descriptor.at("createdWithEngineVersion") == "0.3.0");
    CHECK(descriptor.at("minimumEngineVersion") == "0.3.0");
    CHECK(descriptor.at("template").at("version") == "1.1.0");
}

TEST_CASE("Sandbox requires the Material Ecosystem capable editor line")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());

    auto legacyRequest = Request(temporary.Path() / "LegacySandbox", "keire.sandbox");
    auto legacy = manager.Preflight(legacyRequest);
    REQUIRE_FALSE(legacy);
    CHECK(legacy.Error().Code == HubErrorCode::TemplateIncompatible);

    auto currentRequest = SandboxRequest(temporary.Path() / "CurrentSandbox");
    CHECK(manager.Preflight(currentRequest));
}

TEST_CASE("Preflight rejects incompatible editor versions schemas platforms and unavailable versions")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    auto request = Request(temporary.Path() / "Project", "keire.empty");
    request.EditorVersion = Version("2.0.0");
    auto version = manager.Preflight(request);
    REQUIRE_FALSE(version);
    CHECK(version.Error().Code == HubErrorCode::TemplateIncompatible);

    request.EditorVersion = Version("0.1.0");
    request.EditorMaximumProjectSchema = 2;
    auto schema = manager.Preflight(request);
    REQUIRE_FALSE(schema);
    CHECK(schema.Error().Code == HubErrorCode::TemplateIncompatible);

    request.EditorMaximumProjectSchema = 3;
    request.PlatformTarget = "mobile";
    auto platform = manager.Preflight(request);
    REQUIRE_FALSE(platform);
    CHECK(platform.Error().Code == HubErrorCode::TemplateIncompatible);

    request.PlatformTarget = "desktop";
    request.TemplateVersion = Version("9.0.0");
    auto missing = manager.Preflight(request);
    REQUIRE_FALSE(missing);
    CHECK(missing.Error().Code == HubErrorCode::TemplateNotFound);
}

TEST_CASE("Destination conflicts preserve existing content and create no staging directory")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto destination = temporary.Path() / "Existing";
    std::filesystem::create_directory(destination);
    KeireHubTests::WriteText(destination / "keep.txt", "preserve me");
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    const auto result = manager.CreateProject(Request(destination));
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(destination / "keep.txt") == "preserve me");
    CHECK_FALSE(HasStagingDirectory(temporary.Path()));
}

TEST_CASE("Project creation rejects destinations inside protected application roots")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto applicationRoot = temporary.Path() / "Installed Hub";
    const auto projectParent = applicationRoot / "Projects";
    std::filesystem::create_directories(projectParent);
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());

    auto request = Request(projectParent / "Unsafe");
    request.ForbiddenDestinationRoots = {applicationRoot};
    const auto result = manager.CreateProject(request);

    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::InvalidArgument);
    CHECK(result.Error().Message == "Projects cannot be created inside the installed Hub directory.");
    CHECK_FALSE(std::filesystem::exists(request.Destination));
    CHECK_FALSE(HasStagingDirectory(projectParent));
}

TEST_CASE("Project creation cancellation removes staging and never publishes the destination")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    const auto destination = temporary.Path() / "Cancelled";
    bool cancelled = false;
    auto request = Request(destination);
    request.CancellationRequested = [&] { return cancelled; };
    request.ValidateStagedProject = [&](const std::filesystem::path&)
    {
        cancelled = true;
        return HubStatus::Success();
    };

    const auto result = manager.CreateProject(request);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(HasStagingDirectory(temporary.Path()));
}

TEST_CASE("Editor validation failure rolls back the complete staging transaction")
{
    KeireHubTests::TemporaryDirectory temporary;
    TemplateManager manager(BuiltInTemplates(), Services());
    REQUIRE(manager.Load());
    auto request = SandboxRequest(temporary.Path() / "Rejected");
    request.ValidateStagedProject = [](const std::filesystem::path& staging)
    {
        KeireHubTests::WriteText(staging / "validator-output.txt", "temporary");
        return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                   .Message = "Editor rejected project.",
                                   .TechnicalDetails = "fixture rejection"});
    };
    const auto result = manager.CreateProject(request);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::ProjectValidationFailed);
    CHECK_FALSE(std::filesystem::exists(request.Destination));
    CHECK_FALSE(HasStagingDirectory(temporary.Path()));
}

TEST_CASE("Template payload inventory rejects undeclared files traversal collisions and size changes")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Templates";
    WriteFixtureCatalog(root);
    KeireHubTests::WriteText(root / "Payload" / "README.md", "hello");
    TemplateManager valid(root, Services());
    REQUIRE(valid.Load());

    KeireHubTests::WriteText(root / "Payload" / "undeclared.txt", "unexpected");
    TemplateManager undeclared(root, Services());
    auto undeclaredStatus = undeclared.Load();
    REQUIRE_FALSE(undeclaredStatus);
    CHECK(undeclaredStatus.Error().Code == HubErrorCode::TemplatePayloadInvalid);
    std::filesystem::remove(root / "Payload" / "undeclared.txt");

    KeireHubTests::WriteText(root / "Payload" / "README.md", "changed");
    TemplateManager changed(root, Services());
    CHECK_FALSE(changed.Load());

    WriteFixtureCatalog(root, 5, HelloSha256, "../outside.txt");
    TemplateManager traversal(root, Services());
    CHECK_FALSE(traversal.Load());

    KeireHubTests::WriteText(root / "Payload" / "README.md", "hello");
    auto catalog = KeireHubTests::ReadText(root / "catalog.json");
    const auto insertion = catalog.find("],\"defaultProjectConfiguration\"");
    REQUIRE(insertion != std::string::npos);
    catalog.insert(insertion, R"(,{"path":"README.MD","sizeBytes":5,"sha256":")" + std::string(HelloSha256) + R"("})");
    KeireHubTests::WriteText(root / "catalog.json", catalog);
    TemplateManager collision(root, Services());
    CHECK_FALSE(collision.Load());

    WriteFixtureCatalog(root);
    auto componentCatalog = nlohmann::json::parse(KeireHubTests::ReadText(root / "catalog.json"));
    componentCatalog["templates"][0]["payloadFiles"] = {
        {{"path", "Folder/A.txt"}, {"sizeBytes", 5}, {"sha256", HelloSha256}},
        {{"path", "folder/B.txt"}, {"sizeBytes", 5}, {"sha256", HelloSha256}}};
    KeireHubTests::WriteText(root / "catalog.json", componentCatalog.dump());
    TemplateManager componentCollision(root, Services());
    CHECK_FALSE(componentCollision.Load());

    WriteFixtureCatalog(root);
    auto oversizedCatalog = nlohmann::json::parse(KeireHubTests::ReadText(root / "catalog.json"));
    oversizedCatalog["templates"][0]["estimatedSizeBytes"] = 16ULL * 1024ULL * 1024ULL * 1024ULL + 1ULL;
    KeireHubTests::WriteText(root / "catalog.json", oversizedCatalog.dump());
    TemplateManager oversized(root, Services());
    CHECK_FALSE(oversized.Load());
}

TEST_CASE("Template preflight resolves required packages and accounts for disk space")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Templates";
    WriteFixtureCatalog(root, 5, HelloSha256, "README.md",
                        R"([{"packageId":"template.dependency","version":"^1.0.0"}])");
    KeireHubTests::WriteText(root / "Payload" / "README.md", "hello");
    TemplateManager manager(root, Services());
    REQUIRE(manager.Load());
    auto request = Request(temporary.Path() / "Project", "test.template");
    request.EditorVersion = Version("1.0.0");
    auto missing = manager.Preflight(request);
    REQUIRE_FALSE(missing);
    CHECK(missing.Error().Code == HubErrorCode::PackageMissingDependency);

    request.AvailablePackages = {RequiredPackage()};
    auto plan = manager.Preflight(request);
    REQUIRE(plan);
    REQUIRE(plan.Value().RequiredPackages.InstallOrder.size() == 1);
    CHECK(plan.Value().RequiredPackages.InstallOrder.front().Id == "template.dependency");

    TemplateManager noSpace(root, Services(FixedId, 1024));
    REQUIRE(noSpace.Load());
    auto insufficient = noSpace.Preflight(request);
    REQUIRE_FALSE(insufficient);
    CHECK(insufficient.Error().Code == HubErrorCode::InsufficientDiskSpace);
}
