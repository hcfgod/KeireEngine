#pragma once

#include "Keire/Ui.h"

namespace Keire::Detail
{
    void UiBackendNewFrame();
    [[nodiscard]] bool UiBackendKeyDown(UiKey key) noexcept;
    [[nodiscard]] bool UiBackendKeyPressed(UiKey key) noexcept;
    void UiBackendRequestTextInput() noexcept;
    [[nodiscard]] std::string UiBackendTextInput();
} // namespace Keire::Detail
