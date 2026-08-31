#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Ui/RuntimeUi.h"

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Ui
{
    class VisualElement;
}

namespace Keire
{
    inline constexpr std::size_t MaximumUiDocumentBytes = std::size_t{16} * 1024U * 1024U;
    inline constexpr std::size_t MaximumUiElements = 16'384;
    inline constexpr std::size_t MaximumUiTreeDepth = 128;
    inline constexpr std::size_t MaximumUiTemplateDepth = 32;
    inline constexpr std::size_t MaximumUiStyleRules = 8'192;
    inline constexpr std::size_t MaximumUiStyleProperties = 65'536;
    inline constexpr std::size_t MaximumUiSelectorTraceEntries = 256;

    enum class UiVisualElementType : std::uint8_t
    {
        VisualElement,
        TemplateContainer,
        Label,
        Image,
        Button,
        TextField,
        Toggle,
        Slider,
        ProgressBar,
        ScrollView,
        ListView,
        TreeView,
        DropdownField,
        Foldout,
        TabView,
        Toolbar,
        Spacer,
        Custom,
        Slot
    };

    enum class UiStyleCombinator : std::uint8_t
    {
        None,
        Descendant,
        Child
    };

    enum class UiStylePseudoState : std::uint16_t
    {
        None = 0,
        Hover = 1 << 0,
        Active = 1 << 1,
        Focus = 1 << 2,
        Disabled = 1 << 3,
        Checked = 1 << 4,
        Root = 1 << 5
    };

    [[nodiscard]] constexpr UiStylePseudoState operator|(const UiStylePseudoState left,
                                                         const UiStylePseudoState right) noexcept
    {
        return static_cast<UiStylePseudoState>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    struct UiNamedValue
    {
        std::string Name;
        std::string Value;

        [[nodiscard]] bool operator==(const UiNamedValue&) const = default;
    };

    struct UiBindingDefinition
    {
        std::string Property;
        std::string Path;
        std::string Mode = "OneWay";

        [[nodiscard]] bool operator==(const UiBindingDefinition&) const = default;
    };

    struct UiVisualElementDefinition
    {
        AssetId StableId;
        UiVisualElementType Type = UiVisualElementType::VisualElement;
        std::string CustomType;
        std::string Name;
        std::vector<std::string> Classes;
        std::vector<UiNamedValue> Attributes;
        std::vector<UiNamedValue> InlineStyles;
        std::vector<UiBindingDefinition> Bindings;
        AssetId Template;
        std::string Slot;
        std::vector<UiVisualElementDefinition> Children;

        [[nodiscard]] bool operator==(const UiVisualElementDefinition&) const = default;
    };

    struct UiVisualTreeDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string Name;
        std::vector<AssetId> StyleSheets;
        UiVisualElementDefinition Root;

        [[nodiscard]] bool operator==(const UiVisualTreeDefinition&) const = default;
    };

    struct UiStyleSelectorPart
    {
        UiStyleCombinator Combinator = UiStyleCombinator::None;
        std::string Type;
        std::string Name;
        std::vector<std::string> Classes;
        UiStylePseudoState States = UiStylePseudoState::None;

        [[nodiscard]] bool operator==(const UiStyleSelectorPart&) const = default;
    };

    enum class UiStyleOrientation : std::uint8_t
    {
        Any,
        Landscape,
        Portrait
    };

    enum class UiStylePointerPrecision : std::uint8_t
    {
        Any,
        Fine,
        Coarse,
        None
    };

    enum class UiStyleNavigationMode : std::uint8_t
    {
        Any,
        Pointer,
        Keyboard,
        Gamepad
    };

