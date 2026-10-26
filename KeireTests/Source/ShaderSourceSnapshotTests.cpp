#include "KeireInternal/Assets/ShaderSourceSnapshot.h"

#include "KeireInternal/Assets/AssetInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] std::string Text(const Keire::Detail::ShaderSourceFile& file)
    {
        return {reinterpret_cast<const char*>(file.Bytes.data()), file.Bytes.size()};
    }

    struct SourceFixture final
    {
        std::map<std::filesystem::path, std::string> Files;
        std::map<std::filesystem::path, std::size_t> Reads;
        bool RejectRepeatedReads = true;

        [[nodiscard]] Keire::AssetImportContext Context()
        {
            Keire::AssetImportContext context;
            context.ReadProjectFile = [this](const std::filesystem::path& path)
            {
                const auto count = ++Reads[path];
                if (RejectRepeatedReads && count > 1U)
                {
                    throw std::runtime_error("Source changed after first capture.");
                }
                const auto found = Files.find(path);
                if (found == Files.end())
                {
                    throw std::runtime_error("Missing source.");
                }
                return Bytes(found->second);
            };
            return context;
        }
    };
} // namespace

TEST_CASE("shader snapshot hashes and stages each successfully resolved path from one read")
{
    SourceFixture fixture;
    fixture.Files = {{"Assets/main.hlsl", "#include \"one.hlsl\"\n#include \"two.hlsl\"\n"},
                     {"Assets/one.hlsl", "#include \"shared.hlsl\"\n"},
                     {"Assets/two.hlsl", "#include \"shared.hlsl\"\n"},
                     {"Assets/shared.hlsl", "float retained = 1;\n"}};
    const auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", {});
    REQUIRE(captured.size() == 4U);
    for (const auto& file : captured)
    {
        CHECK(fixture.Reads.at(file.Dependency.RelativePath) == 1U);
        CHECK(file.Dependency.Digest == Keire::Detail::DigestToString(Keire::Detail::Sha256(file.Bytes)));
        CHECK(Text(file) == fixture.Files.at(file.Dependency.RelativePath));
    }
    fixture.Files["Assets/shared.hlsl"] = "#error later edit\n";
    const auto shared = std::ranges::find(captured, std::filesystem::path("Assets/shared.hlsl"),
                                          [](const auto& file) { return file.Dependency.RelativePath; });
    REQUIRE(shared != captured.end());
    CHECK(Text(*shared) == "float retained = 1;\n");
}

TEST_CASE("shader snapshot preserves local-file and ordered include-root precedence")
{
    SourceFixture fixture;
    fixture.Files = {{"Assets/main.hlsl", "#include <common.hlsl>\n"},
                     {"Assets/common.hlsl", "float local;\n"},
                     {"First/common.hlsl", "float first;\n"},
                     {"Second/common.hlsl", "float second;\n"}};
    const std::array<std::filesystem::path, 2> roots{"First", "Second"};
    auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", roots);
    REQUIRE(captured.size() == 2U);
    CHECK(captured.back().Dependency.RelativePath == "Assets/common.hlsl");
    fixture.Files.erase("Assets/common.hlsl");
    fixture.Reads.clear();
    captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", roots);
    CHECK(captured.back().Dependency.RelativePath == "First/common.hlsl");
    CHECK_FALSE(fixture.Reads.contains("Second/common.hlsl"));
}

TEST_CASE("shader snapshot retains project-root-qualified include lookup without duplicate reads")
{
    SourceFixture fixture;
    fixture.Files = {{"Generated/main.hlsl", "#include \"Assets/Shared/common.hlsl\"\n"},
                     {"Assets/Shared/common.hlsl", "float shared;\n"}};
    const std::array<std::filesystem::path, 1> roots{"Assets/Shared"};
    const auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Generated/main.hlsl", roots);
    REQUIRE(captured.size() == 2U);
    CHECK(captured.back().Dependency.RelativePath == "Assets/Shared/common.hlsl");
    CHECK(fixture.Reads.at("Assets/Shared/common.hlsl") == 1U);
}

TEST_CASE("shader snapshot ignores commented includes and recognizes spliced literal directives")
{
    SourceFixture fixture;
    fixture.Files = {{"Assets/main.hlsl", "/*\n#include \"missing.hlsl\"\n*/\n"
                                          "// comment \\\n#include \"also-missing.hlsl\"\n"
                                          "# /* between tokens */ include \\\r\n\"shared.hlsl\" // trailing comment\n"},
                     {"Assets/shared.hlsl", "float shared;\n"}};
    const auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", {});
    REQUIRE(captured.size() == 2U);
    CHECK(fixture.Reads.size() == 2U);
    CHECK(captured.back().Dependency.RelativePath == "Assets/shared.hlsl");
}

