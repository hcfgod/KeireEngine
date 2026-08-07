#include "KeireHub/HubDesignTokens.h"

#include "Keire/Window.h"

namespace KeireHub
{
    HubDesignTokens HubDesignTokens::For(const HubAppearance appearance, const bool systemPrefersDark) noexcept
    {
        const bool dark =
            appearance == HubAppearance::Dark || (appearance == HubAppearance::System && systemPrefersDark);
        if (dark)
        {
            return {.PrimaryText = {0.93F, 0.95F, 0.98F, 1.0F},
                    .SecondaryText = {0.59F, 0.64F, 0.72F, 1.0F},
                    .MutedText = {0.44F, 0.49F, 0.57F, 1.0F},
                    .Accent = {0.24F, 0.50F, 0.94F, 1.0F},
                    .Success = {0.28F, 0.78F, 0.52F, 1.0F},
                    .Warning = {0.96F, 0.69F, 0.25F, 1.0F},
                    .Danger = {0.95F, 0.36F, 0.40F, 1.0F},
                    .Canvas = {0.050F, 0.056F, 0.070F, 1.0F},
                    .Surface = {0.075F, 0.083F, 0.102F, 1.0F},
                    .Elevated = {0.105F, 0.116F, 0.142F, 1.0F},
                    .Border = {0.18F, 0.20F, 0.24F, 1.0F},
                    .Shadow = {0.0F, 0.0F, 0.0F, 0.30F}};
        }
        return {.PrimaryText = {0.10F, 0.12F, 0.16F, 1.0F},
                .SecondaryText = {0.30F, 0.34F, 0.42F, 1.0F},
                .MutedText = {0.45F, 0.48F, 0.54F, 1.0F},
                .Accent = {0.08F, 0.38F, 0.82F, 1.0F},
                .Success = {0.08F, 0.55F, 0.31F, 1.0F},
                .Warning = {0.76F, 0.43F, 0.04F, 1.0F},
                .Danger = {0.78F, 0.16F, 0.20F, 1.0F},
                .Canvas = {0.89F, 0.91F, 0.94F, 1.0F},
                .Surface = {0.94F, 0.95F, 0.97F, 1.0F},
                .Elevated = {1.0F, 1.0F, 1.0F, 1.0F},
                .Border = {0.78F, 0.80F, 0.84F, 1.0F},
                .Shadow = {0.08F, 0.10F, 0.14F, 0.16F}};
    }

    bool HubSystemPrefersDark() noexcept { return Keire::GetSystemColorScheme() != Keire::SystemColorScheme::Light; }

    Keire::UiTheme ResolveHubUiTheme(const HubAppearance appearance) noexcept
    {
        return appearance == HubAppearance::Light || (appearance == HubAppearance::System && !HubSystemPrefersDark())
                   ? Keire::UiTheme::Light
                   : Keire::UiTheme::Dark;
    }
} // namespace KeireHub
