#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>

namespace
{
    constexpr std::array RetiredUiComponentTypes{
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554943ULL, 0x414e564153000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554952ULL, 0x4543545452410001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554954ULL, 0x4558540000000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554949ULL, 0x4d41474500000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554942ULL, 0x5554544f4e000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b4549524555494cULL, 0x41594f5554000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554953ULL, 0x4c49444552000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554954ULL, 0x4f47474c45000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554949ULL, 0x4e505554464c4401ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554953ULL, 0x43524f4c4c000001ULL)),
        Keire::ComponentTypeId(Keire::AssetId(0x4b45495245554941ULL, 0x4343455353000001ULL))};

    class ReservedUiProbeComponent final : public Keire::Component
    {
      public:
        explicit ReservedUiProbeComponent(const Keire::ComponentTypeId type) : Component(type) {}
    };

    [[nodiscard]] Keire::ComponentRegistration ReservedRegistration(const Keire::ComponentTypeId type)
    {
        Keire::ComponentRegistration result;
        result.Type = type;
        result.Name = "Retired UI Probe";
        result.Factory = [type]
        { return Keire::Ref<Keire::Component>(Keire::CreateRef<ReservedUiProbeComponent>(type)); };
        result.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
        result.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
        return result;
    }
} // namespace

TEST_CASE("UI Document component stores one visual tree and panel contract")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registration = registry->Find(Keire::UiDocumentComponent::StaticType());
    REQUIRE(registration);
    CHECK(registration->Name == "UI Document");
    CHECK(registration->Category == "UI Toolkit");
    CHECK(registration->SchemaVersion == 1);

    const auto visualTree = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    const auto panelSettings = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000002");
    const auto source = Keire::CreateRef<Keire::UiDocumentComponent>();
    source->SetVisualTree(visualTree);
    source->SetPanelSettings(panelSettings);
    source->SetSortingOrder(17);
    source->SetReceivesInput(false);

    const auto values = registration->Serialize(*source);
    CHECK(std::get<Keire::AssetId>(values.at("visualTree")) == visualTree);
    CHECK(std::get<Keire::AssetId>(values.at("panelSettings")) == panelSettings);
    CHECK(std::get<std::int64_t>(values.at("sortingOrder")) == 17);
    CHECK_FALSE(std::get<bool>(values.at("receivesInput")));

    const auto restored = registration->Factory();
    registration->Deserialize(*restored, values, registration->SchemaVersion);
    const auto document = Keire::DynamicRefCast<Keire::UiDocumentComponent>(restored);
    REQUIRE(document);
    CHECK(document->VisualTree() == visualTree);
    CHECK(document->PanelSettings() == panelSettings);
    CHECK(document->SortingOrder() == 17);
    CHECK_FALSE(document->ReceivesInput());

    auto invalid = values;
    invalid.insert_or_assign("sortingOrder", static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
    CHECK_THROWS_AS(registration->Deserialize(*restored, invalid, registration->SchemaVersion), std::invalid_argument);
    CHECK(document->SortingOrder() == 17);
}

TEST_CASE("UI Document is the only registered scene UI component and retired IDs remain reserved")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto registrations = registry->Registrations();
    CHECK(std::ranges::count_if(registrations,
                                [](const auto& registration) { return registration.Category == "UI Toolkit"; }) == 1);
    CHECK(registry->Contains(Keire::UiDocumentComponent::StaticType()));
    CHECK_FALSE(Keire::ComponentRegistry::IsReservedType(Keire::UiDocumentComponent::StaticType()));

    for (const auto type : RetiredUiComponentTypes)
    {
        CAPTURE(type.ToString());
        CHECK(Keire::ComponentRegistry::IsReservedType(type));
        CHECK_FALSE(registry->Contains(type));
        CHECK_FALSE(registry->Find(type));
        const auto revision = registry->Revision();
        CHECK_THROWS_WITH_AS(registry->Register(ReservedRegistration(type)),
                             "Component type ID is permanently reserved and cannot be registered.",
                             std::invalid_argument);
        CHECK(registry->Revision() == revision);
    }

    const std::array removal{RetiredUiComponentTypes.front()};
    CHECK_THROWS_WITH_AS(registry->ReplaceBatch(removal, {}),
                         "A permanently reserved component type ID cannot be replaced or removed.",
                         std::invalid_argument);
    CHECK_THROWS_WITH_AS(registry->ReplaceBatch({}, {ReservedRegistration(RetiredUiComponentTypes.front())}),
                         "A permanently reserved component type ID cannot be replaced or removed.",
                         std::invalid_argument);
}
