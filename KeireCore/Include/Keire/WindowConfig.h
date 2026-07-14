#pragma once

#include "Keire/Api.h"
#include "Keire/Window.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace Keire
{
    class KEIRE_API ConfigurationError final : public std::runtime_error
    {
      public:
        ConfigurationError(std::filesystem::path path, std::string location, std::string diagnostic);

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }
        [[nodiscard]] const std::string& Location() const noexcept { return m_Location; }
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        std::filesystem::path m_Path;
        std::string m_Location;
        std::string m_Diagnostic;
    };

    [[nodiscard]] KEIRE_API WindowSpecification LoadWindowSpecification(const std::filesystem::path& path);
} // namespace Keire
