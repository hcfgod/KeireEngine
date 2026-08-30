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
        ProceduralMotionProfile,
        Script,
        ScriptableObjectScript,
        ManagedAssembly,
        ManagedData,
        AudioMixer,
        PhysicsMaterial,
        VfxEffect,
        UiDocument,
        MaterialGraph,
        ShaderGraph,
        MaterialFunction,
        ShaderFunction,
        MaterialLayer,
        MaterialLayerBlend,
        MaterialParameterCollection,
        MaterialInstance,
        Prefab,
        PrefabVariant,
        Shader,
        InputActions
    };

    [[nodiscard]] std::string_view NamedAssetCreationDisplayName(NamedAssetCreationKind kind) noexcept;
} // namespace KeireEditor
