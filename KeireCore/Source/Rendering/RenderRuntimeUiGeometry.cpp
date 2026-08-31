#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"
#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"
#include "KeireInternal/Ui/RuntimeUiTextInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::size_t MaximumRuntimeUiVertices = 1'000'000;
        constexpr std::size_t MaximumRuntimeUiTextBytes = 4096;
        constexpr std::size_t StyledGridSegments = 12;

        [[nodiscard]] bool HasRuntimeUiTransform(const RuntimeUiDrawCommand& command) noexcept
        {
            return command.Translation != Vector2{} || command.TransformScale != Vector2{1.0F, 1.0F} ||
                   command.RotationDegrees != 0.0F;
        }

        [[nodiscard]] RuntimeUiRect LocalRuntimeUiClip(const RuntimeUiDrawCommand& command) noexcept
        {
            return HasRuntimeUiTransform(command) ? command.Rect : command.Rect.Intersect(command.ClipRect);
        }

        [[nodiscard]] Color InterpolateColor(const Color first, const Color second, const float amount) noexcept
        {
            return {first.Red + (second.Red - first.Red) * amount, first.Green + (second.Green - first.Green) * amount,
                    first.Blue + (second.Blue - first.Blue) * amount,
                    first.Alpha + (second.Alpha - first.Alpha) * amount};
        }

        [[nodiscard]] Color GradientColorAt(const RuntimeUiGradient& gradient, const float coordinate) noexcept
        {
            if (gradient.StopCount == 0)
                return {};
            const float value = std::clamp(coordinate, 0.0F, 1.0F);
            if (value <= gradient.Stops[0].Offset)
                return gradient.Stops[0].ColorValue;
            for (std::size_t index = 1; index < gradient.StopCount; ++index)
            {
                const auto& previous = gradient.Stops[index - 1U];
                const auto& current = gradient.Stops[index];
                if (value > current.Offset)
                    continue;
                const float distance = current.Offset - previous.Offset;
                const float amount = distance > 0.000001F ? (value - previous.Offset) / distance : 1.0F;
                return InterpolateColor(previous.ColorValue, current.ColorValue, std::clamp(amount, 0.0F, 1.0F));
            }
            return gradient.Stops[gradient.StopCount - 1U].ColorValue;
        }

        void AppendRuntimeUiRectangle(std::vector<RuntimeUiVertex>& output, const RuntimeUiRect rectangle,
                                      const Color color, const Vector2 uvMinimum = {}, const Vector2 uvMaximum = {})
        {
            if (rectangle.Empty() || color.Alpha <= 0.0F || output.size() > MaximumRuntimeUiVertices - 6U)
                return;
            const RuntimeUiVertex topLeft{{rectangle.X, rectangle.Y, 0.0F}, color, uvMinimum};
            const RuntimeUiVertex topRight{
                {rectangle.X + rectangle.Width, rectangle.Y, 0.0F}, color, {uvMaximum.X, uvMinimum.Y}};
            const RuntimeUiVertex bottomLeft{
                {rectangle.X, rectangle.Y + rectangle.Height, 0.0F}, color, {uvMinimum.X, uvMaximum.Y}};
            const RuntimeUiVertex bottomRight{
                {rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height, 0.0F}, color, uvMaximum};
            output.insert(output.end(), {topLeft, topRight, bottomRight, topLeft, bottomRight, bottomLeft});
        }

        [[nodiscard]] std::vector<float> StyledAxis(const float minimum, const float maximum, const float shapeMinimum,
                                                    const float shapeMaximum, const float radius, const bool detailed)
        {
            std::vector<float> result{minimum, maximum};
            if (detailed)
            {
                for (std::size_t index = 1; index < StyledGridSegments; ++index)
                {
                    const float amount = static_cast<float>(index) / static_cast<float>(StyledGridSegments);
                    const float value = shapeMinimum + (shapeMaximum - shapeMinimum) * amount;
                    if (value > minimum && value < maximum)
                        result.push_back(value);
                }
            }
            if (radius > 0.0F)
            {
                for (const float value :
                     {shapeMinimum + radius - 1.0F, shapeMinimum + radius, shapeMinimum + radius + 1.0F,
                      shapeMaximum - radius - 1.0F, shapeMaximum - radius, shapeMaximum - radius + 1.0F})
                {
                    if (value > minimum && value < maximum)
                        result.push_back(value);
                }
            }
            std::ranges::sort(result);
            const auto duplicate = std::ranges::unique(result, [](const float first, const float second)
                                                       { return std::abs(first - second) <= 0.0001F; });
            result.erase(duplicate.begin(), duplicate.end());
            return result;
        }

        [[nodiscard]] Vector2 NormalizedPosition(const RuntimeUiRect rectangle, const Vector2 position) noexcept
        {
            return {(position.X - rectangle.X) / rectangle.Width, (position.Y - rectangle.Y) / rectangle.Height};
        }

        [[nodiscard]] float EffectiveRadius(const RuntimeUiDrawCommand& command) noexcept
        {
            return std::max({command.CornerRadius, command.CornerRadii.TopLeft, command.CornerRadii.TopRight,
                             command.CornerRadii.BottomRight, command.CornerRadii.BottomLeft});
        }

        [[nodiscard]] float RoundedCoverage(const RuntimeUiDrawCommand& command, const Vector2 position) noexcept
        {
            const auto& rectangle = command.Rect;
            if (!rectangle.Contains(position.X, position.Y))
                return 0.0F;
            const float maximum = std::min(rectangle.Width, rectangle.Height) * 0.5F;
            const auto corner = [&](const float centerX, const float centerY, const float radius)
            {
                const float bounded = std::clamp(radius, 0.0F, maximum);
                if (bounded <= 0.0F)
                    return 1.0F;
                const float x = position.X - centerX;
                const float y = position.Y - centerY;
                return std::clamp(bounded + 0.5F - std::sqrt(x * x + y * y), 0.0F, 1.0F);
            };
            const float topLeft =
                command.CornerRadii.TopLeft > 0.0F ? command.CornerRadii.TopLeft : command.CornerRadius;
            const float topRight =
                command.CornerRadii.TopRight > 0.0F ? command.CornerRadii.TopRight : command.CornerRadius;
            const float bottomRight =
                command.CornerRadii.BottomRight > 0.0F ? command.CornerRadii.BottomRight : command.CornerRadius;
            const float bottomLeft =
                command.CornerRadii.BottomLeft > 0.0F ? command.CornerRadii.BottomLeft : command.CornerRadius;
            if (position.X < rectangle.X + topLeft && position.Y < rectangle.Y + topLeft)
                return corner(rectangle.X + topLeft, rectangle.Y + topLeft, topLeft);
            if (position.X > rectangle.X + rectangle.Width - topRight && position.Y < rectangle.Y + topRight)
                return corner(rectangle.X + rectangle.Width - topRight, rectangle.Y + topRight, topRight);
            if (position.X > rectangle.X + rectangle.Width - bottomRight &&
                position.Y > rectangle.Y + rectangle.Height - bottomRight)
            {
                return corner(rectangle.X + rectangle.Width - bottomRight, rectangle.Y + rectangle.Height - bottomRight,
                              bottomRight);
            }
            if (position.X < rectangle.X + bottomLeft && position.Y > rectangle.Y + rectangle.Height - bottomLeft)
                return corner(rectangle.X + bottomLeft, rectangle.Y + rectangle.Height - bottomLeft, bottomLeft);
            return 1.0F;
        }

        [[nodiscard]] float BorderCoverage(const RuntimeUiDrawCommand& command, const Vector2 position) noexcept
        {
            const float outer = RoundedCoverage(command, position);
            const float thickness = std::min(command.BorderWidth, std::min(command.Rect.Width, command.Rect.Height));
            const RuntimeUiRect inner{command.Rect.X + thickness, command.Rect.Y + thickness,
                                      std::max(0.0F, command.Rect.Width - thickness * 2.0F),
                                      std::max(0.0F, command.Rect.Height - thickness * 2.0F)};
            if (inner.Empty())
                return outer;
            const float innerCoverage =
                RuntimeUiRoundedCoverage(inner, std::max(0.0F, command.CornerRadius - thickness), position);
            return outer * (1.0F - innerCoverage);
        }

        void AppendStyledRectangle(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command,
                                   const bool image, const bool border)
        {
            if (command.Rect.Empty())
                return;
            const auto clipped = LocalRuntimeUiClip(command);
            if (clipped.Empty())
                return;
            const bool gradient = !image && !border && command.BackgroundGradient.Kind != RuntimeUiGradientKind::None;
            const float radius =
                std::clamp(EffectiveRadius(command), 0.0F, std::min(command.Rect.Width, command.Rect.Height) * 0.5F);
            const auto horizontal = StyledAxis(clipped.X, clipped.X + clipped.Width, command.Rect.X,
                                               command.Rect.X + command.Rect.Width, radius, gradient);
            const auto vertical = StyledAxis(clipped.Y, clipped.Y + clipped.Height, command.Rect.Y,
                                             command.Rect.Y + command.Rect.Height, radius, gradient);
            for (std::size_t y = 0; y + 1U < vertical.size(); ++y)
            {
                for (std::size_t x = 0; x + 1U < horizontal.size(); ++x)
                {
                    if (output.size() > MaximumRuntimeUiVertices - 6U)
                        return;
                    const std::array positions{
                        Vector2{horizontal[x], vertical[y]}, Vector2{horizontal[x + 1U], vertical[y]},
                        Vector2{horizontal[x + 1U], vertical[y + 1U]}, Vector2{horizontal[x], vertical[y + 1U]}};
                    std::array<RuntimeUiVertex, 4> vertices;
                    for (std::size_t index = 0; index < vertices.size(); ++index)
                    {
                        const auto normalized = NormalizedPosition(command.Rect, positions[index]);
                        Color color = gradient ? EvaluateRuntimeUiGradient(command.BackgroundGradient, normalized)
                                      : border ? command.BorderColor
                                               : command.ColorValue;
                        const float coverage = border ? BorderCoverage(command, positions[index])
                                                      : RoundedCoverage(command, positions[index]);
                        color.Alpha *= coverage;
                        vertices[index] = {
                            {positions[index].X, positions[index].Y, 0.0F}, color, image ? normalized : Vector2{}};
                    }
                    output.insert(output.end(),
                                  {vertices[0], vertices[1], vertices[2], vertices[0], vertices[2], vertices[3]});
                }
            }
        }

        void AppendRuntimeUiImage(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            const bool sliced = command.ImageSlice.Left > 0.0F || command.ImageSlice.Top > 0.0F ||
                                command.ImageSlice.Right > 0.0F || command.ImageSlice.Bottom > 0.0F;
            if (sliced)
            {
                const float left = std::clamp(command.ImageSlice.Left, 0.0F, command.Rect.Width * 0.5F);
                const float top = std::clamp(command.ImageSlice.Top, 0.0F, command.Rect.Height * 0.5F);
                const float right = std::clamp(command.ImageSlice.Right, 0.0F, command.Rect.Width * 0.5F);
                const float bottom = std::clamp(command.ImageSlice.Bottom, 0.0F, command.Rect.Height * 0.5F);
                const std::array x{command.Rect.X, command.Rect.X + left, command.Rect.X + command.Rect.Width - right,
                                   command.Rect.X + command.Rect.Width};
                const std::array y{command.Rect.Y, command.Rect.Y + top, command.Rect.Y + command.Rect.Height - bottom,
                                   command.Rect.Y + command.Rect.Height};
                const std::array u{0.0F, left / command.Rect.Width, 1.0F - right / command.Rect.Width, 1.0F};
                const std::array v{0.0F, top / command.Rect.Height, 1.0F - bottom / command.Rect.Height, 1.0F};
                for (std::size_t row = 0; row < 3; ++row)
                    for (std::size_t column = 0; column < 3; ++column)
                    {
                        const RuntimeUiRect tile{x[column], y[row], x[column + 1] - x[column], y[row + 1] - y[row]};
                        const auto clipped = tile.Intersect(LocalRuntimeUiClip(command));
                        if (clipped.Empty())
                            continue;
                        const Vector2 uvMinimum{u[column] +
                                                    (u[column + 1] - u[column]) * ((clipped.X - tile.X) / tile.Width),
                                                v[row] + (v[row + 1] - v[row]) * ((clipped.Y - tile.Y) / tile.Height)};
                        const Vector2 uvMaximum{u[column] + (u[column + 1] - u[column]) *
                                                                ((clipped.X + clipped.Width - tile.X) / tile.Width),
                                                v[row] + (v[row + 1] - v[row]) *
                                                             ((clipped.Y + clipped.Height - tile.Y) / tile.Height)};
                        AppendRuntimeUiRectangle(output, clipped, command.ColorValue, uvMinimum, uvMaximum);
                    }
                return;
            }
            if (EffectiveRadius(command) > 0.0F)
            {
                AppendStyledRectangle(output, command, true, false);
                return;
            }
            const auto clipped = LocalRuntimeUiClip(command);
            if (clipped.Empty())
                return;
            const Vector2 uvMinimum{(clipped.X - command.Rect.X) / command.Rect.Width,
                                    (clipped.Y - command.Rect.Y) / command.Rect.Height};
            const Vector2 uvMaximum{(clipped.X + clipped.Width - command.Rect.X) / command.Rect.Width,
                                    (clipped.Y + clipped.Height - command.Rect.Y) / command.Rect.Height};
            AppendRuntimeUiRectangle(output, clipped, command.ColorValue, uvMinimum, uvMaximum);
        }

        void AppendRuntimeUiBorder(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            const bool perEdge = command.BorderWidths.Left > 0.0F || command.BorderWidths.Top > 0.0F ||
                                 command.BorderWidths.Right > 0.0F || command.BorderWidths.Bottom > 0.0F;
            if (perEdge)
            {
                const auto append = [&](const RuntimeUiRect rectangle, const Color color)
                { AppendRuntimeUiRectangle(output, rectangle.Intersect(LocalRuntimeUiClip(command)), color); };
                append({command.Rect.X, command.Rect.Y, command.Rect.Width, command.BorderWidths.Top},
                       command.BorderColors.Top);
                append({command.Rect.X, command.Rect.Y + command.Rect.Height - command.BorderWidths.Bottom,
                        command.Rect.Width, command.BorderWidths.Bottom},
                       command.BorderColors.Bottom);
                append({command.Rect.X, command.Rect.Y + command.BorderWidths.Top, command.BorderWidths.Left,
                        std::max(0.0F, command.Rect.Height - command.BorderWidths.Top - command.BorderWidths.Bottom)},
                       command.BorderColors.Left);
                append({command.Rect.X + command.Rect.Width - command.BorderWidths.Right,
                        command.Rect.Y + command.BorderWidths.Top, command.BorderWidths.Right,
                        std::max(0.0F, command.Rect.Height - command.BorderWidths.Top - command.BorderWidths.Bottom)},
                       command.BorderColors.Right);
                return;
            }
            const float thickness = std::min(command.BorderWidth, std::min(command.Rect.Width, command.Rect.Height));
            if (thickness <= 0.0F || command.BorderColor.Alpha <= 0.0F)
                return;
            if (command.CornerRadius > 0.0F)
            {
                AppendStyledRectangle(output, command, false, true);
                return;
            }
            const auto append = [&](const RuntimeUiRect rectangle)
            {
                AppendRuntimeUiRectangle(output, rectangle.Intersect(LocalRuntimeUiClip(command)), command.BorderColor);
            };
            append({command.Rect.X, command.Rect.Y, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + command.Rect.Height - thickness, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
            append({command.Rect.X + command.Rect.Width - thickness, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
        }

        void AppendRuntimeUiBoxShadows(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            for (std::size_t index = 0; index < command.ShadowCount; ++index)
            {
                const auto& shadow = command.Shadows[index];
                if (shadow.Inset || shadow.ColorValue.Alpha <= 0.0F)
                    continue;
                const auto layers = static_cast<std::size_t>(
                    std::clamp(static_cast<float>(std::ceil(shadow.BlurRadius / 4.0F)), 1.0F, 8.0F));
                for (std::size_t layer = layers; layer > 0; --layer)
                {
                    const float blur = shadow.BlurRadius * static_cast<float>(layer) / static_cast<float>(layers);
                    const float expansion = shadow.SpreadRadius + blur;
                    RuntimeUiDrawCommand candidate = command;
                    candidate.Rect = {command.Rect.X + shadow.Offset.X - expansion,
                                      command.Rect.Y + shadow.Offset.Y - expansion,
                                      command.Rect.Width + expansion * 2.0F, command.Rect.Height + expansion * 2.0F};
                    candidate.ColorValue = shadow.ColorValue;
                    candidate.ColorValue.Alpha /= static_cast<float>(layers);
                    candidate.BackgroundGradient = {};
                    candidate.BorderWidth = 0.0F;
                    candidate.BorderWidths = {};
                    candidate.ShadowCount = 0;
                    candidate.CornerRadius += expansion;
                    AppendStyledRectangle(output, candidate, false, false);
                }
            }
        }

        void TransformRuntimeUiVertices(std::span<RuntimeUiVertex> vertices, const RuntimeUiDrawCommand& command)
        {
            if (vertices.empty() || (command.Translation == Vector2{} &&
                                     command.TransformScale == Vector2{1.0F, 1.0F} && command.RotationDegrees == 0.0F))
                return;
            const Vector2 origin{command.Rect.X + command.Rect.Width * command.TransformOrigin.X,
                                 command.Rect.Y + command.Rect.Height * command.TransformOrigin.Y};
            const float radians = command.RotationDegrees * std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            for (auto& vertex : vertices)
            {
                const float x = (vertex.Position.X - origin.X) * command.TransformScale.X;
                const float y = (vertex.Position.Y - origin.Y) * command.TransformScale.Y;
                vertex.Position.X = origin.X + x * cosine - y * sine + command.Translation.X;
                vertex.Position.Y = origin.Y + x * sine + y * cosine + command.Translation.Y;
            }
        }

        void AppendRuntimeUiText(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command,
                                 const AssetId preparedBinding = {}, const std::size_t preparedFirst = 0U,
                                 const std::size_t preparedCount = (std::numeric_limits<std::size_t>::max)())
        {
            if (command.Text.empty() || command.FontSize <= 0.0F || command.ColorValue.Alpha <= 0.0F ||
                output.size() >= MaximumRuntimeUiVertices)
                return;
            if (command.PreparedFontBinding && !command.PreparedTextGlyphs.empty() &&
                !command.PreparedTextLines.empty())
            {
                float originY = command.Rect.Y;
                if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                    originY += (command.Rect.Height - command.PreparedTextHeight) * 0.5F;
                else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                    originY += command.Rect.Height - command.PreparedTextHeight;
                for (const auto& line : command.PreparedTextLines)
                {
                    float lineOriginX = command.Rect.X;
                    if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                        lineOriginX += (command.Rect.Width - line.Width) * 0.5F;
                    else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                        lineOriginX += command.Rect.Width - line.Width;
                    const auto end = std::min(command.PreparedTextGlyphs.size(), line.FirstGlyph + line.GlyphCount);
                    for (std::size_t index = line.FirstGlyph; index < end; ++index)
                    {
                        if (output.size() > MaximumRuntimeUiVertices - 6U)
                            return;
                        const auto& glyph = command.PreparedTextGlyphs[index];
                        if (index < preparedFirst || index - preparedFirst >= preparedCount ||
                            (preparedBinding && glyph.FontBinding != preparedBinding))
                            continue;
                        const RuntimeUiRect rectangle{lineOriginX + glyph.Position.X + glyph.Offset.X,
                                                      originY + glyph.Position.Y + glyph.Offset.Y, glyph.Size.X,
                                                      glyph.Size.Y};
                        if (rectangle.Empty())
                            continue;
                        const auto clipped = rectangle.Intersect(LocalRuntimeUiClip(command));
                        if (clipped.Empty())
                            continue;
                        const Vector2 uvMinimum{glyph.UvMinimum.X + (glyph.UvMaximum.X - glyph.UvMinimum.X) *
                                                                        ((clipped.X - rectangle.X) / rectangle.Width),
                                                glyph.UvMinimum.Y + (glyph.UvMaximum.Y - glyph.UvMinimum.Y) *
                                                                        ((clipped.Y - rectangle.Y) / rectangle.Height)};
                        const Vector2 uvMaximum{
                            glyph.UvMinimum.X + (glyph.UvMaximum.X - glyph.UvMinimum.X) *
                                                    ((clipped.X + clipped.Width - rectangle.X) / rectangle.Width),
                            glyph.UvMinimum.Y + (glyph.UvMaximum.Y - glyph.UvMinimum.Y) *
                                                    ((clipped.Y + clipped.Height - rectangle.Y) / rectangle.Height)};
                        AppendRuntimeUiRectangle(output, clipped, command.ColorValue, uvMinimum, uvMaximum);
                    }
                }
                return;
            }
            const std::string_view text(command.Text.data(), std::min(command.Text.size(), MaximumRuntimeUiTextBytes));
            const float scale = command.FontSize / 12.0F;
            static thread_local Keire::Detail::RuntimeUiTextLayoutCache textLayouts(512U, 131'072U);
            const auto layout = textLayouts.Resolve({.Text = text,
                                                     .Language = command.Language,
                                                     .Direction = command.TextDirection,
                                                     .Wrap = command.TextWrap,
                                                     .Overflow = command.TextOverflow,
                                                     .FontSize = command.FontSize,
                                                     .AvailableWidth = command.Rect.Width,
                                                     .AuthoredLineHeight = command.LineHeight,
                                                     .LetterSpacing = command.LetterSpacing,
                                                     .WordSpacing = command.WordSpacing,
                                                     .MaximumLines = command.MaximumLines,
                                                     .Weight = command.FontWeight,
                                                     .Slant = command.FontSlant});
            float originX = command.Rect.X;
            float originY = command.Rect.Y;
            if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                originY += (command.Rect.Height - layout->Height) * 0.5F;
            else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                originY += command.Rect.Height - layout->Height;

            for (const auto& line : layout->Lines)
            {
                float lineOriginX = originX;
                if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                    lineOriginX += (command.Rect.Width - line.Width) * 0.5F;
                else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                    lineOriginX += command.Rect.Width - line.Width;
                const auto end = std::min(layout->Glyphs.size(), line.FirstGlyph + line.GlyphCount);
                for (std::size_t index = line.FirstGlyph; index < end; ++index)
                {
                    if (output.size() > MaximumRuntimeUiVertices - 6U)
                        return;
                    const auto& placement = layout->Glyphs[index];
                    if (placement.Codepoint == U'\n')
                        continue;
                    const auto character = placement.Codepoint <= RuntimeUiLastFallbackGlyph
                                               ? static_cast<std::uint8_t>(placement.Codepoint)
                                               : static_cast<std::uint8_t>('?');
                    const auto& glyph = RuntimeUiFallbackGlyph(character);
                    const RuntimeUiRect rectangle{lineOriginX + placement.X + placement.OffsetX +
                                                      glyph.Offset.X * scale,
                                                  originY + placement.Y + placement.OffsetY + glyph.Offset.Y * scale,
                                                  glyph.Width * scale, glyph.Height * scale};
                    const auto clipped = rectangle.Intersect(LocalRuntimeUiClip(command));
                    if (!clipped.Empty())
                    {
                        const Vector2 uvMinimum{glyph.UvMinimum.X + (glyph.UvMaximum.X - glyph.UvMinimum.X) *
                                                                        ((clipped.X - rectangle.X) / rectangle.Width),
                                                glyph.UvMinimum.Y + (glyph.UvMaximum.Y - glyph.UvMinimum.Y) *
                                                                        ((clipped.Y - rectangle.Y) / rectangle.Height)};
                        const Vector2 uvMaximum{
                            glyph.UvMinimum.X + (glyph.UvMaximum.X - glyph.UvMinimum.X) *
                                                    ((clipped.X + clipped.Width - rectangle.X) / rectangle.Width),
                            glyph.UvMinimum.Y + (glyph.UvMaximum.Y - glyph.UvMinimum.Y) *
                                                    ((clipped.Y + clipped.Height - rectangle.Y) / rectangle.Height)};
                        AppendRuntimeUiRectangle(output, clipped, command.ColorValue, uvMinimum, uvMaximum);
                    }
                }
            }
        }

        void AppendRuntimeUiTextShadows(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command,
                                        const AssetId preparedBinding = {}, const std::size_t preparedFirst = 0U,
                                        const std::size_t preparedCount = (std::numeric_limits<std::size_t>::max)())
        {
            for (std::size_t index = 0; index < command.ShadowCount; ++index)
            {
                const auto& shadow = command.Shadows[index];
                if (shadow.Inset || shadow.ColorValue.Alpha <= 0.0F)
                    continue;
                const auto layers = static_cast<std::size_t>(
                    std::clamp(static_cast<float>(std::ceil(shadow.BlurRadius / 2.0F)), 1.0F, 8.0F));
                for (std::size_t layer = 0; layer < layers; ++layer)
                {
                    const float angle =
                        static_cast<float>(layer) * 2.0F * std::numbers::pi_v<float> / static_cast<float>(layers);
                    const float radius = shadow.BlurRadius * 0.5F + shadow.SpreadRadius;
                    RuntimeUiDrawCommand candidate = command;
                    candidate.Rect.X += shadow.Offset.X + std::cos(angle) * radius;
                    candidate.Rect.Y += shadow.Offset.Y + std::sin(angle) * radius;
                    candidate.ColorValue = shadow.ColorValue;
                    candidate.ColorValue.Alpha /= static_cast<float>(layers);
                    candidate.ShadowCount = 0;
                    AppendRuntimeUiText(output, candidate, preparedBinding, preparedFirst, preparedCount);
                }
            }
        }

        [[nodiscard]] std::optional<RuntimeUiVertex>
        ProjectRuntimeUiWorldVertex(const RuntimeUiVertex& source, const CapturedRuntimeUiWorldPanel& panel,
                                    const std::uint32_t width, const std::uint32_t height) noexcept
        {
            const Vector3 local{(source.Position.X / panel.LayoutScale - panel.Pivot.X * panel.ReferenceResolution.X) *
                                    panel.WorldUnitsPerPixel.X,
                                (panel.Pivot.Y * panel.ReferenceResolution.Y - source.Position.Y / panel.LayoutScale) *
                                    panel.WorldUnitsPerPixel.Y,
                                0.0F};
            const auto world = Math::TransformPoint(panel.World, local);
            const auto& matrix = panel.ViewProjection.Elements;
            const float clipX = matrix[0] * world.X + matrix[4] * world.Y + matrix[8] * world.Z + matrix[12];
            const float clipY = matrix[1] * world.X + matrix[5] * world.Y + matrix[9] * world.Z + matrix[13];
            const float clipZ = matrix[2] * world.X + matrix[6] * world.Y + matrix[10] * world.Z + matrix[14];
            const float clipW = matrix[3] * world.X + matrix[7] * world.Y + matrix[11] * world.Z + matrix[15];
            if (!std::isfinite(clipW) || clipW <= 0.0001F)
                return std::nullopt;
            const float inverseW = 1.0F / clipW;
            const float depth = clipZ * inverseW;
            if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F)
                return std::nullopt;
            return RuntimeUiVertex{{(clipX * inverseW * 0.5F + 0.5F) * static_cast<float>(width),
                                    (0.5F - clipY * inverseW * 0.5F) * static_cast<float>(height), depth},
                                   source.ColorValue,
                                   source.UV};
        }
    } // namespace

    Color EvaluateRuntimeUiGradient(const RuntimeUiGradient& gradient, const Vector2 normalizedPosition)
    {
        if (gradient.Kind == RuntimeUiGradientKind::None || gradient.StopCount < 2U)
            return {};
        if (gradient.Kind == RuntimeUiGradientKind::Radial)
        {
            const float x = normalizedPosition.X - gradient.RadialCenter.X;
            const float y = normalizedPosition.Y - gradient.RadialCenter.Y;
            return GradientColorAt(gradient, std::sqrt(x * x + y * y) / gradient.RadialRadius);
        }
        const float radians = gradient.LinearAngleDegrees * std::numbers::pi_v<float> / 180.0F;
        const Vector2 direction{std::sin(radians), -std::cos(radians)};
        const float minimum = std::min(0.0F, direction.X) + std::min(0.0F, direction.Y);
        const float maximum = std::max(0.0F, direction.X) + std::max(0.0F, direction.Y);
        const float projection = normalizedPosition.X * direction.X + normalizedPosition.Y * direction.Y;
        return GradientColorAt(gradient, (projection - minimum) / std::max(0.000001F, maximum - minimum));
    }

    float RuntimeUiRoundedCoverage(const RuntimeUiRect rectangle, const float radius, const Vector2 position) noexcept
    {
        if (rectangle.Empty())
            return 0.0F;
        const float clampedRadius = std::clamp(radius, 0.0F, std::min(rectangle.Width, rectangle.Height) * 0.5F);
        if (clampedRadius <= 0.0F)
            return rectangle.Contains(position.X, position.Y) ? 1.0F : 0.0F;
        const float centerX = rectangle.X + rectangle.Width * 0.5F;
        const float centerY = rectangle.Y + rectangle.Height * 0.5F;
        const float x = std::abs(position.X - centerX) - (rectangle.Width * 0.5F - clampedRadius);
        const float y = std::abs(position.Y - centerY) - (rectangle.Height * 0.5F - clampedRadius);
        const float outside = std::sqrt(std::max(0.0F, x) * std::max(0.0F, x) + std::max(0.0F, y) * std::max(0.0F, y));
        const float distance = outside + std::min(std::max(x, y), 0.0F) - clampedRadius;
        return std::clamp(0.5F - distance, 0.0F, 1.0F);
    }

    void AccumulateRuntimeUiGeometryStatistics(RuntimeUiRendererStatistics& statistics,
                                               const RuntimeUiGeometry& geometry) noexcept
    {
        statistics.RenderedVertices += geometry.Vertices.size();
        statistics.DrawBatches += geometry.Batches.size();
    }

    RuntimeUiGeometry BuildRuntimeUiGeometry(const std::span<const RuntimeUiDrawCommand> commands)
    {
        RuntimeUiGeometry result;
        result.Vertices.reserve(std::min<std::size_t>(commands.size() * 12U, MaximumRuntimeUiVertices));
        const auto appendBatch = [&result](const AssetId asset, const RuntimeUiRect clip, const std::size_t first)
        {
            const auto count = result.Vertices.size() - first;
            if (count == 0U)
                return;
            if (!result.Batches.empty() && result.Batches.back().Asset == asset &&
                result.Batches.back().ClipRect == clip &&
                static_cast<std::size_t>(result.Batches.back().FirstVertex) + result.Batches.back().VertexCount ==
                    first)
            {
                result.Batches.back().VertexCount += static_cast<std::uint32_t>(count);
                return;
            }
            result.Batches.push_back(
                {asset, clip, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count)});
        };
        for (const auto& command : commands)
        {
            if (result.Vertices.size() >= MaximumRuntimeUiVertices || !IsRuntimeUiDrawable(command))
                continue;
            if (command.Type == RuntimeUiDrawType::Text && command.PreparedFontBinding &&
                !command.PreparedTextGlyphs.empty())
            {
                std::size_t runBegin = 0U;
                while (runBegin < command.PreparedTextGlyphs.size())
                {
                    const auto binding = command.PreparedTextGlyphs[runBegin].FontBinding
                                             ? command.PreparedTextGlyphs[runBegin].FontBinding
                                             : command.PreparedFontBinding;
                    auto runEnd = runBegin + 1U;
                    while (runEnd < command.PreparedTextGlyphs.size())
                    {
                        const auto candidate = command.PreparedTextGlyphs[runEnd].FontBinding
                                                   ? command.PreparedTextGlyphs[runEnd].FontBinding
                                                   : command.PreparedFontBinding;
                        if (candidate != binding)
                            break;
                        ++runEnd;
                    }
                    const auto first = result.Vertices.size();
                    AppendRuntimeUiTextShadows(result.Vertices, command, binding, runBegin, runEnd - runBegin);
                    AppendRuntimeUiText(result.Vertices, command, binding, runBegin, runEnd - runBegin);
                    TransformRuntimeUiVertices(
                        std::span(result.Vertices).subspan(first, result.Vertices.size() - first), command);
                    appendBatch(binding, command.ClipRect, first);
                    runBegin = runEnd;
                }
                continue;
            }
            const auto first = result.Vertices.size();
            switch (command.Type)
            {
            case RuntimeUiDrawType::Quad:
                AppendRuntimeUiBoxShadows(result.Vertices, command);
                if (EffectiveRadius(command) > 0.0F || command.BackgroundGradient.Kind != RuntimeUiGradientKind::None)
                    AppendStyledRectangle(result.Vertices, command, false, false);
                else
                    AppendRuntimeUiRectangle(result.Vertices, command.Rect.Intersect(command.ClipRect),
                                             command.ColorValue);
                AppendRuntimeUiBorder(result.Vertices, command);
                break;
            case RuntimeUiDrawType::Image:
                AppendRuntimeUiImage(result.Vertices, command);
                break;
            case RuntimeUiDrawType::Text:
                AppendRuntimeUiTextShadows(result.Vertices, command);
                AppendRuntimeUiText(result.Vertices, command);
                break;
            case RuntimeUiDrawType::PushClip:
            case RuntimeUiDrawType::PopClip:
                break;
            }
            TransformRuntimeUiVertices(std::span(result.Vertices).subspan(first, result.Vertices.size() - first),
                                       command);
            appendBatch(RuntimeUiTextureAsset(command), command.ClipRect, first);
        }
        return result;
    }

    RuntimeUiGeometry BuildRuntimeUiWorldGeometry(const CapturedRuntimeUiWorldPanel& panel, const std::uint32_t width,
                                                  const std::uint32_t height)
    {
        const auto flat = BuildRuntimeUiGeometry(panel.Commands);
        RuntimeUiGeometry result;
        result.Vertices.reserve(flat.Vertices.size());
        result.Batches.reserve(flat.Batches.size());
        for (const auto& batch : flat.Batches)
        {
            const auto firstVertex = result.Vertices.size();
            const auto end = static_cast<std::size_t>(batch.FirstVertex) + batch.VertexCount;
            for (std::size_t first = batch.FirstVertex; first + 2U < end; first += 3U)
            {
                const auto a = ProjectRuntimeUiWorldVertex(flat.Vertices[first], panel, width, height);
                const auto b = ProjectRuntimeUiWorldVertex(flat.Vertices[first + 1U], panel, width, height);
                const auto c = ProjectRuntimeUiWorldVertex(flat.Vertices[first + 2U], panel, width, height);
                if (a && b && c)
                    result.Vertices.insert(result.Vertices.end(), {*a, *b, *c});
            }
            const auto vertexCount = result.Vertices.size() - firstVertex;
            if (vertexCount != 0U)
            {
                const std::array clipCorners{
                    RuntimeUiVertex{{batch.ClipRect.X, batch.ClipRect.Y, 0.0F}},
                    RuntimeUiVertex{{batch.ClipRect.X + batch.ClipRect.Width, batch.ClipRect.Y, 0.0F}},
                    RuntimeUiVertex{
                        {batch.ClipRect.X + batch.ClipRect.Width, batch.ClipRect.Y + batch.ClipRect.Height, 0.0F}},
                    RuntimeUiVertex{{batch.ClipRect.X, batch.ClipRect.Y + batch.ClipRect.Height, 0.0F}}};
                RuntimeUiRect projectedClip;
                bool projected = true;
                float maximumX = 0.0F;
                float maximumY = 0.0F;
                for (std::size_t index = 0; index < clipCorners.size(); ++index)
                {
                    const auto corner = ProjectRuntimeUiWorldVertex(clipCorners[index], panel, width, height);
                    if (!corner)
                    {
                        projected = false;
                        break;
                    }
                    if (index == 0U)
                    {
                        projectedClip.X = corner->Position.X;
                        projectedClip.Y = corner->Position.Y;
                        maximumX = corner->Position.X;
                        maximumY = corner->Position.Y;
                    }
                    else
                    {
                        projectedClip.X = std::min(projectedClip.X, corner->Position.X);
                        projectedClip.Y = std::min(projectedClip.Y, corner->Position.Y);
                        maximumX = std::max(maximumX, corner->Position.X);
                        maximumY = std::max(maximumY, corner->Position.Y);
                    }
                }
                if (projected)
                {
                    projectedClip.Width = maximumX - projectedClip.X;
                    projectedClip.Height = maximumY - projectedClip.Y;
                }
                result.Batches.push_back({batch.Asset, projected ? projectedClip : RuntimeUiRect{},
                                          static_cast<std::uint32_t>(firstVertex),
                                          static_cast<std::uint32_t>(vertexCount)});
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
