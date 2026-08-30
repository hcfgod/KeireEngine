#include "KeireClient/Editor/UiBuilderPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

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
} // namespace KeireEditor
