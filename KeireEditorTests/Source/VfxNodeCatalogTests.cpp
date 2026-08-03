#include "KeireClient/Editor/VfxNodeCatalog.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace
{
    [[nodiscard]] KeireEditor::VfxNodeCatalogEntry Entry(std::string id, std::string name, std::string category,
                                                         std::string type)
    {
        return {
            .Id = std::move(id),
            .Name = std::move(name),
            .Category = std::move(category),
            .TypeName = std::move(type),
        };
    }

    [[nodiscard]] std::vector<std::string> MatchNames(const KeireEditor::VfxNodeSearchIndex& catalog,
                                                      const std::vector<KeireEditor::VfxNodeCatalogMatch>& matches)
    {
        std::vector<std::string> result;
        const auto entries = catalog.Entries();
        result.reserve(matches.size());
        for (const auto& match : matches)
            result.push_back(entries[match.EntryIndex].Name);
        return result;
    }
} // namespace

TEST_CASE("VFX node catalog enforces stable searchable metadata")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    CHECK(catalog.Add(Entry("keire.context.update", "Update", "Contexts", "Context")) == 0);
    CHECK(catalog.Find("keire.context.update") != nullptr);
    CHECK(catalog.Find("missing") == nullptr);

    CHECK_THROWS_AS((void)catalog.Add(Entry("keire.context.update", "Duplicate", "Contexts", "Context")),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)catalog.Add(Entry("", "Missing ID", "Contexts", "Context")), std::invalid_argument);

    auto unavailable = Entry("keire.context.event", "Event", "Contexts", "Context");
    unavailable.CpuSupported = false;
    unavailable.GpuSupported = false;
    unavailable.Support = KeireEditor::VfxNodeCatalogSupportLevel::Unsupported;
    CHECK_THROWS_AS((void)catalog.Add(unavailable), std::invalid_argument);
    unavailable.DisabledReason = "Event contexts are not supported.";
    CHECK(catalog.Add(unavailable) == 1);
    CHECK_FALSE(catalog.Entries()[1].Enabled());

    catalog.Clear();
    CHECK(catalog.Entries().empty());
}

TEST_CASE("VFX node catalog ranks names aliases types categories and fallback text deterministically")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    (void)catalog.Add(Entry("exact", "Force", "Update", "Runtime Module"));
    (void)catalog.Add(Entry("prefix", "Force Field", "Update", "Runtime Module"));

    auto alias = Entry("alias", "Acceleration", "Update", "Runtime Module");
    alias.Aliases = {"Force"};
    (void)catalog.Add(std::move(alias));

    (void)catalog.Add(Entry("type", "Gravity Scale", "Update", "Force"));
    (void)catalog.Add(Entry("category", "Drag", "Force", "Runtime Module"));

    auto contains = Entry("contains", "Reinforce Tint", "Color", "Operator");
    contains.Description = "Applies a force-like tint response.";
    (void)catalog.Add(std::move(contains));

    CHECK(MatchNames(catalog, catalog.Search({.Text = "FoRcE"})) ==
          std::vector<std::string>{"Force", "Force Field", "Acceleration", "Gravity Scale", "Drag", "Reinforce Tint"});
    CHECK(MatchNames(catalog, catalog.Search({.Text = "runtime force"})) ==
          std::vector<std::string>{"Force", "Force Field", "Acceleration", "Drag"});
}

TEST_CASE("VFX node catalog searches aliases keywords and typed pin names")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    auto vector = Entry("normalize", "Normalize", "Operators / Vector", "Operator");
    vector.Aliases = {"Unit Vector"};
    vector.Keywords = {"Direction", "Magnitude"};
    vector.InputTypes = {Keire::VfxValueType::Vector3};
    vector.OutputTypes = {Keire::VfxValueType::Vector3};
    (void)catalog.Add(std::move(vector));

    CHECK(catalog.Search({.Text = "unit vector"}).size() == 1);
    CHECK(catalog.Search({.Text = "magnitude"}).size() == 1);
    CHECK(catalog.Search({.Text = "float3"}).size() == 1);
    CHECK(catalog.Search({.Text = "operators/vector"}).size() == 1);
    CHECK(catalog.Search({.Text = "unrelated"}).empty());
}

