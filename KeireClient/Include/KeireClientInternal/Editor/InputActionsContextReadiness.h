#pragma once

#include "Keire/Assets/Asset.h"

#include <cstdint>
#include <optional>

namespace KeireEditor::Detail
{
    enum class InputActionsContextReadiness : std::uint8_t
    {
        WaitingForCatalog,
        Ready,
        WrongType
    };

    [[nodiscard]] constexpr InputActionsContextReadiness
    EvaluateInputActionsContextReadiness(const std::optional<Keire::AssetTypeId> mountedType,
                                         const Keire::AssetTypeId expectedType) noexcept
    {
        if (!mountedType)
            return InputActionsContextReadiness::WaitingForCatalog;
        return *mountedType == expectedType ? InputActionsContextReadiness::Ready
                                            : InputActionsContextReadiness::WrongType;
    }
} // namespace KeireEditor::Detail
