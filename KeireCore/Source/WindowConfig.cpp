#include "Keire/WindowConfig.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace Keire
{
namespace
{
constexpr std::uintmax_t MaximumConfigurationBytes = 1024U * 1024U;
constexpr std::uint32_t MaximumDimension = 16384;

std::string BuildConfigurationMessage(const std::filesystem::path& path, const std::string& location,
                                      const std::string& diagnostic)
{
    return "Configuration '" + path.string() + "' at " + location + ": " + diagnostic;
}

[[noreturn]] void Fail(const std::filesystem::path& path, std::string location, std::string diagnostic)
{
    throw ConfigurationError(path, std::move(location), std::move(diagnostic));
}

void RejectUnknownKeys(const nlohmann::json& object, const std::unordered_set<std::string>& allowed,
                       const std::filesystem::path& path, const std::string& location)
{
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator)
    {
        if (!allowed.contains(iterator.key()))
        {
            Fail(path, location + "/" + iterator.key(), "unknown key");
        }
    }
}

std::uint32_t ReadDimension(const nlohmann::json& window, const char* key, const std::uint32_t fallback,
                            const std::filesystem::path& path)
{
    const auto iterator = window.find(key);
    if (iterator == window.end())
    {
        return fallback;
    }
    if (!iterator->is_number_integer() || iterator->is_number_float())
    {
        Fail(path, std::string("/window/") + key, "expected an integer");
    }
    const auto value = iterator->get<std::int64_t>();
    if (value < 1 || value > MaximumDimension)
    {
        Fail(path, std::string("/window/") + key, "must be in the range 1..16384");
    }
    return static_cast<std::uint32_t>(value);
}

bool ReadBoolean(const nlohmann::json& window, const char* key, const bool fallback, const std::filesystem::path& path)
{
    const auto iterator = window.find(key);
    if (iterator == window.end())
    {
        return fallback;
    }
    if (!iterator->is_boolean())
    {
        Fail(path, std::string("/window/") + key, "expected a boolean");
    }
    return iterator->get<bool>();
}
} // namespace

ConfigurationError::ConfigurationError(std::filesystem::path path, std::string location, std::string diagnostic)
    : std::runtime_error(BuildConfigurationMessage(path, location, diagnostic)), m_Path(std::move(path)),
      m_Location(std::move(location)), m_Diagnostic(std::move(diagnostic))
{
}

WindowSpecification LoadWindowSpecification(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        Fail(path, "/", "unable to open file");
    }
    if (size > MaximumConfigurationBytes)
    {
        Fail(path, "/", "file exceeds the 1 MiB limit");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        Fail(path, "/", "unable to open file");
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty())
    {
        stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!stream)
        {
            Fail(path, "/", "unable to read file");
        }
    }

    std::vector<std::unordered_set<std::string>> objectKeys;
    const auto duplicateRejector =
        [&path, &objectKeys](int, nlohmann::json::parse_event_t event, nlohmann::json& parsed)
    {
        if (event == nlohmann::json::parse_event_t::object_start)
        {
            objectKeys.emplace_back();
        }
        else if (event == nlohmann::json::parse_event_t::key)
        {
            const auto key = parsed.get<std::string>();
            if (!objectKeys.back().insert(key).second)
            {
                Fail(path, "/" + key, "duplicate key");
            }
        }
        else if (event == nlohmann::json::parse_event_t::object_end)
        {
            objectKeys.pop_back();
        }
        return true;
    };

    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(contents, duplicateRejector, true, false);
    }
    catch (const ConfigurationError&)
    {
        throw;
    }
    catch (const nlohmann::json::parse_error& exception)
    {
        Fail(path, "/", "invalid JSON or UTF-8 near byte " + std::to_string(exception.byte));
    }

    if (!document.is_object())
    {
        Fail(path, "/", "expected an object");
    }
    RejectUnknownKeys(document, {"window"}, path, "");
    const auto windowIterator = document.find("window");
    if (windowIterator == document.end() || !windowIterator->is_object())
    {
        Fail(path, "/window", "expected an object");
    }
    const auto& window = *windowIterator;
    RejectUnknownKeys(window,
                      {"title", "width", "height", "resizable", "highPixelDensity", "visible", "maximized", "mode"},
                      path, "/window");

    WindowSpecification specification;
    if (const auto title = window.find("title"); title != window.end())
    {
        if (!title->is_string())
        {
            Fail(path, "/window/title", "expected a string");
        }
        specification.Title = title->get<std::string>();
        if (specification.Title.empty() || specification.Title.size() > 1024)
        {
            Fail(path, "/window/title", "must contain 1..1024 UTF-8 bytes");
        }
    }
    specification.Width = ReadDimension(window, "width", specification.Width, path);
    specification.Height = ReadDimension(window, "height", specification.Height, path);
    specification.Resizable = ReadBoolean(window, "resizable", specification.Resizable, path);
    specification.HighPixelDensity = ReadBoolean(window, "highPixelDensity", specification.HighPixelDensity, path);
    specification.Visible = ReadBoolean(window, "visible", specification.Visible, path);
    specification.Maximized = ReadBoolean(window, "maximized", specification.Maximized, path);

    if (const auto mode = window.find("mode"); mode != window.end())
    {
        if (!mode->is_string())
        {
            Fail(path, "/window/mode", "expected a string");
        }
        const auto value = mode->get<std::string>();
        if (value == "windowed")
        {
            specification.Mode = WindowMode::Windowed;
        }
        else if (value == "borderlessFullscreen")
        {
            specification.Mode = WindowMode::BorderlessFullscreen;
        }
        else
        {
            Fail(path, "/window/mode", "unsupported mode; expected 'windowed' or 'borderlessFullscreen'");
        }
    }
    if (specification.Maximized && specification.Mode == WindowMode::BorderlessFullscreen)
    {
        Fail(path, "/window/maximized", "cannot be true in borderless fullscreen mode");
    }
    return specification;
}
} // namespace Keire
