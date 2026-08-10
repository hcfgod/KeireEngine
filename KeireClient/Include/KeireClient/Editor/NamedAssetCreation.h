#pragma once

#include <cstdint>
#include <string_view>

namespace KeireEditor
{
    enum class NamedAssetCreationKind : std::uint8_t
    {
        None,
        Scene,
        Material,
        AnimationGraph,
        Script,
        ManagedAssembly,
        ManagedData,
        AudioMixer,
        PhysicsMaterial,
        VfxEffect,
        MaterialGraph,
        ShaderGraph,
        MaterialInstance,
        Prefab,
        PrefabVariant,
        Shader,
        InputActions
    };

    [[nodiscard]] std::string_view NamedAssetCreationDisplayName(NamedAssetCreationKind kind) noexcept;
} // namespace KeireEditor
