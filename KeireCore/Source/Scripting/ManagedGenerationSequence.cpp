#include "KeireInternal/Scripting/ManagedGenerationSequence.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    std::uint64_t NextManagedGeneration(const std::filesystem::path& outputRoot)
    {
        std::uint64_t highestGeneration = 0;
        const auto generations = outputRoot / "Generations";
        if (std::filesystem::is_directory(generations))
        {
            for (const auto& entry : std::filesystem::directory_iterator(generations))
            {
                if (!entry.is_directory())
                    continue;
                const auto name = entry.path().filename().string();
                try
                {
                    std::size_t consumed = 0;
                    const auto generation = static_cast<std::uint64_t>(std::stoull(name, &consumed));
                    if (consumed == name.size())
                        highestGeneration = std::max(highestGeneration, generation);
                }
                catch (const std::exception&)
                {
                }
            }
        }

        const auto manifest = outputRoot / "active-generation.json";
        if (std::filesystem::is_regular_file(manifest))
        {
            try
            {
                const auto document = nlohmann::json::parse(ReadTextFile(manifest, std::size_t{1} << 20U));
                highestGeneration = std::max(highestGeneration, document.value("generation", std::uint64_t{0}));
            }
            catch (const nlohmann::json::exception&)
            {
            }
        }

        if (highestGeneration == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Managed generation sequence is exhausted.");
        return highestGeneration + 1;
    }
} // namespace Keire::Detail
