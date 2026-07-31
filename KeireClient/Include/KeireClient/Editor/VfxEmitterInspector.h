#pragma once

#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct VfxEmitterInspectorCallbacks final
    {
        std::function<void(Keire::AssetId parameter, std::string_view message, bool warning)> Status;
        std::function<bool(Keire::AssetId parameter)> Reset;
        std::function<bool(Keire::AssetId parameter)> RemoveStale;
    };

    class VfxEmitterInspector final
    {
      public:
        [[nodiscard]] static std::size_t
        VisibleEntryCount(const Keire::VfxEffectDefinition& effect,
                          std::span<const Keire::VfxParameterOverride> overrides) noexcept;

        [[nodiscard]] bool Draw(IPropertyEditor& editor, const Keire::VfxEffectDefinition& effect,
                                std::vector<Keire::VfxParameterOverride>& overrides,
                                const VfxEmitterInspectorCallbacks& callbacks) const;

        [[nodiscard]] static std::string SerializeOverrides(const Keire::ComponentRegistration& registration,
                                                            const Keire::ComponentPropertyBag& componentValues,
                                                            std::span<const Keire::VfxParameterOverride> overrides);
    };
} // namespace KeireEditor
