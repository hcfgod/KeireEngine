#include "Keire/Assets/BuiltinAssetRegistry.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Assets/PhysicsMaterialAsset.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "Keire/Vfx/VfxSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <unordered_set>

TEST_CASE("Built-in asset registration is deterministic and covers authored production assets")
{
    const auto firstImporters = Keire::CreateBuiltinAssetImporters();
    const auto secondImporters = Keire::CreateBuiltinAssetImporters();
    REQUIRE(firstImporters.size() == secondImporters.size());

    std::unordered_set<std::string> names;
    std::unordered_set<std::string> extensions;
    for (std::size_t index = 0; index < firstImporters.size(); ++index)
    {
        CHECK(firstImporters[index].Name == secondImporters[index].Name);
        CHECK(names.emplace(firstImporters[index].Name).second);
        for (const auto& extension : firstImporters[index].Extensions)
            CHECK(extensions.emplace(extension).second);
    }
    CHECK(extensions.contains(".keireanimgraph"));
    CHECK(extensions.contains(".keireavatarmask"));
    CHECK(extensions.contains(".keireanim"));
    CHECK(extensions.contains(".keiremixer"));
    CHECK(extensions.contains(".keirephysicsmaterial"));
    CHECK(extensions.contains(".keiredata"));
    CHECK(extensions.contains(".keirevfx"));
    CHECK(extensions.contains(".mp3"));
    CHECK(extensions.contains(".aac"));
    CHECK(extensions.contains(".m4a"));
    CHECK(extensions.contains(".mp4"));
    CHECK(extensions.contains(".mkv"));
    CHECK(extensions.contains(".webm"));
    CHECK(extensions.contains(".opus"));
    CHECK(extensions.contains(".wma"));
    CHECK(extensions.contains(".aiff"));
    CHECK(std::ranges::any_of(firstImporters,
                              [](const Keire::AssetImporterRegistration& value)
                              {
                                  return std::ranges::find(value.CompatibleTypes,
                                                           Keire::AnimationSourceAsset::StaticType()) !=
                                         value.CompatibleTypes.end();
                              }));

    auto decoders = Keire::CreateBuiltinAssetDecoders();
    const auto originalSize = decoders.size();
    Keire::AppendMissingBuiltinAssetDecoders(decoders);
    CHECK(decoders.size() == originalSize);
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::AnimationGraphAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::AnimationSourceAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::AvatarMaskAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::AudioMixerAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::PhysicsMaterialAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::ManagedDataAsset::StaticType(); }));
    CHECK(std::ranges::any_of(decoders, [](const Keire::AssetDecoderRegistration& value)
                              { return value.Type == Keire::VfxEffectAsset::StaticType(); }));
}
