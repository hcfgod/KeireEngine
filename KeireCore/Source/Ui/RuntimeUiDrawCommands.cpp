#include "KeireInternal/Ui/RuntimeUiDrawCommandsInternal.h"

#include "KeireInternal/Ui/RuntimeUiStyleInternal.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Keire::Detail
{
    void AppendRuntimeUiDrawCommands(std::vector<RuntimeUiDrawCommand>& output, const RuntimeUiElementState& state,
                                     const RuntimeUiElementId element, const float scale)
    {
        const auto emit = [&output, &state, scale](RuntimeUiDrawCommand command)
        {
            command.BorderWidths = state.Style.BorderWidths;
            command.BorderWidths.Left *= scale;
            command.BorderWidths.Top *= scale;
            command.BorderWidths.Right *= scale;
            command.BorderWidths.Bottom *= scale;
            command.BorderColors = state.Style.BorderColors;
            command.CornerRadii = state.Style.CornerRadii;
            command.CornerRadii.TopLeft *= scale;
            command.CornerRadii.TopRight *= scale;
            command.CornerRadii.BottomRight *= scale;
            command.CornerRadii.BottomLeft *= scale;
            const bool text = command.Type == RuntimeUiDrawType::Text;
            command.Shadows.fill({});
            command.ShadowCount = text ? state.Style.TextShadowCount : state.Style.BoxShadowCount;
            for (std::size_t shadow = 0; shadow < command.ShadowCount; ++shadow)
            {
                command.Shadows[shadow] = text ? state.Style.TextShadows[shadow] : state.Style.BoxShadows[shadow];
                command.Shadows[shadow].Offset = {command.Shadows[shadow].Offset.X * scale,
                                                  command.Shadows[shadow].Offset.Y * scale};
                command.Shadows[shadow].BlurRadius *= scale;
                command.Shadows[shadow].SpreadRadius *= scale;
                command.Shadows[shadow].ColorValue =
                    ApplyRuntimeUiOpacity(command.Shadows[shadow].ColorValue, state.Style.Opacity);
            }
            command.AlphaMask = state.Style.AlphaMask;
            command.Translation = {state.Style.Translation.X * scale, state.Style.Translation.Y * scale};
            command.TransformScale = state.Style.TransformScale;
            command.RotationDegrees = state.Style.RotationDegrees;
            command.TransformOrigin = state.Style.TransformOrigin;
            output.push_back(std::move(command));
        };

        auto background = state.Style.Background;
        auto backgroundGradient = state.Style.BackgroundGradient;
        if (!state.Enabled && state.Style.DisabledBackground.Alpha > 0.0F)
        {
            background = state.Style.DisabledBackground;
            backgroundGradient = {};
        }
        else if (state.Pressed && state.Style.PressedBackground.Alpha > 0.0F)
        {
            background = state.Style.PressedBackground;
            backgroundGradient = {};
        }
        else if ((state.Hovered || state.Focused) && state.Style.HoverBackground.Alpha > 0.0F)
        {
            background = state.Style.HoverBackground;
            backgroundGradient = {};
        }
        if (background.Alpha > 0.0F || backgroundGradient.Kind != RuntimeUiGradientKind::None)
        {
            emit({.Type = RuntimeUiDrawType::Quad,
                  .Element = element,
                  .Rect = state.Rect,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(background, state.Style.Opacity),
                  .BorderColor = ApplyRuntimeUiOpacity(state.Style.Border, state.Style.Opacity),
                  .BackgroundGradient = ApplyRuntimeUiOpacity(backgroundGradient, state.Style.Opacity),
                  .CornerRadius = state.Style.CornerRadius * scale,
                  .BorderWidth = state.Style.BorderWidth * scale});
        }
        if (state.Style.BackgroundImage)
        {
            emit({.Type = RuntimeUiDrawType::Image,
                  .Element = element,
                  .Rect = state.Rect,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.BackgroundTint, state.Style.Opacity),
                  .Asset = state.Style.BackgroundImage,
                  .CornerRadius = state.Style.CornerRadius * scale,
                  .ImageSlice = {state.Style.BackgroundSlice.Left * scale, state.Style.BackgroundSlice.Top * scale,
                                 state.Style.BackgroundSlice.Right * scale, state.Style.BackgroundSlice.Bottom * scale},
                  .ImageFit = state.Style.BackgroundFit,
                  .ImageRepeat = state.Style.BackgroundRepeat,
                  .ImagePosition = state.Style.BackgroundPosition});
        }

        if (state.Type == RuntimeUiElementType::Slider)
        {
            const float range = state.Control.Maximum - state.Control.Minimum;
            const float normalized =
                range > 0.0F ? std::clamp((state.Control.Value - state.Control.Minimum) / range, 0.0F, 1.0F) : 0.0F;
            const float position = state.Control.Reversed ? 1.0F - normalized : normalized;
            const float inset = std::min(6.0F * scale, std::min(state.Rect.Width, state.Rect.Height) * 0.25F);
            RuntimeUiRect fill = state.Rect;
            RuntimeUiRect handle = state.Rect;
            if (state.Control.Vertical)
            {
                const float y =
                    state.Rect.Y + (state.Control.Reversed ? normalized : 1.0F - normalized) * state.Rect.Height;
                fill.X += inset;
                fill.Width = std::max(0.0F, fill.Width - inset * 2.0F);
                fill.Y = state.Control.Reversed ? state.Rect.Y : y;
                fill.Height = state.Control.Reversed ? std::max(0.0F, y - state.Rect.Y)
                                                     : std::max(0.0F, state.Rect.Y + state.Rect.Height - y);
                handle = {state.Rect.X + inset * 0.5F, y - inset, std::max(0.0F, state.Rect.Width - inset),
                          inset * 2.0F};
            }
            else
            {
                const float x = state.Rect.X + position * state.Rect.Width;
                fill.Y += inset;
                fill.Height = std::max(0.0F, fill.Height - inset * 2.0F);
                fill.X = state.Control.Reversed ? x : state.Rect.X;
                fill.Width = state.Control.Reversed ? std::max(0.0F, state.Rect.X + state.Rect.Width - x)
                                                    : std::max(0.0F, x - state.Rect.X);
                handle = {x - inset, state.Rect.Y + inset * 0.5F, inset * 2.0F,
                          std::max(0.0F, state.Rect.Height - inset)};
            }
            emit({.Type = RuntimeUiDrawType::Quad,
                  .Element = element,
                  .Rect = fill,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                  .CornerRadius = state.Style.CornerRadius * scale});
            emit({.Type = RuntimeUiDrawType::Quad,
                  .Element = element,
                  .Rect = handle,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(Color{0.94F, 0.98F, 1.0F, 1.0F}, state.Style.Opacity),
                  .CornerRadius = state.Style.CornerRadius * scale});
        }
        else if (state.Type == RuntimeUiElementType::Toggle && state.Control.Checked)
        {
            const float inset = std::min(7.0F * scale, std::min(state.Rect.Width, state.Rect.Height) * 0.3F);
            const float indicatorSize = std::min(22.0F * scale, std::max(0.0F, state.Rect.Height - inset * 2.0F));
            emit({.Type = RuntimeUiDrawType::Quad,
                  .Element = element,
                  .Rect = {state.Rect.X + state.Rect.Width - inset - indicatorSize,
                           state.Rect.Y + (state.Rect.Height - indicatorSize) * 0.5F, indicatorSize, indicatorSize},
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                  .CornerRadius = std::min(state.Style.CornerRadius * scale, indicatorSize * 0.5F)});
        }
        else if (state.Type == RuntimeUiElementType::ScrollView)
        {
            const float contentWidth = state.Control.ContentSize.X * scale;
            const float contentHeight = state.Control.ContentSize.Y * scale;
            const float widthOverflow = std::max(0.0F, contentWidth - state.Rect.Width);
            const float heightOverflow = std::max(0.0F, contentHeight - state.Rect.Height);
            if (heightOverflow > 0.0F)
            {
                const float thumbHeight =
                    std::max(12.0F * scale, state.Rect.Height * state.Rect.Height / contentHeight);
                const float track = std::max(0.0F, state.Rect.Height - thumbHeight);
                const float fraction = std::clamp(state.Style.ContentOffset.Y * scale / heightOverflow, 0.0F, 1.0F);
                emit({.Type = RuntimeUiDrawType::Quad,
                      .Element = element,
                      .Rect = {state.Rect.X + state.Rect.Width - 5.0F * scale, state.Rect.Y + track * fraction,
                               4.0F * scale, thumbHeight},
                      .ClipRect = state.ClipRect,
                      .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                      .CornerRadius = 2.0F * scale});
            }
            if (widthOverflow > 0.0F)
            {
                const float thumbWidth = std::max(12.0F * scale, state.Rect.Width * state.Rect.Width / contentWidth);
                const float track = std::max(0.0F, state.Rect.Width - thumbWidth);
                const float fraction = std::clamp(state.Style.ContentOffset.X * scale / widthOverflow, 0.0F, 1.0F);
                emit({.Type = RuntimeUiDrawType::Quad,
                      .Element = element,
                      .Rect = {state.Rect.X + track * fraction, state.Rect.Y + state.Rect.Height - 5.0F * scale,
                               thumbWidth, 4.0F * scale},
                      .ClipRect = state.ClipRect,
                      .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                      .CornerRadius = 2.0F * scale});
            }
        }

        if (state.Content.Image)
        {
            emit({.Type = RuntimeUiDrawType::Image,
                  .Element = element,
                  .Rect = state.Rect,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                  .Asset = state.Content.Image,
                  .CornerRadius = state.Style.CornerRadius * scale,
                  .ImageFit = RuntimeUiBackgroundFit::Contain});
        }
        else if (state.Content.RenderTexture)
        {
            emit({.Type = RuntimeUiDrawType::Image,
                  .Element = element,
                  .Rect = state.Rect,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                  .RenderTexture = state.Content.RenderTexture,
                  .CornerRadius = state.Style.CornerRadius * scale,
                  .ImageFit = RuntimeUiBackgroundFit::Contain});
        }
        if (!state.Content.Text.empty())
        {
            emit({.Type = RuntimeUiDrawType::Text,
                  .Element = element,
                  .Rect = state.Rect,
                  .ClipRect = state.ClipRect,
                  .ColorValue = ApplyRuntimeUiOpacity(state.Style.Foreground, state.Style.Opacity),
                  .Asset = state.Content.Font ? state.Content.Font : state.Style.FontFamily,
                  .Text = state.Content.Text,
                  .FontSize = state.Style.FontSize * scale,
                  .LineHeight = state.Style.LineHeight * scale,
                  .LetterSpacing = state.Style.LetterSpacing * scale,
                  .WordSpacing = state.Style.WordSpacing * scale,
                  .TextWrap = state.Style.TextWrap,
                  .TextOverflow = state.Style.TextOverflow,
                  .TextDirection = state.Style.TextDirection,
                  .Language = state.Style.Language,
                  .MaximumLines = state.Style.MaximumLines,
                  .FontWeight = state.Style.FontWeight,
                  .FontSlant = state.Style.FontSlant,
                  .HorizontalAlignment = state.Style.HorizontalAlignment,
                  .VerticalAlignment = state.Style.VerticalAlignment});
        }
    }
} // namespace Keire::Detail
