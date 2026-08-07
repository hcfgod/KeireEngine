#pragma once

#include <filesystem>

#include <Coral/HostInstance.hpp>

#include <string>

namespace Keire::Detail
{
    [[nodiscard]] Coral::HostSettings CreateCoralHostSettings(std::string coralDirectory);
}
