#pragma once

#include "Keire/Application.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace Keire
{
    class KEIRE_API ApplicationCommandLineArguments final
    {
      public:
        ApplicationCommandLineArguments(int count, char* const* values) noexcept;

        [[nodiscard]] std::size_t Size() const noexcept { return m_Count; }
        [[nodiscard]] bool Empty() const noexcept { return m_Count == 0; }
        [[nodiscard]] std::string_view operator[](std::size_t index) const noexcept;
        [[nodiscard]] std::string_view Executable() const noexcept;

      private:
        std::size_t m_Count = 0;
        char* const* m_Values = nullptr;
    };

    class KEIRE_API CommandLineError final : public std::runtime_error
    {
      public:
        using std::runtime_error::runtime_error;
    };

    struct ApplicationCommandLineOption
    {
        std::string_view Syntax;
        std::string_view Description;
    };

    struct ApplicationCommandLineDescription
    {
        std::string_view UsageSuffix;
        std::span<const ApplicationCommandLineOption> Options;
        // Optional hidden/preflight commands run inside the real client executable before Application construction.
        std::optional<int> (*HandleWithoutApplication)(const ApplicationCommandLineArguments&) = nullptr;
    };

    // Implemented once by a managed client executable. The description must refer
    // to static storage so KeireCore can render help without initializing services.
    [[nodiscard]] ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept;

    // Implemented once by a managed client executable. KeireCore owns main(),
    // exception handling, application lifetime, and the Run() call.
    [[nodiscard]] std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments);
} // namespace Keire
