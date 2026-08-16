#include "KeireClient/Editor/NamedAssetCreation.h"

namespace KeireEditor
{
    std::string_view NamedAssetCreationDisplayName(const NamedAssetCreationKind kind) noexcept
    {
        switch (kind)
        {
        case NamedAssetCreationKind::Scene:
            return "scene";
        case NamedAssetCreationKind::Material:
            return "material";
        case NamedAssetCreationKind::AnimationGraph:
            return "Animator Controller";
        case NamedAssetCreationKind::ProceduralMotionProfile:
            return "Procedural Motion Profile";
        case NamedAssetCreationKind::Script:
            return "C# script";
        case NamedAssetCreationKind::ManagedAssembly:
            return "managed assembly";
        case NamedAssetCreationKind::ManagedData:
            return "managed data asset";
        case NamedAssetCreationKind::AudioMixer:
            return "audio mixer";
        case NamedAssetCreationKind::PhysicsMaterial:
            return "physics material";
        case NamedAssetCreationKind::VfxEffect:
            return "VFX effect";
        case NamedAssetCreationKind::MaterialGraph:
            return "material graph";
        case NamedAssetCreationKind::ShaderGraph:
            return "shader graph";
        case NamedAssetCreationKind::MaterialFunction:
            return "material function";
        case NamedAssetCreationKind::ShaderFunction:
            return "shader function";
        case NamedAssetCreationKind::MaterialLayer:
            return "material layer";
        case NamedAssetCreationKind::MaterialLayerBlend:
            return "material layer blend";
        case NamedAssetCreationKind::MaterialParameterCollection:
            return "material parameter collection";
        case NamedAssetCreationKind::MaterialInstance:
            return "material instance";
        case NamedAssetCreationKind::Prefab:
            return "prefab";
        case NamedAssetCreationKind::PrefabVariant:
            return "prefab variant";
        case NamedAssetCreationKind::Shader:
            return "shader";
        case NamedAssetCreationKind::InputActions:
            return "Input Actions asset";
        case NamedAssetCreationKind::None:
            return "asset";
        }
        return "asset";
    }
} // namespace KeireEditor
