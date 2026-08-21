#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ManagedDataSchemaVersion = 1;

    class KEIRE_API ManagedTypeId final
    {
      public:
        constexpr ManagedTypeId() noexcept = default;
        explicit constexpr ManagedTypeId(const AssetId value) noexcept : m_Value(value) {}

        [[nodiscard]] static ManagedTypeId Parse(const std::string_view value)
        {
            return ManagedTypeId(AssetId::Parse(value));
        }
        [[nodiscard]] std::string ToString() const { return m_Value.ToString(); }
        [[nodiscard]] constexpr const AssetId& Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const ManagedTypeId&) const noexcept = default;

      private:
        AssetId m_Value;
    };

    enum class ManagedAssetPropertyKind : std::uint8_t
    {
        Boolean,
        Integer,
        UnsignedInteger,
        Scalar,
        Text,
        Enum,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        SerializableObject,
        Array,
        List,
        AssetReference
    };

    struct ManagedAssetPropertyDescriptor
    {
        AssetId StableFieldId;
        std::string Name;
        std::string DisplayName;
        std::string ManagedTypeName;
        ManagedAssetPropertyKind Kind = ManagedAssetPropertyKind::Text;
        bool ReadOnly = false;
        bool Hidden = false;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
        std::string Header;
        std::string Tooltip;
        std::optional<AssetTypeId> ExpectedAssetType;
        std::optional<ManagedTypeId> ExpectedManagedType;
        bool IncludeDerivedAssetTypes = true;
        std::vector<ManagedAssetPropertyDescriptor> Children;
        double Step = 0.1;
        bool Slider = false;
        std::uint32_t TextLines = 1;

        [[nodiscard]] bool operator==(const ManagedAssetPropertyDescriptor&) const = default;
    };

    struct ManagedAssetTypeDescriptor
    {
        ManagedTypeId StableTypeId;
        std::string FullName;
        std::string DisplayName;
        std::optional<ManagedTypeId> BaseTypeId;
        std::string MenuPath;
        std::string DefaultFileName;
        std::vector<ManagedAssetPropertyDescriptor> Properties;

        [[nodiscard]] bool operator==(const ManagedAssetTypeDescriptor&) const = default;
    };

    using ManagedAssetScalarValue = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string,
                                                 Vector2, Vector3, Vector4, Quaternion, Color, AssetId>;

    struct ManagedAssetValueNode
    {
        AssetId StableFieldId;
        ManagedAssetPropertyKind Kind = ManagedAssetPropertyKind::Text;
        ManagedAssetScalarValue Value;
        std::vector<ManagedAssetValueNode> Children;

        [[nodiscard]] bool operator==(const ManagedAssetValueNode&) const = default;
    };

    [[nodiscard]] KEIRE_API ManagedAssetValueNode
    DecodeManagedAssetValue(std::string_view value, const ManagedAssetPropertyDescriptor& property);
    [[nodiscard]] KEIRE_API std::string EncodeManagedAssetValue(const ManagedAssetValueNode& value,
                                                                const ManagedAssetPropertyDescriptor& property);

    struct ManagedDataFieldState
    {
        AssetId StableFieldId;
        std::string Name;
        std::string ManagedTypeName;
        std::vector<std::string> FormerNames;
        std::string Value;

        [[nodiscard]] bool operator==(const ManagedDataFieldState&) const = default;
    };

    struct ManagedDataAssetDependency
    {
        AssetId Asset;
        AssetTypeId AssetType;
        std::optional<ManagedTypeId> ManagedType;

        [[nodiscard]] bool operator==(const ManagedDataAssetDependency&) const = default;
    };

    struct ManagedDataDefinition
    {
        std::uint32_t SchemaVersion = ManagedDataSchemaVersion;
        ManagedTypeId ManagedType;
        std::string ManagedTypeName;
        std::vector<ManagedDataFieldState> Fields;
        std::vector<ManagedDataAssetDependency> Dependencies;

        [[nodiscard]] bool operator==(const ManagedDataDefinition&) const = default;
    };

    struct ManagedDataReloadResult
    {
        bool Applied = false;
        std::uint64_t Revision = 0;
        std::string Diagnostic;
    };

    struct ManagedDataCookAsset
    {
        AssetId Asset;
        AssetTypeId AssetType;
        std::optional<ManagedTypeId> ManagedType;
    };

    class KEIRE_API ManagedDataAsset final : public Asset
    {
      public:
        ManagedDataAsset() noexcept = default;
        explicit ManagedDataAsset(ManagedDataDefinition definition);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4441ULL, 0x5441415353455401ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return m_ResidentBytes; }
        [[nodiscard]] const ManagedDataDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] std::vector<AssetId> AssetDependencies() const;

        [[nodiscard]] static Ref<ManagedDataAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ManagedDataDefinition& definition);
        [[nodiscard]] static ManagedDataDefinition Canonicalize(ManagedDataDefinition definition);
        static void Validate(const ManagedDataDefinition& definition);

        // AssetSystem invokes reloads on its owner thread. A failed candidate leaves this object's identity and data
        // unchanged so managed references may safely retain the last-good instance.
        [[nodiscard]] ManagedDataReloadResult TryReload(std::span<const std::byte> bytes);

      private:
        ManagedDataDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
        std::uint64_t m_Revision = 0;
    };

    KEIRE_API void ValidateManagedAssetTypeDescriptor(const ManagedAssetTypeDescriptor& descriptor);
    [[nodiscard]] KEIRE_API std::string
    EncodeManagedAssetTypeCatalog(std::span<const ManagedAssetTypeDescriptor> descriptors);
    [[nodiscard]] KEIRE_API std::vector<ManagedAssetTypeDescriptor>
    DecodeManagedAssetTypeCatalog(std::string_view catalog);
    KEIRE_API void ValidateManagedDataForCook(AssetId asset, const ManagedDataDefinition& definition,
                                              std::span<const ManagedAssetTypeDescriptor> descriptors,
                                              std::span<const ManagedDataCookAsset> assets);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateManagedDataAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateManagedDataAssetDecoder();
} // namespace Keire

template <> struct std::hash<Keire::ManagedTypeId>
{
    std::size_t operator()(const Keire::ManagedTypeId& value) const noexcept
    {
        return std::hash<Keire::AssetId>{}(value.Value());
    }
};
