#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"
#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"

#include <algorithm>
#include <cmath>
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

        [[nodiscard]] float BorderCoverage(const RuntimeUiDrawCommand& command, const Vector2 position) noexcept
        {
            const float outer = RuntimeUiRoundedCoverage(command.Rect, command.CornerRadius, position);
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
            const auto clipped = command.Rect.Intersect(command.ClipRect);
            if (clipped.Empty())
                return;
            const bool gradient = !image && !border && command.BackgroundGradient.Kind != RuntimeUiGradientKind::None;
            const float radius =
                std::clamp(command.CornerRadius, 0.0F, std::min(command.Rect.Width, command.Rect.Height) * 0.5F);
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
                        const float coverage = border
                                                   ? BorderCoverage(command, positions[index])
                                                   : RuntimeUiRoundedCoverage(command.Rect, radius, positions[index]);
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
            if (command.CornerRadius > 0.0F)
            {
                AppendStyledRectangle(output, command, true, false);
                return;
            }
            const auto clipped = command.Rect.Intersect(command.ClipRect);
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
            const float thickness = std::min(command.BorderWidth, std::min(command.Rect.Width, command.Rect.Height));
            if (thickness <= 0.0F || command.BorderColor.Alpha <= 0.0F)
                return;
            if (command.CornerRadius > 0.0F)
            {
                AppendStyledRectangle(output, command, false, true);
                return;
            }
            const auto append = [&](const RuntimeUiRect rectangle)
            { AppendRuntimeUiRectangle(output, rectangle.Intersect(command.ClipRect), command.BorderColor); };
            append({command.Rect.X, command.Rect.Y, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + command.Rect.Height - thickness, command.Rect.Width, thickness});
            append({command.Rect.X, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
            append({command.Rect.X + command.Rect.Width - thickness, command.Rect.Y + thickness, thickness,
                    std::max(0.0F, command.Rect.Height - thickness * 2.0F)});
        }

        void AppendRuntimeUiText(std::vector<RuntimeUiVertex>& output, const RuntimeUiDrawCommand& command)
        {
            if (command.Text.empty() || command.FontSize <= 0.0F || command.ColorValue.Alpha <= 0.0F ||
                output.size() >= MaximumRuntimeUiVertices)
                return;
            const std::string_view text(command.Text.data(), std::min(command.Text.size(), MaximumRuntimeUiTextBytes));
            const float scale = command.FontSize / 12.0F;
            const float lineHeight = 16.0F * scale;
            float width = 0.0F;
            float lineWidth = 0.0F;
            std::size_t lineCount = 1U;
            for (const auto byte : text)
            {
                if (byte == '\n')
                {
                    width = std::max(width, lineWidth);
                    lineWidth = 0.0F;
                    ++lineCount;
                }
                else if ((static_cast<std::uint8_t>(byte) & 0xc0U) != 0x80U)
                {
                    lineWidth += RuntimeUiFallbackGlyph(static_cast<std::uint8_t>(byte)).Advance * scale;
                }
            }
            width = std::max(width, lineWidth);
            const float height = static_cast<float>(lineCount) * lineHeight;
            float originX = command.Rect.X;
            float originY = command.Rect.Y;
            if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                originX += (command.Rect.Width - width) * 0.5F;
            else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                originX += command.Rect.Width - width;
            if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                originY += (command.Rect.Height - height) * 0.5F;
            else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                originY += command.Rect.Height - height;

            float penX = originX;
            float penY = originY;
            for (const auto byte : text)
            {
                if (output.size() > MaximumRuntimeUiVertices - 6U)
                    return;
                if (byte == '\n')
                {
                    penX = originX;
                    penY += lineHeight;
                    continue;
                }
                const auto character = static_cast<std::uint8_t>(byte);
                if ((character & 0xc0U) == 0x80U)
                    continue;
                const auto& glyph = RuntimeUiFallbackGlyph(character);
                const RuntimeUiRect rectangle{penX + glyph.Offset.X * scale, penY + glyph.Offset.Y * scale,
                                              glyph.Width * scale, glyph.Height * scale};
                const auto clipped = rectangle.Intersect(command.ClipRect);
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
                penX += glyph.Advance * scale;
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
        for (const auto& run : BuildRuntimeUiTextureRuns(commands))
        {
            const auto firstVertex = result.Vertices.size();
            const auto end = std::min(commands.size(), run.FirstCommand + run.CommandCount);
            for (std::size_t index = run.FirstCommand; index < end; ++index)
            {
                if (result.Vertices.size() >= MaximumRuntimeUiVertices)
                    break;
                const auto& command = commands[index];
                switch (command.Type)
                {
                case RuntimeUiDrawType::Quad:
                    if (command.CornerRadius > 0.0F || command.BackgroundGradient.Kind != RuntimeUiGradientKind::None)
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
                    AppendRuntimeUiText(result.Vertices, command);
                    break;
                case RuntimeUiDrawType::PushClip:
                case RuntimeUiDrawType::PopClip:
                    break;
                }
            }
            const auto vertexCount = result.Vertices.size() - firstVertex;
            if (vertexCount != 0U)
                result.Batches.push_back({run.Asset, run.ClipRect, static_cast<std::uint32_t>(firstVertex),
                                          static_cast<std::uint32_t>(vertexCount)});
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
