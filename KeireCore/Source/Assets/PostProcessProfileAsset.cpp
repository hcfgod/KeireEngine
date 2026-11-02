#include "Keire/Assets/PostProcessProfileAsset.h"
#include "Keire/Assets/RenderingAssets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumProfileBytes = 64U * 1024U;
        constexpr std::array<PostProcessParameterDescription, static_cast<std::size_t>(PostProcessParameter::Count)>
            ParameterDescriptions{{
                {"exposureStops", "Exposure Compensation", "EV", 0.0, -20.0, 20.0, 0.1},
                {"bloomIntensity", "Bloom Intensity", "", 0.1, 0.0, 16.0, 0.01},
                {"bloomThreshold", "Bloom Threshold", "HDR", 1.0, 0.0, 65504.0, 0.1},
                {"bloomSoftKnee", "Bloom Soft Knee", "", 0.5, 0.0, 1.0, 0.01},
                {"bloomRadius", "Bloom Radius", "buffer px", 2.0, 0.5, 4.0, 0.1},
                {"temperature", "Temperature", "", 0.0, -100.0, 100.0, 1.0},
                {"tint", "Tint", "", 0.0, -100.0, 100.0, 1.0},
                {"contrast", "Contrast", "", 1.0, 0.0, 4.0, 0.01},
                {"saturation", "Saturation", "", 1.0, 0.0, 4.0, 0.01},
                {"redGain", "Red Gain", "", 1.0, 0.0, 8.0, 0.01},
                {"greenGain", "Green Gain", "", 1.0, 0.0, 8.0, 0.01},
                {"blueGain", "Blue Gain", "", 1.0, 0.0, 8.0, 0.01},
                {"vignetteIntensity", "Vignette Intensity", "", 0.3, 0.0, 1.0, 0.01},
                {"vignetteSmoothness", "Vignette Smoothness", "", 0.5, 0.01, 1.0, 0.01},
                {"vignetteRoundness", "Vignette Roundness", "", 1.0, 0.0, 1.0, 0.01},
                {"grainIntensity", "Grain Intensity", "", 0.1, 0.0, 1.0, 0.01},
                {"grainResponse", "Grain Highlight Suppression", "", 0.8, 0.0, 1.0, 0.01},
                {"chromaticAberration", "Chromatic Aberration", "px", 1.0, 0.0, 20.0, 0.1},
                {"focusDistance", "Focus Distance", "m", 10.0, 0.01, 1.0e12, 0.1},
                {"aperture", "Aperture", "f-stop", 5.6, 0.7, 64.0, 0.1},
                {"focalLength", "Focal Length", "mm", 50.0, 1.0, 1000.0, 1.0},
                {"depthOfFieldStrength", "Depth of Field Strength", "", 1.0, 0.0, 1.0, 0.01},
                {"motionBlurStrength", "Motion Blur Strength", "", 0.5, 0.0, 1.0, 0.01},
            }};
        constexpr std::array<std::string_view, static_cast<std::size_t>(PostProcessEffect::Count)> EffectKeys{
            "bloom", "colorGrading", "vignette", "filmGrain", "chromaticAberration", "depthOfField", "motionBlur"};

        void RequireObject(const Json& value, const char* description)
        {
            if (!value.is_object())
            {
                throw std::runtime_error(std::string("Post-processing profile requires an object for ") + description);
            }
        }
    } // namespace

    std::span<const PostProcessParameterDescription> PostProcessParameterDescriptions() noexcept
    {
        return ParameterDescriptions;
    }

    std::span<const std::string_view> PostProcessEffectKeys() noexcept { return EffectKeys; }

    std::uint32_t ValidatePostProcessColorLookup(const Texture2DAsset& texture)
    {
        const auto edge = texture.Height();
        if ((edge != 16 && edge != 32 && edge != 64) || texture.Width() != edge * edge)
        {
            throw std::invalid_argument("Color lookup requires a horizontal 16, 32 or 64 cube strip (N*N by N).");
        }
        const auto& settings = texture.Settings();
        if (settings.Semantic != TextureSemantic::Data || settings.ColorSpace != TextureColorSpace::Linear ||
            settings.HighDynamicRange || settings.Mips != TextureMipPolicy::None || texture.Mips().size() != 1)
        {
            throw std::invalid_argument("Color lookup requires a linear, non-HDR Data texture without mipmaps.");
        }
        const auto& sampler = settings.Sampler;
        if (sampler.Minimum != TextureFilter::Linear || sampler.Magnification != TextureFilter::Linear ||
            sampler.AddressU != TextureAddressMode::Clamp || sampler.AddressV != TextureAddressMode::Clamp ||
            sampler.AddressW != TextureAddressMode::Clamp || sampler.Anisotropy != 1)
        {
            throw std::invalid_argument("Color lookup requires linear filtering, clamp addressing and no anisotropy.");
        }
        return edge;
    }

    PostProcessProfileDefinition::PostProcessProfileDefinition()
    {
        for (std::size_t index = 0; index < Parameters.size(); ++index)
        {
            Parameters[index].Value = ParameterDescriptions[index].Default;
        }
    }

    PostProcessProfileAsset::PostProcessProfileAsset(PostProcessProfileDefinition definition)
        : m_Definition(std::move(definition))
    {
        Validate(m_Definition);
    }

    void PostProcessProfileAsset::Validate(const PostProcessProfileDefinition& definition)
    {
        if (definition.SchemaVersion < 1 || definition.SchemaVersion > 3)
        {
            throw std::invalid_argument("Post-processing profile has an unsupported schema.");
        }
        if (definition.SchemaVersion < 3 &&
            (definition.CustomEffects.Override || !definition.CustomEffects.Value.empty()))
        {
            throw std::invalid_argument("Custom effects require post-processing profile schema 3.");
        }
        if (definition.CustomEffects.Value.size() > MaximumPostProcessCustomEffects)
        {
            throw std::invalid_argument("Post-processing custom stack exceeds 16 effects.");
        }
        for (const auto& effect : definition.CustomEffects.Value)
        {
            if (!effect.Material || effect.Stage > PostProcessCustomStage::AfterUi)
            {
                throw std::invalid_argument("Custom effects require a material and a supported stage.");
            }
        }
        if (!std::isfinite(definition.ColorLookupWeight.Value) || definition.ColorLookupWeight.Value < 0.0 ||
            definition.ColorLookupWeight.Value > 1.0)
        {
            throw std::invalid_argument("Color lookup weight must be between zero and one.");
        }
        if (definition.SchemaVersion == 1 && (definition.ColorLookup != PostProcessOverride<AssetId>{} ||
                                              definition.ColorLookupWeight != PostProcessOverride<double>{false, 1.0}))
        {
            throw std::invalid_argument("Color lookup settings require post-processing profile schema 2.");
        }
        for (std::size_t index = 0; index < definition.Parameters.size(); ++index)
        {
            const auto& description = ParameterDescriptions[index];
            const double value = definition.Parameters[index].Value;
            if (!std::isfinite(value) || value < description.Minimum || value > description.Maximum)
            {
                throw std::invalid_argument("Post-processing parameter is outside its supported range: " +
                                            std::string(description.Key));
            }
        }
    }

    Ref<PostProcessProfileAsset> PostProcessProfileAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumProfileBytes)
        {
            throw std::runtime_error("Post-processing profile is empty or exceeds 64 KiB.");
        }
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            RequireObject(document, "document");
            if (!document.at("schemaVersion").is_number_integer() ||
                (document.at("schemaVersion") != 1 && document.at("schemaVersion") != 2 &&
                 document.at("schemaVersion") != 3))
            {
                throw std::runtime_error("Post-processing profile has an unsupported schema.");
            }
            const auto schema = document.at("schemaVersion").get<std::uint32_t>();
            if (document.size() != (schema == 1 ? 3U : schema == 2 ? 4U : 5U))
            {
                throw std::runtime_error("Post-processing profile contains missing or unsupported top-level fields.");
            }
            const auto& parameters = document.at("parameters");
            const auto& effects = document.at("effects");
            RequireObject(parameters, "parameters");
            RequireObject(effects, "effects");
            for (const auto& [key, value] : parameters.items())
            {
                (void)value;
                if (std::ranges::none_of(ParameterDescriptions,
                                         [&](const auto& description) { return description.Key == key; }))
                {
                    throw std::runtime_error("Unsupported post-processing parameter: " + key);
                }
            }
            for (const auto& [key, value] : effects.items())
            {
                (void)value;
                if (std::ranges::find(EffectKeys, key) == EffectKeys.end())
                {
                    throw std::runtime_error("Unsupported post-processing effect: " + key);
                }
            }
            PostProcessProfileDefinition definition;
            definition.SchemaVersion = schema;
            if (schema >= 2)
            {
                const auto& lookup = document.at("colorLookup");
                RequireObject(lookup, "color lookup");
                if (lookup.size() != 4 || !lookup.at("override").is_boolean() || !lookup.at("texture").is_string() ||
                    !lookup.at("weightOverride").is_boolean() || !lookup.at("weight").is_number())
                {
                    throw std::runtime_error(
                        "Color lookup requires texture, override, weight and weightOverride fields.");
                }
                definition.ColorLookup = {lookup.at("override").get<bool>(),
                                          AssetId::Parse(lookup.at("texture").get<std::string>())};
                definition.ColorLookupWeight = {lookup.at("weightOverride").get<bool>(),
                                                lookup.at("weight").get<double>()};
            }
            if (schema >= 3)
            {
                const auto& stack = document.at("customEffects");
                RequireObject(stack, "custom effects");
                if (stack.size() != 2 || !stack.at("override").is_boolean() || !stack.at("effects").is_array() ||
                    stack.at("effects").size() > MaximumPostProcessCustomEffects)
                {
                    throw std::runtime_error("Custom effects require an override and at most 16 entries.");
                }
                definition.CustomEffects.Override = stack.at("override").get<bool>();
                for (const auto& entry : stack.at("effects"))
                {
                    RequireObject(entry, "custom effect");
                    if (entry.size() != 3 || !entry.at("material").is_string() || !entry.at("enabled").is_boolean() ||
                        !entry.at("stage").is_number_integer() || entry.at("stage") < 0 || entry.at("stage") > 2)
                    {
                        throw std::runtime_error("Custom effect requires material, enabled and stage (0 through 2).");
                    }
                    definition.CustomEffects.Value.push_back(
                        {AssetId::Parse(entry.at("material").get<std::string>()),
                         static_cast<PostProcessCustomStage>(entry.at("stage").get<std::uint8_t>()),
                         entry.at("enabled").get<bool>()});
                }
            }
            for (std::size_t index = 0; index < ParameterDescriptions.size(); ++index)
            {
                const auto found = parameters.find(std::string(ParameterDescriptions[index].Key));
                if (found != parameters.end())
                {
                    RequireObject(*found, "parameter override");
                    if (found->size() != 2 || !found->at("override").is_boolean() || !found->at("value").is_number())
                    {
                        throw std::runtime_error(
                            "Post-processing parameter requires a Boolean override and numeric value.");
                    }
                    definition.Parameters[index] = {found->at("override").get<bool>(),
                                                    found->at("value").get<double>()};
                }
            }
            for (std::size_t index = 0; index < EffectKeys.size(); ++index)
            {
                const auto found = effects.find(std::string(EffectKeys[index]));
                if (found != effects.end())
                {
                    RequireObject(*found, "effect override");
                    if (found->size() != 2 || !found->at("override").is_boolean() || !found->at("value").is_boolean())
                    {
                        throw std::runtime_error("Post-processing effect requires Boolean override and value fields.");
                    }
                    definition.Effects[index] = {found->at("override").get<bool>(), found->at("value").get<bool>()};
                }
            }
            return CreateRef<PostProcessProfileAsset>(definition);
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("Post-processing profile JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> PostProcessProfileAsset::Encode(const PostProcessProfileDefinition& definition)
    {
        Validate(definition);
        Json parameters = Json::object();
        Json effects = Json::object();
        for (std::size_t index = 0; index < ParameterDescriptions.size(); ++index)
        {
            const auto& parameter = definition.Parameters[index];
            parameters[std::string(ParameterDescriptions[index].Key)] =
                Json{{"override", parameter.Override}, {"value", parameter.Value}};
        }
        for (std::size_t index = 0; index < EffectKeys.size(); ++index)
        {
            const auto& effect = definition.Effects[index];
            effects[std::string(EffectKeys[index])] = Json{{"override", effect.Override}, {"value", effect.Value}};
        }
        Json document{{"schemaVersion", definition.SchemaVersion}, {"parameters", parameters}, {"effects", effects}};
        if (definition.SchemaVersion >= 2)
        {
            document["colorLookup"] = Json{{"override", definition.ColorLookup.Override},
                                           {"texture", definition.ColorLookup.Value.ToString()},
                                           {"weightOverride", definition.ColorLookupWeight.Override},
                                           {"weight", definition.ColorLookupWeight.Value}};
        }
        if (definition.SchemaVersion >= 3)
        {
            Json entries = Json::array();
            for (const auto& effect : definition.CustomEffects.Value)
            {
                entries.push_back({{"material", effect.Material.ToString()},
                                   {"stage", static_cast<std::uint8_t>(effect.Stage)},
                                   {"enabled", effect.Enabled}});
            }
            document["customEffects"] = {{"override", definition.CustomEffects.Override}, {"effects", entries}};
        }
        const auto encoded = document.dump(2);
        std::vector<std::byte> result(encoded.size());
        std::memcpy(result.data(), encoded.data(), encoded.size());
        return result;
    }

    AssetImporterRegistration CreatePostProcessProfileAssetImporter()
    {
        AssetImporterRegistration result{
            "Keire.PostProcessProfile",
            3,
            PostProcessProfileAsset::StaticType(),
            {".keirepostprocess"},
            [](const std::span<const std::byte> bytes)
            { return PostProcessProfileAsset::Encode(PostProcessProfileAsset::Decode(bytes)->Definition()); }};
        const auto import = [](const std::span<const std::byte> bytes)
        {
            const auto decoded = PostProcessProfileAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = PostProcessProfileAsset::Encode(decoded->Definition());
            if (const auto lookup = decoded->Definition().ColorLookup.Value)
            {
                // Preserve inactive authored references for cooking and later override activation.
                output.AssetDependencies.push_back(lookup);
            }
            for (const auto& effect : decoded->Definition().CustomEffects.Value)
            {
                if (std::ranges::find(output.AssetDependencies, effect.Material) == output.AssetDependencies.end())
                {
                    output.AssetDependencies.push_back(effect.Material);
                }
            }
            return output;
        };
        result.ContextualImport = [import](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            auto output = import(bytes);
            const auto definition = PostProcessProfileAsset::Decode(bytes)->Definition();
            for (const auto dependency : output.AssetDependencies)
            {
                if (dependency == context.Asset)
                {
                    throw std::invalid_argument("A post-processing profile cannot reference itself.");
                }
                if (context.ResolveAssetSource)
                {
                    const auto source = context.ResolveAssetSource(dependency);
                    if (source && dependency == definition.ColorLookup.Value &&
                        source->Type != Texture2DAsset::StaticType())
                    {
                        throw std::invalid_argument("Post-processing color lookup must reference a Texture2D asset.");
                    }
                    const bool customMaterial =
                        std::ranges::any_of(definition.CustomEffects.Value,
                                            [&](const auto& effect) { return effect.Material == dependency; });
                    if (source && customMaterial && source->Type != MaterialAsset::StaticType())
                    {
                        throw std::invalid_argument(
                            "Post-processing custom effect must reference a compiled material asset.");
                    }
                }
            }
            return output;
        };
        result.RestoreCachedOutput = import;
        return result;
    }

    AssetDecoderRegistration CreatePostProcessProfileAssetDecoder()
    {
        return {PostProcessProfileAsset::StaticType(), CreateRef<PostProcessProfileAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return PostProcessProfileAsset::Decode(bytes); }};
    }
} // namespace Keire
