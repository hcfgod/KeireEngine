#pragma once

#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClientInternal/Editor/WaterBodyAuthoring.h"

#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor::Detail
{
    /// Uses the caller's scene snapshot history, including both creation and configured surface state.
    template <typename Record>
    [[nodiscard]] Keire::EntityId CreateWaterEntity(SceneDocument& document, const WaterSetupPreset preset,
                                                    const Keire::EntityId parent, Record&& record)
    {
        Keire::WaterBodySettings settings;
        ApplyWaterSetup(settings, preset);
        constexpr std::array names{"Sea", "Lake", "Pond"};
        const std::string name = names[static_cast<std::size_t>(preset)];
        const auto prepared = Keire::CreateRef<Keire::WaterBodyComponent>();
        prepared->Configure(settings);
        const auto values = Keire::CreateWaterBodyComponentRegistration().Serialize(*prepared);
        const auto history = document.History();
        if (!history || !history->IsOpen())
        {
            throw std::runtime_error("Creating water requires an open scene history.");
        }
        auto transaction = history->BeginTransaction("Create " + name);
        record("Create " + name);
        const auto created = document.CreateEntity(name, parent, Keire::WaterBodyComponent::StaticType());
        const auto component = document.ActiveScene()->FindEntity(created).GetComponent<Keire::WaterBodyComponent>();
        document.SetComponentValues(created, component, values);
        transaction->Commit();
        return created;
    }

    struct WaterEditTarget
    {
        Keire::AssetId Entity;
        Keire::Ref<Keire::WaterBodyComponent> Component;
    };

    [[nodiscard]] inline std::vector<WaterEditTarget>
    ResolveWaterTargets(const Keire::Ref<Keire::Scene>& scene, const std::span<const Keire::AssetId> entities,
                        const std::size_t ordinal)
    {
        std::vector<WaterEditTarget> targets;
        targets.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto components =
                scene->FindEntity(Keire::EntityId(entity)).GetComponents(Keire::WaterBodyComponent::StaticType());
            if (ordinal >= components.size())
            {
                throw std::invalid_argument("Every selected object must contain this Water Body component.");
            }
            targets.push_back({entity, Keire::DynamicRefCast<Keire::WaterBodyComponent>(components[ordinal])});
            if (!targets.back().Component)
            {
                throw std::invalid_argument("Water Body registration returned an incompatible component.");
            }
        }
        return targets;
    }

    template <typename Edit, typename Record>
    void ApplyWaterEdit(SceneDocument& document, const std::vector<WaterEditTarget>& targets, const std::size_t ordinal,
                        const std::string_view name, Edit&& edit, Record&& record)
    {
        std::vector<Keire::ComponentPropertyBag> changes;
        changes.reserve(targets.size());
        bool anyChanges = false;
        for (const auto& target : targets)
        {
            const auto& before = target.Component->Settings();
            auto candidate = before;
            edit(candidate);
            auto update = Detail::WaterBodyPropertyChanges(before, candidate);
            anyChanges = anyChanges || !update.empty();
            changes.push_back(std::move(update));
        }
        if (!anyChanges)
        {
            return;
        }
        const auto history = document.History();
        if (!history || !history->IsOpen())
        {
            throw std::runtime_error("Water editing requires an open scene history.");
        }
        // The existing scene snapshot command restores all targets if any document edit fails.
        // Its transaction destructor cancels on exceptional exit and preserves the original exception.
        auto transaction = history->BeginTransaction(std::string(name));
        record(name);
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            if (!changes[index].empty())
            {
                const std::array entity{targets[index].Entity};
                document.SetComponentsProperties(entity, Keire::WaterBodyComponent::StaticType(), changes[index],
                                                 ordinal);
            }
        }
        transaction->Commit();
    }

} // namespace KeireEditor::Detail
