#pragma once

#include "Keire/Core.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace KeireEditor
{
    struct SceneViewportCenteredStateLayout final
    {
        Keire::UiItemRect Reservation;
        Keire::UiPosition CreateAction;
        Keire::UiPosition OpenAction;
        bool ShowActions = false;
    };

    [[nodiscard]] inline SceneViewportCenteredStateLayout
    CalculateSceneViewportCenteredStateLayout(const Keire::UiItemRect viewport, const float contentWidth,
                                              const bool requestActions) noexcept
    {
        constexpr float inset = 8.0F;
        const float centerX = (viewport.Minimum.X + viewport.Maximum.X) * 0.5F;
        const float centerY = (viewport.Minimum.Y + viewport.Maximum.Y) * 0.5F;
        SceneViewportCenteredStateLayout result;
        result.Reservation = {{std::max(viewport.Minimum.X + inset, centerX - contentWidth * 0.5F - inset),
                               std::max(viewport.Minimum.Y + inset, centerY - 26.0F)},
                              {std::min(viewport.Maximum.X - inset, centerX + contentWidth * 0.5F + inset),
                               std::min(viewport.Maximum.Y - inset, centerY + 30.0F)}};
        result.CreateAction = {centerX - 36.0F, centerY + 42.0F};
        result.OpenAction = {centerX + 4.0F, centerY + 42.0F};
        result.ShowActions = requestActions && result.CreateAction.X >= viewport.Minimum.X + inset &&
                             result.OpenAction.X + 32.0F <= viewport.Maximum.X - inset &&
                             result.CreateAction.Y >= viewport.Minimum.Y + inset &&
                             result.CreateAction.Y + 28.0F <= viewport.Maximum.Y - inset;
        if (result.ShowActions)
        {
            result.Reservation.Minimum.X = std::min(result.Reservation.Minimum.X, centerX - 44.0F);
            result.Reservation.Maximum.X = std::max(result.Reservation.Maximum.X, centerX + 44.0F);
            result.Reservation.Maximum.Y = std::max(result.Reservation.Maximum.Y, centerY + 78.0F);
        }
        return result;
    }

    [[nodiscard]] inline bool CanPlaceSceneViewportStatus(const Keire::UiItemRect viewport) noexcept
    {
        constexpr float toolbarBottomInset = 36.0F;
        constexpr float separation = 8.0F;
        constexpr float statusTopFromBottom = 27.0F;
        return viewport.Size().Height >= toolbarBottomInset + separation + statusTopFromBottom;
    }

    [[nodiscard]] inline bool SceneViewportRectanglesOverlap(const Keire::UiItemRect left,
                                                             const Keire::UiItemRect right) noexcept
    {
        return left.Minimum.X < right.Maximum.X && left.Maximum.X > right.Minimum.X &&
               left.Minimum.Y < right.Maximum.Y && left.Maximum.Y > right.Minimum.Y;
    }

    [[nodiscard]] inline std::optional<Keire::UiItemRect>
    PlaceViewportPerformanceOverlay(const Keire::UiItemRect viewport, const Keire::UiSize size, const float topInset,
                                    const bool preferRight, const std::optional<Keire::UiItemRect> occupied = {},
                                    const float bottomInset = 12.0F) noexcept
    {
        constexpr float inset = 12.0F;
        const auto fits = [&](const Keire::UiItemRect rectangle)
        {
            return rectangle.Minimum.X >= viewport.Minimum.X + inset &&
                   rectangle.Maximum.X <= viewport.Maximum.X - inset &&
                   rectangle.Minimum.Y >= viewport.Minimum.Y + topInset &&
                   rectangle.Maximum.Y <= viewport.Maximum.Y - bottomInset;
        };
        const auto available = [&](const Keire::UiItemRect rectangle)
        { return fits(rectangle) && (!occupied || !SceneViewportRectanglesOverlap(rectangle, *occupied)); };
        const float top = viewport.Minimum.Y + topInset;
        const Keire::UiItemRect left{{viewport.Minimum.X + inset, top},
                                     {viewport.Minimum.X + inset + size.Width, top + size.Height}};
        const Keire::UiItemRect right{{viewport.Maximum.X - inset - size.Width, top},
                                      {viewport.Maximum.X - inset, top + size.Height}};
        if (preferRight)
        {
            if (available(right))
                return right;
            if (available(left))
                return left;
        }
        else
        {
            if (available(left))
                return left;
            if (available(right))
                return right;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<Keire::UiItemRect>
    PlaceSceneCameraPreview(const Keire::UiItemRect viewport) noexcept
    {
        constexpr float aspect = 16.0F / 9.0F;
        constexpr float minimumWidth = 160.0F;
        constexpr float maximumWidth = 320.0F;
        constexpr float horizontalInset = 12.0F;
        constexpr float topInset = 48.0F;
        constexpr float bottomInset = 34.0F;
        const float availableHeight = viewport.Size().Height - topInset - bottomInset;
        const float widthLimit =
            std::min({maximumWidth, viewport.Size().Width - horizontalInset * 2.0F, availableHeight * aspect});
        if (widthLimit < minimumWidth)
            return std::nullopt;
        const float width = std::clamp(viewport.Size().Width * 0.30F, minimumWidth, widthLimit);
        const float height = width / aspect;
        return Keire::UiItemRect{
            {viewport.Maximum.X - width - horizontalInset, viewport.Maximum.Y - height - bottomInset},
            {viewport.Maximum.X - horizontalInset, viewport.Maximum.Y - bottomInset}};
    }

    struct SceneViewportRightToolbarLayout final
    {
        Keire::UiItemRect Rectangle;
        std::size_t ButtonCount = 0;
        bool ShowProjection = false;
        bool ShowAxes = false;
        bool ShowCameraPreview = false;
        bool ShowOcclusionVisibility = false;
        bool ShowOcclusionMetadata = false;
    };

    [[nodiscard]] inline SceneViewportRightToolbarLayout
    CalculateSceneViewportRightToolbarLayout(const Keire::UiItemRect viewport,
                                             const Keire::UiItemRect leftToolbar) noexcept
    {
        constexpr float buttonSize = 28.0F;
        constexpr float buttonGap = 3.0F;
        constexpr float viewportPadding = 8.0F;
        constexpr float toolbarSeparation = 3.0F;
        constexpr std::size_t maximumButtons = 7;

        const float right = viewport.Maximum.X - viewportPadding;
        const float availableWidth = right - leftToolbar.Maximum.X - toolbarSeparation;
        const auto capacity = availableWidth >= buttonSize
                                  ? std::min(maximumButtons, static_cast<std::size_t>((availableWidth + buttonGap) /
                                                                                      (buttonSize + buttonGap)))
                                  : std::size_t{0};
        SceneViewportRightToolbarLayout result;
        result.ShowProjection = capacity >= 1U;
        result.ShowAxes = capacity >= 5U;
        result.ShowCameraPreview = capacity >= 2U;
        result.ShowOcclusionVisibility = capacity >= (result.ShowAxes ? 6U : 3U);
        result.ShowOcclusionMetadata = capacity >= (result.ShowAxes ? 7U : 4U);
        result.ButtonCount = static_cast<std::size_t>(result.ShowProjection) +
                             (result.ShowAxes ? std::size_t{3} : std::size_t{0}) +
                             static_cast<std::size_t>(result.ShowCameraPreview) +
                             static_cast<std::size_t>(result.ShowOcclusionVisibility) +
                             static_cast<std::size_t>(result.ShowOcclusionMetadata);
        if (result.ButtonCount == 0U)
            return result;
        const float width = buttonSize * static_cast<float>(result.ButtonCount) +
                            buttonGap * static_cast<float>(result.ButtonCount - 1U);
        result.Rectangle = {{right - width, viewport.Minimum.Y + viewportPadding},
                            {right, viewport.Minimum.Y + viewportPadding + buttonSize}};
        return result;
    }

    [[nodiscard]] inline std::optional<Keire::UiItemRect>
    PlaceSceneOcclusionDiagnostics(const Keire::UiItemRect viewport, const float maximumY,
                                   const std::optional<Keire::UiItemRect> occupied = {}) noexcept
    {
        constexpr float width = 416.0F;
        constexpr float height = 112.0F;
        constexpr float gap = 8.0F;
        constexpr float minimumYInset = 48.0F;
        const auto fits = [&](const Keire::UiItemRect rectangle)
        {
            return rectangle.Minimum.X >= viewport.Minimum.X + gap && rectangle.Maximum.X <= viewport.Maximum.X - gap &&
                   rectangle.Minimum.Y >= viewport.Minimum.Y + minimumYInset && rectangle.Maximum.Y <= maximumY;
        };
        Keire::UiItemRect candidate{{viewport.Maximum.X - width - gap, maximumY - height},
                                    {viewport.Maximum.X - gap, maximumY}};
        if (!fits(candidate))
            return std::nullopt;
        if (!occupied || !SceneViewportRectanglesOverlap(candidate, *occupied))
            return candidate;

        candidate.Maximum.X = occupied->Minimum.X - gap;
        candidate.Minimum.X = candidate.Maximum.X - width;
        if (fits(candidate) && !SceneViewportRectanglesOverlap(candidate, *occupied))
            return candidate;

        candidate = {{viewport.Maximum.X - width - gap, occupied->Maximum.Y + gap},
                     {viewport.Maximum.X - gap, occupied->Maximum.Y + gap + height}};
        if (fits(candidate) && !SceneViewportRectanglesOverlap(candidate, *occupied))
            return candidate;

        candidate = {{viewport.Maximum.X - width - gap, occupied->Minimum.Y - gap - height},
                     {viewport.Maximum.X - gap, occupied->Minimum.Y - gap}};
        if (fits(candidate) && !SceneViewportRectanglesOverlap(candidate, *occupied))
            return candidate;
        return std::nullopt;
    }
} // namespace KeireEditor
