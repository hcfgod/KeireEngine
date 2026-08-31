#include "Keire/Ui/UiToolkit.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumUiPanelSettingsBytes = 16U * 1024U * 1024U;

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
        {
            std::vector<std::byte> result(text.size());
            std::memcpy(result.data(), text.data(), text.size());
            return result;
        }

        [[nodiscard]] const char* ToString(const UiPanelTarget value) noexcept
        {
            switch (value)
            {
            case UiPanelTarget::ScreenOverlay:
                return "ScreenOverlay";
            case UiPanelTarget::CameraOverlay:
                return "CameraOverlay";
            case UiPanelTarget::RenderTexture:
                return "RenderTexture";
            case UiPanelTarget::WorldSurface:
                return "WorldSurface";
            }
            return "ScreenOverlay";
        }

        [[nodiscard]] UiPanelTarget ParsePanelTarget(const std::string_view value)
        {
            if (value == "ScreenOverlay")
                return UiPanelTarget::ScreenOverlay;
            if (value == "CameraOverlay")
                return UiPanelTarget::CameraOverlay;
            if (value == "RenderTexture")
                return UiPanelTarget::RenderTexture;
            if (value == "WorldSurface")
                return UiPanelTarget::WorldSurface;
            throw std::runtime_error("UI panel settings contain an unsupported target.");
        }

        [[nodiscard]] const char* ToString(const RuntimeUiScaleMode value) noexcept
        {
            switch (value)
            {
            case RuntimeUiScaleMode::ConstantPixels:
                return "ConstantPixels";
            case RuntimeUiScaleMode::ScaleWithViewport:
                return "ScaleWithViewport";
            case RuntimeUiScaleMode::ConstantPhysicalSize:
                return "ConstantPhysicalSize";
            }
            return "ScaleWithViewport";
        }

        [[nodiscard]] RuntimeUiScaleMode ParseScaleMode(const std::string_view value)
        {
            if (value == "ConstantPixels")
                return RuntimeUiScaleMode::ConstantPixels;
            if (value == "ScaleWithViewport")
                return RuntimeUiScaleMode::ScaleWithViewport;
            if (value == "ConstantPhysicalSize")
                return RuntimeUiScaleMode::ConstantPhysicalSize;
            throw std::runtime_error("UI panel settings contain an unsupported scale mode.");
        }

        [[nodiscard]] Json ParseJson(const std::span<const std::byte> bytes)
        {
            if (bytes.size() > MaximumUiPanelSettingsBytes)
                throw std::runtime_error("UI panel settings asset exceeds the 16 MiB safety limit.");
            try
            {
                return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                   reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            }
            catch (const Json::exception& error)
            {
                throw std::runtime_error(std::string("UI panel settings asset JSON is malformed: ") + error.what());
            }
        }
    } // namespace

    UiPanelSettingsAsset::UiPanelSettingsAsset(UiPanelSettingsDefinition definition)
        : m_Definition(std::move(definition))
    {
        Validate(m_Definition);
    }

    RuntimeUiCanvasSettings UiPanelSettingsAsset::CanvasSettings() const noexcept
    {
        return {m_Definition.ReferenceWidth,
                m_Definition.ReferenceHeight,
                m_Definition.ScaleMode,
                m_Definition.MatchWidthOrHeight,
                1.0F,
                m_Definition.RespectSafeArea};
    }

    Ref<UiPanelSettingsAsset> UiPanelSettingsAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = ParseJson(bytes);
        if (!document.is_object() || document.value("schemaVersion", 0) != 1)
            throw std::runtime_error("UI panel settings asset has an unsupported schema.");
        UiPanelSettingsDefinition definition;
        definition.Target = ParsePanelTarget(document.at("target").get<std::string>());
        definition.ScaleMode = ParseScaleMode(document.at("scaleMode").get<std::string>());
        definition.ReferenceWidth = document.at("referenceWidth").get<float>();
        definition.ReferenceHeight = document.at("referenceHeight").get<float>();
        definition.MatchWidthOrHeight = document.at("matchWidthOrHeight").get<float>();
        definition.SortingOrder = document.value("sortingOrder", 0);
        definition.Camera =
            AssetId::Parse(document.value("camera", std::string("00000000-0000-0000-0000-000000000000")));
        definition.RenderTexture =
            AssetId::Parse(document.value("renderTexture", std::string("00000000-0000-0000-0000-000000000000")));
        definition.RespectSafeArea = document.value("respectSafeArea", true);
        definition.WorldWidth = document.value("worldWidth", 1.92F);
        definition.WorldHeight = document.value("worldHeight", 1.08F);
        definition.PixelsPerUnit = document.value("pixelsPerUnit", 1000.0F);
        definition.DepthTest = document.value("depthTest", true);
        return CreateRef<UiPanelSettingsAsset>(definition);
    }

    std::vector<std::byte> UiPanelSettingsAsset::Encode(const UiPanelSettingsDefinition& definition)
    {
        Validate(definition);
        const Json document{{"schemaVersion", 1},
                            {"target", ToString(definition.Target)},
                            {"scaleMode", ToString(definition.ScaleMode)},
                            {"referenceWidth", definition.ReferenceWidth},
                            {"referenceHeight", definition.ReferenceHeight},
                            {"matchWidthOrHeight", definition.MatchWidthOrHeight},
                            {"sortingOrder", definition.SortingOrder},
                            {"camera", definition.Camera.ToString()},
                            {"renderTexture", definition.RenderTexture.ToString()},
                            {"respectSafeArea", definition.RespectSafeArea},
                            {"worldWidth", definition.WorldWidth},
                            {"worldHeight", definition.WorldHeight},
                            {"pixelsPerUnit", definition.PixelsPerUnit},
                            {"depthTest", definition.DepthTest}};
        return Bytes(document.dump(2) + '\n');
    }

    void UiPanelSettingsAsset::Validate(const UiPanelSettingsDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || !std::isfinite(definition.ReferenceWidth) ||
            !std::isfinite(definition.ReferenceHeight) || !std::isfinite(definition.MatchWidthOrHeight) ||
            !std::isfinite(definition.WorldWidth) || !std::isfinite(definition.WorldHeight) ||
            !std::isfinite(definition.PixelsPerUnit) || definition.ReferenceWidth <= 0.0F ||
            definition.ReferenceHeight <= 0.0F || definition.ReferenceWidth > 65'536.0F ||
            definition.ReferenceHeight > 65'536.0F || definition.MatchWidthOrHeight < 0.0F ||
            definition.MatchWidthOrHeight > 1.0F || definition.SortingOrder < -32'768 ||
            definition.SortingOrder > 32'767 || definition.WorldWidth <= 0.0F || definition.WorldHeight <= 0.0F ||
            definition.WorldWidth > 10'000.0F || definition.WorldHeight > 10'000.0F ||
            definition.PixelsPerUnit <= 0.0F || definition.PixelsPerUnit > 100'000.0F ||
            (definition.Target == UiPanelTarget::RenderTexture && !definition.RenderTexture) ||
            (definition.Target != UiPanelTarget::RenderTexture && definition.RenderTexture) ||
            (definition.Target == UiPanelTarget::CameraOverlay && !definition.Camera) ||
            (definition.Target != UiPanelTarget::CameraOverlay && definition.Camera))
            throw std::invalid_argument("UI panel settings contain an invalid target or dimensions.");
    }

    AssetImporterRegistration CreateUiPanelSettingsAssetImporter()
    {
        return {"Keire.UiPanelSettings",
                1,
                UiPanelSettingsAsset::StaticType(),
                {".keireuipanel"},
                [](const std::span<const std::byte> bytes)
                { return UiPanelSettingsAsset::Encode(UiPanelSettingsAsset::Decode(bytes)->Definition()); }};
    }

    AssetDecoderRegistration CreateUiPanelSettingsAssetDecoder()
    {
        return {UiPanelSettingsAsset::StaticType(), CreateRef<UiPanelSettingsAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return UiPanelSettingsAsset::Decode(bytes); }};
    }
} // namespace Keire
