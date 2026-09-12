#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Undo.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace KeireEditor
{
    struct MaterialSourceSnapshot final
    {
        Keire::AssetId Asset;
        std::filesystem::path RelativePath;
        std::vector<std::byte> Source;
        bool operator==(const MaterialSourceSnapshot&) const = default;
    };

    /// Excludes import workers and publishes all reviewed source bytes with recoverable rollback.
    void PublishMaterialSelection(const std::filesystem::path& root, std::span<const MaterialSourceSnapshot> before,
                                  std::span<const MaterialSourceSnapshot> after);

    using MaterialSelectionSourceWriter =
        std::function<void(std::span<const MaterialSourceSnapshot>, std::span<const MaterialSourceSnapshot>)>;

    [[nodiscard]] std::unique_ptr<Keire::UndoCommand>
    CreateMaterialSelectionEdit(std::vector<MaterialSourceSnapshot> before, std::vector<MaterialSourceSnapshot> after,
                                std::uint64_t editSerial, MaterialSelectionSourceWriter apply,
                                Keire::UndoAvailability available = {});
} // namespace KeireEditor
