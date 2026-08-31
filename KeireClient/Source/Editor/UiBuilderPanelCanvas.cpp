#include "KeireClient/Editor/UiBuilderPanel.h"

#include "Keire/Ui/UiElements.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::array ResolutionPresets{
            std::pair{UiBuilderResolutionPreset::Hd, "HD 1280 x 720"},
            std::pair{UiBuilderResolutionPreset::FullHd, "Full HD 1920 x 1080"},
            std::pair{UiBuilderResolutionPreset::Qhd, "QHD 2560 x 1440"},
            std::pair{UiBuilderResolutionPreset::UltraHd, "Ultra HD 3840 x 2160"},
            std::pair{UiBuilderResolutionPreset::Custom, "Custom"},
        };

        [[nodiscard]] std::string_view ResolutionPresetName(const UiBuilderResolutionPreset preset) noexcept
        {
            const auto found =
                std::ranges::find_if(ResolutionPresets, [preset](const auto& entry) { return entry.first == preset; });
            return found == ResolutionPresets.end() ? "Custom" : found->second;
        }

        void CollectTemplateReferences(const Keire::UiVisualElementDefinition& element, std::set<Keire::AssetId>& out)
        {
            if (element.Template)
                out.insert(element.Template);
            for (const auto& child : element.Children)
                CollectTemplateReferences(child, out);
        }

        struct ResolvedTemplates final
        {
            std::map<Keire::AssetId, Keire::Ref<const Keire::UiVisualTreeAsset>> Assets;
            std::vector<const Keire::UiVisualTreeAsset*> Identities;
        };

        [[nodiscard]] ResolvedTemplates ResolveTemplates(const Keire::Ref<Keire::AssetSystem>& assets,
                                                         const Keire::UiVisualTreeDefinition& definition)
        {
            ResolvedTemplates result;
            if (!assets)
                return result;
            std::set<Keire::AssetId> pending;
            std::set<Keire::AssetId> visited;
            CollectTemplateReferences(definition.Root, pending);
            while (!pending.empty())
            {
                const auto id = *pending.begin();
                pending.erase(pending.begin());
                if (!visited.insert(id).second)
                    continue;
                const auto loaded =
                    assets->Load<Keire::UiVisualTreeAsset>(id, Keire::AssetPriority::High).TryGetLoaded();
                if (!loaded)
                    throw std::runtime_error("UI template " + id.ToString() + " is still loading.");
                CollectTemplateReferences(loaded->Definition().Root, pending);
                result.Identities.push_back(loaded.Get());
                result.Assets.emplace(id, loaded);
            }
            return result;
        }
        constexpr std::array CanvasLibraryTypes{
            Keire::UiVisualElementType::VisualElement, Keire::UiVisualElementType::Label,
            Keire::UiVisualElementType::Image,         Keire::UiVisualElementType::Button,
            Keire::UiVisualElementType::TextField,     Keire::UiVisualElementType::Toggle,
            Keire::UiVisualElementType::Slider,        Keire::UiVisualElementType::ProgressBar,
            Keire::UiVisualElementType::ScrollView,    Keire::UiVisualElementType::ListView,
            Keire::UiVisualElementType::TreeView,      Keire::UiVisualElementType::DropdownField,
            Keire::UiVisualElementType::Foldout,       Keire::UiVisualElementType::TabView,
            Keire::UiVisualElementType::Toolbar,       Keire::UiVisualElementType::Spacer};
        [[nodiscard]] Keire::UiColor ToUiColor(const Keire::Color color) noexcept
        {
            return {color.Red, color.Green, color.Blue, color.Alpha};
        }

        [[nodiscard]] Keire::UiItemRect TransformPreviewRect(const Keire::RuntimeUiRect rectangle,
                                                             const Keire::UiPosition origin, const float scale) noexcept
        {
            return {{origin.X + rectangle.X * scale, origin.Y + rectangle.Y * scale},
                    {origin.X + (rectangle.X + rectangle.Width) * scale,
                     origin.Y + (rectangle.Y + rectangle.Height) * scale}};
        }

        [[nodiscard]] bool IsPositiveFinite(const Keire::UiItemRect rectangle) noexcept
        {
            return std::isfinite(rectangle.Minimum.X) && std::isfinite(rectangle.Minimum.Y) &&
                   std::isfinite(rectangle.Maximum.X) && std::isfinite(rectangle.Maximum.Y) &&
                   rectangle.Maximum.X > rectangle.Minimum.X && rectangle.Maximum.Y > rectangle.Minimum.Y;
        }

        [[nodiscard]] float RulerStep(const float rasterScale) noexcept
        {
            const float logicalTarget = 72.0F / std::max(rasterScale, 0.001F);
            const float magnitude = std::pow(10.0F, std::floor(std::log10(logicalTarget)));
            const float normalized = logicalTarget / magnitude;
            return (normalized <= 2.0F ? 2.0F : normalized <= 5.0F ? 5.0F : 10.0F) * magnitude;
        }

        [[nodiscard]] Keire::UiCursorShape CanvasCursorShape(const UiBuilderCanvasGesture gesture) noexcept
        {
            switch (gesture)
            {
            case UiBuilderCanvasGesture::Move:
                return Keire::UiCursorShape::Move;
            case UiBuilderCanvasGesture::ResizeLeft:
            case UiBuilderCanvasGesture::ResizeRight:
                return Keire::UiCursorShape::ResizeHorizontal;
            case UiBuilderCanvasGesture::ResizeTop:
            case UiBuilderCanvasGesture::ResizeBottom:
                return Keire::UiCursorShape::ResizeVertical;
            case UiBuilderCanvasGesture::ResizeTopLeft:
            case UiBuilderCanvasGesture::ResizeBottomRight:
                return Keire::UiCursorShape::ResizeNorthwestSoutheast;
            case UiBuilderCanvasGesture::ResizeTopRight:
            case UiBuilderCanvasGesture::ResizeBottomLeft:
                return Keire::UiCursorShape::ResizeNortheastSouthwest;
            case UiBuilderCanvasGesture::None:
                return Keire::UiCursorShape::Default;
            }
            return Keire::UiCursorShape::Default;
        }

        [[nodiscard]] Keire::UiVisualElementDefinition* FindElement(Keire::UiVisualElementDefinition& element,
                                                                    const Keire::AssetId id) noexcept
        {
            if (element.StableId == id)
                return &element;
            for (auto& child : element.Children)
                if (auto* found = FindElement(child, id))
                    return found;
            return nullptr;
        }

        [[nodiscard]] const Keire::UiVisualElementDefinition*
        FindElement(const Keire::UiVisualElementDefinition& element, const Keire::AssetId id) noexcept
        {
            if (element.StableId == id)
                return &element;
            for (const auto& child : element.Children)
                if (const auto* found = FindElement(child, id))
                    return found;
            return nullptr;
        }

        void CollectRuntimeElements(const Keire::UiVisualElementDefinition& element,
                                    const UiBuilderRetainedPreview& preview,
                                    std::vector<Keire::RuntimeUiElementId>& result)
        {
            if (const auto found =
                    std::ranges::find(preview.Elements, element.StableId, &UiBuilderPreviewElement::StableId);
                found != preview.Elements.end())
            {
                result.push_back(found->RuntimeId);
            }
            for (const auto& child : element.Children)
                CollectRuntimeElements(child, preview, result);
        }

    } // namespace

    void UiBuilderPanel::DrawPreviewToolbar(Keire::UiFrame& ui)
    {
        auto& settings = m_PreviewSettings;
        ui.SetNextItemWidth(210.0F);
        if (auto combo = ui.BeginCombo("Resolution", ResolutionPresetName(settings.Preset)); combo)
        {
            for (const auto& [preset, label] : ResolutionPresets)
                if (ui.Selectable(label, settings.Preset == preset))
                    settings.ApplyPreset(preset);
        }
        ui.SameLine();
        if (ui.Button("Landscape"))
            settings.ApplyOrientation(UiBuilderOrientation::Landscape);
        ui.SameLine();
        if (ui.Button("Portrait"))
            settings.ApplyOrientation(UiBuilderOrientation::Portrait);
        ui.SameLine();
        const auto gameView = m_Controller.UiBuilderGameViewSize();
        if (auto disabled = ui.BeginDisabled(!gameView || gameView->Width < 1.0F || gameView->Height < 1.0F); disabled)
            if (ui.Button("Match Game View") && gameView)
                (void)settings.MatchGameView(static_cast<std::uint32_t>(std::lround(gameView->Width)),
                                             static_cast<std::uint32_t>(std::lround(gameView->Height)));
        ui.SameLine();
        if (ui.Button("Fit"))
            settings.ResetView();
        ui.SameLine();
        if (ui.Button("-"))
            settings.ZoomBy(1.0F / 1.2F);
        ui.SameLine();
        if (ui.Button("+"))
            settings.ZoomBy(1.2F);
        ui.SameLine();
        ui.Text(std::to_string(static_cast<int>(std::lround(settings.Zoom * 100.0F))) + "% of fit");

        if (auto canvasSettings = ui.BeginTreeNode("Canvas settings, safe area, guides, and pseudo-states");
            canvasSettings)
        {
            std::uint64_t width = settings.Width;
            std::uint64_t height = settings.Height;
            bool changed = ui.DragUnsignedInteger("Width", width, 8.0, 64, 8192);
            changed = ui.DragUnsignedInteger("Height", height, 8.0, 64, 8192) || changed;
            if (changed)
            {
                settings.Width = static_cast<std::uint32_t>(width);
                settings.Height = static_cast<std::uint32_t>(height);
                settings.Preset = UiBuilderResolutionPreset::Custom;
            }
            std::uint64_t referenceWidth = settings.ReferenceWidth;
            std::uint64_t referenceHeight = settings.ReferenceHeight;
            changed = ui.DragUnsignedInteger("Reference Width", referenceWidth, 8.0, 64, 8192);
            changed = ui.DragUnsignedInteger("Reference Height", referenceHeight, 8.0, 64, 8192) || changed;
            if (changed)
            {
                settings.ReferenceWidth = static_cast<std::uint32_t>(referenceWidth);
                settings.ReferenceHeight = static_cast<std::uint32_t>(referenceHeight);
            }
            double dpi = settings.Dpi;
            if (ui.DragScalar("DPI", dpi, 1.0, 48.0, 288.0))
                settings.Dpi = static_cast<float>(dpi);
            const auto scaleModeName = [](const Keire::RuntimeUiScaleMode mode) noexcept -> std::string_view
            {
                switch (mode)
                {
                case Keire::RuntimeUiScaleMode::ConstantPixels:
                    return "Constant Pixels";
                case Keire::RuntimeUiScaleMode::ScaleWithViewport:
                    return "Scale With Viewport";
                case Keire::RuntimeUiScaleMode::ConstantPhysicalSize:
                    return "Constant Physical Size";
                }
                return "Scale With Viewport";
            };
            if (auto scaleMode = ui.BeginCombo("Scale Mode", scaleModeName(settings.ScaleMode)); scaleMode)
            {
                constexpr std::array modes{
                    Keire::RuntimeUiScaleMode::ConstantPixels,
                    Keire::RuntimeUiScaleMode::ScaleWithViewport,
                    Keire::RuntimeUiScaleMode::ConstantPhysicalSize,
                };
                for (const auto mode : modes)
                {
                    if (ui.Selectable(scaleModeName(mode), settings.ScaleMode == mode))
                        settings.ScaleMode = mode;
                }
            }
            (void)ui.SliderFloat("UI Scale", settings.UserScale, 0.5F, 2.0F);
            (void)ui.SliderFloat("Match Width / Height", settings.MatchWidthOrHeight, 0.0F, 1.0F);
            (void)ui.Checkbox("Safe Area", settings.ShowSafeArea);
            double left = settings.SafeArea.Left;
            double top = settings.SafeArea.Top;
            double right = settings.SafeArea.Right;
            double bottom = settings.SafeArea.Bottom;
            changed = ui.DragScalar("Safe Left", left, 1.0, 0.0, static_cast<double>(settings.Width));
            changed = ui.DragScalar("Safe Top", top, 1.0, 0.0, static_cast<double>(settings.Height)) || changed;
            changed = ui.DragScalar("Safe Right", right, 1.0, 0.0, static_cast<double>(settings.Width)) || changed;
            changed = ui.DragScalar("Safe Bottom", bottom, 1.0, 0.0, static_cast<double>(settings.Height)) || changed;
            if (changed)
                settings.SafeArea = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                                     static_cast<float>(bottom)};
            (void)ui.Checkbox("Rulers", settings.ShowRulers);
            ui.SameLine();
            (void)ui.Checkbox("Guides", settings.ShowGuides);
            double verticalGuide = settings.VerticalGuide;
            double horizontalGuide = settings.HorizontalGuide;
            if (ui.DragScalar("Vertical Guide", verticalGuide, 1.0, -1.0, static_cast<double>(settings.Width)))
                settings.VerticalGuide = static_cast<float>(verticalGuide);
            if (ui.DragScalar("Horizontal Guide", horizontalGuide, 1.0, -1.0, static_cast<double>(settings.Height)))
                settings.HorizontalGuide = static_cast<float>(horizontalGuide);
            constexpr std::array pseudoStates{
                std::pair{"Hover", Keire::UiStylePseudoState::Hover},
                std::pair{"Active", Keire::UiStylePseudoState::Active},
                std::pair{"Focus", Keire::UiStylePseudoState::Focus},
                std::pair{"Disabled", Keire::UiStylePseudoState::Disabled},
                std::pair{"Checked", Keire::UiStylePseudoState::Checked},
            };
            for (const auto& [label, state] : pseudoStates)
            {
                auto enabled = settings.HasPseudoState(state);
                if (ui.Checkbox(label, enabled))
                    settings.SetPseudoState(state, enabled);
                ui.SameLine();
            }
            ui.TextColored(m_Controller.UiBuilderTheme().MutedText, "Pseudo-state preview");
        }
        settings.Normalize();
    }

    Keire::UiTemplateResolver
    UiBuilderPanel::CreateTemplateResolver(const Keire::UiVisualTreeDefinition& definition,
                                           std::vector<const Keire::UiVisualTreeAsset*>& identities) const
    {
        auto templates = ResolveTemplates(m_Controller.UiBuilderAssets(), definition);
        identities = std::move(templates.Identities);
        return [resolved = std::move(templates.Assets)](const Keire::AssetId id)
        {
            const auto found = resolved.find(id);
            return found == resolved.end() ? Keire::Ref<const Keire::UiVisualTreeAsset>{} : found->second;
        };
    }

    void UiBuilderPanel::RefreshPreviewSnapshot()
    {
        auto& document = m_Controller.UiBuilderState();
        std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>> styleSheets;
        std::vector<const Keire::UiStyleSheetAsset*> styleSheetIdentities;
        const auto assets = m_Controller.UiBuilderAssets();
        if (assets)
        {
            for (const auto asset : document.Definition().StyleSheets)
            {
                const auto styleSheet =
                    assets->Load<Keire::UiStyleSheetAsset>(asset, Keire::AssetPriority::High).TryGetLoaded();
                if (!styleSheet)
                    continue;
                styleSheetIdentities.push_back(styleSheet.Get());
                styleSheets.push_back(styleSheet);
            }
        }
        auto templates = ResolveTemplates(assets, document.Definition());
        auto layoutSettings = m_PreviewSettings;
        layoutSettings.ResetView();
        if (m_PreviewSnapshot && m_PreviewAsset == document.Asset() && m_PreviewSelection == document.Selection() &&
            m_PreviewGeneration == document.Generation() && m_PreviewStyleSheets == styleSheetIdentities &&
            m_PreviewTemplates == templates.Identities && m_BuiltPreviewSettings == layoutSettings)
            return;

        m_PreviewAsset = document.Asset();
        m_PreviewSelection = document.Selection();
        m_PreviewGeneration = document.Generation();
        m_PreviewStyleSheets = std::move(styleSheetIdentities);
        m_PreviewTemplates = templates.Identities;
        m_BuiltPreviewSettings = layoutSettings;
        m_PreviewSnapshot = BuildUiBuilderRetainedPreview(
            document.Definition(), document.Selection(), layoutSettings, styleSheets,
            [resolved = std::move(templates.Assets)](const Keire::AssetId id)
            {
                const auto found = resolved.find(id);
                return found == resolved.end() ? Keire::Ref<const Keire::UiVisualTreeAsset>{} : found->second;
            });
        m_PreviewDiagnostic.clear();
    }

    Keire::RuntimeUiRect UiBuilderPanel::CanvasParentBounds(const Keire::AssetId parent) const noexcept
    {
        if (m_PreviewSnapshot)
        {
            const auto found =
                std::ranges::find(m_PreviewSnapshot->Elements, parent, &UiBuilderPreviewElement::StableId);
            if (found != m_PreviewSnapshot->Elements.end())
            {
                const auto& state = found->State;
                const float scale = state.LayoutScale;
                return {
                    state.Rect.X + state.Style.Padding.Left * scale,
                    state.Rect.Y + state.Style.Padding.Top * scale,
                    std::max(0.0F, state.Rect.Width - (state.Style.Padding.Left + state.Style.Padding.Right) * scale),
                    std::max(0.0F, state.Rect.Height - (state.Style.Padding.Top + state.Style.Padding.Bottom) * scale),
                };
            }
        }
        return {0.0F, 0.0F, static_cast<float>(m_PreviewSettings.Width), static_cast<float>(m_PreviewSettings.Height)};
    }

    void UiBuilderPanel::DrawViewport(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        DrawPreviewToolbar(ui);
        ui.Separator();
        try
        {
            RefreshPreviewSnapshot();
        }
        catch (const std::exception& error)
        {
            m_PreviewSnapshot.reset();
            m_PreviewDiagnostic = error.what();
        }

        const auto available = ui.ContentAvailable();
        const Keire::UiSize viewportSize{std::max(160.0F, available.Width), std::max(120.0F, available.Height)};
        (void)ui.InvisibleButton("UiBuilderRetainedViewport", viewportSize);
        const auto viewport = ui.LastItemRect();
        const auto viewportState = ui.LastItemState();
        const auto pointer = ui.PointerState();
        if (viewportState.Hovered && pointer.Wheel != 0.0F)
        {
            m_PreviewSettings.ZoomBy(std::pow(1.15F, pointer.Wheel));
            ui.CapturePointerWheel();
        }
        if (viewportState.Hovered && (pointer.MiddleDown || pointer.RightDown))
            m_PreviewSettings.PanBy({pointer.Delta.X, pointer.Delta.Y});

        const auto& theme = m_Controller.UiBuilderTheme();
        ui.DrawFilledRectangle(viewport, {0.035F, 0.04F, 0.05F, 1.0F});
        if (!m_PreviewSnapshot)
        {
            ui.DrawOverlayText({viewport.Minimum.X + 12.0F, viewport.Minimum.Y + 12.0F}, theme.Error,
                               "Preview unavailable: " + m_PreviewDiagnostic, 0.0F, viewport);
            return;
        }

        const float availableWidth = std::max(1.0F, viewport.Size().Width - 24.0F);
        const float availableHeight = std::max(1.0F, viewport.Size().Height - 24.0F);
        const float fit = std::min(availableWidth / static_cast<float>(m_PreviewSettings.Width),
                                   availableHeight / static_cast<float>(m_PreviewSettings.Height));
        const float rasterScale = std::max(0.001F, fit * m_PreviewSettings.Zoom);
        const Keire::UiSize canvasSize{static_cast<float>(m_PreviewSettings.Width) * rasterScale,
                                       static_cast<float>(m_PreviewSettings.Height) * rasterScale};
        const Keire::UiPosition canvasOrigin{
            viewport.Minimum.X + (viewport.Size().Width - canvasSize.Width) * 0.5F + m_PreviewSettings.Pan.X,
            viewport.Minimum.Y + (viewport.Size().Height - canvasSize.Height) * 0.5F + m_PreviewSettings.Pan.Y,
        };
        const Keire::UiItemRect canvas{canvasOrigin,
                                       {canvasOrigin.X + canvasSize.Width, canvasOrigin.Y + canvasSize.Height}};

        if (auto target = ui.BeginDragTarget(canvas, "UiBuilderCanvasControlDrop"); target)
        {
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_UI_CONTROL", payload) && payload.size() == 1)
            {
                const auto type = static_cast<Keire::UiVisualElementType>(std::to_integer<std::uint8_t>(payload[0]));
                if (std::ranges::find(CanvasLibraryTypes, type) != CanvasLibraryTypes.end())
                {
                    try
                    {
                        const auto parent = document.Definition().Root.StableId;
                        const auto bounds = CanvasParentBounds(parent);
                        const auto created =
                            document.AddCanvasElement(parent, type, bounds,
                                                      {(pointer.Position.X - canvasOrigin.X) / rasterScale,
                                                       (pointer.Position.Y - canvasOrigin.Y) / rasterScale});
                        document.Select(created);
                        m_DraftElement = {};
                        m_Message = "Placed " + std::string(UiBuilderElementTypeName(type)) + " on the canvas.";
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                        m_Controller.ReportUiBuilderError(m_Message);
                    }
                }
            }
            if (ui.AcceptDragPayload("KEIRE_UI_CUSTOM_CONTROL", payload) && !payload.empty())
            {
                try
                {
                    const std::string customType(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto parent = document.Definition().Root.StableId;
                    const auto bounds = CanvasParentBounds(parent);
                    const auto created =
                        document.AddCanvasCustomElement(parent, customType, bounds,
                                                        {(pointer.Position.X - canvasOrigin.X) / rasterScale,
                                                         (pointer.Position.Y - canvasOrigin.Y) / rasterScale});
                    document.Select(created);
                    m_DraftElement = {};
                    m_Message = "Placed " + customType + " on the canvas.";
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
        }

        UiBuilderCanvasGesture hoveredSelectionGesture = UiBuilderCanvasGesture::None;
        if (m_PreviewSnapshot->SelectedState && document.Selection() != document.Definition().Root.StableId)
        {
            const auto selected = TransformPreviewRect(m_CanvasGesture.Gesture == UiBuilderCanvasGesture::None
                                                           ? m_PreviewSnapshot->SelectedState->Rect
                                                           : m_CanvasGesture.Draft,
                                                       canvasOrigin, rasterScale);
            hoveredSelectionGesture = HitTestUiBuilderCanvasGesture(selected, pointer.Position);
        }
        auto cursorGesture =
            m_CanvasGesture.Gesture != UiBuilderCanvasGesture::None ? m_CanvasGesture.Gesture : hoveredSelectionGesture;
        if (cursorGesture == UiBuilderCanvasGesture::None && viewportState.Hovered && canvas.Contains(pointer.Position))
        {
            const float x = (pointer.Position.X - canvasOrigin.X) / rasterScale;
            const float y = (pointer.Position.Y - canvasOrigin.Y) / rasterScale;
            const auto hovered =
                std::ranges::find_if(m_PreviewSnapshot->Elements.rbegin(), m_PreviewSnapshot->Elements.rend(),
                                     [&](const auto& element)
                                     {
                                         return element.StableId != document.Definition().Root.StableId &&
                                                element.State.Visible && element.State.Rect.Contains(x, y) &&
                                                element.State.ClipRect.Contains(x, y);
                                     });
            if (hovered != m_PreviewSnapshot->Elements.rend())
                cursorGesture = UiBuilderCanvasGesture::Move;
        }
        if (cursorGesture != UiBuilderCanvasGesture::None)
            ui.SetCursorShape(CanvasCursorShape(cursorGesture));

        if (viewportState.Hovered && pointer.LeftPressed && canvas.Contains(pointer.Position))
        {
            constexpr float HandleRadius = 9.0F;
            const auto beginGesture = [&](const UiBuilderPreviewElement& element, const UiBuilderCanvasGesture gesture)
            {
                m_CanvasGesture.Element = element.StableId;
                m_CanvasGesture.Initial = element.State.Rect;
                m_CanvasGesture.Draft = element.State.Rect;
                const auto parent = document.ParentOf(element.StableId);
                m_CanvasGesture.ParentBounds =
                    CanvasParentBounds(parent ? parent : document.Definition().Root.StableId);
                m_CanvasGesture.StartPointer = pointer.Position;
                m_CanvasGesture.Gesture = gesture;
                m_CanvasGesture.RuntimeElements.clear();
                if (const auto* definition = FindElement(document.Definition().Root, element.StableId))
                    CollectRuntimeElements(*definition, *m_PreviewSnapshot, m_CanvasGesture.RuntimeElements);
                m_CanvasGesture.Changed = false;
            };

            const UiBuilderPreviewElement* hitElement = nullptr;
            UiBuilderCanvasGesture gesture = UiBuilderCanvasGesture::None;
            if (m_PreviewSnapshot->SelectedState && document.Selection() != document.Definition().Root.StableId)
            {
                const auto selection =
                    TransformPreviewRect(m_PreviewSnapshot->SelectedState->Rect, canvasOrigin, rasterScale);
                gesture = HitTestUiBuilderCanvasGesture(selection, pointer.Position, HandleRadius);
                if (gesture != UiBuilderCanvasGesture::None)
                {
                    const auto selected = std::ranges::find(m_PreviewSnapshot->Elements, document.Selection(),
                                                            &UiBuilderPreviewElement::StableId);
                    if (selected != m_PreviewSnapshot->Elements.end())
                        hitElement = &*selected;
                }
            }

            if (!hitElement)
            {
                const float x = (pointer.Position.X - canvasOrigin.X) / rasterScale;
                const float y = (pointer.Position.Y - canvasOrigin.Y) / rasterScale;
                for (auto element = m_PreviewSnapshot->Elements.rbegin(); element != m_PreviewSnapshot->Elements.rend();
                     ++element)
                {
                    if (!element->State.Visible || !element->State.Rect.Contains(x, y) ||
                        !element->State.ClipRect.Contains(x, y))
                    {
                        continue;
                    }
                    hitElement = &*element;
                    document.Select(element->StableId);
                    m_DraftElement = {};
                    const auto selection = TransformPreviewRect(element->State.Rect, canvasOrigin, rasterScale);
                    gesture = HitTestUiBuilderCanvasGesture(selection, pointer.Position, HandleRadius);
                    if (gesture == UiBuilderCanvasGesture::None)
                        gesture = UiBuilderCanvasGesture::Move;
                    break;
                }
            }
            if (hitElement && document.Selection() != document.Definition().Root.StableId)
                beginGesture(*hitElement, gesture);
        }

        if (m_CanvasGesture.Gesture != UiBuilderCanvasGesture::None && pointer.LeftDown)
        {
            const float deltaX = (pointer.Position.X - m_CanvasGesture.StartPointer.X) / rasterScale;
            const float deltaY = (pointer.Position.Y - m_CanvasGesture.StartPointer.Y) / rasterScale;
            m_CanvasGesture.Changed = m_CanvasGesture.Changed ||
                                      std::abs(pointer.Position.X - m_CanvasGesture.StartPointer.X) >= 2.0F ||
                                      std::abs(pointer.Position.Y - m_CanvasGesture.StartPointer.Y) >= 2.0F;
            m_CanvasGesture.Draft = ResolveUiBuilderCanvasGesture(m_CanvasGesture.Initial, m_CanvasGesture.ParentBounds,
                                                                  {deltaX, deltaY}, m_CanvasGesture.Gesture);
        }
        bool finishCanvasGestureAfterPresentation = false;
        if (m_CanvasGesture.Gesture != UiBuilderCanvasGesture::None && pointer.LeftReleased)
        {
            if (m_CanvasGesture.Changed)
            {
                try
                {
                    auto candidate = document.Definition();
                    auto* element = FindElement(candidate.Root, m_CanvasGesture.Element);
                    if (!element)
                        throw std::runtime_error("The canvas element was removed before the gesture completed.");
                    PersistUiBuilderCanvasGeometry(*element, m_CanvasGesture.ParentBounds, m_CanvasGesture.Draft);
                    (void)document.Edit(m_CanvasGesture.Gesture == UiBuilderCanvasGesture::Move ? "Move UI element"
                                                                                                : "Resize UI element",
                                        std::move(candidate));
                    m_DraftElement = {};
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportUiBuilderError(m_Message);
                }
            }
            // The retained preview was built before input handling this frame. Keep presenting the committed draft
            // through the release frame so the stale snapshot cannot flash at the element's previous position.
            finishCanvasGestureAfterPresentation = true;
        }

        [[maybe_unused]] auto viewportClip = ui.PushClipRect(viewport);
        constexpr float GridSpacing = 24.0F;
        const Keire::UiColor minorGrid{0.11F, 0.125F, 0.15F, 0.55F};
        for (float x = viewport.Minimum.X; x < viewport.Maximum.X; x += GridSpacing)
            ui.DrawLine({x, viewport.Minimum.Y}, {x, viewport.Maximum.Y}, minorGrid);
        for (float y = viewport.Minimum.Y; y < viewport.Maximum.Y; y += GridSpacing)
            ui.DrawLine({viewport.Minimum.X, y}, {viewport.Maximum.X, y}, minorGrid);
        ui.DrawFilledRectangle(canvas, {0.075F, 0.08F, 0.095F, 1.0F});
        ui.DrawRectangle(canvas, {0.32F, 0.36F, 0.44F, 1.0F});
        if (m_PreviewSettings.ShowSafeArea)
        {
            const Keire::RuntimeUiRect safe{
                m_PreviewSettings.SafeArea.Left,
                m_PreviewSettings.SafeArea.Top,
                std::max(0.0F, static_cast<float>(m_PreviewSettings.Width) - m_PreviewSettings.SafeArea.Left -
                                   m_PreviewSettings.SafeArea.Right),
                std::max(0.0F, static_cast<float>(m_PreviewSettings.Height) - m_PreviewSettings.SafeArea.Top -
                                   m_PreviewSettings.SafeArea.Bottom),
            };
            ui.DrawRectangle(TransformPreviewRect(safe, canvasOrigin, rasterScale), {0.28F, 0.72F, 1.0F, 0.8F}, 1.0F);
        }

        for (const auto& sourceCommand : m_PreviewSnapshot->DrawCommands)
        {
            auto command = sourceCommand;
            if (m_CanvasGesture.Gesture != UiBuilderCanvasGesture::None && m_CanvasGesture.Changed &&
                std::ranges::find(m_CanvasGesture.RuntimeElements, command.Element) !=
                    m_CanvasGesture.RuntimeElements.end())
            {
                command.Rect =
                    TransformUiBuilderCanvasPreviewRect(command.Rect, m_CanvasGesture.Initial, m_CanvasGesture.Draft);
                constexpr float Epsilon = 0.01F;
                const bool clipOwnedBySelection =
                    command.ClipRect.X >= m_CanvasGesture.Initial.X - Epsilon &&
                    command.ClipRect.Y >= m_CanvasGesture.Initial.Y - Epsilon &&
                    command.ClipRect.X + command.ClipRect.Width <=
                        m_CanvasGesture.Initial.X + m_CanvasGesture.Initial.Width + Epsilon &&
                    command.ClipRect.Y + command.ClipRect.Height <=
                        m_CanvasGesture.Initial.Y + m_CanvasGesture.Initial.Height + Epsilon;
                if (clipOwnedBySelection)
                {
                    command.ClipRect = TransformUiBuilderCanvasPreviewRect(command.ClipRect, m_CanvasGesture.Initial,
                                                                           m_CanvasGesture.Draft);
                }
                command.ClipRect = command.ClipRect.Intersect(m_CanvasGesture.ParentBounds);
            }
            if (command.Type == Keire::RuntimeUiDrawType::PushClip || command.Type == Keire::RuntimeUiDrawType::PopClip)
            {
                continue;
            }
            const auto rectangle = TransformPreviewRect(command.Rect, canvasOrigin, rasterScale);
            const auto clipRectangle = TransformPreviewRect(command.ClipRect, canvasOrigin, rasterScale);
            if (!IsPositiveFinite(rectangle) || !IsPositiveFinite(clipRectangle))
                continue;
            [[maybe_unused]] auto commandClip = ui.PushClipRect(clipRectangle);
            switch (command.Type)
            {
            case Keire::RuntimeUiDrawType::Quad:
                ui.DrawFilledRectangle(rectangle, ToUiColor(command.ColorValue), command.CornerRadius * rasterScale);
                if (command.BorderWidth > 0.0F && command.BorderColor.Alpha > 0.0F)
                    ui.DrawRectangle(rectangle, ToUiColor(command.BorderColor),
                                     std::max(1.0F, command.BorderWidth * rasterScale),
                                     command.CornerRadius * rasterScale);
                break;
            case Keire::RuntimeUiDrawType::Image:
                ui.DrawFilledRectangle(rectangle, {0.12F, 0.15F, 0.2F, command.ColorValue.Alpha},
                                       command.CornerRadius * rasterScale);
                ui.DrawLine(rectangle.Minimum, rectangle.Maximum, {0.38F, 0.48F, 0.62F, 0.85F});
                ui.DrawLine({rectangle.Maximum.X, rectangle.Minimum.Y}, {rectangle.Minimum.X, rectangle.Maximum.Y},
                            {0.38F, 0.48F, 0.62F, 0.85F});
                break;
            case Keire::RuntimeUiDrawType::Text:
            {
                const float fontSize = std::max(1.0F, command.FontSize * rasterScale);
                const auto measured = ui.MeasureText(command.Text, fontSize);
                Keire::UiPosition position = rectangle.Minimum;
                if (command.HorizontalAlignment == Keire::RuntimeUiAlignment::Center)
                    position.X += (rectangle.Size().Width - measured.Width) * 0.5F;
                else if (command.HorizontalAlignment == Keire::RuntimeUiAlignment::End)
                    position.X += rectangle.Size().Width - measured.Width;
                if (command.VerticalAlignment == Keire::RuntimeUiAlignment::Center)
                    position.Y += (rectangle.Size().Height - measured.Height) * 0.5F;
                else if (command.VerticalAlignment == Keire::RuntimeUiAlignment::End)
                    position.Y += rectangle.Size().Height - measured.Height;
                ui.DrawOverlayText(position, ToUiColor(command.ColorValue), command.Text, fontSize, clipRectangle);
                break;
            }
            case Keire::RuntimeUiDrawType::PushClip:
            case Keire::RuntimeUiDrawType::PopClip:
                break;
            }
        }

        if (m_PreviewSettings.ShowGuides)
        {
            const Keire::UiColor guide{0.22F, 0.78F, 1.0F, 0.9F};
            if (m_PreviewSettings.VerticalGuide >= 0.0F)
            {
                const float x = canvasOrigin.X + m_PreviewSettings.VerticalGuide * rasterScale;
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Maximum.Y}, guide, 1.0F);
            }
            if (m_PreviewSettings.HorizontalGuide >= 0.0F)
            {
                const float y = canvasOrigin.Y + m_PreviewSettings.HorizontalGuide * rasterScale;
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Maximum.X, y}, guide, 1.0F);
            }
        }
        if (m_PreviewSettings.ShowRulers)
        {
            constexpr float RulerHeight = 18.0F;
            constexpr float RulerWidth = 38.0F;
            const Keire::UiColor rulerBackground{0.035F, 0.04F, 0.055F, 0.88F};
            const Keire::UiColor rulerText{0.62F, 0.69F, 0.78F, 1.0F};
            ui.DrawFilledRectangle({canvas.Minimum, {canvas.Maximum.X, canvas.Minimum.Y + RulerHeight}},
                                   rulerBackground);
            ui.DrawFilledRectangle({canvas.Minimum, {canvas.Minimum.X + RulerWidth, canvas.Maximum.Y}},
                                   rulerBackground);
            const float step = RulerStep(rasterScale);
            for (float value = 0.0F; value <= static_cast<float>(m_PreviewSettings.Width); value += step)
            {
                const float x = canvasOrigin.X + value * rasterScale;
                ui.DrawLine({x, canvas.Minimum.Y}, {x, canvas.Minimum.Y + 6.0F}, rulerText);
                ui.DrawOverlayText({x + 2.0F, canvas.Minimum.Y + 5.0F}, rulerText,
                                   std::to_string(static_cast<int>(std::lround(value))), 9.0F, canvas);
            }
            for (float value = 0.0F; value <= static_cast<float>(m_PreviewSettings.Height); value += step)
            {
                const float y = canvasOrigin.Y + value * rasterScale;
                ui.DrawLine({canvas.Minimum.X, y}, {canvas.Minimum.X + 6.0F, y}, rulerText);
                ui.DrawOverlayText({canvas.Minimum.X + 7.0F, y + 1.0F}, rulerText,
                                   std::to_string(static_cast<int>(std::lround(value))), 9.0F, canvas);
            }
        }

        if (m_PreviewSnapshot->SelectedState && document.Selection() != document.Definition().Root.StableId)
        {
            const auto selectedRect = TransformPreviewRect(m_CanvasGesture.Gesture == UiBuilderCanvasGesture::None
                                                               ? m_PreviewSnapshot->SelectedState->Rect
                                                               : m_CanvasGesture.Draft,
                                                           canvasOrigin, rasterScale);
            auto selectionFill = theme.Accent;
            selectionFill.Alpha = 0.055F;
            ui.DrawFilledRectangle(selectedRect, selectionFill, 1.0F);
            ui.DrawRectangle(selectedRect, {0.01F, 0.015F, 0.02F, 0.92F}, 3.0F, 1.0F);
            ui.DrawRectangle(selectedRect, theme.Accent, 1.0F, 1.0F);
            const float centerX = (selectedRect.Minimum.X + selectedRect.Maximum.X) * 0.5F;
            const float centerY = (selectedRect.Minimum.Y + selectedRect.Maximum.Y) * 0.5F;
            struct SelectionGrip final
            {
                UiBuilderCanvasGesture Gesture = UiBuilderCanvasGesture::None;
                Keire::UiPosition Center;
                Keire::UiSize Size;
            };
            const std::array grips{
                SelectionGrip{UiBuilderCanvasGesture::ResizeTopLeft, selectedRect.Minimum, {7.0F, 7.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeTop, {centerX, selectedRect.Minimum.Y}, {11.0F, 5.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeTopRight,
                              {selectedRect.Maximum.X, selectedRect.Minimum.Y},
                              {7.0F, 7.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeRight, {selectedRect.Maximum.X, centerY}, {5.0F, 11.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeBottomRight, selectedRect.Maximum, {7.0F, 7.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeBottom, {centerX, selectedRect.Maximum.Y}, {11.0F, 5.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeBottomLeft,
                              {selectedRect.Minimum.X, selectedRect.Maximum.Y},
                              {7.0F, 7.0F}},
                SelectionGrip{UiBuilderCanvasGesture::ResizeLeft, {selectedRect.Minimum.X, centerY}, {5.0F, 11.0F}},
            };
            for (const auto& grip : grips)
            {
                const bool active = m_CanvasGesture.Gesture == grip.Gesture;
                const bool hovered = !active && hoveredSelectionGesture == grip.Gesture;
                const float expansion = active ? 2.0F : hovered ? 1.0F : 0.0F;
                const float halfWidth = grip.Size.Width * 0.5F + expansion;
                const float halfHeight = grip.Size.Height * 0.5F + expansion;
                const Keire::UiItemRect rectangle{{grip.Center.X - halfWidth, grip.Center.Y - halfHeight},
                                                  {grip.Center.X + halfWidth, grip.Center.Y + halfHeight}};
                const Keire::UiColor fill = active    ? theme.Accent
                                            : hovered ? Keire::UiColor{0.78F, 0.9F, 1.0F, 1.0F}
                                                      : Keire::UiColor{0.94F, 0.97F, 1.0F, 1.0F};
                ui.DrawFilledRectangle(rectangle, {0.01F, 0.015F, 0.02F, 0.95F}, 2.5F);
                const Keire::UiItemRect inset{{rectangle.Minimum.X + 1.0F, rectangle.Minimum.Y + 1.0F},
                                              {rectangle.Maximum.X - 1.0F, rectangle.Maximum.Y - 1.0F}};
                ui.DrawFilledRectangle(inset, fill, 1.5F);
            }
        }
        if (finishCanvasGestureAfterPresentation)
            m_CanvasGesture = {};
    }
} // namespace KeireEditor
