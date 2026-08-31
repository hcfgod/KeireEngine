#include "KeireClient/Editor/UiBuilderDocument.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
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

        void SetInlineStyle(std::vector<Keire::UiNamedValue>& styles, const std::string_view name, std::string value)
        {
            const auto found = std::ranges::find(styles, name, &Keire::UiNamedValue::Name);
            if (found != styles.end())
                found->Value = std::move(value);
            else
                styles.push_back({std::string(name), std::move(value)});
        }

        [[nodiscard]] std::string CanvasPixelValue(float value)
        {
            if (value == 0.0F)
                value = 0.0F;
            std::array<char, 64> buffer{};
            const auto [end, error] =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general,
                              std::numeric_limits<float>::max_digits10);
            if (error != std::errc{})
                throw std::runtime_error("UI Builder could not encode canvas geometry.");
            return std::string(buffer.data(), end) + "px";
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

        void AddDefaultAuthoringProperties(Keire::UiVisualElementDefinition& element)
        {
            using Type = Keire::UiVisualElementType;
            const auto size = [&element](const std::string_view width, const std::string_view height)
            {
                element.InlineStyles.push_back({"width", std::string(width)});
                element.InlineStyles.push_back({"height", std::string(height)});
            };
            const auto text = [&element](const std::string_view value)
            { element.Attributes.push_back({"text", std::string(value)}); };

            switch (element.Type)
            {
            case Type::VisualElement:
                size("320px", "180px");
                break;
            case Type::Label:
                size("240px", "40px");
                text("Label");
                break;
            case Type::Image:
                size("180px", "140px");
                break;
            case Type::Button:
                size("220px", "48px");
                text("Button");
                break;
            case Type::TextField:
                size("260px", "48px");
                element.Attributes.push_back({"value", "Text field"});
                break;
            case Type::Toggle:
                size("220px", "40px");
                text("Toggle");
                element.Attributes.push_back({"checked", "false"});
                break;
            case Type::Slider:
            case Type::ProgressBar:
                size("280px", "32px");
                element.Attributes.push_back({"minimum", "0"});
                element.Attributes.push_back({"maximum", "100"});
                element.Attributes.push_back({"value", "50"});
                break;
            case Type::ScrollView:
            case Type::ListView:
            case Type::TreeView:
                size("320px", "240px");
                break;
            case Type::DropdownField:
                size("240px", "44px");
                element.Attributes.push_back({"value", "Option"});
                break;
            case Type::Foldout:
                size("320px", "160px");
                text("Foldout");
                break;
            case Type::TabView:
                size("420px", "260px");
                break;
            case Type::Toolbar:
                size("480px", "48px");
                element.InlineStyles.push_back({"flex-direction", "row"});
                break;
            case Type::Spacer:
                size("100px", "24px");
                break;
            case Type::Custom:
                size("220px", "64px");
                break;
            case Type::TemplateContainer:
            case Type::Slot:
                break;
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
                    result.push_back({definition.StableId, *runtime, *state});
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

        class ContinuousUiDocumentUndoCommand final : public Keire::UndoCommand
        {
          public:
            ContinuousUiDocumentUndoCommand(std::string name, std::string mergeKey, Keire::UndoOperation redo,
                                            Keire::UndoOperation undo)
                : m_Name(std::move(name)), m_MergeKey(std::move(mergeKey)), m_Redo(std::move(redo)),
                  m_Undo(std::move(undo))
            {
            }

            [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return 1024U; }
            void Redo() override { m_Redo(); }
            void Undo() override { m_Undo(); }
            [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
            {
                const auto* command = dynamic_cast<const ContinuousUiDocumentUndoCommand*>(&newer);
                return command && !m_MergeKey.empty() && command->m_MergeKey == m_MergeKey;
            }

          private:
            std::string m_Name;
            std::string m_MergeKey;
            Keire::UndoOperation m_Redo;
            Keire::UndoOperation m_Undo;
        };
    } // namespace

    UiBuilderCanvasGesture HitTestUiBuilderCanvasGesture(const Keire::UiItemRect rectangle,
                                                         const Keire::UiPosition position, const float radius) noexcept
    {
        if (!std::isfinite(radius) || radius <= 0.0F || !std::isfinite(rectangle.Minimum.X) ||
            !std::isfinite(rectangle.Minimum.Y) || !std::isfinite(rectangle.Maximum.X) ||
            !std::isfinite(rectangle.Maximum.Y) || rectangle.Maximum.X <= rectangle.Minimum.X ||
            rectangle.Maximum.Y <= rectangle.Minimum.Y)
        {
            return UiBuilderCanvasGesture::None;
        }

        const float width = rectangle.Maximum.X - rectangle.Minimum.X;
        const float height = rectangle.Maximum.Y - rectangle.Minimum.Y;
        const float centerX = (rectangle.Minimum.X + rectangle.Maximum.X) * 0.5F;
        const float centerY = (rectangle.Minimum.Y + rectangle.Maximum.Y) * 0.5F;
        const float cornerRadius = std::min(radius, std::max(1.0F, std::min(width, height) * 0.5F - 2.0F));
        const float horizontalNormalRadius = std::min({radius, 5.0F, std::max(1.0F, width * 0.5F - 2.0F)});
        const float verticalNormalRadius = std::min({radius, 5.0F, std::max(1.0F, height * 0.5F - 2.0F)});
        const float tangentRadius = std::min(radius, 9.0F);
        const auto near =
            [&](const Keire::UiPosition center, const float horizontalRadius, const float verticalRadius) noexcept
        {
            return std::abs(position.X - center.X) <= horizontalRadius &&
                   std::abs(position.Y - center.Y) <= verticalRadius;
        };

        if (near(rectangle.Minimum, cornerRadius, cornerRadius))
            return UiBuilderCanvasGesture::ResizeTopLeft;
        if (near({rectangle.Maximum.X, rectangle.Minimum.Y}, cornerRadius, cornerRadius))
            return UiBuilderCanvasGesture::ResizeTopRight;
        if (near(rectangle.Maximum, cornerRadius, cornerRadius))
            return UiBuilderCanvasGesture::ResizeBottomRight;
        if (near({rectangle.Minimum.X, rectangle.Maximum.Y}, cornerRadius, cornerRadius))
            return UiBuilderCanvasGesture::ResizeBottomLeft;
        if (near({centerX, rectangle.Minimum.Y}, tangentRadius, verticalNormalRadius))
            return UiBuilderCanvasGesture::ResizeTop;
        if (near({rectangle.Maximum.X, centerY}, horizontalNormalRadius, tangentRadius))
            return UiBuilderCanvasGesture::ResizeRight;
        if (near({centerX, rectangle.Maximum.Y}, tangentRadius, verticalNormalRadius))
            return UiBuilderCanvasGesture::ResizeBottom;
        if (near({rectangle.Minimum.X, centerY}, horizontalNormalRadius, tangentRadius))
            return UiBuilderCanvasGesture::ResizeLeft;
        return rectangle.Contains(position) ? UiBuilderCanvasGesture::Move : UiBuilderCanvasGesture::None;
    }

    Keire::RuntimeUiRect ResolveUiBuilderCanvasGesture(const Keire::RuntimeUiRect initial,
                                                       const Keire::RuntimeUiRect parentBounds,
                                                       const Keire::Vector2 delta,
                                                       const UiBuilderCanvasGesture gesture) noexcept
    {
        auto result = initial;
        const float parentRight = parentBounds.X + std::max(0.0F, parentBounds.Width);
        const float parentBottom = parentBounds.Y + std::max(0.0F, parentBounds.Height);
        const float initialRight = initial.X + initial.Width;
        const float initialBottom = initial.Y + initial.Height;
        const float minimumWidth = std::min(8.0F, std::max(0.0F, parentBounds.Width));
        const float minimumHeight = std::min(8.0F, std::max(0.0F, parentBounds.Height));

        if (gesture == UiBuilderCanvasGesture::Move)
        {
            result.X =
                std::clamp(initial.X + delta.X, parentBounds.X, std::max(parentBounds.X, parentRight - initial.Width));
            result.Y = std::clamp(initial.Y + delta.Y, parentBounds.Y,
                                  std::max(parentBounds.Y, parentBottom - initial.Height));
            return result;
        }

        const bool resizeLeft = gesture == UiBuilderCanvasGesture::ResizeLeft ||
                                gesture == UiBuilderCanvasGesture::ResizeTopLeft ||
                                gesture == UiBuilderCanvasGesture::ResizeBottomLeft;
        const bool resizeRight = gesture == UiBuilderCanvasGesture::ResizeRight ||
                                 gesture == UiBuilderCanvasGesture::ResizeTopRight ||
                                 gesture == UiBuilderCanvasGesture::ResizeBottomRight;
        const bool resizeTop = gesture == UiBuilderCanvasGesture::ResizeTop ||
                               gesture == UiBuilderCanvasGesture::ResizeTopLeft ||
                               gesture == UiBuilderCanvasGesture::ResizeTopRight;
        const bool resizeBottom = gesture == UiBuilderCanvasGesture::ResizeBottom ||
                                  gesture == UiBuilderCanvasGesture::ResizeBottomLeft ||
                                  gesture == UiBuilderCanvasGesture::ResizeBottomRight;

        if (resizeLeft)
        {
            result.X =
                std::clamp(initial.X + delta.X, parentBounds.X, std::max(parentBounds.X, initialRight - minimumWidth));
            result.Width = initialRight - result.X;
        }
        else if (resizeRight)
        {
            result.Width =
                std::clamp(initial.Width + delta.X, minimumWidth, std::max(minimumWidth, parentRight - initial.X));
        }

        if (resizeTop)
        {
            result.Y = std::clamp(initial.Y + delta.Y, parentBounds.Y,
                                  std::max(parentBounds.Y, initialBottom - minimumHeight));
            result.Height = initialBottom - result.Y;
        }
        else if (resizeBottom)
        {
            result.Height =
                std::clamp(initial.Height + delta.Y, minimumHeight, std::max(minimumHeight, parentBottom - initial.Y));
        }
        return result;
    }

    Keire::RuntimeUiRect ResolveUiBuilderCanvasPlacement(const Keire::RuntimeUiRect parentBounds,
                                                         const Keire::UiSize desiredSize,
                                                         const Keire::UiPosition center) noexcept
    {
        const float parentX = std::isfinite(parentBounds.X) ? parentBounds.X : 0.0F;
        const float parentY = std::isfinite(parentBounds.Y) ? parentBounds.Y : 0.0F;
        const float parentWidth = std::isfinite(parentBounds.Width) ? std::max(0.0F, parentBounds.Width) : 0.0F;
        const float parentHeight = std::isfinite(parentBounds.Height) ? std::max(0.0F, parentBounds.Height) : 0.0F;
        const float inset = std::min(16.0F, std::min(parentWidth, parentHeight) * 0.125F);
        const float availableWidth = std::max(0.0F, parentWidth - inset * 2.0F);
        const float availableHeight = std::max(0.0F, parentHeight - inset * 2.0F);
        const float width =
            std::min(std::isfinite(desiredSize.Width) ? std::max(0.0F, desiredSize.Width) : 0.0F, availableWidth);
        const float height =
            std::min(std::isfinite(desiredSize.Height) ? std::max(0.0F, desiredSize.Height) : 0.0F, availableHeight);
        const float requestedX = std::isfinite(center.X) ? center.X : parentX + parentWidth * 0.5F;
        const float requestedY = std::isfinite(center.Y) ? center.Y : parentY + parentHeight * 0.5F;
        const float minimumX = parentX + inset;
        const float minimumY = parentY + inset;
        const float maximumX = std::max(minimumX, parentX + parentWidth - inset - width);
        const float maximumY = std::max(minimumY, parentY + parentHeight - inset - height);
        return {
            std::clamp(requestedX - width * 0.5F, minimumX, maximumX),
            std::clamp(requestedY - height * 0.5F, minimumY, maximumY),
            width,
            height,
        };
    }

    Keire::UiSize UiBuilderCanvasControlDefaultSize(const Keire::UiVisualElementType type) noexcept
    {
        switch (type)
        {
        case Keire::UiVisualElementType::Label:
            return {240.0F, 40.0F};
        case Keire::UiVisualElementType::Image:
            return {180.0F, 140.0F};
        case Keire::UiVisualElementType::Slider:
        case Keire::UiVisualElementType::ProgressBar:
            return {280.0F, 32.0F};
        case Keire::UiVisualElementType::ScrollView:
        case Keire::UiVisualElementType::ListView:
        case Keire::UiVisualElementType::TreeView:
            return {320.0F, 240.0F};
        case Keire::UiVisualElementType::VisualElement:
            return {320.0F, 180.0F};
        default:
            return {220.0F, 48.0F};
        }
    }

    void PersistUiBuilderCanvasGeometry(Keire::UiVisualElementDefinition& element,
                                        const Keire::RuntimeUiRect parentBounds, const Keire::RuntimeUiRect geometry)
    {
        const bool finite = std::isfinite(parentBounds.X) && std::isfinite(parentBounds.Y) &&
                            std::isfinite(geometry.X) && std::isfinite(geometry.Y) && std::isfinite(geometry.Width) &&
                            std::isfinite(geometry.Height);
        if (!finite || geometry.Width <= 0.0F || geometry.Height <= 0.0F)
            throw std::invalid_argument("UI Builder canvas geometry must be finite and have positive dimensions.");

        constexpr std::array conflictingProperties{"position",     "left",         "top",         "width",
                                                   "height",       "margin",       "margin-left", "margin-top",
                                                   "margin-right", "margin-bottom"};
        std::erase_if(element.InlineStyles, [&](const Keire::UiNamedValue& value)
                      { return std::ranges::find(conflictingProperties, value.Name) != conflictingProperties.end(); });
        SetInlineStyle(element.InlineStyles, "position", "absolute");
        SetInlineStyle(element.InlineStyles, "left", CanvasPixelValue(geometry.X - parentBounds.X));
        SetInlineStyle(element.InlineStyles, "top", CanvasPixelValue(geometry.Y - parentBounds.Y));
        SetInlineStyle(element.InlineStyles, "width", CanvasPixelValue(geometry.Width));
        SetInlineStyle(element.InlineStyles, "height", CanvasPixelValue(geometry.Height));
        SetInlineStyle(element.InlineStyles, "margin", "0px");
    }

    Keire::RuntimeUiRect TransformUiBuilderCanvasPreviewRect(const Keire::RuntimeUiRect rectangle,
                                                             const Keire::RuntimeUiRect initial,
                                                             const Keire::RuntimeUiRect draft) noexcept
    {
        const float scaleX = initial.Width > 0.001F ? draft.Width / initial.Width : 1.0F;
        const float scaleY = initial.Height > 0.001F ? draft.Height / initial.Height : 1.0F;
        return {
            draft.X + (rectangle.X - initial.X) * scaleX,
            draft.Y + (rectangle.Y - initial.Y) * scaleY,
            rectangle.Width * scaleX,
            rectangle.Height * scaleY,
        };
    }

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
        (void)runtime->SetStyleEvaluationContext({.Width = static_cast<float>(settings.Width),
                                                  .Height = static_cast<float>(settings.Height),
                                                  .Dpi = settings.Dpi,
                                                  .Pointer = Keire::UiStylePointerPrecision::Fine,
                                                  .Navigation = Keire::UiStyleNavigationMode::Pointer});
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
            result.SelectedStyleTrace = runtime->ResolvedStyleTrace(found->RuntimeId);
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

    bool UiBuilderDocument::Edit(const std::string_view name, Keire::UiVisualTreeDefinition candidate,
                                 std::string mergeKey)
    {
        if (!m_Asset)
            throw std::logic_error("Open a UI document before editing it.");
        Keire::UiVisualTreeAsset::Validate(candidate);
        if (Keire::UiVisualTreeAsset::Encode(candidate) == Keire::UiVisualTreeAsset::Encode(m_Definition))
            return false;
        auto before = m_Definition;
        m_Definition = std::move(candidate);
        NormalizeSelection();
        RecordApplied(name, std::move(before), std::move(mergeKey));
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
        AddDefaultAuthoringProperties(added);
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
        AddDefaultAuthoringProperties(added);
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Add custom UI element", std::move(candidate));
        Select(id);
        return id;
    }

    Keire::AssetId UiBuilderDocument::AddCanvasElement(const Keire::AssetId parent,
                                                       const Keire::UiVisualElementType type,
                                                       const Keire::RuntimeUiRect parentBounds,
                                                       const Keire::UiPosition center)
    {
        auto candidate = m_Definition;
        auto* destination = FindElement(candidate.Root, parent ? parent : candidate.Root.StableId);
        if (!destination)
            throw std::invalid_argument("The selected UI parent no longer exists.");
        Keire::UiVisualElementDefinition added;
        added.StableId = Keire::AssetId::Generate();
        added.Type = type;
        added.Name = UniqueElementName(candidate.Root, type);
        AddDefaultAuthoringProperties(added);
        PersistUiBuilderCanvasGeometry(
            added, parentBounds,
            ResolveUiBuilderCanvasPlacement(parentBounds, UiBuilderCanvasControlDefaultSize(type), center));
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Add UI element to canvas", std::move(candidate));
        Select(id);
        return id;
    }

    Keire::AssetId UiBuilderDocument::AddCanvasCustomElement(const Keire::AssetId parent, std::string customType,
                                                             const Keire::RuntimeUiRect parentBounds,
                                                             const Keire::UiPosition center)
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
        AddDefaultAuthoringProperties(added);
        PersistUiBuilderCanvasGeometry(
            added, parentBounds,
            ResolveUiBuilderCanvasPlacement(parentBounds, UiBuilderCanvasControlDefaultSize(added.Type), center));
        const auto id = added.StableId;
        destination->Children.push_back(std::move(added));
        (void)Edit("Add custom UI element to canvas", std::move(candidate));
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

    bool UiBuilderDocument::SetInlineStyleProperty(const std::span<const Keire::AssetId> elements,
                                                   const std::string_view name, const std::string_view value)
    {
        if (elements.empty() || name.empty() || value.empty())
            return false;
        auto candidate = m_Definition;
        bool found = false;
        for (const auto element : elements)
        {
            if (auto* definition = FindElement(candidate.Root, element))
            {
                SetInlineStyle(definition->InlineStyles, name, std::string(value));
                found = true;
            }
        }
        return found && Edit(elements.size() == 1 ? "Edit UI inline style" : "Edit shared UI inline style",
                             std::move(candidate));
    }

    bool UiBuilderDocument::RemoveInlineStyleProperty(const std::span<const Keire::AssetId> elements,
                                                      const std::string_view name)
    {
        if (elements.empty() || name.empty())
            return false;
        auto candidate = m_Definition;
        bool removed = false;
        for (const auto element : elements)
        {
            if (auto* definition = FindElement(candidate.Root, element))
            {
                removed = std::erase_if(definition->InlineStyles,
                                        [name](const auto& property) { return property.Name == name; }) > 0 ||
                          removed;
            }
        }
        return removed && Edit(elements.size() == 1 ? "Remove UI inline style" : "Remove shared UI inline style",
                               std::move(candidate));
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
            (void)Edit("Edit UI source", std::move(candidate), m_Asset.ToString() + ":source");
            diagnostic.clear();
            return true;
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
        auto before = m_Definition;
        const bool changed = Keire::UiVisualTreeAsset::Encode(before) != Keire::UiVisualTreeAsset::Encode(definition);
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        NormalizeSelection();
        if (!Find(m_Selection))
            Select(m_Definition.Root.StableId);
        if (changed)
            RecordApplied("Revert UI document", std::move(before));
        RefreshDirtyState();
        if (changed)
            AdvanceGeneration();
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

    void UiBuilderDocument::RecordApplied(const std::string_view name, Keire::UiVisualTreeDefinition before,
                                          std::string mergeKey)
    {
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<Keire::UiVisualTreeDefinition>>();
        const auto asset = m_Asset;
        m_Undo->RecordApplied(std::make_unique<ContinuousUiDocumentUndoCommand>(
            std::string(name), std::move(mergeKey),
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
            }));
    }
} // namespace KeireEditor
