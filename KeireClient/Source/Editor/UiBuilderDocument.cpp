#include "KeireClient/Editor/UiBuilderDocument.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::UiVisualElementDefinition* FindElement(Keire::UiVisualElementDefinition& root,
                                                                    const Keire::AssetId id) noexcept
        {
            if (root.StableId == id)
                return &root;
            for (auto& child : root.Children)
            {
                if (auto* found = FindElement(child, id))
                    return found;
            }
            return nullptr;
        }

        [[nodiscard]] const Keire::UiVisualElementDefinition* FindElement(const Keire::UiVisualElementDefinition& root,
                                                                          const Keire::AssetId id) noexcept
        {
            if (root.StableId == id)
                return &root;
            for (const auto& child : root.Children)
            {
                if (const auto* found = FindElement(child, id))
                    return found;
            }
            return nullptr;
        }

        [[nodiscard]] Keire::AssetId FindParent(const Keire::UiVisualElementDefinition& root,
                                                const Keire::AssetId id) noexcept
        {
            for (const auto& child : root.Children)
            {
                if (child.StableId == id)
                    return root.StableId;
                if (const auto parent = FindParent(child, id))
                    return parent;
            }
            return {};
        }

        [[nodiscard]] bool TakeElement(Keire::UiVisualElementDefinition& root, const Keire::AssetId id,
                                       Keire::UiVisualElementDefinition& result)
        {
            const auto found = std::ranges::find(root.Children, id, &Keire::UiVisualElementDefinition::StableId);
            if (found != root.Children.end())
            {
                result = std::move(*found);
                root.Children.erase(found);
                return true;
            }
            return std::ranges::any_of(root.Children, [&](auto& child) { return TakeElement(child, id, result); });
        }

        [[nodiscard]] std::string DefaultElementName(const Keire::UiVisualElementType type)
        {
            switch (type)
            {
            case Keire::UiVisualElementType::VisualElement:
                return "visual-element";
            case Keire::UiVisualElementType::TemplateContainer:
                return "template";
            case Keire::UiVisualElementType::Label:
                return "label";
            case Keire::UiVisualElementType::Image:
                return "image";
            case Keire::UiVisualElementType::Button:
                return "button";
            case Keire::UiVisualElementType::TextField:
                return "text-field";
            case Keire::UiVisualElementType::Toggle:
                return "toggle";
            case Keire::UiVisualElementType::Slider:
                return "slider";
            case Keire::UiVisualElementType::ProgressBar:
                return "progress-bar";
            case Keire::UiVisualElementType::ScrollView:
                return "scroll-view";
            case Keire::UiVisualElementType::ListView:
                return "list-view";
            case Keire::UiVisualElementType::TreeView:
                return "tree-view";
            case Keire::UiVisualElementType::DropdownField:
                return "dropdown";
            case Keire::UiVisualElementType::Foldout:
                return "foldout";
            case Keire::UiVisualElementType::TabView:
                return "tab-view";
            case Keire::UiVisualElementType::Toolbar:
                return "toolbar";
            case Keire::UiVisualElementType::Spacer:
                return "spacer";
            case Keire::UiVisualElementType::Custom:
                return "custom-element";
            case Keire::UiVisualElementType::Slot:
                return "slot";
            }
            return "element";
        }

        [[nodiscard]] bool ContainsName(const Keire::UiVisualElementDefinition& root,
                                        const std::string_view name) noexcept
        {
            if (root.Name == name)
                return true;
            return std::ranges::any_of(root.Children, [&](const auto& child) { return ContainsName(child, name); });
        }

        [[nodiscard]] std::string UniqueElementName(const Keire::UiVisualElementDefinition& root,
                                                    const Keire::UiVisualElementType type)
        {
            const auto base = DefaultElementName(type);
            if (!ContainsName(root, base))
                return base;
            for (std::size_t suffix = 2;; ++suffix)
            {
                auto candidate = base + '-' + std::to_string(suffix);
                if (!ContainsName(root, candidate))
                    return candidate;
            }
        }

        [[nodiscard]] std::size_t CountInlineStyleProperties(const Keire::UiVisualElementDefinition& element) noexcept
        {
            std::size_t result = element.InlineStyles.size();
            for (const auto& child : element.Children)
                result += CountInlineStyleProperties(child);
            return result;
        }

        void CollectPreviewElements(const Keire::UiVisualElementDefinition& definition,
                                    const Keire::UiDocument& document, const Keire::RuntimeUiTree& tree,
                                    std::vector<UiBuilderPreviewElement>& result)
        {
            if (const auto runtime = document.Find(definition.StableId))
            {
                if (const auto state = tree.State(*runtime))
                    result.push_back({definition.StableId, *state});
            }
            for (const auto& child : definition.Children)
                CollectPreviewElements(child, document, tree, result);
        }

        [[nodiscard]] bool IsFinite(const Keire::RuntimeUiRect rectangle) noexcept
        {
            return std::isfinite(rectangle.X) && std::isfinite(rectangle.Y) && std::isfinite(rectangle.Width) &&
                   std::isfinite(rectangle.Height);
        }

        [[nodiscard]] bool IsRenderablePreviewCommand(const Keire::RuntimeUiDrawCommand& command) noexcept
        {
            if (command.Type == Keire::RuntimeUiDrawType::PushClip || command.Type == Keire::RuntimeUiDrawType::PopClip)
                return true;
            const auto clipped = command.Rect.Intersect(command.ClipRect);
            return IsFinite(clipped) && !clipped.Empty();
        }

        void CollectRootSelections(const Keire::UiVisualElementDefinition& parent,
                                   const std::span<const Keire::AssetId> selections,
                                   std::vector<Keire::AssetId>& result)
        {
            for (const auto& child : parent.Children)
            {
                if (std::ranges::find(selections, child.StableId) != selections.end())
                    result.push_back(child.StableId);
                else
                    CollectRootSelections(child, selections, result);
            }
        }

        [[nodiscard]] std::vector<Keire::AssetId> RootSelections(const Keire::UiVisualTreeDefinition& definition,
                                                                 const std::span<const Keire::AssetId> selections)
        {
            std::vector<Keire::AssetId> result;
            CollectRootSelections(definition.Root, selections, result);
            return result;
        }

        void CollectNames(const Keire::UiVisualElementDefinition& element, std::unordered_set<std::string>& result)
        {
            if (!element.Name.empty())
                result.insert(element.Name);
            for (const auto& child : element.Children)
                CollectNames(child, result);
        }

        [[nodiscard]] std::string UniqueClipboardName(std::unordered_set<std::string>& names,
                                                      const Keire::UiVisualElementDefinition& element)
        {
            const auto source = element.Name.empty() ? DefaultElementName(element.Type) : element.Name;
            auto candidate = source + "-copy";
            for (std::size_t suffix = 2; names.contains(candidate); ++suffix)
                candidate = source + "-copy-" + std::to_string(suffix);
            names.insert(candidate);
            return candidate;
        }

        void RegenerateClipboardIdentity(Keire::UiVisualElementDefinition& element,
                                         std::unordered_set<std::string>& names)
        {
            element.StableId = Keire::AssetId::Generate();
            element.Name = UniqueClipboardName(names, element);
            for (auto& child : element.Children)
                RegenerateClipboardIdentity(child, names);
        }

        [[nodiscard]] std::vector<std::string> NormalizeClasses(std::vector<std::string> classes)
        {
            std::erase_if(classes, [](const std::string& value) { return value.empty(); });
            std::vector<std::string> result;
            result.reserve(classes.size());
            for (auto& value : classes)
                if (std::ranges::find(result, value) == result.end())
                    result.push_back(std::move(value));
            return result;
        }
    } // namespace

    void UiBuilderPreviewSettings::ApplyPreset(const UiBuilderResolutionPreset preset) noexcept
    {
        Preset = preset;
        switch (preset)
        {
        case UiBuilderResolutionPreset::Hd:
            Width = 1280;
            Height = 720;
            break;
        case UiBuilderResolutionPreset::FullHd:
            Width = 1920;
            Height = 1080;
            break;
        case UiBuilderResolutionPreset::Qhd:
            Width = 2560;
            Height = 1440;
            break;
        case UiBuilderResolutionPreset::UltraHd:
            Width = 3840;
            Height = 2160;
            break;
        case UiBuilderResolutionPreset::Custom:
            break;
        }
        Normalize();
    }

    void UiBuilderPreviewSettings::ApplyOrientation(const UiBuilderOrientation orientation) noexcept
    {
        if ((orientation == UiBuilderOrientation::Landscape && Width < Height) ||
            (orientation == UiBuilderOrientation::Portrait && Width > Height))
        {
            std::swap(Width, Height);
        }
        Preset = UiBuilderResolutionPreset::Custom;
        Normalize();
    }

    bool UiBuilderPreviewSettings::MatchGameView(const std::uint32_t width, const std::uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
            return false;
        Width = width;
        Height = height;
        Preset = UiBuilderResolutionPreset::Custom;
        Normalize();
        return true;
    }

    void UiBuilderPreviewSettings::Normalize() noexcept
    {
        Width = std::clamp(Width, 64U, 8192U);
        Height = std::clamp(Height, 64U, 8192U);
        ReferenceWidth = std::clamp(ReferenceWidth, 64U, 8192U);
        ReferenceHeight = std::clamp(ReferenceHeight, 64U, 8192U);
        Dpi = std::isfinite(Dpi) ? std::clamp(Dpi, 48.0F, 288.0F) : 96.0F;
        UserScale = std::isfinite(UserScale) ? std::clamp(UserScale, 0.5F, 2.0F) : 1.0F;
        MatchWidthOrHeight = std::isfinite(MatchWidthOrHeight) ? std::clamp(MatchWidthOrHeight, 0.0F, 1.0F) : 0.5F;
        Zoom = std::isfinite(Zoom) ? std::clamp(Zoom, 0.1F, 4.0F) : 1.0F;
        VerticalGuide =
            std::isfinite(VerticalGuide) ? std::clamp(VerticalGuide, -1.0F, static_cast<float>(Width)) : -1.0F;
        HorizontalGuide =
            std::isfinite(HorizontalGuide) ? std::clamp(HorizontalGuide, -1.0F, static_cast<float>(Height)) : -1.0F;
        Pan.X = std::isfinite(Pan.X) ? std::clamp(Pan.X, -32768.0F, 32768.0F) : 0.0F;
        Pan.Y = std::isfinite(Pan.Y) ? std::clamp(Pan.Y, -32768.0F, 32768.0F) : 0.0F;
        SafeArea.Left =
            std::isfinite(SafeArea.Left) ? std::clamp(SafeArea.Left, 0.0F, static_cast<float>(Width)) : 0.0F;
        SafeArea.Top = std::isfinite(SafeArea.Top) ? std::clamp(SafeArea.Top, 0.0F, static_cast<float>(Height)) : 0.0F;
        SafeArea.Right = std::isfinite(SafeArea.Right)
                             ? std::clamp(SafeArea.Right, 0.0F, static_cast<float>(Width) - SafeArea.Left)
                             : 0.0F;
        SafeArea.Bottom = std::isfinite(SafeArea.Bottom)
                              ? std::clamp(SafeArea.Bottom, 0.0F, static_cast<float>(Height) - SafeArea.Top)
                              : 0.0F;
    }

    void UiBuilderPreviewSettings::SetPseudoState(const Keire::UiStylePseudoState state, const bool enabled) noexcept
    {
        const auto current = static_cast<std::uint16_t>(PseudoStates);
        const auto flag = static_cast<std::uint16_t>(state);
        PseudoStates = static_cast<Keire::UiStylePseudoState>(enabled ? current | flag : current & ~flag);
    }

    bool UiBuilderPreviewSettings::HasPseudoState(const Keire::UiStylePseudoState state) const noexcept
    {
        const auto current = static_cast<std::uint16_t>(PseudoStates);
        const auto flag = static_cast<std::uint16_t>(state);
        return flag != 0 && (current & flag) == flag;
    }

    void UiBuilderPreviewSettings::PanBy(const Keire::Vector2 delta) noexcept
    {
        Pan.X += delta.X;
        Pan.Y += delta.Y;
        Normalize();
    }

    void UiBuilderPreviewSettings::ZoomBy(const float factor) noexcept
    {
        if (std::isfinite(factor) && factor > 0.0F)
            Zoom *= factor;
        Normalize();
    }

    void UiBuilderPreviewSettings::ResetView() noexcept
    {
        Pan = {};
        Zoom = 1.0F;
    }

    Keire::RuntimeUiCanvasSettings UiBuilderPreviewSettings::CanvasSettings() const noexcept
    {
        auto normalized = *this;
        normalized.Normalize();
        Keire::RuntimeUiCanvasSettings result;
        result.ReferenceWidth = static_cast<float>(normalized.ReferenceWidth);
        result.ReferenceHeight = static_cast<float>(normalized.ReferenceHeight);
        result.ScaleMode = normalized.ScaleMode;
        result.MatchWidthOrHeight = normalized.MatchWidthOrHeight;
        result.AccessibilityScale = std::clamp(normalized.Dpi / 96.0F * normalized.UserScale, 0.5F, 3.0F);
        result.RespectSafeArea = normalized.ShowSafeArea;
        return result;
    }

    UiBuilderRetainedPreview
    BuildUiBuilderRetainedPreview(const Keire::UiVisualTreeDefinition& definition, const Keire::AssetId selected,
                                  const UiBuilderPreviewSettings& requestedSettings,
                                  const std::span<const Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets,
                                  Keire::UiTemplateResolver templateResolver)
    {
        auto settings = requestedSettings;
        settings.Normalize();
        Keire::UiVisualTreeAsset::Validate(definition);
        auto visualTree = Keire::CreateRef<Keire::UiVisualTreeAsset>(definition);
        auto runtime = Keire::CreateRef<Keire::UiDocument>(
            visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>(styleSheets.begin(), styleSheets.end()),
            std::move(templateResolver));
        if (const auto selectedRuntime = runtime->Find(selected))
        {
            constexpr std::array states{Keire::UiStylePseudoState::Hover, Keire::UiStylePseudoState::Active,
                                        Keire::UiStylePseudoState::Focus, Keire::UiStylePseudoState::Disabled,
                                        Keire::UiStylePseudoState::Checked};
            for (const auto state : states)
                if (settings.HasPseudoState(state))
                    runtime->SetPseudoState(*selectedRuntime, state, true);
        }
        const auto& tree = runtime->Tree();
        tree->Layout(static_cast<float>(settings.Width), static_cast<float>(settings.Height), settings.SafeArea,
                     settings.CanvasSettings());

        UiBuilderRetainedPreview result;
        result.Statistics = tree->Statistics();
        for (const auto& command : tree->DrawCommands())
        {
            if (IsRenderablePreviewCommand(command))
                result.DrawCommands.push_back(command);
        }
        result.LinkedStyleSheets = definition.StyleSheets.size();
        result.ResolvedStyleSheets = styleSheets.size();
        result.InlineStyleProperties = CountInlineStyleProperties(definition.Root);
        CollectPreviewElements(definition.Root, *runtime, *tree, result.Elements);
        if (const auto found = std::ranges::find(result.Elements, selected, &UiBuilderPreviewElement::StableId);
            found != result.Elements.end())
        {
            result.SelectedState = found->State;
        }
        return result;
    }

    UiBuilderRuntimeDebugSnapshot
    BuildUiBuilderRuntimeDebugSnapshot(const Keire::UiVisualTreeDefinition& definition, const Keire::AssetId selected,
                                       const std::span<const Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets,
                                       const float viewportWidth, const float viewportHeight,
                                       Keire::UiTemplateResolver templateResolver)
    {
        if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0F ||
            viewportHeight <= 0.0F || viewportWidth > 8192.0F || viewportHeight > 8192.0F)
        {
            throw std::invalid_argument("UI Builder runtime preview dimensions must be positive finite values.");
        }
        UiBuilderPreviewSettings settings;
        settings.Width = static_cast<std::uint32_t>(viewportWidth);
        settings.Height = static_cast<std::uint32_t>(viewportHeight);
        settings.Preset = UiBuilderResolutionPreset::Custom;
        const auto preview =
            BuildUiBuilderRetainedPreview(definition, selected, settings, styleSheets, std::move(templateResolver));

        UiBuilderRuntimeDebugSnapshot result;
        result.Statistics = preview.Statistics;
        result.SelectedState = preview.SelectedState;
        result.LinkedStyleSheets = preview.LinkedStyleSheets;
        result.ResolvedStyleSheets = preview.ResolvedStyleSheets;
        result.InlineStyleProperties = preview.InlineStyleProperties;
        return result;
    }

    void UiBuilderLiveDebugStore::Refresh(const Keire::AssetId expectedVisualTree,
                                          UiBuilderLiveDebugCapture capture) noexcept
    {
        try
        {
            if (m_VisualTree != expectedVisualTree)
            {
                m_VisualTree = expectedVisualTree;
                m_Current.reset();
                m_Diagnostic.clear();
                m_Status = UiBuilderLiveDebugStatus::Unavailable;
            }

            const bool valid = expectedVisualTree && capture.Snapshot &&
                               capture.Snapshot->VisualTree == expectedVisualTree && capture.Snapshot->Sequence != 0;
            const bool ordered = valid && (!m_Current || capture.Snapshot->Sequence >= m_Current->Sequence);
            if (ordered)
            {
                m_Current = std::move(capture.Snapshot);
                m_Diagnostic = std::move(capture.Diagnostic);
                m_Status = UiBuilderLiveDebugStatus::Live;
                return;
            }

            if (capture.Diagnostic.empty())
            {
                capture.Diagnostic = valid ? "The live UI debugger rejected an out-of-order snapshot."
                                           : "Live Play UI data is unavailable.";
            }
            m_Diagnostic = std::move(capture.Diagnostic);
            m_Status = m_Current ? UiBuilderLiveDebugStatus::Stale : UiBuilderLiveDebugStatus::Unavailable;
        }
        catch (...)
        {
            m_Status = m_Current ? UiBuilderLiveDebugStatus::Stale : UiBuilderLiveDebugStatus::Unavailable;
        }
    }

    void UiBuilderLiveDebugStore::Close() noexcept
    {
        m_VisualTree = {};
        m_Current.reset();
        m_Diagnostic.clear();
        m_Status = UiBuilderLiveDebugStatus::Unavailable;
    }

    void UiBuilderDocument::Open(const Keire::AssetId asset, Keire::UiVisualTreeDefinition definition,
                                 const std::uint64_t revision, std::filesystem::path source,
                                 Keire::Ref<Keire::UndoContext> undo)
    {
        if (!asset || revision == 0 || source.empty())
            throw std::invalid_argument("Opening a UI Builder document requires an asset, revision, and source path.");
        Keire::UiVisualTreeAsset::Validate(definition);
        Close();
        m_Asset = asset;
        m_Selection = definition.Root.StableId;
        m_Selections = {m_Selection};
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        m_Revision = revision;
        m_Generation = 1;
        m_Source = std::move(source);
        m_Undo = std::move(undo);
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Clear();
    }

    void UiBuilderDocument::Close() noexcept
    {
        m_Asset = {};
        m_Selection = {};
        m_Selections.clear();
        m_Definition = {};
        m_Baseline = {};
        m_Revision = 0;
        m_Generation = 0;
        m_Source.clear();
        m_Undo.Reset();
        m_Dirty = false;
    }

    void UiBuilderDocument::Select(const Keire::AssetId element) noexcept
    {
        if (Find(element))
        {
            m_Selection = element;
            m_Selections = {element};
        }
    }

    void UiBuilderDocument::SetSelection(const std::span<const Keire::AssetId> elements,
                                         const Keire::AssetId primary) noexcept
    {
        m_Selections.clear();
        for (const auto element : elements)
            if (Find(element) && std::ranges::find(m_Selections, element) == m_Selections.end())
                m_Selections.push_back(element);
        m_Selection = primary && Find(primary) ? primary
                      : m_Selections.empty()   ? Keire::AssetId{}
                                               : m_Selections.back();
        NormalizeSelection();
    }

    void UiBuilderDocument::ToggleSelection(const Keire::AssetId element) noexcept
    {
        if (!Find(element))
            return;
        if (element == m_Definition.Root.StableId)
        {
            Select(element);
            return;
        }
        const auto found = std::ranges::find(m_Selections, element);
        if (found == m_Selections.end())
        {
            std::erase(m_Selections, m_Definition.Root.StableId);
            m_Selections.push_back(element);
            m_Selection = element;
        }
        else if (m_Selections.size() > 1)
        {
            m_Selections.erase(found);
            if (m_Selection == element)
                m_Selection = m_Selections.back();
        }
    }

    bool UiBuilderDocument::IsSelected(const Keire::AssetId element) const noexcept
    {
        return std::ranges::find(m_Selections, element) != m_Selections.end();
    }

    const Keire::UiVisualElementDefinition* UiBuilderDocument::Find(const Keire::AssetId element) const noexcept
    {
        return element ? FindElement(m_Definition.Root, element) : nullptr;
    }

    Keire::UiVisualElementDefinition* UiBuilderDocument::Find(const Keire::AssetId element) noexcept
    {
        return element ? FindElement(m_Definition.Root, element) : nullptr;
    }

    Keire::AssetId UiBuilderDocument::ParentOf(const Keire::AssetId element) const noexcept
    {
        return element && element != m_Definition.Root.StableId ? FindParent(m_Definition.Root, element)
                                                                : Keire::AssetId{};
    }

    bool UiBuilderDocument::Edit(const std::string_view name, Keire::UiVisualTreeDefinition candidate)
    {
        if (!m_Asset)
            throw std::logic_error("Open a UI document before editing it.");
        Keire::UiVisualTreeAsset::Validate(candidate);
        if (Keire::UiVisualTreeAsset::Encode(candidate) == Keire::UiVisualTreeAsset::Encode(m_Definition))
            return false;
        auto before = m_Definition;
        m_Definition = std::move(candidate);
        NormalizeSelection();
        RecordApplied(name, std::move(before));
        RefreshDirtyState();
        AdvanceGeneration();
        return true;
    }

    Keire::AssetId UiBuilderDocument::AddElement(const Keire::AssetId parent, const Keire::UiVisualElementType type)
    {
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The selected UI parent no longer exists.");
        Keire::UiVisualElementDefinition added;
        added.StableId = Keire::AssetId::Generate();
        added.Type = type;
        added.Name = UniqueElementName(candidate.Root, type);
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Add UI element", std::move(candidate));
        m_Selection = id;
        m_Selections = {id};
        return id;
    }

    Keire::AssetId UiBuilderDocument::AddCustomElement(const Keire::AssetId parent, std::string customType)
    {
        if (customType.empty())
            throw std::invalid_argument("A custom UI element requires its registered type name.");
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The selected UI parent no longer exists.");
        Keire::UiVisualElementDefinition added;
        added.StableId = Keire::AssetId::Generate();
        added.Type = Keire::UiVisualElementType::Custom;
        added.CustomType = std::move(customType);
        added.Name = UniqueElementName(candidate.Root, added.Type);
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Add custom UI element", std::move(candidate));
        Select(id);
        return id;
    }

    bool UiBuilderDocument::RemoveElement(const Keire::AssetId element)
    {
        if (!element || element == m_Definition.Root.StableId)
            return false;
        auto candidate = m_Definition;
        const auto parent = FindParent(candidate.Root, element);
        Keire::UiVisualElementDefinition removed;
        if (!TakeElement(candidate.Root, element, removed))
            return false;
        if (!Edit("Remove UI element", std::move(candidate)))
            return false;
        m_Selection = parent ? parent : m_Definition.Root.StableId;
        m_Selections = {m_Selection};
        return true;
    }

    bool UiBuilderDocument::RemoveSelection()
    {
        const auto removing = RootSelections(m_Definition, m_Selections);
        if (removing.empty())
            return false;
        auto candidate = m_Definition;
        auto nextSelection = FindParent(candidate.Root, removing.front());
        for (const auto element : removing)
        {
            Keire::UiVisualElementDefinition removed;
            if (!TakeElement(candidate.Root, element, removed))
                return false;
        }
        if (!Edit("Remove UI elements", std::move(candidate)))
            return false;
        if (!nextSelection || !Find(nextSelection))
            nextSelection = m_Definition.Root.StableId;
        m_Selection = nextSelection;
        m_Selections = {nextSelection};
        return true;
    }

    bool UiBuilderDocument::ReparentElement(const Keire::AssetId element, const Keire::AssetId parent,
                                            const std::size_t index)
    {
        const std::array elements{element};
        return ReparentElements(elements, parent, index);
    }

    bool UiBuilderDocument::ReparentElements(const std::span<const Keire::AssetId> elements,
                                             const Keire::AssetId parent, const std::size_t index)
    {
        if (!parent || !Find(parent))
            return false;
        const auto moving = RootSelections(m_Definition, elements);
        if (moving.empty())
            return false;
        for (const auto element : moving)
        {
            const auto* movingElement = Find(element);
            if (!movingElement || element == parent || FindElement(*movingElement, parent))
                return false;
        }
        std::size_t adjustedIndex = index;
        if (const auto* currentParent = Find(parent))
        {
            for (const auto element : moving)
            {
                const auto found =
                    std::ranges::find(currentParent->Children, element, &Keire::UiVisualElementDefinition::StableId);
                if (found != currentParent->Children.end() &&
                    static_cast<std::size_t>(std::distance(currentParent->Children.begin(), found)) < index &&
                    adjustedIndex > 0)
                {
                    --adjustedIndex;
                }
            }
        }
        auto candidate = m_Definition;
        std::vector<Keire::UiVisualElementDefinition> movedDefinitions;
        movedDefinitions.reserve(moving.size());
        for (const auto element : moving)
        {
            Keire::UiVisualElementDefinition moved;
            if (!TakeElement(candidate.Root, element, moved))
                return false;
            movedDefinitions.push_back(std::move(moved));
        }
        auto* destination = FindElement(candidate.Root, parent);
        if (!destination)
            return false;
        auto insertion = destination->Children.begin() +
                         static_cast<std::ptrdiff_t>(std::min(adjustedIndex, destination->Children.size()));
        destination->Children.insert(insertion, std::make_move_iterator(movedDefinitions.begin()),
                                     std::make_move_iterator(movedDefinitions.end()));
        if (!Edit(moving.size() == 1 ? "Reparent UI element" : "Reparent UI elements", std::move(candidate)))
            return false;
        SetSelection(moving, moving.back());
        return true;
    }

    UiBuilderClipboard UiBuilderDocument::CopySelection() const
    {
        UiBuilderClipboard result;
        for (const auto element : RootSelections(m_Definition, m_Selections))
            if (const auto* definition = Find(element))
                result.Elements.push_back(*definition);
        return result;
    }

    std::vector<Keire::AssetId> UiBuilderDocument::PasteElements(const Keire::AssetId parent,
                                                                 const UiBuilderClipboard& clipboard)
    {
        if (clipboard.Empty())
            return {};
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The UI paste destination no longer exists.");
        std::unordered_set<std::string> names;
        CollectNames(candidate.Root, names);
        std::vector<Keire::UiVisualElementDefinition> pasted = clipboard.Elements;
        std::vector<Keire::AssetId> pastedRoots;
        pastedRoots.reserve(pasted.size());
        for (auto& element : pasted)
        {
            RegenerateClipboardIdentity(element, names);
            pastedRoots.push_back(element.StableId);
        }
        destination->Children.insert(destination->Children.end(), std::make_move_iterator(pasted.begin()),
                                     std::make_move_iterator(pasted.end()));
        if (!Edit(pastedRoots.size() == 1 ? "Paste UI element" : "Paste UI elements", std::move(candidate)))
            return {};
        SetSelection(pastedRoots, pastedRoots.back());
        return pastedRoots;
    }

    Keire::AssetId UiBuilderDocument::AddTemplate(const Keire::AssetId parent, const Keire::AssetId visualTree)
    {
        if (!visualTree)
            throw std::invalid_argument("A UI template instance requires a visual-tree asset.");
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The selected UI parent no longer exists.");
        Keire::UiVisualElementDefinition added;
        added.StableId = Keire::AssetId::Generate();
        added.Type = Keire::UiVisualElementType::TemplateContainer;
        added.Name = UniqueElementName(candidate.Root, added.Type);
        added.Template = visualTree;
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Insert UI template", std::move(candidate));
        Select(id);
        return id;
    }

    Keire::AssetId UiBuilderDocument::AddSlot(const Keire::AssetId parent, std::string slot)
    {
        if (slot.empty())
            throw std::invalid_argument("A UI template slot requires a non-empty name.");
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The selected UI parent no longer exists.");
        if (ContainsName(candidate.Root, slot))
            throw std::invalid_argument("The UI template slot name is already used in this document.");
        Keire::UiVisualElementDefinition added;
        added.StableId = Keire::AssetId::Generate();
        added.Type = Keire::UiVisualElementType::Slot;
        added.Name = std::move(slot);
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Insert UI slot", std::move(candidate));
        Select(id);
        return id;
    }

    bool UiBuilderDocument::SetClasses(const std::span<const Keire::AssetId> elements, std::vector<std::string> classes)
    {
        classes = NormalizeClasses(std::move(classes));
        auto candidate = m_Definition;
        bool found = false;
        for (const auto element : elements)
        {
            if (auto* definition = FindElement(candidate.Root, element))
            {
                definition->Classes = classes;
                found = true;
            }
        }
        return found && Edit(elements.size() == 1 ? "Edit UI classes" : "Edit shared UI classes", std::move(candidate));
    }

    bool UiBuilderDocument::SetTemplate(const Keire::AssetId element, const Keire::AssetId visualTree)
    {
        auto candidate = m_Definition;
        auto* definition = FindElement(candidate.Root, element);
        if (!definition || definition->Type != Keire::UiVisualElementType::TemplateContainer || !visualTree)
            return false;
        definition->Template = visualTree;
        return Edit("Assign UI template", std::move(candidate));
    }

    bool UiBuilderDocument::SetSlot(const Keire::AssetId element, std::string slot)
    {
        auto candidate = m_Definition;
        auto* definition = FindElement(candidate.Root, element);
        if (!definition || definition->Type == Keire::UiVisualElementType::Slot)
            return false;
        definition->Slot = std::move(slot);
        return Edit("Edit UI slot", std::move(candidate));
    }

    bool UiBuilderDocument::SetBindings(const Keire::AssetId element, std::vector<Keire::UiBindingDefinition> bindings)
    {
        auto candidate = m_Definition;
        auto* definition = FindElement(candidate.Root, element);
        if (!definition)
            return false;
        definition->Bindings = std::move(bindings);
        return Edit("Edit UI bindings", std::move(candidate));
    }

    bool UiBuilderDocument::ApplySource(const std::span<const std::byte> source, std::string& diagnostic)
    {
        try
        {
            auto candidate = Keire::UiVisualTreeAsset::ParseSource(source);
            const bool changed = Edit("Edit UI source", std::move(candidate));
            diagnostic.clear();
            return changed;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    std::string UiBuilderDocument::SourcePreview() const
    {
        if (!m_Asset)
            return {};
        const auto bytes = Keire::UiVisualTreeAsset::EncodeSource(m_Definition);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    void UiBuilderDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI document before saving it.");
        const auto bytes = Keire::UiVisualTreeAsset::EncodeSource(m_Definition);
        Keire::Detail::WriteTextFileAtomically(m_Source, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        m_Baseline = m_Definition;
        m_Dirty = false;
        AdvanceGeneration();
    }

    void UiBuilderDocument::ReloadFromSource(const bool discardLocalChanges)
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI document before reloading it.");
        if (m_Dirty && !discardLocalChanges)
            throw std::logic_error("The UI document has unsaved changes. Revert explicitly to discard them.");
        const auto text = Keire::Detail::ReadTextFile(m_Source, Keire::MaximumUiDocumentBytes);
        const auto bytes = std::as_bytes(std::span(text));
        auto definition = Keire::UiVisualTreeAsset::ParseSource(bytes);
        Keire::UiVisualTreeAsset::Validate(definition);
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        m_Selection = m_Definition.Root.StableId;
        m_Selections = {m_Selection};
        m_Dirty = false;
        AdvanceGeneration();
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Clear();
    }

    bool UiBuilderDocument::Undo() { return m_Undo && m_Undo->Undo(); }

    bool UiBuilderDocument::Redo() { return m_Undo && m_Undo->Redo(); }

    void UiBuilderDocument::AdvanceGeneration() noexcept
    {
        if (++m_Generation == 0)
            ++m_Generation;
    }

    void UiBuilderDocument::RefreshDirtyState()
    {
        m_Dirty = Keire::UiVisualTreeAsset::Encode(m_Definition) != Keire::UiVisualTreeAsset::Encode(m_Baseline);
    }

    void UiBuilderDocument::NormalizeSelection() noexcept
    {
        std::erase_if(m_Selections, [this](const Keire::AssetId element) { return !Find(element); });
        for (auto selection = m_Selections.begin(); selection != m_Selections.end();)
        {
            if (std::ranges::find(m_Selections.begin(), selection, *selection) != selection)
                selection = m_Selections.erase(selection);
            else
                ++selection;
        }
        if (!Find(m_Selection))
            m_Selection = m_Selections.empty() ? m_Definition.Root.StableId : m_Selections.back();
        if (!Find(m_Selection))
        {
            m_Selection = {};
            m_Selections.clear();
            return;
        }
        if (std::ranges::find(m_Selections, m_Selection) == m_Selections.end())
            m_Selections.push_back(m_Selection);
        if (m_Selection == m_Definition.Root.StableId)
            m_Selections = {m_Selection};
    }

    void UiBuilderDocument::RecordApplied(const std::string_view name, Keire::UiVisualTreeDefinition before)
    {
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<Keire::UiVisualTreeDefinition>>();
        const auto asset = m_Asset;
        m_Undo->RecordApplied(Keire::CreateUndoCommand(
            std::string(name),
            [this, after, asset]
            {
                if (m_Asset != asset || !after->has_value())
                    return;
                m_Definition = **after;
                RefreshDirtyState();
                AdvanceGeneration();
                NormalizeSelection();
            },
            [this, after, before = std::move(before), asset]() mutable
            {
                if (m_Asset != asset)
                    return;
                *after = std::move(m_Definition);
                m_Definition = before;
                RefreshDirtyState();
                AdvanceGeneration();
                NormalizeSelection();
            },
            sizeof(Keire::UiVisualTreeDefinition)));
    }
} // namespace KeireEditor
