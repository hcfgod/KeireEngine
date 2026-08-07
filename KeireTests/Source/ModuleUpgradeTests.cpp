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
        auto document = nlohmann::json::parse(Keire::Detail::ReadTextFile(path, std::size_t{1024} * 1024U));
        document["schemaVersion"] = 1;
        document.erase("requiredModules");
        Keire::Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
    }

    void WriteUpgradeJournal(const std::filesystem::path& root, const std::string_view phase)
    {
        const auto active = root / "Library" / "ProjectUpgrades" / "Active";
        std::filesystem::create_directories(active);
        const nlohmann::json journal{
            {"schemaVersion", 1},
            {"transactionId", "interrupted-test"},
            {"phase", phase},
            {"fromSchema", 1},
            {"targetSchema", 2},
            {"published", 0},
            {"steps", {"keire.project.v1-to-v2-required-source-modules"}},
            {"files", {{{"path", "ProjectSettings/Project.keireproject"}, {"existed", true}}}}};
        Keire::Detail::WriteTextFileAtomically(active / "journal.json", journal.dump(2) + '\n');
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

TEST_CASE("An empty source module catalog has a stable package-compatible fingerprint")
{
    const auto first = Keire::CreateRef<Keire::ModuleRegistry>();
    const auto second = Keire::CreateRef<Keire::ModuleRegistry>();

    CHECK(first->OrderedCatalog().empty());
    CHECK_FALSE(first->Fingerprint().empty());
    CHECK(first->Fingerprint() == second->Fingerprint());
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

TEST_CASE("Module semantic-version ranges follow caret compatibility rules without integer wraparound")
{
    const auto stable = Keire::ModuleVersionRange::Parse("^1.2.3");
    CHECK(stable.Contains({1, 2, 3}));
    CHECK(stable.Contains({1, 99, 99}));
    CHECK_FALSE(stable.Contains({2, 0, 0}));

    const auto preOneMinor = Keire::ModuleVersionRange::Parse("^0.2.3");
    CHECK(preOneMinor.Contains({0, 2, 99}));
    CHECK_FALSE(preOneMinor.Contains({0, 3, 0}));

    const auto preOnePatch = Keire::ModuleVersionRange::Parse("^0.0.3");
    CHECK(preOnePatch.Contains({0, 0, 3}));
    CHECK_FALSE(preOnePatch.Contains({0, 0, 4}));

    CHECK_THROWS_AS((void)Keire::ModuleVersionRange::Parse("^4294967295.0.0"), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::ModuleVersionRange::Parse("1.0.4294967295"), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::ModuleVersionRange::Parse("^0.4294967295.0"), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::ModuleVersionRange::Parse("^0.0.4294967295"), std::invalid_argument);
}

TEST_CASE("Project schema upgrades are dry-run first and publish transactionally")
{
    TemporaryDirectory directory("ProjectUpgradeTransaction");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    DowngradeToVersion1(root);
    const auto descriptor = root / "ProjectSettings" / "Project.keireproject";
    const auto original = Keire::Detail::ReadTextFile(descriptor, std::size_t{1024} * 1024U);

    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::UpgradeAvailable);
    Keire::ProjectUpgradeService upgrades(root);
    const auto plan = upgrades.Plan();
    REQUIRE(plan.Steps.size() == 1);
    CHECK(plan.CurrentSchema == 1);
    CHECK(plan.TargetSchema == 2);
    CHECK(plan.EstimatedBackupBytes == original.size());
    CHECK(Keire::Detail::ReadTextFile(descriptor, std::size_t{1024} * 1024U) == original);

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

TEST_CASE("Project upgrade recovery removes orphaned transaction state")
{
    TemporaryDirectory directory("ProjectUpgradeOrphanRecovery");
    const auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto active = project->Root() / "Library" / "ProjectUpgrades" / "Active";
    std::filesystem::create_directories(active / "staging");

    Keire::ProjectUpgradeService upgrades(project->Root());
    CHECK(upgrades.State() == Keire::ProjectUpgradeTransactionState::Interrupted);
    CHECK_NOTHROW(upgrades.Recover());
    CHECK(upgrades.State() == Keire::ProjectUpgradeTransactionState::Clean);
    CHECK_FALSE(std::filesystem::exists(active));
}

TEST_CASE("Project upgrade recovery resumes initialization from the unchanged project")
{
    TemporaryDirectory directory("ProjectUpgradeInitializingRecovery");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    DowngradeToVersion1(root);
    WriteUpgradeJournal(root, "initializing");

    Keire::ProjectUpgradeService upgrades(root);
    CHECK_NOTHROW(upgrades.Recover());
    CHECK(upgrades.State() == Keire::ProjectUpgradeTransactionState::Clean);
    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::Ready);
}

TEST_CASE("Project upgrade recovery rebuilds partial staging from its durable snapshot")
{
    TemporaryDirectory directory("ProjectUpgradePreparedRecovery");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    DowngradeToVersion1(root);
    const auto descriptor = root / "ProjectSettings" / "Project.keireproject";
    const auto active = root / "Library" / "ProjectUpgrades" / "Active";
    const auto relative = std::filesystem::path("ProjectSettings") / "Project.keireproject";
    WriteUpgradeJournal(root, "prepared");
    Keire::Detail::WriteTextFileAtomically(active / "before" / relative,
                                           Keire::Detail::ReadTextFile(descriptor, std::size_t{1024} * 1024U));
    Keire::Detail::WriteTextFileAtomically(active / "staging" / relative, "partially-written staging data");

    Keire::ProjectUpgradeService upgrades(root);
    CHECK_NOTHROW(upgrades.Recover());
    CHECK(upgrades.State() == Keire::ProjectUpgradeTransactionState::Clean);
    CHECK(Keire::Project::Inspect(root) == Keire::ProjectStatus::Ready);
}

TEST_CASE("Project upgrade recovery rejects unsafe persisted transaction identifiers")
{
    TemporaryDirectory directory("ProjectUpgradeJournalValidation");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    DowngradeToVersion1(root);
    WriteUpgradeJournal(root, "initializing");
    const auto journalPath = root / "Library" / "ProjectUpgrades" / "Active" / "journal.json";
    auto journal = nlohmann::json::parse(Keire::Detail::ReadTextFile(journalPath, std::size_t{1024} * 1024U));
    journal["transactionId"] = "../../escaped";
    Keire::Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');

    Keire::ProjectUpgradeService upgrades(root);
    CHECK_THROWS_AS(upgrades.Recover(), std::runtime_error);
    CHECK(upgrades.State() == Keire::ProjectUpgradeTransactionState::Interrupted);
    CHECK_FALSE(std::filesystem::exists(root / "Library" / "escaped"));
}

TEST_CASE("Project upgrades reject affected paths through symbolic links")
{
    TemporaryDirectory directory("ProjectUpgradeSymlinkConfinement");
    const auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto outside = directory.Path / "Outside";
    std::filesystem::create_directories(outside);
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, project->Root() / "Assets" / "Escape", linkError);
    if (linkError)
    {
        MESSAGE("Symbolic-link creation is unavailable in this environment: " << linkError.message());
        return;
    }

    Keire::ProjectUpgradeStep unsafe;
    unsafe.Id = "unsafe-symlink";
    unsafe.FromSchema = 2;
    unsafe.ToSchema = 3;
    unsafe.AffectedPaths = {"Assets/Escape/outside.txt"};
    unsafe.Apply = [](const Keire::ProjectUpgradeStepContext&) {};
    CHECK_THROWS_AS(Keire::ProjectUpgradeService(project->Root(), {std::move(unsafe)}), std::invalid_argument);
}

TEST_CASE("Project upgrades reject linked transaction storage outside the project")
{
    TemporaryDirectory directory("ProjectUpgradeTransactionSymlinkConfinement");
    auto project = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    project.Reset();
    const auto outside = directory.Path / "OutsideLibrary";
    std::filesystem::create_directories(outside);
    std::filesystem::remove_all(root / "Library");
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, root / "Library", linkError);
    if (linkError)
    {
        MESSAGE("Symbolic-link creation is unavailable in this environment: " << linkError.message());
        return;
    }

    CHECK_THROWS_AS((void)Keire::ProjectUpgradeService(root), std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(outside / "ProjectUpgrades"));
}
