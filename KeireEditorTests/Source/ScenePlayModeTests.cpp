#include "KeireClient/Editor/ManagedRuntimeSessionResolver.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Scenes/SceneSystem.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct RuntimeWorldLifecycleProbe
    {
        Keire::SceneRuntimeWorld* World = nullptr;
        std::vector<std::string> Callbacks;
        std::vector<bool> WorldReady;
    };

    class RuntimeWorldLifecycleProbeComponent final : public Keire::Component
    {
      public:
        explicit RuntimeWorldLifecycleProbeComponent(std::shared_ptr<RuntimeWorldLifecycleProbe> probe)
            : Component(StaticType()), m_Probe(std::move(probe))
        {
        }

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x656469746f722d77ULL, 0x6f726c642d6c6966ULL));
        }

      protected:
        void Awake() override { Observe("Awake"); }
        void OnEnable() override { Observe("OnEnable"); }

      private:
        void Observe(std::string callback)
        {
            const bool worldReady =
                m_Probe->World && m_Probe->World->Active() &&
                m_Probe->World->QueryName("Lookup Target", Keire::SceneQueryScope::Active).size() == 1;
            m_Probe->Callbacks.push_back(std::move(callback));
            m_Probe->WorldReady.push_back(worldReady);
        }

        std::shared_ptr<RuntimeWorldLifecycleProbe> m_Probe;
    };

    [[nodiscard]] Keire::ComponentRegistration
    MakeRuntimeWorldLifecycleProbeRegistration(const std::shared_ptr<RuntimeWorldLifecycleProbe>& probe)
    {
        Keire::ComponentRegistration result;
        result.Type = RuntimeWorldLifecycleProbeComponent::StaticType();
        result.Name = "Runtime World Lifecycle Probe";
        result.Factory = [probe]
        { return Keire::Ref<Keire::Component>(Keire::CreateRef<RuntimeWorldLifecycleProbeComponent>(probe)); };
        result.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
        result.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
        return result;
    }
} // namespace

TEST_CASE("scene document adopts its runtime session before Play lifecycle callbacks")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000061");
    const auto probe = std::make_shared<RuntimeWorldLifecycleProbe>();
    probe->World = world.Get();
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(MakeRuntimeWorldLifecycleProbeRegistration(probe));
    auto scene =
        Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("World play document"), registry);
    (void)scene->CreateEntity("Lookup Target");
    auto observer = scene->CreateEntity("Lifecycle Observer");
    REQUIRE(observer.AddComponent<RuntimeWorldLifecycleProbeComponent>());
    document.Open(scene, asset);

    REQUIRE_NOTHROW(document.BeginPlay({}, assets, {}, {}, {}, world));
    REQUIRE(document.PlaySession());
    REQUIRE(world->Active());
    CHECK(world->Session(world->Active()) == document.PlaySession());
    CHECK(world->Find(world->Active()) == document.ActiveScene());
    CHECK(probe->Callbacks == std::vector<std::string>{"Awake", "OnEnable"});
    CHECK(probe->WorldReady == std::vector<bool>{true, true});

    document.EndPlay();
    world->Close();
    scenes->Close();
    assets->Close();
    document.Close();
}

TEST_CASE("managed runtime services resolve the primary Play session before world adoption")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000062");
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Managed Play services"));
    const auto entity = scene->CreateEntity("Service consumer").Id();
    document.Open(scene, asset);

    REQUIRE_NOTHROW(document.BeginPlay({}, assets));
    const auto primary = document.PlaySession();
    REQUIRE(primary);
    REQUIRE(primary->RuntimeScene());
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, primary) == primary);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, primary, entity.Value()) == primary);

    const auto adopted = world->Adopt(primary);
    REQUIRE(adopted);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, {}) == primary);
    CHECK(KeireEditor::ResolveManagedRuntimeSession(world, {}, entity.Value()) == primary);

    document.EndPlay();
    world->Close();
    scenes->Close();
    assets->Close();
    document.Close();
}