    struct UiStyleMediaCondition
    {
        std::optional<float> MinimumWidth;
        std::optional<float> MaximumWidth;
        std::optional<float> MinimumHeight;
        std::optional<float> MaximumHeight;
        std::optional<float> MinimumAspectRatio;
        std::optional<float> MaximumAspectRatio;
        std::optional<float> MinimumDpi;
        std::optional<float> MaximumDpi;
        UiStyleOrientation Orientation = UiStyleOrientation::Any;
        UiStylePointerPrecision Pointer = UiStylePointerPrecision::Any;
        UiStyleNavigationMode Navigation = UiStyleNavigationMode::Any;
        std::optional<bool> ReducedMotion;

        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] bool operator==(const UiStyleMediaCondition&) const = default;
    };

    struct UiStyleEvaluationContext
    {
        float Width = 1920.0F;
        float Height = 1080.0F;
        float Dpi = 96.0F;
        UiStylePointerPrecision Pointer = UiStylePointerPrecision::Fine;
        UiStyleNavigationMode Navigation = UiStyleNavigationMode::Pointer;
        bool ReducedMotion = false;

        [[nodiscard]] bool operator==(const UiStyleEvaluationContext&) const = default;
    };

    struct UiStyleRuleDefinition
    {
        std::string Selector;
        std::vector<UiStyleSelectorPart> Parts;
        std::uint32_t Specificity = 0;
        std::vector<UiNamedValue> Properties;
        std::optional<UiStyleMediaCondition> Media;

        [[nodiscard]] bool operator==(const UiStyleRuleDefinition&) const = default;
    };

    struct UiStyleSheetDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::vector<UiStyleRuleDefinition> Rules;

        [[nodiscard]] bool operator==(const UiStyleSheetDefinition&) const = default;
    };

    [[nodiscard]] KEIRE_API bool MatchesUiStyleMediaCondition(const UiStyleMediaCondition& condition,
                                                              const UiStyleEvaluationContext& context) noexcept;
    [[nodiscard]] KEIRE_API std::string EncodeUiStyleMediaCondition(const UiStyleMediaCondition& condition);

    enum class UiPanelTarget : std::uint8_t
    {
        ScreenOverlay,
        CameraOverlay,
        RenderTexture,
        WorldSurface
    };

    struct UiPanelSettingsDefinition
    {
        std::uint32_t SchemaVersion = 1;
        UiPanelTarget Target = UiPanelTarget::ScreenOverlay;
        RuntimeUiScaleMode ScaleMode = RuntimeUiScaleMode::ScaleWithViewport;
        float ReferenceWidth = 1920.0F;
        float ReferenceHeight = 1080.0F;
        float MatchWidthOrHeight = 0.5F;
        std::int32_t SortingOrder = 0;
        AssetId Camera;
        AssetId RenderTexture;
        bool RespectSafeArea = true;
        float WorldWidth = 1.92F;
        float WorldHeight = 1.08F;
        float PixelsPerUnit = 1000.0F;
        bool DepthTest = true;

        [[nodiscard]] bool operator==(const UiPanelSettingsDefinition&) const = default;
    };

    class KEIRE_API UiVisualTreeAsset final : public Asset
    {
      public:
        explicit UiVisualTreeAsset(UiVisualTreeDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245554954ULL, 0x5245450000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const UiVisualTreeDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] const UiVisualElementDefinition* Find(AssetId stableId) const noexcept;
        [[nodiscard]] const UiVisualElementDefinition* Find(std::string_view name) const noexcept;

        [[nodiscard]] static Ref<UiVisualTreeAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const UiVisualTreeDefinition& definition);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const UiVisualTreeDefinition& definition);
        [[nodiscard]] static UiVisualTreeDefinition ParseSource(std::span<const std::byte> bytes);
        static void Validate(const UiVisualTreeDefinition& definition);

      private:
        UiVisualTreeDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    class KEIRE_API UiStyleSheetAsset final : public Asset
    {
      public:
        explicit UiStyleSheetAsset(UiStyleSheetDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245554953ULL, 0x54594c4500000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const UiStyleSheetDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<UiStyleSheetAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const UiStyleSheetDefinition& definition);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const UiStyleSheetDefinition& definition);
        [[nodiscard]] static UiStyleSheetDefinition ParseSource(std::span<const std::byte> bytes);
        static void Validate(const UiStyleSheetDefinition& definition);

      private:
        UiStyleSheetDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    class KEIRE_API UiPanelSettingsAsset final : public Asset
    {
      public:
        explicit UiPanelSettingsAsset(UiPanelSettingsDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245554950ULL, 0x414e454c00000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return sizeof(*this); }
        [[nodiscard]] const UiPanelSettingsDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] RuntimeUiCanvasSettings CanvasSettings() const noexcept;

        [[nodiscard]] static Ref<UiPanelSettingsAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const UiPanelSettingsDefinition& definition);
        static void Validate(const UiPanelSettingsDefinition& definition);

      private:
        UiPanelSettingsDefinition m_Definition;
    };

    struct UiQuery
    {
        std::optional<UiVisualElementType> Type;
        std::string Name;
        std::string Class;
    };

    struct UiDocumentElementInfo
    {
        AssetId StableId;
        std::string Name;
        UiVisualElementType SourceType = UiVisualElementType::VisualElement;
        RuntimeUiElementType RuntimeType = RuntimeUiElementType::Panel;
    };

    struct UiResolvedStyleSelectorTrace
    {
        std::string Selector;
        std::uint32_t Specificity = 0;
        std::size_t SourceOrder = 0;
        std::vector<std::string> AppliedProperties;
    };

    using UiTemplateResolver = std::function<Ref<const UiVisualTreeAsset>(AssetId)>;

    class KEIRE_API UiDocumentBindingSource : public RefCounted
    {
      public:
        ~UiDocumentBindingSource() noexcept override = default;

        [[nodiscard]] virtual std::any Read(std::string_view path) const = 0;
        virtual void Write(std::string_view path, const std::any& value);
    };

    struct UiDocumentBindingDiagnostic
    {
        AssetId Element;
        std::string Property;
        std::string Path;
        std::string Code;
        std::string Message;
    };

    class KEIRE_API UiDocument final : public RefCounted
    {
      public:
        UiDocument(Ref<const UiVisualTreeAsset> visualTree, std::vector<Ref<const UiStyleSheetAsset>> styleSheets = {},
                   std::size_t maximumEvents = 4'096);
        UiDocument(Ref<const UiVisualTreeAsset> visualTree, std::vector<Ref<const UiStyleSheetAsset>> styleSheets,
                   UiTemplateResolver templateResolver, std::size_t maximumEvents = 4'096);
        UiDocument(Ref<const UiVisualTreeAsset> visualTree, std::vector<Ref<const UiStyleSheetAsset>> styleSheets,
                   Ref<RuntimeUiTree> sharedTree, RuntimeUiElementId parent = {}, std::size_t maximumEvents = 4'096);
        UiDocument(Ref<const UiVisualTreeAsset> visualTree, std::vector<Ref<const UiStyleSheetAsset>> styleSheets,
                   Ref<RuntimeUiTree> sharedTree, RuntimeUiElementId parent, UiTemplateResolver templateResolver,
                   std::size_t maximumEvents = 4'096);
        ~UiDocument() noexcept override;

        UiDocument(const UiDocument&) = delete;
        UiDocument& operator=(const UiDocument&) = delete;

        [[nodiscard]] const Ref<RuntimeUiTree>& Tree() const noexcept;
        [[nodiscard]] RuntimeUiElementId Root() const noexcept;
        [[nodiscard]] const Ref<Ui::VisualElement>& VisualRoot() const noexcept;
        [[nodiscard]] Ref<Ui::VisualElement> Visual(AssetId stableId) const noexcept;
        [[nodiscard]] Ref<Ui::VisualElement> Visual(std::string_view name) const noexcept;
        [[nodiscard]] std::optional<RuntimeUiElementId> Find(AssetId stableId) const noexcept;
        [[nodiscard]] std::optional<RuntimeUiElementId> Find(std::string_view name) const noexcept;
        [[nodiscard]] std::optional<UiDocumentElementInfo> Describe(RuntimeUiElementId element) const;
        [[nodiscard]] std::vector<UiResolvedStyleSelectorTrace> ResolvedStyleTrace(RuntimeUiElementId element) const;
        [[nodiscard]] std::vector<RuntimeUiElementId> Query(const UiQuery& query) const;
        void SetBindingSource(Ref<UiDocumentBindingSource> source);
        [[nodiscard]] const std::vector<UiDocumentBindingDiagnostic>& BindingDiagnostics() const noexcept;
        void UpdateBindings();
        [[nodiscard]] bool DispatchRuntimeEvent(const RuntimeUiEvent& event);
        [[nodiscard]] bool SynchronizeInteractionStates();
        [[nodiscard]] bool Advance(float deltaSeconds);
        void SetPseudoState(RuntimeUiElementId element, UiStylePseudoState state, bool enabled);
        [[nodiscard]] bool SetStyleEvaluationContext(UiStyleEvaluationContext context);
        void RefreshStyles();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateUiVisualTreeAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateUiStyleSheetAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateUiPanelSettingsAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateUiVisualTreeAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateUiStyleSheetAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateUiPanelSettingsAssetDecoder();
} // namespace Keire
