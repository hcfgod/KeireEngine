#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    namespace Detail
    {
        void RequireCompiledVfxSystems(const Keire::VfxEffectDefinition& definition, Keire::VfxBackend backend);
        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path);
        [[nodiscard]] std::string FormatAssetDiagnostic(const Keire::AssetImportDiagnostic& diagnostic);
        void WriteBytesAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes);
        [[nodiscard]] bool IsCSharpIdentifier(std::string_view value);
        [[nodiscard]] std::vector<std::byte> TextBytes(std::string_view text);
        [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate);
    } // namespace Detail
} // namespace KeireEditor
