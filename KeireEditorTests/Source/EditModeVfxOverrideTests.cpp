#include "KeireClient/Editor/EditModeVfxPreview.h"

#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId EditVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x454449545646584fULL, value);
    }
} // namespace

TEST_CASE("Edit-mode VFX collection filters authored Blackboard overrides against the loaded effect")
{
    auto scene = Keire::CreateRef<Keire::Scene>(EditVfxId(1), Keire::SceneAsset::EmptyDefinition("Edit VFX overrides"));
    auto entity = scene->CreateEntity("Preview VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(EditVfxId(2));
    emitter->SetEditModePreview(true);
    emitter->SetParameterOverride({EditVfxId(20), 8.0F});
    emitter->SetParameterOverride({EditVfxId(21), Keire::Vector3{1.0F, 2.0F, 3.0F}});
    emitter->SetParameterOverride({EditVfxId(22), 3.0F});
    emitter->SetParameterOverride({EditVfxId(23), 4.0F});

    const auto collected = KeireEditor::CollectEditModeVfxEmitters(scene);
    REQUIRE(collected.size() == 1);
    REQUIRE(collected.front().ParameterOverrides.size() == 4);

    Keire::VfxEffectDefinition definition;
    definition.Blackboard = {
        {EditVfxId(20), "Rate", Keire::VfxValueType::Scalar, 2.0F, true},
        {EditVfxId(21), "Hidden Direction", Keire::VfxValueType::Vector3, Keire::Vector3{}, false},
        {EditVfxId(22), "Count", Keire::VfxValueType::Integer, std::int64_t{1}, true},
    };
    const auto compatible =
        KeireEditor::CompatibleEditModeVfxOverrides(definition, collected.front().ParameterOverrides);
    REQUIRE(compatible.size() == 1);
    CHECK(compatible.front() == (Keire::VfxParameterOverride{EditVfxId(20), 8.0F}));

    scene->Close();
}
