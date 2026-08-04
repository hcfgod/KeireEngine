#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class TestModule final : public Keire::EngineModule
    {
      public:
        TestModule(Keire::ModuleDescriptor descriptor, std::vector<std::string>* lifecycle = nullptr,
                   const bool failRegistration = false)
            : m_Descriptor(std::move(descriptor)), m_Lifecycle(lifecycle), m_FailRegistration(failRegistration)
        {
        }

        [[nodiscard]] Keire::ModuleDescriptor Descriptor() const override { return m_Descriptor; }

        void Register(Keire::ModuleRegistrationContext& context) override
        {
            if (!m_FailRegistration)
                return;
            context.RegisterMemoryDomain({m_Descriptor.Id, {}});
            throw std::runtime_error("registration failed");
        }

        void OnStart(Keire::Application&) override
        {
            if (m_Lifecycle)
                m_Lifecycle->push_back("start:" + m_Descriptor.Id);
        }

        void OnStop() noexcept override
        {
            if (m_Lifecycle)
                m_Lifecycle->push_back("stop:" + m_Descriptor.Id);
        }

      private:
        Keire::ModuleDescriptor m_Descriptor;
        std::vector<std::string>* m_Lifecycle = nullptr;
        bool m_FailRegistration = false;
    };

    struct TemporaryDirectory final
    {
        explicit TemporaryDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    void DowngradeToVersion1(const std::filesystem::path& root)
    {
        const auto path = root / "ProjectSettings" / "Project.keireproject";
        auto document = nlohmann::json::parse(Keire::Detail::ReadTextFile(path, 1024U * 1024U));
        document["schemaVersion"] = 1;
        document.erase("requiredModules");
        Keire::Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
    }
} // namespace

TEST_CASE("Source modules resolve dependencies deterministically and stop in reverse order")
{
    std::vector<std::string> lifecycle;
    Keire::ModuleDescriptor base{.Id = "sample.base", .DisplayName = "Base", .Version = {1, 2, 0}};
    Keire::ModuleDescriptor gameplay{.Id = "sample.gameplay", .DisplayName = "Gameplay", .Version = {2, 0, 0}};
    gameplay.Dependencies.push_back({"sample.base", Keire::ModuleVersionRange::Parse("^1.0.0")});

    Keire::ModuleRegistrySpecification specification;
    specification.Modules = {Keire::CreateRef<TestModule>(gameplay, &lifecycle),
                             Keire::CreateRef<TestModule>(base, &lifecycle)};
    auto registry = Keire::CreateRef<Keire::ModuleRegistry>(std::move(specification));
    const auto catalog = registry->OrderedCatalog();
    REQUIRE(catalog.size() == 2);
    CHECK(catalog[0].Id == "sample.base");
    CHECK(catalog[1].Id == "sample.gameplay");
    const std::array compatible{Keire::RequiredSourceModule{"sample.gameplay", ">=2.0.0 <3.0.0"}};
    registry->ValidateRequired(compatible);
    const std::array incompatible{Keire::RequiredSourceModule{"sample.base", ">=2.0.0"}};
    CHECK_THROWS_AS(registry->ValidateRequired(incompatible), std::runtime_error);

    Keire::Application application;
    registry->Start(application);
    registry->Close();
    CHECK(lifecycle == std::vector<std::string>{"start:sample.base", "start:sample.gameplay", "stop:sample.gameplay",
                                                "stop:sample.base"});
}

TEST_CASE("Source modules reject cycles and discard failed registration")
{
    Keire::ModuleDescriptor left{.Id = "cycle.left", .DisplayName = "Left", .Version = {1, 0, 0}};
    Keire::ModuleDescriptor right{.Id = "cycle.right", .DisplayName = "Right", .Version = {1, 0, 0}};
    left.Dependencies.push_back({"cycle.right", {}});
    right.Dependencies.push_back({"cycle.left", {}});
    CHECK_THROWS_AS(Keire::CreateRef<Keire::ModuleRegistry>(Keire::ModuleRegistrySpecification{
                        {Keire::CreateRef<TestModule>(left), Keire::CreateRef<TestModule>(right)}}),
                    std::invalid_argument);

    Keire::ModuleDescriptor failing{.Id = "sample.failing", .DisplayName = "Failing", .Version = {1, 0, 0}};
    CHECK_THROWS_WITH_AS(Keire::CreateRef<Keire::ModuleRegistry>(Keire::ModuleRegistrySpecification{
                             {Keire::CreateRef<TestModule>(failing, nullptr, true)}}),
                         "registration failed", std::runtime_error);
}

TEST_CASE("Project schema upgrades are dry-run first and publish transactionally")
{
    TemporaryDirectory directory("ProjectUpgradeTransaction");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    DowngradeToVersion1(root);
    const auto descriptor = root / "ProjectSettings" / "Project.keireproject";
    const auto original = Keire::Detail::ReadTextFile(descriptor, 1024U * 1024U);

    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::UpgradeAvailable);
    Keire::ProjectUpgradeService upgrades(root);
    const auto plan = upgrades.Plan();
    REQUIRE(plan.Steps.size() == 1);
    CHECK(plan.CurrentSchema == 1);
    CHECK(plan.TargetSchema == 2);
    CHECK(plan.EstimatedBackupBytes == original.size());
    CHECK(Keire::Detail::ReadTextFile(descriptor, 1024U * 1024U) == original);

    upgrades.Apply(plan);
    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::Ready);
    const auto opened = Keire::Project::Open(root);
    CHECK(opened->Descriptor().SchemaVersion == 2);
    CHECK(opened->Descriptor().RequiredModules.empty());
    const auto backups = root / "Library" / "ProjectUpgrades" / "Backups";
    std::size_t backupCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(backups))
        if (entry.is_directory())
            ++backupCount;
    CHECK(backupCount == 1);
}

TEST_CASE("Project upgrade paths are confined to the project")
{
    TemporaryDirectory directory("ProjectUpgradeConfinement");
    const auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    Keire::ProjectUpgradeStep unsafe;
    unsafe.Id = "unsafe";
    unsafe.FromSchema = 2;
    unsafe.ToSchema = 3;
    unsafe.AffectedPaths = {"../outside"};
    unsafe.Apply = [](const Keire::ProjectUpgradeStepContext&) {};
    CHECK_THROWS_AS(Keire::ProjectUpgradeService(project->Root(), {std::move(unsafe)}), std::invalid_argument);
}
