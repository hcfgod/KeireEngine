#include "KeireClient/Editor/ManagedAssemblyEditing.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireInternal/FileSystem.h"
#include <algorithm>
#include <stdexcept>

namespace KeireEditor
{
    void SaveManagedAssemblySettings(const std::filesystem::path& path, const std::span<const std::byte> bytes,
                                     const std::span<const std::byte> expected)
    {
        if (path.extension() == ".keireasm")
            (void)Keire::ManagedAssemblyAsset::Decode(bytes);
        else if (path.extension() == ".asmref")
        {
            if (Keire::ManagedAssemblyReferenceAsset::Decode(bytes)->Reference().empty())
                throw std::invalid_argument("Choose an assembly before saving its reference.");
        }
        else
            throw std::invalid_argument("Only assembly definitions and references can be saved here.");
        const auto current = Keire::Detail::ReadTextFile(path, 1024U * 1024U);
        if (!std::ranges::equal(std::as_bytes(std::span(current)), expected))
            throw std::runtime_error("Assembly source changed on disk. Revert to reload it before applying edits.");
        Detail::WriteBytesAtomically(path, bytes);
    }
} // namespace KeireEditor