TEST_CASE("VFX node catalog filters context pin direction backend and availability")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    auto update = Entry("update-scalar", "Update Scalar", "Operators", "Operator");
    update.Contexts = {Keire::VfxContextType::Update};
    update.InputTypes = {Keire::VfxValueType::Scalar};
    update.OutputTypes = {Keire::VfxValueType::Vector3};
    update.GpuSupported = false;
    (void)catalog.Add(std::move(update));

    auto spawn = Entry("spawn-vector", "Spawn Vector", "Operators", "Operator");
    spawn.Contexts = {Keire::VfxContextType::Spawn};
    spawn.InputTypes = {Keire::VfxValueType::Vector3};
    spawn.CpuSupported = false;
    (void)catalog.Add(std::move(spawn));

    auto parameter = Entry("parameter", "Speed", "Blackboard", "Parameter");
    parameter.OutputTypes = {Keire::VfxValueType::Scalar};
    (void)catalog.Add(std::move(parameter));

    auto disabled = Entry("disabled", "Disabled Scalar", "Operators", "Operator");
    disabled.InputTypes = {Keire::VfxValueType::Scalar};
    disabled.DisabledReason = "Already placed.";
    (void)catalog.Add(std::move(disabled));

    CHECK(MatchNames(catalog, catalog.Search({.Context = Keire::VfxContextType::Update})) ==
          std::vector<std::string>{"Disabled Scalar", "Speed", "Update Scalar"});
    CHECK(MatchNames(catalog, catalog.Search({.PinType = Keire::VfxValueType::Scalar,
                                              .PinDirection = KeireEditor::VfxNodeCatalogPinDirection::Input})) ==
          std::vector<std::string>{"Disabled Scalar", "Update Scalar"});
    CHECK(MatchNames(catalog, catalog.Search({.PinType = Keire::VfxValueType::Scalar,
                                              .PinDirection = KeireEditor::VfxNodeCatalogPinDirection::Output})) ==
          std::vector<std::string>{"Speed"});
    CHECK(MatchNames(catalog, catalog.Search({.Backend = Keire::VfxBackend::Gpu})) ==
          std::vector<std::string>{"Disabled Scalar", "Spawn Vector", "Speed"});

    const auto inclusive = catalog.Search(
        {.Backend = Keire::VfxBackend::Gpu, .IncludeDisabled = false, .IncludeUnsupportedBackend = true});
    CHECK(MatchNames(catalog, inclusive) == std::vector<std::string>{"Spawn Vector", "Speed", "Update Scalar"});
    CHECK(inclusive.back().BackendSupported == false);
    CHECK_THROWS_AS((void)catalog.Search({.PinDirection = KeireEditor::VfxNodeCatalogPinDirection::Input}),
                    std::invalid_argument);
}

TEST_CASE("VFX node catalog usage metadata provides favorites and recents tie breakers")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    (void)catalog.Add(Entry("alpha", "Alpha", "Operators", "Operator"));
    (void)catalog.Add(Entry("beta", "Beta", "Operators", "Operator"));
    (void)catalog.Add(Entry("gamma", "Gamma", "Operators", "Operator"));

    CHECK(catalog.SetUsage("beta", {.Favorite = true, .LastUsedSequence = 1, .UseCount = 1}));
    CHECK(catalog.SetUsage("gamma", {.LastUsedSequence = 9, .UseCount = 4}));
    CHECK_FALSE(catalog.SetUsage("missing", {.Favorite = true}));
    CHECK(MatchNames(catalog, catalog.Search()) == std::vector<std::string>{"Beta", "Gamma", "Alpha"});
}

TEST_CASE("VFX node catalog exposes concise backend support badges")
{
    auto both = Entry("both", "Both", "Operators", "Operator");
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(both) == "CPU + GPU");

    both.GpuSupported = false;
    both.Support = KeireEditor::VfxNodeCatalogSupportLevel::Experimental;
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(both) == "CPU / Experimental");

    both.CpuSupported = false;
    both.Support = KeireEditor::VfxNodeCatalogSupportLevel::Unsupported;
    both.DisabledReason = "Not implemented.";
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(both) == "Unsupported");
}

