#pragma once

#include "KeireHubRuntime/HubError.h"

#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub
{
    class AccountSessionStore final
    {
      public:
        explicit AccountSessionStore(std::filesystem::path path);

        [[nodiscard]] bool PersistentStorageAvailable() const noexcept;
        [[nodiscard]] HubResult<std::optional<std::string>> LoadRefreshToken() const;
        [[nodiscard]] HubStatus SaveRefreshToken(std::string_view refreshToken) const;
        [[nodiscard]] HubStatus Clear() const;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        std::filesystem::path m_Path;
    };
} // namespace KeireHub
