#pragma once

#include "Keire/Application.h"

#include <cstddef>
#include <memory>
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

class CommandLineError final : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// Implemented once by the client executable. KeireCore owns main(), exception
// handling, application lifetime, and the Run() call.
[[nodiscard]] std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments);
} // namespace Keire
