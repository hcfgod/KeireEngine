#include "doctest/doctest.h"

#include "KeireClient/Editor/ShaderGraphPublication.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class TemporaryShaderGraphProject final
    {
      public:
        TemporaryShaderGraphProject()
            : Asset(Keire::AssetId::Generate()),
              Root(std::filesystem::absolute(std::filesystem::path("Build") /
                                             ("ShaderGraphPublication-" + Asset.ToString())))
        {
            std::filesystem::create_directories(GraphSource().parent_path());
        }

        ~TemporaryShaderGraphProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] std::filesystem::path GraphSource() const
        {
            return Root / "Assets/Materials/Test.keireshadergraph";
        }

        [[nodiscard]] std::filesystem::path GeneratedRoot() const
        {
            return Root / "Assets/Generated/ShaderGraphs" / Asset.ToString();
        }

        void Write(const std::filesystem::path& path, const std::string_view contents) const
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            REQUIRE(stream.good());
        }

        [[nodiscard]] std::string Read(const std::filesystem::path& path) const
        {
            std::ifstream stream(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        Keire::AssetId Asset;
        std::filesystem::path Root;
    };

    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] Keire::ShaderGraphShaderVariant Variant(const TemporaryShaderGraphProject& project,
                                                          const std::string_view name)
    {
        Keire::ShaderGraphShaderVariant result;
        result.GeneratedSource = std::filesystem::path("Assets/Generated/ShaderGraphs") / project.Asset.ToString() /
                                 (std::string(name) + ".hlsl");
        result.Hlsl = "shader " + std::string(name);
        result.Manifest = "manifest " + std::string(name);
        return result;
    }
} // namespace

TEST_CASE("Shader Graph publication commits graph and generated variants as one recoverable transaction")
{
    TemporaryShaderGraphProject project;
    project.Write(project.GraphSource(), "old graph");
    project.Write(project.GeneratedRoot() / "ShaderGraph-current.hlsl", "old shader");
    project.Write(project.GeneratedRoot() / "ShaderGraph-current.hlsl.keiremeta", "stable metadata");
    project.Write(project.GeneratedRoot() / "ShaderGraph-stale.hlsl", "stale shader");
    project.Write(project.GeneratedRoot() / "ShaderGraph-stale.hlsl.keiremeta", "stale metadata");

    const std::array variants{Variant(project, "ShaderGraph-current"), Variant(project, "ShaderGraph-new")};
    const auto graph = Bytes("new graph");
    KeireEditor::PublishShaderGraph({.ProjectRoot = project.Root,
                                     .SourceDirectory = "Assets",
                                     .GraphRelativePath = "Materials/Test.keireshadergraph",
                                     .Asset = project.Asset,
                                     .Variants = variants,
                                     .GraphBytes = graph});

    CHECK(project.Read(project.GraphSource()) == "new graph");
    CHECK(project.Read(project.GeneratedRoot() / "ShaderGraph-current.hlsl") == "shader ShaderGraph-current");
    CHECK(project.Read(project.GeneratedRoot() / "ShaderGraph-current.hlsl.keiremeta") == "stable metadata");
    CHECK(project.Read(project.GeneratedRoot() / "ShaderGraph-new.keireshader") == "manifest ShaderGraph-new");
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "ShaderGraph-stale.hlsl"));
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "ShaderGraph-stale.hlsl.keiremeta"));
}

TEST_CASE("Shader Graph publication restores generated variants when graph publication fails")
{
    TemporaryShaderGraphProject project;
    std::filesystem::create_directories(project.GraphSource());
    project.Write(project.GeneratedRoot() / "ShaderGraph-original.hlsl", "original shader");
    project.Write(project.GeneratedRoot() / "ShaderGraph-original.hlsl.keiremeta", "original metadata");

    const std::array variants{Variant(project, "ShaderGraph-replacement")};
    const auto graph = Bytes("replacement graph");
    CHECK_THROWS(KeireEditor::PublishShaderGraph({.ProjectRoot = project.Root,
                                                  .SourceDirectory = "Assets",
                                                  .GraphRelativePath = "Materials/Test.keireshadergraph",
                                                  .Asset = project.Asset,
                                                  .Variants = variants,
                                                  .GraphBytes = graph}));

    CHECK(project.Read(project.GeneratedRoot() / "ShaderGraph-original.hlsl") == "original shader");
    CHECK(project.Read(project.GeneratedRoot() / "ShaderGraph-original.hlsl.keiremeta") == "original metadata");
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "ShaderGraph-replacement.hlsl"));
}