TEST_CASE("VFX node catalog adapts compiler descriptors without owning execution metadata")
{
    const auto* descriptor = Keire::FindVfxNodeDescriptor("keire.operator.add");
    REQUIRE(descriptor != nullptr);
    const auto entry = KeireEditor::BuildVfxNodeCatalogEntry(*descriptor);
    CHECK(entry.Id == descriptor->TypeId.Value);
    CHECK(entry.Name == "Add");
    CHECK(entry.Category == "Operator/Math/Arithmetic");
    CHECK(entry.TypeName == "Operator");
    CHECK(entry.InputTypes == std::vector<Keire::VfxValueType>{Keire::VfxValueType::Scalar});
    CHECK(entry.OutputTypes == std::vector<Keire::VfxValueType>{Keire::VfxValueType::Scalar});
    CHECK(entry.Contexts == descriptor->ValidContexts);
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(entry) == "CPU + GPU");

    auto cpuOnly = *descriptor;
    cpuOnly.BackendTier = Keire::VfxNodeBackendTier::CpuOnly;
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(KeireEditor::BuildVfxNodeCatalogEntry(cpuOnly)) == "CPU");

    auto gpuRequired = *descriptor;
    gpuRequired.BackendTier = Keire::VfxNodeBackendTier::GpuRequired;
    gpuRequired.SupportTier = Keire::VfxNodeSupportTier::GpuRequired;
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(KeireEditor::BuildVfxNodeCatalogEntry(gpuRequired)) ==
          "GPU / GPU Required");

    auto unavailable = *descriptor;
    unavailable.SupportTier = Keire::VfxNodeSupportTier::Disabled;
    unavailable.DisabledReason = "Awaiting backend implementation.";
    const auto unavailableEntry = KeireEditor::BuildVfxNodeCatalogEntry(unavailable);
    CHECK_FALSE(unavailableEntry.Enabled());
    CHECK(unavailableEntry.DisabledReason == unavailable.DisabledReason);
    CHECK(KeireEditor::VfxNodeCatalogSupportBadge(unavailableEntry) == "Unsupported");
}

TEST_CASE("VFX node catalog exposes type-correct Split variants for filtered search")
{
    KeireEditor::VfxNodeSearchIndex catalog;
    for (const auto typeId : {"keire.operator.split-vector2", "keire.operator.split-vector3",
                              "keire.operator.split-vector4", "keire.operator.split-color"})
    {
        const auto* descriptor = Keire::FindVfxNodeDescriptor(typeId);
        REQUIRE(descriptor != nullptr);
        const auto entry = KeireEditor::BuildVfxNodeCatalogEntry(*descriptor);
        CHECK(entry.Enabled());
        CHECK(KeireEditor::VfxNodeCatalogSupportBadge(entry) == "CPU + GPU");
        (void)catalog.Add(entry);
    }

    const auto vector2Matches = catalog.Search({.Text = "split",
                                                .PinType = Keire::VfxValueType::Vector2,
                                                .PinDirection = KeireEditor::VfxNodeCatalogPinDirection::Input});
    REQUIRE(vector2Matches.size() == 1);
    CHECK(catalog.Entries()[vector2Matches.front().EntryIndex].Id == "keire.operator.split-vector2");
    CHECK(catalog.Entries()[vector2Matches.front().EntryIndex].OutputTypes ==
          std::vector<Keire::VfxValueType>{Keire::VfxValueType::Scalar});

    const auto colorMatches = catalog.Search({.Text = "components",
                                              .PinType = Keire::VfxValueType::Color,
                                              .PinDirection = KeireEditor::VfxNodeCatalogPinDirection::Input});
    REQUIRE(colorMatches.size() == 1);
    CHECK(catalog.Entries()[colorMatches.front().EntryIndex].Id == "keire.operator.split-color");
}

TEST_CASE("VFX graph node kind labels cover every schema-4 node class")
{
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Context) == "Context");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Module) == "Runtime Module");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Parameter) == "Blackboard Parameter");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::CustomHlsl) == "Custom HLSL");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Operator) == "Operator");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Attribute) == "Attribute");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind::Subgraph) == "Subgraph");
    CHECK(KeireEditor::VfxGraphNodeKindLabel(static_cast<Keire::VfxGraphNodeKind>(255)) == "Unknown");
}
