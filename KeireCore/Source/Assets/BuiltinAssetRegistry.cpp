#include "Keire/Assets/BuiltinAssetRegistry.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/ProceduralMotion.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/Assets/PhysicsMaterialAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "Keire/Ui/UiToolkit.h"
#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"
#include "Keire/Vfx/VfxVolumeAsset.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Keire
{
    std::vector<AssetImporterRegistration> CreateBuiltinAssetImporters()
    {
        std::vector<AssetImporterRegistration> result;
        result.emplace_back(CreateTextAssetImporter());
        result.emplace_back(CreateInputActionAssetImporter());
        result.emplace_back(CreateSceneAssetImporter());
        result.emplace_back(CreatePrefabAssetImporter());
        result.emplace_back(CreateManagedAssemblyAssetImporter());
        result.emplace_back(CreateShaderAssetImporter());
        result.emplace_back(CreateMaterialAssetImporter());
        result.emplace_back(CreateMaterialGraphAssetImporter());
        result.emplace_back(CreateMaterialInstanceAssetImporter());
        result.emplace_back(CreateMaterialFunctionAssetImporter());
        result.emplace_back(CreateShaderFunctionAssetImporter());
        result.emplace_back(CreateMaterialLayerAssetImporter());
        result.emplace_back(CreateMaterialLayerBlendAssetImporter());
        result.emplace_back(CreateMaterialParameterCollectionAssetImporter());
        result.emplace_back(CreateShaderGraphAssetImporter());
        result.emplace_back(CreateShaderGraphInstanceAssetImporter());
        result.emplace_back(CreateMeshAssetImporter());
        result.emplace_back(CreateTexture2DAssetImporter());
        result.emplace_back(CreateLightingTextureArrayAssetImporter());
        result.emplace_back(CreateLightProbeVolumeAssetImporter());
        result.emplace_back(CreateLightingSetAssetImporter());
        result.emplace_back(CreateAudioClipAssetImporter());
        result.emplace_back(CreateAnimationClipAssetImporter());
        result.emplace_back(CreateAnimationGraphAssetImporter());
        result.emplace_back(CreateAvatarMaskAssetImporter());
        result.emplace_back(CreateRigDefinitionAssetImporter());
        result.emplace_back(CreateProceduralMotionProfileAssetImporter());
        result.emplace_back(CreateAudioMixerAssetImporter());
        result.emplace_back(CreatePhysicsMaterialAssetImporter());
        result.emplace_back(CreateManagedDataAssetImporter());
        result.emplace_back(CreateUiVisualTreeAssetImporter());
        result.emplace_back(CreateUiStyleSheetAssetImporter());
        result.emplace_back(CreateUiPanelSettingsAssetImporter());
        result.emplace_back(CreateVfxEffectAssetImporter());
        result.emplace_back(CreateVfxSubgraphAssetImporter());
        result.emplace_back(CreateVfxVolumeAssetImporter());

        std::unordered_set<std::string> names;
        std::unordered_set<std::string> extensions;
        for (const auto& registration : result)
        {
            if (registration.Name.empty() || !registration.Type || !names.emplace(registration.Name).second)
                throw std::logic_error("Built-in asset importers must have unique names and valid asset types.");
            for (const auto& extension : registration.Extensions)
            {
                if (extension.empty() || !extensions.emplace(extension).second)
                    throw std::logic_error("Built-in source extensions must map to exactly one importer.");
            }
        }
        return result;
    }

    std::vector<AssetDecoderRegistration> CreateBuiltinAssetDecoders()
    {
        std::vector<AssetDecoderRegistration> result;
        result.emplace_back(CreateInputActionAssetDecoder());
        result.emplace_back(CreateSceneAssetDecoder());
        result.emplace_back(CreateShaderAssetDecoder());
        result.emplace_back(CreateMaterialAssetDecoder());
        result.emplace_back(CreateMaterialGraphAssetDecoder());
        result.emplace_back(CreateMaterialInstanceAssetDecoder());
        result.emplace_back(CreateMaterialFunctionAssetDecoder());
        result.emplace_back(CreateShaderFunctionAssetDecoder());
        result.emplace_back(CreateMaterialLayerAssetDecoder());
        result.emplace_back(CreateMaterialLayerBlendAssetDecoder());
        result.emplace_back(CreateMaterialParameterCollectionAssetDecoder());
        result.emplace_back(CreateShaderGraphAssetDecoder());
        result.emplace_back(CreateShaderGraphInstanceAssetDecoder());
        result.emplace_back(CreateMeshAssetDecoder());
        result.emplace_back(CreateTexture2DAssetDecoder());
        result.emplace_back(CreateLightingTextureArrayAssetDecoder());
        result.emplace_back(CreateLightProbeVolumeAssetDecoder());
        result.emplace_back(CreateLightingSetAssetDecoder());
        result.emplace_back(CreateAudioClipAssetDecoder());
        result.emplace_back(CreateSkeletonAssetDecoder());
        result.emplace_back(CreateSkinnedMeshAssetDecoder());
        result.emplace_back(CreateAnimationClipAssetDecoder());
        result.emplace_back(CreateAnimationSourceAssetDecoder());
        result.emplace_back(CreateAnimationGraphAssetDecoder());
        result.emplace_back(CreateAvatarMaskAssetDecoder());
        result.emplace_back(CreateRigDefinitionAssetDecoder());
        result.emplace_back(CreateProceduralMotionProfileAssetDecoder());
        result.emplace_back(CreatePrefabAssetDecoder());
        result.emplace_back(CreateManagedAssemblyAssetDecoder());
        result.emplace_back(CreateAudioMixerAssetDecoder());
        result.emplace_back(CreatePhysicsMaterialAssetDecoder());
        result.emplace_back(CreateManagedDataAssetDecoder());
        result.emplace_back(CreateUiVisualTreeAssetDecoder());
        result.emplace_back(CreateUiStyleSheetAssetDecoder());
        result.emplace_back(CreateUiPanelSettingsAssetDecoder());
        result.emplace_back(CreateVfxEffectAssetDecoder());
        result.emplace_back(CreateVfxSubgraphAssetDecoder());
        result.emplace_back(CreateVfxVolumeAssetDecoder());

        std::unordered_set<AssetTypeId> types;
        for (const auto& registration : result)
        {
            if (!registration.Type || !registration.Fallback || !registration.Decode ||
                !types.emplace(registration.Type).second)
            {
                throw std::logic_error("Built-in asset decoders must have unique types, fallbacks, and callbacks.");
            }
        }
        return result;
    }

    void AppendMissingBuiltinAssetDecoders(std::vector<AssetDecoderRegistration>& registrations)
    {
        auto builtins = CreateBuiltinAssetDecoders();
        for (auto& builtin : builtins)
        {
            if (std::ranges::find(registrations, builtin.Type, &AssetDecoderRegistration::Type) == registrations.end())
                registrations.emplace_back(std::move(builtin));
        }
    }
} // namespace Keire
