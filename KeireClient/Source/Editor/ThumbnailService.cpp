#include "KeireClient/Editor/ThumbnailService.h"

#include "Keire/Audio/AudioAssets.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr std::uint32_t ThumbnailWidth = 96;
        constexpr std::uint32_t ThumbnailHeight = 96;

        void PutPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                      const std::uint32_t y, const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue,
                      const std::uint8_t alpha = 255)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[offset] = static_cast<std::byte>(red);
            pixels[offset + 1] = static_cast<std::byte>(green);
            pixels[offset + 2] = static_cast<std::byte>(blue);
            pixels[offset + 3] = static_cast<std::byte>(alpha);
        }

        [[nodiscard]] std::vector<std::byte> MakeIcon(const std::uint32_t width, const std::uint32_t height,
                                                      const std::array<std::uint8_t, 3> background,
                                                      const std::array<std::uint8_t, 3> accent, const char glyph,
                                                      const bool missing)
        {
            std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const bool border = x < 3 || y < 3 || x + 3 >= width || y + 3 >= height;
                    const auto color = border ? accent : background;
                    PutPixel(pixels, width, x, y, color[0], color[1], color[2]);
                }
            const auto centerX = width / 2;
            const auto centerY = height / 2;
            const std::uint32_t radius = std::max(4U, std::min(width, height) / 5);
            for (std::uint32_t y = centerY - radius; y <= centerY + radius; ++y)
                for (std::uint32_t x = centerX - radius; x <= centerX + radius; ++x)
                    if (x < width && y < height &&
                        (x == centerX - radius || x == centerX + radius || y == centerY - radius ||
                         y == centerY + radius ||
                         (glyph == 'X' && (x - (centerX - radius) == y - (centerY - radius) ||
                                           x - (centerX - radius) + y - (centerY - radius) == radius * 2))))
                        PutPixel(pixels, width, x, y, accent[0], accent[1], accent[2]);
            if (missing)
            {
                for (std::uint32_t index = 8; index + 8 < std::min(width, height); ++index)
                {
                    PutPixel(pixels, width, index, index, 235, 72, 82);
                    PutPixel(pixels, width, width - index - 1, index, 235, 72, 82);
                }
            }
            return pixels;
        }

        [[nodiscard]] constexpr std::array<std::uint8_t, 5> GlyphRows(const char glyph) noexcept
        {
            switch (glyph)
            {
            case 'F':
                return {0b111, 0b100, 0b110, 0b100, 0b100};
            case 'G':
                return {0b111, 0b100, 0b101, 0b101, 0b111};
            case 'I':
                return {0b111, 0b010, 0b010, 0b010, 0b111};
            case 'M':
                return {0b101, 0b111, 0b111, 0b101, 0b101};
            case 'V':
                return {0b101, 0b101, 0b101, 0b101, 0b010};
            case 'X':
                return {0b101, 0b101, 0b010, 0b101, 0b101};
            default:
                return {};
            }
        }

        void ApplyBadge(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height,
                        const std::string_view text)
        {
            if (pixels.size() != static_cast<std::size_t>(width) * height * 4 || width < 24 || height < 16 ||
                text.size() != 2)
            {
                return;
            }
            constexpr std::uint32_t badgeWidth = 22;
            constexpr std::uint32_t badgeHeight = 14;
            const auto left = width - badgeWidth - 3;
            const auto top = height - badgeHeight - 3;
            for (std::uint32_t y = top; y < top + badgeHeight; ++y)
                for (std::uint32_t x = left; x < left + badgeWidth; ++x)
                {
                    const bool border =
                        x == left || y == top || x + 1 == left + badgeWidth || y + 1 == top + badgeHeight;
                    PutPixel(pixels, width, x, y, border ? 229 : 20, border ? 178 : 25, border ? 65 : 34, 255);
                }
            for (std::size_t character = 0; character < text.size(); ++character)
            {
                const auto rows = GlyphRows(text[character]);
                const auto glyphLeft = left + 4 + static_cast<std::uint32_t>(character) * 8;
                for (std::uint32_t row = 0; row < rows.size(); ++row)
                    for (std::uint32_t column = 0; column < 3; ++column)
                        if ((rows[row] & (1U << (2U - column))) != 0)
                            for (std::uint32_t pixelY = 0; pixelY < 2; ++pixelY)
                                for (std::uint32_t pixelX = 0; pixelX < 2; ++pixelX)
                                    PutPixel(pixels, width, glyphLeft + column * 2 + pixelX, top + 2 + row * 2 + pixelY,
                                             248, 225, 151, 255);
            }
        }

        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const char input)
                                   { return static_cast<char>(std::tolower(static_cast<unsigned char>(input))); });
            return value;
        }

        [[nodiscard]] std::uint8_t Byte(const std::byte value) noexcept { return std::to_integer<std::uint8_t>(value); }

        template <typename Value> [[nodiscard]] constexpr float AsFloat(const Value value) noexcept
        {
            return static_cast<float>(value);
        }

        template <typename Value> [[nodiscard]] constexpr double AsDouble(const Value value) noexcept
        {
            return static_cast<double>(value);
        }

        void FillPreviewBackground(std::vector<std::byte>& pixels, const std::uint32_t width,
                                   const std::uint32_t height)
        {
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto shade = static_cast<std::uint8_t>(25 + 18 * y / std::max(height, 1U));
                    PutPixel(pixels, width, x, y, shade, static_cast<std::uint8_t>(shade + 3),
                             static_cast<std::uint8_t>(shade + 8));
                }
        }

        [[nodiscard]] std::array<std::uint8_t, 4> SampleTexture(const Keire::Texture2DAsset& texture, float u, float v)
        {
            if (texture.Mips().empty())
                return {255, 255, 255, 255};
            const auto& mip = texture.Mips().front();
            if (mip.Width == 0 || mip.Height == 0 ||
                mip.Pixels.size() != static_cast<std::size_t>(mip.Width) * mip.Height * 4)
                return {255, 255, 255, 255};
            u -= std::floor(u);
            v -= std::floor(v);
            const auto x = std::min(static_cast<std::uint32_t>(u * AsFloat(mip.Width)), mip.Width - 1);
            const auto y = std::min(static_cast<std::uint32_t>(v * AsFloat(mip.Height)), mip.Height - 1);
            const auto offset = (static_cast<std::size_t>(y) * mip.Width + x) * 4;
            return {Byte(mip.Pixels[offset]), Byte(mip.Pixels[offset + 1]), Byte(mip.Pixels[offset + 2]),
                    Byte(mip.Pixels[offset + 3])};
        }

        [[nodiscard]] std::vector<std::byte> MakeTexturePreview(const ThumbnailRequest& request,
                                                                const std::uint32_t width, const std::uint32_t height)
        {
            const auto texture = Keire::DynamicRefCast<const Keire::Texture2DAsset>(request.PreviewAsset);
            if (!texture || texture->Mips().empty())
                return MakeIcon(width, height, {38, 38, 44}, {205, 72, 205}, 'X', true);
            std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                    const auto checker = static_cast<std::uint8_t>(light ? 96 : 58);
                    PutPixel(result, width, x, y, checker, checker, checker);
                }
            const float sourceAspect = AsFloat(texture->Width()) / AsFloat(std::max(texture->Height(), 1U));
            const float targetAspect = AsFloat(width) / AsFloat(std::max(height, 1U));
            const std::uint32_t drawWidth =
                sourceAspect > targetAspect ? width
                                            : std::max(1U, static_cast<std::uint32_t>(AsFloat(height) * sourceAspect));
            const std::uint32_t drawHeight =
                sourceAspect > targetAspect ? std::max(1U, static_cast<std::uint32_t>(AsFloat(width) / sourceAspect))
                                            : height;
            const std::uint32_t left = (width - drawWidth) / 2;
            const std::uint32_t top = (height - drawHeight) / 2;
            for (std::uint32_t y = 0; y < drawHeight; ++y)
                for (std::uint32_t x = 0; x < drawWidth; ++x)
                {
                    auto sample = SampleTexture(*texture, (AsFloat(x) + 0.5F) / AsFloat(drawWidth),
                                                (AsFloat(y) + 0.5F) / AsFloat(drawHeight));
                    if (texture->Settings().HighDynamicRange)
                    {
                        const float exponent = std::ldexp(1.0F, static_cast<int>(sample[3]) - 136);
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            const float radiance = static_cast<float>(sample[channel]) * exponent;
                            const float mapped = std::clamp(radiance * (2.51F * radiance + 0.03F) /
                                                                (radiance * (2.43F * radiance + 0.59F) + 0.14F),
                                                            0.0F, 1.0F);
                            sample[channel] = static_cast<std::uint8_t>(std::pow(mapped, 1.0F / 2.2F) * 255.0F);
                        }
                        sample[3] = 255;
                    }
                    const float alpha = static_cast<float>(sample[3]) / 255.0F;
                    const bool light = (((left + x) / 8) + ((top + y) / 8)) % 2 == 0;
                    const float checker = light ? 96.0F : 58.0F;
                    PutPixel(
                        result, width, left + x, top + y,
                        static_cast<std::uint8_t>(static_cast<float>(sample[0]) * alpha + checker * (1.0F - alpha)),
                        static_cast<std::uint8_t>(static_cast<float>(sample[1]) * alpha + checker * (1.0F - alpha)),
                        static_cast<std::uint8_t>(static_cast<float>(sample[2]) * alpha + checker * (1.0F - alpha)));
                }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeMaterialPreview(const ThumbnailRequest& request,
                                                                 const std::uint32_t width, const std::uint32_t height)
        {
            const auto material = Keire::DynamicRefCast<const Keire::MaterialAsset>(request.PreviewAsset);
            if (!material)
                return MakeIcon(width, height, {48, 31, 48}, {226, 78, 211}, 'M', true);
            Keire::Color tint{1.0F, 1.0F, 1.0F, 1.0F};
            Keire::AssetId baseTexture;
            const auto& definition = material->Definition();
            if (const auto found = definition.Properties.find("Tint"); found != definition.Properties.end())
                if (const auto* color = std::get_if<Keire::Color>(&found->second))
                    tint = *color;
            if (const auto found = definition.Properties.find("ErrorColor"); found != definition.Properties.end())
                if (const auto* color = std::get_if<Keire::Color>(&found->second))
                    tint = *color;
            if (const auto found = definition.Properties.find("MainTexture"); found != definition.Properties.end())
                if (const auto* asset = std::get_if<Keire::AssetId>(&found->second))
                    baseTexture = *asset;
            if (request.PreviewShader)
            {
                for (const auto& property : request.PreviewShader->Definition().Properties)
                {
                    if (property.TextureSemantic != Keire::ShaderTextureSemantic::BaseColor)
                        continue;
                    if (const auto found = definition.Properties.find(property.Name);
                        found != definition.Properties.end())
                        if (const auto* asset = std::get_if<Keire::AssetId>(&found->second))
                            baseTexture = *asset;
                    break;
                }
            }
            const auto texture = baseTexture ? request.PreviewTexture : Keire::Ref<const Keire::Texture2DAsset>{};
            std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4);
            FillPreviewBackground(result, width, height);
            const float widthFloat = AsFloat(width);
            const float heightFloat = AsFloat(height);
            const float centerX = widthFloat * 0.5F;
            const float centerY = heightFloat * 0.48F;
            const float radius = AsFloat(std::min(width, height)) * 0.39F;
            constexpr float pi = 3.14159265358979323846F;
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const float nx = (AsFloat(x) + 0.5F - centerX) / radius;
                    const float ny = (centerY - AsFloat(y) - 0.5F) / radius;
                    const float radiusSquared = nx * nx + ny * ny;
                    if (radiusSquared > 1.0F)
                        continue;
                    const float nz = std::sqrt(std::max(0.0F, 1.0F - radiusSquared));
                    const float diffuse = std::max(0.0F, nx * -0.35F + ny * 0.55F + nz * 0.76F);
                    const float rim = std::pow(std::max(0.0F, 1.0F - nz), 2.0F) * 0.18F;
                    const float specular = std::pow(std::max(0.0F, nx * -0.18F + ny * 0.42F + nz * 0.89F), 38.0F);
                    const float u = 0.5F + std::atan2(nx, nz) / (2.0F * pi);
                    const float v = 0.5F - std::asin(std::clamp(ny, -1.0F, 1.0F)) / pi;
                    const auto sample =
                        texture ? SampleTexture(*texture, u, v) : std::array<std::uint8_t, 4>{255, 255, 255, 255};
                    const float light = 0.18F + diffuse * 0.72F + rim;
                    const auto channel = [&](const std::uint8_t value, const float factor)
                    {
                        return static_cast<std::uint8_t>(
                            std::clamp(static_cast<float>(value) * factor * light + specular * 70.0F, 0.0F, 255.0F));
                    };
                    PutPixel(result, width, x, y, channel(sample[0], tint.Red), channel(sample[1], tint.Green),
                             channel(sample[2], tint.Blue));
                }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeShaderGraphPreview(const ThumbnailRequest& request,
                                                                    const std::uint32_t width,
                                                                    const std::uint32_t height,
                                                                    const std::string_view badge)
        {
            auto result = MakeMaterialPreview(request, width, height);
            ApplyBadge(result, width, height, badge);
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeVfxPreview(const ThumbnailRequest& request, const std::uint32_t width,
                                                            const std::uint32_t height)
        {
            auto fallback =
                MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), width, height, request.Missing);
            const auto effect = Keire::DynamicRefCast<const Keire::VfxEffectAsset>(request.PreviewAsset);
            if (!effect)
                return fallback;
            try
            {
                Keire::VfxWorld world({.MaximumEffects = 1,
                                       .MaximumSystemsPerEffect = 16,
                                       .MaximumParticles = 2048,
                                       .Backend = Keire::VfxBackend::Cpu});
                const auto handle = world.Activate({.Effect = effect, .Revision = 1});
                if (!handle)
                    return fallback;
                for (std::size_t frame = 0; frame < 36; ++frame)
                    world.Update(1.0F / 30.0F);
                const auto snapshot = world.CaptureRenderSnapshot(2048);
                if (snapshot.Particles().empty())
                    return fallback;

                std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4);
                FillPreviewBackground(result, width, height);
                float extent = 0.5F;
                for (const auto& particle : snapshot.Particles())
                {
                    extent = std::max({extent, std::abs(particle.Position.X), std::abs(particle.Position.Y),
                                       std::abs(particle.Position.Z)});
                }
                const auto drawParticle = [&](const Keire::VfxRenderParticle& particle)
                {
                    const float projectedX = (particle.Position.X - particle.Position.Z) * 0.70710678F / extent;
                    const float projectedY =
                        (particle.Position.Y * 0.82F - (particle.Position.X + particle.Position.Z) * 0.2F) / extent;
                    const int centerX = static_cast<int>(static_cast<float>(width) * (0.5F + projectedX * 0.36F));
                    const int centerY = static_cast<int>(static_cast<float>(height) * (0.52F - projectedY * 0.36F));
                    const int radius = std::clamp(static_cast<int>(particle.Size * 4.0F / extent), 1, 6);
                    for (int y = -radius; y <= radius; ++y)
                        for (int x = -radius; x <= radius; ++x)
                        {
                            const int pixelX = centerX + x;
                            const int pixelY = centerY + y;
                            if (x * x + y * y > radius * radius || pixelX < 0 || pixelY < 0 ||
                                pixelX >= static_cast<int>(width) || pixelY >= static_cast<int>(height))
                            {
                                continue;
                            }
                            const float alpha =
                                std::clamp(particle.Tint.Alpha * (1.0F - std::sqrt(static_cast<float>(x * x + y * y)) /
                                                                             static_cast<float>(radius + 1)),
                                           0.15F, 1.0F);
                            const auto offset =
                                (static_cast<std::size_t>(pixelY) * width + static_cast<std::size_t>(pixelX)) * 4;
                            const auto blend = [&](const std::size_t channel, const float value)
                            {
                                return static_cast<std::uint8_t>(
                                    std::clamp(static_cast<float>(Byte(result[offset + channel])) * (1.0F - alpha) +
                                                   value * 255.0F * alpha,
                                               0.0F, 255.0F));
                            };
                            PutPixel(result, width, static_cast<std::uint32_t>(pixelX),
                                     static_cast<std::uint32_t>(pixelY), blend(0, particle.Tint.Red),
                                     blend(1, particle.Tint.Green), blend(2, particle.Tint.Blue), 255);
                        }
                };
                for (const auto& particle : snapshot.Particles())
                    drawParticle(particle);
                ApplyBadge(result, width, height, "FX");
                return result;
            }
            catch (const std::exception&)
            {
                return fallback;
            }
        }

        void DrawLine(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height, int x0,
                      int y0, const int x1, const int y1, const std::array<std::uint8_t, 3> color)
        {
            const int dx = std::abs(x1 - x0);
            const int sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0);
            const int sy = y0 < y1 ? 1 : -1;
            int error = dx + dy;
            for (;;)
            {
                if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(width) && y0 < static_cast<int>(height))
                    PutPixel(pixels, width, static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), color[0],
                             color[1], color[2]);
                if (x0 == x1 && y0 == y1)
                    break;
                const int doubled = error * 2;
                if (doubled >= dy)
                {
                    error += dy;
                    x0 += sx;
                }
                if (doubled <= dx)
                {
                    error += dx;
                    y0 += sy;
                }
            }
        }

        [[nodiscard]] std::vector<std::byte> MakeMeshPreview(const ThumbnailRequest& request, const std::uint32_t width,
                                                             const std::uint32_t height)
        {
            const auto mesh = Keire::DynamicRefCast<const Keire::MeshAsset>(request.PreviewAsset);
            if (!mesh || mesh->Vertices().empty())
                return MakeIcon(width, height, {31, 42, 49}, {84, 190, 214}, 'M', true);
            std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4);
            FillPreviewBackground(result, width, height);
            const auto& bounds = mesh->Bounds();
            const Keire::Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                        (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                                        (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
            const float extent = std::max({bounds.Maximum.X - bounds.Minimum.X, bounds.Maximum.Y - bounds.Minimum.Y,
                                           bounds.Maximum.Z - bounds.Minimum.Z, 0.0001F});
            struct PreviewVertex final
            {
                float X = 0.0F;
                float Y = 0.0F;
                float Depth = 0.0F;
                float Light = 0.0F;
            };
            std::vector<PreviewVertex> projected;
            projected.reserve(mesh->Vertices().size());
            float minimumX = std::numeric_limits<float>::max();
            float minimumY = std::numeric_limits<float>::max();
            float maximumX = std::numeric_limits<float>::lowest();
            float maximumY = std::numeric_limits<float>::lowest();
            for (const auto& vertex : mesh->Vertices())
            {
                const float x = (vertex.Position.X - center.X) / extent;
                const float y = (vertex.Position.Y - center.Y) / extent;
                const float z = (vertex.Position.Z - center.Z) / extent;
                const float viewX = (x - z) * 0.70710678F;
                const float viewY = y * 0.8660254F - (x + z) * 0.25F;
                const float depth = (x + z) * 0.6123724F + y * 0.5F;
                const float diffuse = std::clamp(
                    vertex.Normal.X * -0.36F + vertex.Normal.Y * 0.78F + vertex.Normal.Z * -0.51F, -1.0F, 1.0F);
                projected.push_back({viewX, viewY, depth, 0.24F + std::max(diffuse, 0.0F) * 0.76F});
                minimumX = std::min(minimumX, viewX);
                minimumY = std::min(minimumY, viewY);
                maximumX = std::max(maximumX, viewX);
                maximumY = std::max(maximumY, viewY);
            }
            const float projectedWidth = std::max(maximumX - minimumX, 0.0001F);
            const float projectedHeight = std::max(maximumY - minimumY, 0.0001F);
            const float widthFloat = AsFloat(width);
            const float heightFloat = AsFloat(height);
            const float scale = std::min(widthFloat * 0.82F / projectedWidth, heightFloat * 0.82F / projectedHeight);
            for (auto& vertex : projected)
            {
                vertex.X = widthFloat * 0.5F + (vertex.X - (minimumX + maximumX) * 0.5F) * scale;
                vertex.Y = heightFloat * 0.52F - (vertex.Y - (minimumY + maximumY) * 0.5F) * scale;
            }

            std::vector<float> depthBuffer(static_cast<std::size_t>(width) * height,
                                           std::numeric_limits<float>::infinity());
            const auto indices = mesh->Indices();
            constexpr std::size_t maximumPreviewTriangles = 50000;
            const std::size_t triangleStride =
                std::max<std::size_t>(1, (indices.size() / 3 + maximumPreviewTriangles - 1) / maximumPreviewTriangles);
            for (std::size_t index = 0; index + 2 < indices.size(); index += 3 * triangleStride)
            {
                if (indices[index] >= projected.size() || indices[index + 1] >= projected.size() ||
                    indices[index + 2] >= projected.size())
                    continue;
                const auto first = projected[indices[index]];
                const auto second = projected[indices[index + 1]];
                const auto third = projected[indices[index + 2]];
                const float area =
                    (second.X - first.X) * (third.Y - first.Y) - (second.Y - first.Y) * (third.X - first.X);
                if (std::abs(area) < 0.0001F)
                    continue;
                const int left = std::max(0, static_cast<int>(std::floor(std::min({first.X, second.X, third.X}))));
                const int right = std::min(static_cast<int>(width) - 1,
                                           static_cast<int>(std::ceil(std::max({first.X, second.X, third.X}))));
                const int top = std::max(0, static_cast<int>(std::floor(std::min({first.Y, second.Y, third.Y}))));
                const int bottom = std::min(static_cast<int>(height) - 1,
                                            static_cast<int>(std::ceil(std::max({first.Y, second.Y, third.Y}))));
                for (int y = top; y <= bottom; ++y)
                    for (int x = left; x <= right; ++x)
                    {
                        const float sampleX = AsFloat(x) + 0.5F;
                        const float sampleY = AsFloat(y) + 0.5F;
                        const float firstWeight =
                            ((second.X - sampleX) * (third.Y - sampleY) - (second.Y - sampleY) * (third.X - sampleX)) /
                            area;
                        const float secondWeight =
                            ((third.X - sampleX) * (first.Y - sampleY) - (third.Y - sampleY) * (first.X - sampleX)) /
                            area;
                        const float thirdWeight = 1.0F - firstWeight - secondWeight;
                        if (firstWeight < -0.0001F || secondWeight < -0.0001F || thirdWeight < -0.0001F)
                            continue;
                        const float depth =
                            first.Depth * firstWeight + second.Depth * secondWeight + third.Depth * thirdWeight;
                        const auto pixel = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                        if (depth >= depthBuffer[pixel])
                            continue;
                        depthBuffer[pixel] = depth;
                        const float light = std::clamp(first.Light * firstWeight + second.Light * secondWeight +
                                                           third.Light * thirdWeight,
                                                       0.0F, 1.0F);
                        PutPixel(result, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                 static_cast<std::uint8_t>(48.0F + light * 142.0F),
                                 static_cast<std::uint8_t>(55.0F + light * 145.0F),
                                 static_cast<std::uint8_t>(66.0F + light * 151.0F));
                    }
            }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakePrefabPreview(const ThumbnailRequest& request,
                                                               const std::uint32_t width, const std::uint32_t height)
        {
            if (request.PreviewMeshes.empty())
                return MakeIcon(width, height, {34, 42, 55}, {89, 174, 236}, 'P', request.Missing);
            struct WorldVertex final
            {
                Keire::Vector3 Position;
                Keire::Vector3 Normal;
            };
            struct PreviewVertex final
            {
                float X = 0.0F;
                float Y = 0.0F;
                float Depth = 0.0F;
                float Light = 0.0F;
            };
            std::vector<WorldVertex> vertices;
            std::vector<std::uint32_t> indices;
            for (const auto& instance : request.PreviewMeshes)
            {
                if (!instance.Mesh)
                    continue;
                const auto base = static_cast<std::uint32_t>(vertices.size());
                vertices.reserve(vertices.size() + instance.Mesh->Vertices().size());
                for (const auto& vertex : instance.Mesh->Vertices())
                {
                    auto normal = Keire::Math::TransformDirection(instance.Transform, vertex.Normal);
                    const float normalLength =
                        std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
                    if (normalLength > 0.000001F)
                    {
                        normal.X /= normalLength;
                        normal.Y /= normalLength;
                        normal.Z /= normalLength;
                    }
                    vertices.push_back({Keire::Math::TransformPoint(instance.Transform, vertex.Position), normal});
                }
                indices.reserve(indices.size() + instance.Mesh->Indices().size());
                for (const auto index : instance.Mesh->Indices())
                    indices.push_back(base + index);
            }
            if (vertices.empty())
                return MakeIcon(width, height, {34, 42, 55}, {89, 174, 236}, 'P', request.Missing);

            Keire::Vector3 minimum = vertices.front().Position;
            Keire::Vector3 maximum = minimum;
            for (const auto& vertex : vertices)
            {
                minimum.X = std::min(minimum.X, vertex.Position.X);
                minimum.Y = std::min(minimum.Y, vertex.Position.Y);
                minimum.Z = std::min(minimum.Z, vertex.Position.Z);
                maximum.X = std::max(maximum.X, vertex.Position.X);
                maximum.Y = std::max(maximum.Y, vertex.Position.Y);
                maximum.Z = std::max(maximum.Z, vertex.Position.Z);
            }
            const Keire::Vector3 center{(minimum.X + maximum.X) * 0.5F, (minimum.Y + maximum.Y) * 0.5F,
                                        (minimum.Z + maximum.Z) * 0.5F};
            const float extent =
                std::max({maximum.X - minimum.X, maximum.Y - minimum.Y, maximum.Z - minimum.Z, 0.0001F});
            std::vector<PreviewVertex> projected;
            projected.reserve(vertices.size());
            float minimumX = std::numeric_limits<float>::max();
            float minimumY = std::numeric_limits<float>::max();
            float maximumX = std::numeric_limits<float>::lowest();
            float maximumY = std::numeric_limits<float>::lowest();
            for (const auto& vertex : vertices)
            {
                const float x = (vertex.Position.X - center.X) / extent;
                const float y = (vertex.Position.Y - center.Y) / extent;
                const float z = (vertex.Position.Z - center.Z) / extent;
                const float viewX = (x - z) * 0.70710678F;
                const float viewY = y * 0.8660254F - (x + z) * 0.25F;
                const float depth = (x + z) * 0.6123724F + y * 0.5F;
                const float diffuse = std::clamp(
                    vertex.Normal.X * -0.36F + vertex.Normal.Y * 0.78F + vertex.Normal.Z * -0.51F, -1.0F, 1.0F);
                projected.push_back({viewX, viewY, depth, 0.24F + std::max(diffuse, 0.0F) * 0.76F});
                minimumX = std::min(minimumX, viewX);
                minimumY = std::min(minimumY, viewY);
                maximumX = std::max(maximumX, viewX);
                maximumY = std::max(maximumY, viewY);
            }
            const float projectedWidth = std::max(maximumX - minimumX, 0.0001F);
            const float projectedHeight = std::max(maximumY - minimumY, 0.0001F);
            const float widthFloat = AsFloat(width);
            const float heightFloat = AsFloat(height);
            const float scale = std::min(widthFloat * 0.82F / projectedWidth, heightFloat * 0.82F / projectedHeight);
            for (auto& vertex : projected)
            {
                vertex.X = widthFloat * 0.5F + (vertex.X - (minimumX + maximumX) * 0.5F) * scale;
                vertex.Y = heightFloat * 0.52F - (vertex.Y - (minimumY + maximumY) * 0.5F) * scale;
            }

            std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4);
            FillPreviewBackground(result, width, height);
            std::vector<float> depthBuffer(static_cast<std::size_t>(width) * height,
                                           std::numeric_limits<float>::infinity());
            constexpr std::size_t maximumPreviewTriangles = 50000;
            const std::size_t triangleStride =
                std::max<std::size_t>(1, (indices.size() / 3 + maximumPreviewTriangles - 1) / maximumPreviewTriangles);
            for (std::size_t index = 0; index + 2 < indices.size(); index += 3 * triangleStride)
            {
                if (indices[index] >= projected.size() || indices[index + 1] >= projected.size() ||
                    indices[index + 2] >= projected.size())
                    continue;
                const auto first = projected[indices[index]];
                const auto second = projected[indices[index + 1]];
                const auto third = projected[indices[index + 2]];
                const float area =
                    (second.X - first.X) * (third.Y - first.Y) - (second.Y - first.Y) * (third.X - first.X);
                if (std::abs(area) < 0.0001F)
                    continue;
                const int left = std::max(0, static_cast<int>(std::floor(std::min({first.X, second.X, third.X}))));
                const int right = std::min(static_cast<int>(width) - 1,
                                           static_cast<int>(std::ceil(std::max({first.X, second.X, third.X}))));
                const int top = std::max(0, static_cast<int>(std::floor(std::min({first.Y, second.Y, third.Y}))));
                const int bottom = std::min(static_cast<int>(height) - 1,
                                            static_cast<int>(std::ceil(std::max({first.Y, second.Y, third.Y}))));
                for (int y = top; y <= bottom; ++y)
                    for (int x = left; x <= right; ++x)
                    {
                        const float sampleX = AsFloat(x) + 0.5F;
                        const float sampleY = AsFloat(y) + 0.5F;
                        const float firstWeight =
                            ((second.X - sampleX) * (third.Y - sampleY) - (second.Y - sampleY) * (third.X - sampleX)) /
                            area;
                        const float secondWeight =
                            ((third.X - sampleX) * (first.Y - sampleY) - (third.Y - sampleY) * (first.X - sampleX)) /
                            area;
                        const float thirdWeight = 1.0F - firstWeight - secondWeight;
                        if (firstWeight < -0.0001F || secondWeight < -0.0001F || thirdWeight < -0.0001F)
                            continue;
                        const float depth =
                            first.Depth * firstWeight + second.Depth * secondWeight + third.Depth * thirdWeight;
                        const auto pixel = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                        if (depth >= depthBuffer[pixel])
                            continue;
                        depthBuffer[pixel] = depth;
                        const float light = std::clamp(first.Light * firstWeight + second.Light * secondWeight +
                                                           third.Light * thirdWeight,
                                                       0.0F, 1.0F);
                        PutPixel(result, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                 static_cast<std::uint8_t>(36.0F + light * 87.0F),
                                 static_cast<std::uint8_t>(68.0F + light * 133.0F),
                                 static_cast<std::uint8_t>(91.0F + light * 151.0F));
                    }
            }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeScenePreview(const ThumbnailRequest& request,
                                                              const std::uint32_t width, const std::uint32_t height)
        {
            auto result = MakeIcon(width, height, {25, 35, 52}, {69, 142, 238}, ' ', request.Missing);
            const float widthFloat = AsFloat(width);
            const float heightFloat = AsFloat(height);
            const auto horizon = static_cast<int>(heightFloat * 0.64F);
            DrawLine(result, width, height, 12, horizon, static_cast<int>(width) - 12, horizon, {75, 132, 203});
            DrawLine(result, width, height, 14, horizon, static_cast<int>(widthFloat * 0.38F),
                     static_cast<int>(heightFloat * 0.38F), {112, 183, 245});
            DrawLine(result, width, height, static_cast<int>(widthFloat * 0.38F), static_cast<int>(heightFloat * 0.38F),
                     static_cast<int>(widthFloat * 0.58F), horizon, {112, 183, 245});
            DrawLine(result, width, height, static_cast<int>(widthFloat * 0.48F), horizon,
                     static_cast<int>(widthFloat * 0.70F), static_cast<int>(heightFloat * 0.29F), {84, 158, 229});
            DrawLine(result, width, height, static_cast<int>(widthFloat * 0.70F), static_cast<int>(heightFloat * 0.29F),
                     static_cast<int>(width) - 13, horizon, {84, 158, 229});
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeShaderPreview(const ThumbnailRequest& request,
                                                               const std::uint32_t width, const std::uint32_t height)
        {
            auto result = MakeIcon(width, height, {25, 42, 39}, {70, 202, 145}, ' ', request.Missing);
            const auto drawNode = [&](const int centerX, const int centerY, const std::array<std::uint8_t, 3> color)
            {
                for (int y = centerY - 6; y <= centerY + 6; ++y)
                    for (int x = centerX - 9; x <= centerX + 9; ++x)
                        if (x >= 0 && y >= 0 && x < static_cast<int>(width) && y < static_cast<int>(height))
                            PutPixel(result, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                     color[0], color[1], color[2]);
            };
            DrawLine(result, width, height, 28, 30, 68, 48, {91, 219, 168});
            DrawLine(result, width, height, 28, 66, 68, 48, {91, 219, 168});
            drawNode(24, 30, {52, 139, 111});
            drawNode(24, 66, {52, 139, 111});
            drawNode(72, 48, {69, 196, 139});
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeInputPreview(const ThumbnailRequest& request,
                                                              const std::uint32_t width, const std::uint32_t height)
        {
            auto result = MakeIcon(width, height, {38, 29, 53}, {157, 100, 237}, ' ', request.Missing);
            const float widthFloat = AsFloat(width);
            const float heightFloat = AsFloat(height);
            for (std::uint32_t y = height / 3; y < height * 2 / 3; ++y)
                for (std::uint32_t x = width / 5; x < width * 4 / 5; ++x)
                {
                    const float nx = (AsFloat(x) - widthFloat * 0.5F) / (widthFloat * 0.32F);
                    const float ny = (AsFloat(y) - heightFloat * 0.52F) / (heightFloat * 0.22F);
                    if (nx * nx + ny * ny < 1.0F)
                        PutPixel(result, width, x, y, 104, 70, 164);
                }
            DrawLine(result, width, height, 28, 49, 44, 49, {225, 211, 248});
            DrawLine(result, width, height, 36, 41, 36, 57, {225, 211, 248});
            for (int y = 43; y <= 49; ++y)
                for (int x = 66; x <= 72; ++x)
                    if ((x - 69) * (x - 69) + (y - 46) * (y - 46) <= 9)
                        PutPixel(result, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 231, 173,
                                 92);
            for (int y = 53; y <= 59; ++y)
                for (int x = 58; x <= 64; ++x)
                    if ((x - 61) * (x - 61) + (y - 56) * (y - 56) <= 9)
                        PutPixel(result, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 101, 207,
                                 174);
            return result;
        }

        [[nodiscard]] std::vector<std::byte> MakeAudioPreview(const ThumbnailRequest& request,
                                                              const std::uint32_t width, const std::uint32_t height)
        {
            auto result = MakeIcon(width, height, {22, 38, 46}, {58, 209, 174}, ' ', request.Missing);
            const auto asset = Keire::DynamicRefCast<const Keire::AudioClipAsset>(request.PreviewAsset);
            if (!asset)
                return result;
            const auto& clip = *asset->Clip();
            const auto center = static_cast<int>(height / 2);
            DrawLine(result, width, height, 8, center, static_cast<int>(width) - 9, center, {47, 103, 105});
            const auto columns = width > 16 ? width - 16 : 1;
            const auto columnsDouble = AsDouble(columns);
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                float amplitude = 0.0F;
                if (!clip.Samples.empty())
                {
                    const auto first =
                        static_cast<std::size_t>(AsDouble(column) / columnsDouble * AsDouble(clip.Samples.size()));
                    const auto last =
                        std::min(clip.Samples.size(), static_cast<std::size_t>(AsDouble(column + 1U) / columnsDouble *
                                                                               AsDouble(clip.Samples.size())) +
                                                          1U);
                    for (std::size_t sample = first; sample < last; ++sample)
                        amplitude = std::max(amplitude, std::abs(clip.Samples[sample]));
                }
                else if (!clip.EncodedSource.empty())
                {
                    const auto first = static_cast<std::size_t>(AsDouble(column) / columnsDouble *
                                                                AsDouble(clip.EncodedSource.size()));
                    const auto last = std::min(clip.EncodedSource.size(),
                                               static_cast<std::size_t>(AsDouble(column + 1U) / columnsDouble *
                                                                        AsDouble(clip.EncodedSource.size())) +
                                                   1U);
                    for (std::size_t sample = first; sample < last; ++sample)
                    {
                        const auto normalized =
                            AsFloat(std::abs(
                                static_cast<int>(std::to_integer<std::uint8_t>(clip.EncodedSource[sample])) - 128)) /
                            128.0F;
                        amplitude = std::max(amplitude, normalized);
                    }
                    amplitude *= 0.72F;
                }
                const auto halfHeight =
                    std::max(1, static_cast<int>(std::clamp(amplitude, 0.0F, 1.0F) * (AsFloat(height) * 0.34F)));
                const auto x = static_cast<int>(column + 8);
                DrawLine(result, width, height, x, center - halfHeight, x, center + halfHeight, {72, 231, 190});
            }
            return result;
        }
    } // namespace

    std::vector<std::byte> MakeFolderThumbnail(const std::uint32_t width, const std::uint32_t height,
                                               const bool missing)
    {
        auto pixels = MakeIcon(width, height, {35, 43, 58}, {238, 181, 68}, 'F', missing);
        const auto top = height / 3;
        const auto left = width / 6;
        const auto right = width - left;
        for (std::uint32_t y = top; y < height - height / 6; ++y)
            for (std::uint32_t x = left; x < right; ++x)
                PutPixel(pixels, width, x, y, 216, 154, 48);
        for (std::uint32_t y = top - height / 12; y < top; ++y)
            for (std::uint32_t x = left; x < width / 2; ++x)
                PutPixel(pixels, width, x, y, 238, 181, 68);
        return pixels;
    }

    std::vector<std::byte> MakeAssetFallbackThumbnail(const Keire::AssetTypeId type, const std::uint32_t width,
                                                      const std::uint32_t height, const bool missing)
    {
        if (type == Keire::ShaderGraphAsset::StaticType())
        {
            auto result = MakeIcon(width, height, {34, 35, 57}, {105, 151, 255}, 'S', missing);
            ApplyBadge(result, width, height, "SG");
            return result;
        }
        if (type == Keire::MaterialGraphAsset::StaticType())
        {
            auto result = MakeIcon(width, height, {47, 31, 48}, {213, 94, 199}, 'M', missing);
            ApplyBadge(result, width, height, "MG");
            return result;
        }
        if (type == Keire::ShaderGraphInstanceAsset::StaticType())
        {
            auto result = MakeIcon(width, height, {43, 33, 47}, {196, 111, 212}, 'M', missing);
            ApplyBadge(result, width, height, "MI");
            return result;
        }
        if (type == Keire::VfxEffectAsset::StaticType())
        {
            auto result = MakeIcon(width, height, {25, 37, 52}, {74, 181, 238}, 'V', missing);
            ApplyBadge(result, width, height, "FX");
            return result;
        }
        return MakeIcon(width, height, {40, 44, 52}, {130, 142, 162}, 'X', missing);
    }

    std::optional<bool> PrepareGeneratedAssetThumbnail(const Keire::Ref<Keire::AssetSystem>& assets,
                                                       const Keire::AssetSourceRecord& source,
                                                       ThumbnailRequest& request)
    {
        if (!assets)
            return std::nullopt;
        if (source.Type == Keire::VfxEffectAsset::StaticType())
        {
            const auto handle = assets->Load<Keire::VfxEffectAsset>(source.Id, Keire::AssetPriority::Low);
            request.PreviewAsset = handle.TryGetLoaded();
            request.Missing = handle.State() == Keire::AssetState::Failed;
            if (!request.PreviewAsset && request.Missing)
                request.PreviewAsset = handle.Get();
            return static_cast<bool>(request.PreviewAsset);
        }

        Keire::AssetId materialId;
        if (source.Type == Keire::MaterialAsset::StaticType())
        {
            materialId = source.Id;
        }
        else if (source.Type == Keire::MaterialGraphAsset::StaticType() ||
                 source.Type == Keire::ShaderGraphAsset::StaticType() ||
                 source.Type == Keire::ShaderGraphInstanceAsset::StaticType())
        {
            const auto preview = std::ranges::find_if(source.SubAssets,
                                                      [&](const Keire::AssetId subAsset)
                                                      {
                                                          const auto type = assets->TryGetType(subAsset);
                                                          return type && *type == Keire::MaterialAsset::StaticType();
                                                      });
            if (preview == source.SubAssets.end())
                return false;
            materialId = *preview;
        }
        else
        {
            return std::nullopt;
        }

        const auto handle = assets->Load<Keire::MaterialAsset>(materialId, Keire::AssetPriority::Low);
        const auto material = handle.TryGetLoaded();
        request.PreviewAsset = material;
        request.Missing = handle.State() == Keire::AssetState::Failed;
        if (!request.PreviewAsset && request.Missing)
            request.PreviewAsset = handle.Get();
        bool ready = static_cast<bool>(request.PreviewAsset);
        if (material && material->Definition().Shader)
        {
            const auto shaderHandle =
                assets->Load<Keire::ShaderAsset>(material->Definition().Shader, Keire::AssetPriority::Low);
            request.PreviewShader = shaderHandle.TryGetLoaded();
            if (!request.PreviewShader && shaderHandle.State() == Keire::AssetState::Failed)
                request.PreviewShader = shaderHandle.Get();
            ready = ready && static_cast<bool>(request.PreviewShader);
        }

        Keire::AssetId texture;
        if (material)
        {
            if (const auto found = material->Definition().Properties.find("MainTexture");
                found != material->Definition().Properties.end())
                if (const auto* id = std::get_if<Keire::AssetId>(&found->second))
                    texture = *id;
            if (request.PreviewShader)
            {
                for (const auto& property : request.PreviewShader->Definition().Properties)
                {
                    if (property.TextureSemantic != Keire::ShaderTextureSemantic::BaseColor)
                        continue;
                    if (const auto found = material->Definition().Properties.find(property.Name);
                        found != material->Definition().Properties.end())
                        if (const auto* id = std::get_if<Keire::AssetId>(&found->second))
                            texture = *id;
                    break;
                }
            }
        }
        if (texture)
        {
            const auto textureHandle = assets->Load<Keire::Texture2DAsset>(texture, Keire::AssetPriority::Low);
            request.PreviewTexture = textureHandle.TryGetLoaded();
            if (!request.PreviewTexture && textureHandle.State() == Keire::AssetState::Failed)
                request.PreviewTexture = textureHandle.Get();
            ready = ready && static_cast<bool>(request.PreviewTexture);
        }
        return ready;
    }

    class ThumbnailService::Impl final
    {
      public:
        struct ProviderRecord
        {
            std::uint32_t Version = 0;
            Provider Generate;
        };

        struct Job
        {
            ThumbnailRequest Request;
            std::string Key;
            std::uint64_t Generation = 0;
        };

        Impl(std::filesystem::path cacheDirectory, const std::size_t capacity, Keire::Ref<Keire::JobSystem> jobs)
            : CacheDirectory(std::move(cacheDirectory)), Capacity(capacity), OwnerThread(std::this_thread::get_id()),
              Scheduler(std::move(jobs))
        {
            if (Capacity == 0 || Capacity > 4096)
                throw std::invalid_argument("Thumbnail queue capacity must be in the range 1..4096.");
            if (!Scheduler)
            {
                Keire::JobSystemSpecification specification;
                specification.WorkerCount = 1;
                specification.BlockingWorkerCount = 1;
                Scheduler = Keire::CreateRef<Keire::JobSystem>(specification);
                OwnScheduler = true;
            }
            WorkScope = Scheduler->CreateScope("Editor thumbnails");
            std::filesystem::create_directories(CacheDirectory);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("ThumbnailService::") + operation +
                                       " must run on the owner thread.");
        }

        [[nodiscard]] std::string KeyFor(const ThumbnailRequest& request, const ProviderRecord& provider) const
        {
            auto extension = Lower(request.RelativePath.extension().string());
            if (!extension.empty() && extension.front() == '.')
                extension.erase(extension.begin());
            std::ranges::transform(extension, extension.begin(),
                                   [](const char input)
                                   {
                                       const auto character = static_cast<unsigned char>(input);
                                       return std::isalnum(character) ? static_cast<char>(character) : '_';
                                   });
            return request.Digest + "-" + std::to_string(provider.Version) + "-" + extension;
        }

        [[nodiscard]] std::filesystem::path CachePath(const std::string& key) const
        {
            return CacheDirectory / (key + ".rgba");
        }

        [[nodiscard]] std::vector<std::byte> ReadCache(const std::filesystem::path& path) const
        {
            constexpr auto expected = static_cast<std::size_t>(ThumbnailWidth) * ThumbnailHeight * 4;
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != expected)
                return {};
            std::ifstream input(path, std::ios::binary);
            std::vector<std::byte> result(expected);
            if (!input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
                return {};
            return result;
        }

        void WriteCache(const std::filesystem::path& path, const std::span<const std::byte> pixels) const
        {
            try
            {
                Keire::Detail::WriteFileAtomically(path, pixels);
            }
            catch (const std::exception&)
            {
                // Thumbnail persistence is an optional cache; generation still succeeds in memory.
            }
        }

        void Run(const Job& job, const ProviderRecord& provider, Keire::JobContext& context)
        {
            if (context.StopRequested())
                return;
            auto pixels = job.Request.Missing ? std::vector<std::byte>{} : ReadCache(CachePath(job.Key));
            if (pixels.empty() && !context.StopRequested())
            {
                try
                {
                    pixels = provider.Generate(job.Request, ThumbnailWidth, ThumbnailHeight);
                }
                catch (...)
                {
                    pixels = MakeIcon(ThumbnailWidth, ThumbnailHeight, {40, 32, 44}, {226, 72, 108}, 'X', true);
                }
                if (!job.Request.Missing &&
                    pixels.size() == static_cast<std::size_t>(ThumbnailWidth) * ThumbnailHeight * 4)
                    WriteCache(CachePath(job.Key), pixels);
            }
            std::scoped_lock lock(Mutex);
            Pending.erase(job.Request.Asset);
            if (!context.StopRequested() && job.Generation == Generation && !pixels.empty())
                Completed.push_back({job.Request.Asset, ThumbnailWidth, ThumbnailHeight, std::move(pixels)});
        }

        std::filesystem::path CacheDirectory;
        std::size_t Capacity = 0;
        std::thread::id OwnerThread;
        mutable std::mutex Mutex;
        std::unordered_map<std::string, ProviderRecord> Providers;
        std::deque<ThumbnailResult> Completed;
        std::unordered_set<Keire::AssetId> Pending;
        std::uint64_t Generation = 1;
        Keire::Ref<Keire::JobSystem> Scheduler;
        Keire::Ref<Keire::JobScope> WorkScope;
        bool OwnScheduler = false;
    };

    ThumbnailService::ThumbnailService(std::filesystem::path cacheDirectory, const std::size_t queueCapacity,
                                       Keire::Ref<Keire::JobSystem> jobs)
        : m_Impl(std::make_unique<Impl>(std::move(cacheDirectory), queueCapacity, std::move(jobs)))
    {
        const auto icon =
            [](const std::array<std::uint8_t, 3> background, const std::array<std::uint8_t, 3> accent, const char glyph)
        {
            return [=](const ThumbnailRequest& request, const auto width, const auto height)
            { return MakeIcon(width, height, background, accent, glyph, request.Missing); };
        };
        RegisterProvider(".keirematerial", 5, MakeMaterialPreview);
        RegisterProvider(".keireshadergraph", 1,
                         [](const ThumbnailRequest& request, const auto width, const auto height)
                         { return MakeShaderGraphPreview(request, width, height, "SG"); });
        RegisterProvider(".keirematerialgraph", 1,
                         [](const ThumbnailRequest& request, const auto width, const auto height)
                         { return MakeShaderGraphPreview(request, width, height, "MG"); });
        RegisterProvider(".keireshadergraphinstance", 1,
                         [](const ThumbnailRequest& request, const auto width, const auto height)
                         { return MakeShaderGraphPreview(request, width, height, "MI"); });
        RegisterProvider(".keirevfx", 1, MakeVfxPreview);
        RegisterProvider(".png", 4, MakeTexturePreview);
        RegisterProvider(".jpg", 4, MakeTexturePreview);
        RegisterProvider(".jpeg", 4, MakeTexturePreview);
        RegisterProvider(".tga", 4, MakeTexturePreview);
        RegisterProvider(".bmp", 4, MakeTexturePreview);
        RegisterProvider(".hdr", 1, MakeTexturePreview);
        RegisterProvider(".keiremesh", 5, MakeMeshPreview);
        RegisterProvider(".obj", 5, MakeMeshPreview);
        RegisterProvider(".fbx", 5, MakeMeshPreview);
        RegisterProvider(".gltf", 5, MakeMeshPreview);
        RegisterProvider(".glb", 5, MakeMeshPreview);
        RegisterProvider(".keirescene", 3, MakeScenePreview);
        RegisterProvider(".keireprefab", 1, MakePrefabPreview);
        RegisterProvider(".keireshader", 3, MakeShaderPreview);
        RegisterProvider(".hlsl", 3, MakeShaderPreview);
        RegisterProvider(".keireinput", 3, MakeInputPreview);
        RegisterProvider(".wav", 1, MakeAudioPreview);
        RegisterProvider(".ogg", 1, MakeAudioPreview);
        RegisterProvider(".flac", 1, MakeAudioPreview);
        RegisterProvider("*", 2, icon({40, 44, 52}, {130, 142, 162}, 'X'));
    }

    ThumbnailService::~ThumbnailService()
    {
        m_Impl->WorkScope->Cancel();
        m_Impl->WorkScope->Wait();
        if (m_Impl->OwnScheduler)
            m_Impl->Scheduler->Close();
    }

    void ThumbnailService::RegisterProvider(std::string extension, const std::uint32_t version, Provider provider)
    {
        m_Impl->RequireOwner("RegisterProvider");
        extension = Lower(std::move(extension));
        if (extension.empty() || version == 0 || !provider || m_Impl->Providers.contains(extension))
            throw std::invalid_argument("Thumbnail provider registration is invalid or duplicated.");
        m_Impl->Providers.emplace(std::move(extension), Impl::ProviderRecord{version, std::move(provider)});
    }

    bool ThumbnailService::Request(ThumbnailRequest request)
    {
        m_Impl->RequireOwner("Request");
        if (!request.Asset || request.Digest.empty())
            return false;
        const auto extension = Lower(request.RelativePath.extension().string());
        const auto provider =
            m_Impl->Providers.contains(extension) ? m_Impl->Providers.at(extension) : m_Impl->Providers.at("*");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Pending.contains(request.Asset) || m_Impl->Pending.size() >= m_Impl->Capacity)
            return false;
        m_Impl->Pending.insert(request.Asset);
        Impl::Job job{std::move(request), {}, m_Impl->Generation};
        const auto asset = job.Request.Asset;
        job.Key = m_Impl->KeyFor(job.Request, provider);
        try
        {
            (void)m_Impl->WorkScope->Submit(
                {.Name = "Generate thumbnail",
                 .Priority = Keire::JobPriority::Low,
                 .Class = Keire::JobClass::Blocking,
                 .Domain = Keire::JobDomain::Tooling},
                [implementation = m_Impl.get(), job = std::move(job), provider](Keire::JobContext& context) mutable
                { implementation->Run(job, provider, context); });
        }
        catch (...)
        {
            m_Impl->Pending.erase(asset);
            throw;
        }
        return true;
    }

    std::vector<ThumbnailResult> ThumbnailService::DrainCompleted(const std::size_t maximum)
    {
        m_Impl->RequireOwner("DrainCompleted");
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<ThumbnailResult> result;
        const auto count = std::min(maximum, m_Impl->Completed.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            result.push_back(std::move(m_Impl->Completed.front()));
            m_Impl->Completed.pop_front();
        }
        return result;
    }

    void ThumbnailService::CancelAll() noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        ++m_Impl->Generation;
        m_Impl->Completed.clear();
        m_Impl->Pending.clear();
    }

    std::size_t ThumbnailService::PendingCount() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Pending.size();
    }
} // namespace KeireEditor
