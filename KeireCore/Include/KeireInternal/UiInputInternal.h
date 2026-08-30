#pragma once

#include "Keire/Ui.h"

namespace Keire::Detail
{
    [[nodiscard]] bool UiBackendKeyDown(UiKey key) noexcept;
    [[nodiscard]] bool UiBackendKeyPressed(UiKey key) noexcept;
    void UiBackendRequestTextInput() noexcept;
    [[nodiscard]] std::string UiBackendTextInput();
} // namespace Keire::Detail
