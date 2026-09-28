#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"
#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"
#include "KeireInternal/Ui/RuntimeUiTextInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
                                                    const float shapeMaximum, const float radius,
                                                    const float startCornerRadius, const float endCornerRadius,
                                                    const bool detailed)
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
            const auto appendCornerSamples =
                [&](const float cornerStart, const float cornerRadius, const float direction)
            {
                if (cornerRadius <= 0.0F)
                    return;
                const float span = cornerRadius + 1.0F;
                const auto segments = static_cast<std::size_t>(std::clamp(std::ceil(span / 2.0F), 1.0F, 32.0F));
                for (std::size_t index = 0; index <= segments; ++index)
                {
                    const float amount = static_cast<float>(index) / static_cast<float>(segments);
                    const float value = cornerStart + direction * span * amount;
                    if (value > minimum && value < maximum)
                        result.push_back(value);
                }
            };
            appendCornerSamples(shapeMinimum, startCornerRadius, 1.0F);
            appendCornerSamples(shapeMaximum, endCornerRadius, -1.0F);
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
            auto innerCommand = command;
            innerCommand.Rect = inner;
            innerCommand.CornerRadius = std::max(0.0F, command.CornerRadius - thickness);
            const auto innerRadius = [thickness](const float radius)
            { return radius > 0.0F ? std::max(0.0001F, radius - thickness) : 0.0F; };
            innerCommand.CornerRadii.TopLeft = innerRadius(command.CornerRadii.TopLeft);
            innerCommand.CornerRadii.TopRight = innerRadius(command.CornerRadii.TopRight);
            innerCommand.CornerRadii.BottomRight = innerRadius(command.CornerRadii.BottomRight);
            innerCommand.CornerRadii.BottomLeft = innerRadius(command.CornerRadii.BottomLeft);
            const float innerCoverage = RoundedCoverage(innerCommand, position);
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
            const float maximumRadius = std::min(command.Rect.Width, command.Rect.Height) * 0.5F;
            const auto cornerRadius = [&](const float specified)
            { return std::clamp(specified > 0.0F ? specified : command.CornerRadius, 0.0F, maximumRadius); };
            const float topLeftRadius = cornerRadius(command.CornerRadii.TopLeft);
            const float topRightRadius = cornerRadius(command.CornerRadii.TopRight);
            const float bottomRightRadius = cornerRadius(command.CornerRadii.BottomRight);
            const float bottomLeftRadius = cornerRadius(command.CornerRadii.BottomLeft);
            const auto horizontal = StyledAxis(
                clipped.X, clipped.X + clipped.Width, command.Rect.X, command.Rect.X + command.Rect.Width, radius,
                std::max(topLeftRadius, bottomLeftRadius), std::max(topRightRadius, bottomRightRadius), gradient);
            const auto vertical = StyledAxis(
                clipped.Y, clipped.Y + clipped.Height, command.Rect.Y, command.Rect.Y + command.Rect.Height, radius,
                std::max(topLeftRadius, topRightRadius), std::max(bottomLeftRadius, bottomRightRadius), gradient);
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

        struct WorldUiClipVertex
        {
            RuntimeUiVertex Source;
            std::array<float, 4> Clip;
        };

        [[nodiscard]] WorldUiClipVertex WorldUiClipPoint(const RuntimeUiVertex& source,
                                                         const CapturedRuntimeUiWorldPanel& panel) noexcept
        {
            const Vector3 local{(source.Position.X / panel.LayoutScale - panel.Pivot.X * panel.ReferenceResolution.X) *
                                    panel.WorldUnitsPerPixel.X,
                                (panel.Pivot.Y * panel.ReferenceResolution.Y - source.Position.Y / panel.LayoutScale) *
                                    panel.WorldUnitsPerPixel.Y,
                                0.0F};
            const auto world = Math::TransformPoint(panel.World, local);
            const auto& m = panel.ViewProjection.Elements;
            return {source,
                    {m[0] * world.X + m[4] * world.Y + m[8] * world.Z + m[12],
                     m[1] * world.X + m[5] * world.Y + m[9] * world.Z + m[13],
                     m[2] * world.X + m[6] * world.Y + m[10] * world.Z + m[14],
                     m[3] * world.X + m[7] * world.Y + m[11] * world.Z + m[15]}};
        }

        [[nodiscard]] WorldUiClipVertex InterpolateWorldUi(const WorldUiClipVertex& a, const WorldUiClipVertex& b,
                                                           const float t) noexcept
        {
            WorldUiClipVertex result;
            for (std::size_t i = 0; i < result.Clip.size(); ++i)
                result.Clip[i] = std::lerp(a.Clip[i], b.Clip[i], t);
            const auto& x = a.Source;
            const auto& y = b.Source;
            result.Source = {{std::lerp(x.Position.X, y.Position.X, t), std::lerp(x.Position.Y, y.Position.Y, t), 0.0F},
                             {std::lerp(x.ColorValue.Red, y.ColorValue.Red, t),
                              std::lerp(x.ColorValue.Green, y.ColorValue.Green, t),
                              std::lerp(x.ColorValue.Blue, y.ColorValue.Blue, t),
                              std::lerp(x.ColorValue.Alpha, y.ColorValue.Alpha, t)},
                             {std::lerp(x.UV.X, y.UV.X, t), std::lerp(x.UV.Y, y.UV.Y, t)}};
            return result;
        }

        void AppendClippedWorldUiTriangle(std::vector<RuntimeUiVertex>& output,
                                          const std::array<RuntimeUiVertex, 3>& triangle,
                                          const CapturedRuntimeUiWorldPanel& panel, const RuntimeUiRect clip,
                                          const std::uint32_t width, const std::uint32_t height)
        {
            // Each half-space can add at most one vertex to a convex triangle: 3 + 11 fits in 16.
            std::array<WorldUiClipVertex, 16> polygon{};
            std::size_t count = 0;
            for (const auto& vertex : triangle)
            {
                const auto point = WorldUiClipPoint(vertex, panel);
                if (!std::ranges::all_of(point.Clip, [](const float value) { return std::isfinite(value); }))
                    return;
                polygon[count++] = point;
            }
            // Clip in homogeneous space before dividing by W. Local clipping preserves panel overflow rules.
            for (int plane = 0; plane < 11 && count != 0; ++plane)
            {
                const auto distance = [&](const WorldUiClipVertex& vertex)
                {
                    const auto& v = vertex.Clip;
                    switch (plane)
                    {
                    case 0:
                        return vertex.Source.Position.X - clip.X;
                    case 1:
                        return clip.X + clip.Width - vertex.Source.Position.X;
                    case 2:
                        return vertex.Source.Position.Y - clip.Y;
                    case 3:
                        return clip.Y + clip.Height - vertex.Source.Position.Y;
                    case 4:
                        return v[3] - 0.0001F;
                    case 5:
                        return v[0] + v[3];
                    case 6:
                        return v[3] - v[0];
                    case 7:
                        return v[1] + v[3];
                    case 8:
                        return v[3] - v[1];
                    case 9:
                        return v[2];
                    default:
                        return v[3] - v[2];
                    }
                };
                std::array<WorldUiClipVertex, 16> clipped{};
                std::size_t clippedCount = 0;
                auto previous = polygon[count - 1];
                float previousDistance = distance(previous);
                for (std::size_t index = 0; index < count; ++index)
                {
                    const auto& current = polygon[index];
                    const float currentDistance = distance(current);
                    if ((previousDistance >= 0.0F) != (currentDistance >= 0.0F))
                        clipped[clippedCount++] = InterpolateWorldUi(
                            previous, current, previousDistance / (previousDistance - currentDistance));
                    if (currentDistance >= 0.0F)
                        clipped[clippedCount++] = current;
                    previous = current;
                    previousDistance = currentDistance;
                }
                polygon = clipped;
                count = clippedCount;
            }
            const auto project = [&](const WorldUiClipVertex& vertex)
            {
                auto result = vertex.Source;
                const auto& v = vertex.Clip;
                result.PerspectiveW = v[3];
                result.Position = {(v[0] / v[3] * 0.5F + 0.5F) * static_cast<float>(width),
                                   (0.5F - v[1] / v[3] * 0.5F) * static_cast<float>(height),
                                   std::clamp(v[2] / v[3], 0.0F, 1.0F)};
                return result;
            };
            for (std::size_t i = 1; i + 1 < count; ++i)
            {
                if (output.size() > MaximumRuntimeUiVertices - 3U)
                    return;
                output.insert(output.end(), {project(polygon[0]), project(polygon[i]), project(polygon[i + 1])});
            }
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
        const auto appendBatch =
            [&result](const AssetId asset, const RuntimeUiRect clip, const std::size_t first, const AssetId material)
        {
            const auto count = result.Vertices.size() - first;
            if (count == 0U)
                return;
            if (!result.Batches.empty() && result.Batches.back().Asset == asset &&
                result.Batches.back().ClipRect == clip && result.Batches.back().Material == material &&
                static_cast<std::size_t>(result.Batches.back().FirstVertex) + result.Batches.back().VertexCount ==
                    first)
            {
                result.Batches.back().VertexCount += static_cast<std::uint32_t>(count);
                return;
            }
            result.Batches.push_back(
                {asset, clip, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count), material});
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
                    appendBatch(binding, command.ClipRect, first, command.Material);
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
            if (command.Material && command.Type == RuntimeUiDrawType::Quad)
                for (auto& vertex : std::span(result.Vertices).subspan(first))
                    vertex.UV = NormalizedPosition(command.Rect, {vertex.Position.X, vertex.Position.Y});
            TransformRuntimeUiVertices(std::span(result.Vertices).subspan(first, result.Vertices.size() - first),
                                       command);
            appendBatch(RuntimeUiTextureAsset(command), command.ClipRect, first, command.Material);
        }
        return result;
    }

    RuntimeUiGeometry BuildRuntimeUiCameraGeometry(const CapturedRuntimeUiCameraPanel& panel, const std::uint32_t width,
                                                   const std::uint32_t height)
    {
        if (!std::isfinite(panel.Viewport.X) || !std::isfinite(panel.Viewport.Y) || panel.Viewport.X <= 0.0F ||
            panel.Viewport.Y <= 0.0F || width == 0 || height == 0)
            throw std::invalid_argument("Camera UI geometry requires finite positive viewport and surface extents.");
        auto result = BuildRuntimeUiGeometry(panel.Commands);
        const float scaleX = static_cast<float>(width) / panel.Viewport.X;
        const float scaleY = static_cast<float>(height) / panel.Viewport.Y;
        for (auto& vertex : result.Vertices)
        {
            vertex.Position.X *= scaleX;
            vertex.Position.Y *= scaleY;
        }
        for (auto& batch : result.Batches)
        {
            batch.ClipRect.X *= scaleX;
            batch.ClipRect.Y *= scaleY;
            batch.ClipRect.Width *= scaleX;
            batch.ClipRect.Height *= scaleY;
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
                AppendClippedWorldUiTriangle(
                    result.Vertices, {flat.Vertices[first], flat.Vertices[first + 1U], flat.Vertices[first + 2U]},
                    panel, batch.ClipRect, width, height);
            }
            const auto vertexCount = result.Vertices.size() - firstVertex;
            if (vertexCount != 0U)
            {
                result.Batches.push_back({batch.Asset,
                                          {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)},
                                          static_cast<std::uint32_t>(firstVertex),
                                          static_cast<std::uint32_t>(vertexCount),
                                          batch.Material});
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
