#include "Keire/WindowConfig.h"

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
class TemporaryConfiguration
{
  public:
    explicit TemporaryConfiguration(const std::string& contents)
    {
        static std::atomic<unsigned> sequence = 0;
        m_Path = std::filesystem::temp_directory_path() /
                 ("keire-window-config-" + std::to_string(sequence.fetch_add(1)) + ".json");
        std::ofstream stream(m_Path, std::ios::binary);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    ~TemporaryConfiguration()
    {
        std::error_code error;
        std::filesystem::remove(m_Path, error);
    }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

  private:
    std::filesystem::path m_Path;
};
} // namespace

TEST_CASE("Window configuration accepts complete, partial, and Unicode documents")
{
    TemporaryConfiguration partial(R"({"window":{"title":"Kéire \"Editor\"","width":800}})");
    const auto specification = Keire::LoadWindowSpecification(partial.Path());
    CHECK(specification.Title == "Kéire \"Editor\"");
    CHECK(specification.Width == 800);
    CHECK(specification.Height == 720);
    CHECK(specification.Resizable);

    TemporaryConfiguration complete(
        R"({"window":{"title":"Client","width":1920,"height":1080,"resizable":false,"highPixelDensity":false,"visible":false,"maximized":false,"mode":"borderlessFullscreen"}})");
    const auto fullscreen = Keire::LoadWindowSpecification(complete.Path());
    CHECK(fullscreen.Mode == Keire::WindowMode::BorderlessFullscreen);
    CHECK_FALSE(fullscreen.Visible);
}

TEST_CASE("Window configuration rejects structural and semantic errors")
{
    const auto reject = [](const std::string& contents)
    {
        TemporaryConfiguration configuration(contents);
        CHECK_THROWS_AS((void)Keire::LoadWindowSpecification(configuration.Path()), Keire::ConfigurationError);
    };
    reject("[]");
    reject(R"({})");
    reject(R"({"unknown":1,"window":{}})");
    reject(R"({"window":{"unknown":true}})");
    reject(R"({"window":{"width":"wide"}})");
    reject(R"({"window":{"width":0}})");
    reject(R"({"window":{"height":16385}})");
    reject(R"({"window":{"title":""}})");
    reject(R"({"window":{"mode":"exclusive"}})");
    reject(R"({"window":{"mode":"borderlessFullscreen","maximized":true}})");
    reject(R"({"window":{"title":"a","title":"b"}})");
    reject(std::string("{\"window\":{\"title\":\"") + static_cast<char>(0xFF) + "\"}}");
}

TEST_CASE("Window configuration reports paths and enforces its size cap")
{
    TemporaryConfiguration invalid(R"({"window":{"width":0}})");
    try
    {
        (void)Keire::LoadWindowSpecification(invalid.Path());
        FAIL("expected ConfigurationError");
    }
    catch (const Keire::ConfigurationError& error)
    {
        CHECK(error.Path() == invalid.Path());
        CHECK(error.Location() == "/window/width");
    }

    TemporaryConfiguration oversized(std::string(1024U * 1024U + 1U, ' '));
    CHECK_THROWS_AS((void)Keire::LoadWindowSpecification(oversized.Path()), Keire::ConfigurationError);
}

TEST_CASE("Missing explicit configuration is an error")
{
    const auto missing = std::filesystem::temp_directory_path() / "keire-definitely-missing-client.json";
    std::error_code error;
    std::filesystem::remove(missing, error);
    CHECK_THROWS_AS((void)Keire::LoadWindowSpecification(missing), Keire::ConfigurationError);
}
