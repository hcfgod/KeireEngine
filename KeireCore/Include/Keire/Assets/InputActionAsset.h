#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class InputActionType : std::uint8_t
    {
        Button,
        Value,
        PassThrough
    };

    enum class InputValueType : std::uint8_t
    {
        Boolean,
        Axis1D,
        Axis2D
    };

    enum class InputCapturePolicy : std::uint8_t
    {
        RespectUiCapture,
        AlwaysReceive
    };

    struct InputParameter
    {
        std::string Name;
        double Value = 0.0;
    };

    struct InputBehaviorDefinition
    {
        std::string Name;
        std::vector<InputParameter> Parameters;
    };

    struct InputActionDefinition
    {
        AssetId Id;
        std::string Name;
        InputActionType Type = InputActionType::Button;
        InputValueType ValueType = InputValueType::Boolean;
        std::vector<InputBehaviorDefinition> Interactions;
        std::vector<InputBehaviorDefinition> Processors;
    };

    struct InputBindingDefinition
    {
        AssetId Id;
        AssetId Action;
        std::string Name;
        std::string Path;
        std::string Composite;
        std::string CompositePart;
        std::vector<std::string> Groups;
        std::vector<InputBehaviorDefinition> Interactions;
        std::vector<InputBehaviorDefinition> Processors;
    };

    struct InputActionMapDefinition
    {
        AssetId Id;
        std::string Name;
        InputCapturePolicy CapturePolicy = InputCapturePolicy::RespectUiCapture;
        std::vector<InputActionDefinition> Actions;
        std::vector<InputBindingDefinition> Bindings;
    };

    struct InputDeviceRequirement
    {
        std::string Device;
        bool Optional = false;
    };

    struct InputControlSchemeDefinition
    {
        AssetId Id;
        std::string Name;
        std::string BindingGroup;
        std::vector<InputDeviceRequirement> Devices;
    };

    struct InputActionAssetDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string Name;
        std::vector<InputControlSchemeDefinition> ControlSchemes;
        std::vector<InputActionMapDefinition> ActionMaps;
    };

    class KEIRE_API InputActionAsset final : public Asset
    {
      public:
        explicit InputActionAsset(InputActionAssetDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245494e50ULL, 0x5554414354494f01ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const InputActionAssetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] const InputActionMapDefinition* FindMap(AssetId id) const noexcept;
        [[nodiscard]] const InputActionMapDefinition* FindMap(std::string_view name) const noexcept;
        [[nodiscard]] const InputActionDefinition* FindAction(AssetId id) const noexcept;

        [[nodiscard]] static Ref<InputActionAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const InputActionAssetDefinition& definition);
        [[nodiscard]] static InputActionAssetDefinition DefaultDefinition();
        [[nodiscard]] static InputActionAssetDefinition GameplayDefinition();
        [[nodiscard]] static InputActionAssetDefinition UiDefinition();
        static void Validate(const InputActionAssetDefinition& definition);

      private:
        InputActionAssetDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateInputActionAssetDecoder();
} // namespace Keire