TEST_CASE("shader snapshot rejects malformed or unsupported include expressions before compiler launch")
{
    for (const auto directive :
         {"#include HEADER\n", "#include \"unterminated\n", "#include <>\n", "#include \"one.hlsl\" unexpected\n"})
    {
        CAPTURE(directive);
        SourceFixture fixture;
        fixture.Files["Assets/main.hlsl"] = directive;
        CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", {}),
                        std::invalid_argument);
        CHECK(fixture.Reads.size() == 1U);
    }
}

TEST_CASE("shader snapshot confines sources roots and include probes to the project")
{
    for (const auto path : {"../outside", "/outside", "C:outside", "C:/outside", "\\outside", ".", ""})
    {
        SourceFixture fixture;
        CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), path, {}),
                        std::invalid_argument);
        CHECK(fixture.Reads.empty());
    }
    SourceFixture fixture;
    fixture.Files = {{"Assets/main.hlsl", "#include \"../../outside.hlsl\"\n"}};
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", {}),
                    std::invalid_argument);
    CHECK(fixture.Reads.size() == 1U);
    const std::array<std::filesystem::path, 1> escapingRoot{"../outside"};
    fixture.Reads.clear();
    CHECK_THROWS_AS(
        (void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "Assets/main.hlsl", escapingRoot),
        std::invalid_argument);
    CHECK(fixture.Reads.empty());
    CHECK(Keire::Detail::ConfinedShaderSourcePath("Assets/../Shared/file.hlsl") == "Shared/file.hlsl");
    CHECK(Keire::Detail::ConfinedShaderSourcePath("..shared", true) == "..shared");
    CHECK(Keire::Detail::ConfinedShaderSourcePath(".", true) == ".");
}

TEST_CASE("shader snapshot enforces an aggregate byte bound and does not disguise budget failures as missing includes")
{
    SourceFixture fixture;
    fixture.Files = {
        {"main.hlsl", "#include \"large.hlsl\"\n"}, {"large.hlsl", std::string(64, 'x')}, {"fallback/large.hlsl", ""}};
    auto context = fixture.Context();
    context.MaximumDependencyBytes = 64;
    const std::array<std::filesystem::path, 1> roots{"fallback"};
    CHECK_THROWS_WITH_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(context, "main.hlsl", roots),
                         doctest::Contains("aggregate byte limit"), std::invalid_argument);
    CHECK_FALSE(fixture.Reads.contains("fallback/large.hlsl"));
    context.MaximumDependencyBytes = 0;
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(context, "main.hlsl", {}), std::invalid_argument);
}

TEST_CASE("shader snapshot bounds dependency count and preserves include-cycle rejection")
{
    SourceFixture fixture;
    fixture.Files = {{"main.hlsl", "#include \"other.hlsl\"\n"}, {"other.hlsl", "#include \"main.hlsl\"\n"}};
    CHECK_THROWS_WITH_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "main.hlsl", {}),
                         doctest::Contains("cycle"), std::invalid_argument);
    fixture.Files.clear();
    fixture.Reads.clear();
    for (std::size_t index = 0; index < 257U; ++index)
    {
        fixture.Files[std::to_string(index) + ".hlsl"] = "#include \"" + std::to_string(index + 1U) + ".hlsl\"\n";
    }
    CHECK_THROWS_WITH_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "0.hlsl", {}),
                         doctest::Contains("dependency limit"), std::invalid_argument);
}

TEST_CASE("shader snapshot preserves separators inside angle header names and contains allocation failures")
{
    SourceFixture fixture;
    fixture.Files = {{"main.hlsl", "#include <folder//header.hlsl>\n"}, {"folder/header.hlsl", "float shared;\n"}};
    const auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "main.hlsl", {});
    REQUIRE(captured.size() == 2U);
    CHECK(captured.back().Dependency.RelativePath == "folder/header.hlsl");
    auto context = fixture.Context();
    context.ReadProjectFile = [](const std::filesystem::path& path)
    {
        if (path == "main.hlsl")
        {
            return Bytes("#include \"allocation.hlsl\"\n");
        }
        throw std::bad_alloc();
    };
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(context, "main.hlsl", {}), std::bad_alloc);
    const std::vector<std::filesystem::path> tooManyRoots(17U, ".");
    fixture.Reads.clear();
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "main.hlsl", tooManyRoots),
                    std::invalid_argument);
    CHECK(fixture.Reads.empty());
}

TEST_CASE("shader snapshot bounds long include names without recursive regex matching")
{
    SourceFixture fixture;
    fixture.Files["main.hlsl"] = "#include \"" + std::string(32768, 'x') + "\"\n";
    CHECK_THROWS_WITH_AS((void)Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "main.hlsl", {}),
                         doctest::Contains("bounded literal"), std::invalid_argument);
    CHECK(fixture.Reads.size() == 1U);
    fixture.Reads.clear();
    fixture.Files["main.hlsl"] = "#include_macro not-a-directive\nfloat comparison = 1 < 2;\n";
    const auto captured = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context(), "main.hlsl", {});
    CHECK(captured.size() == 1U);
}
