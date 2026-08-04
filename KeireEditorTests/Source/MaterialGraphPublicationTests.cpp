#include "doctest/doctest.h"

#include "KeireClient/Editor/MaterialGraphPublication.h"

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
    class TemporaryMaterialGraphProject final
    {
      public:
        TemporaryMaterialGraphProject()
            : Asset(Keire::AssetId::Generate()),
              Root(std::filesystem::absolute(std::filesystem::path("Build") /
                                             ("MaterialGraphPublication-" + Asset.ToString())))
        {
            std::filesystem::create_directories(GraphSource().parent_path());
        }

        ~TemporaryMaterialGraphProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] std::filesystem::path GraphSource() const
        {
            return Root / "Assets/Materials/Test.keirematerialgraph";
        }

        [[nodiscard]] std::filesystem::path GeneratedRoot() const
        {
            return Root / "Assets/Generated/MaterialGraphs" / Asset.ToString();
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

    [[nodiscard]] Keire::MaterialGraphShaderVariant Variant(const TemporaryMaterialGraphProject& project,
                                                            const std::string_view name)
    {
        Keire::MaterialGraphShaderVariant result;
        result.GeneratedSource = std::filesystem::path("Assets/Generated/MaterialGraphs") / project.Asset.ToString() /
                                 (std::string(name) + ".hlsl");
        result.Hlsl = "shader " + std::string(name);
        result.Manifest = "manifest " + std::string(name);
        return result;
    }
} // namespace

TEST_CASE("Material Graph publication commits graph and generated variants as one recoverable transaction")
{
    TemporaryMaterialGraphProject project;
    project.Write(project.GraphSource(), "old graph");
    project.Write(project.GeneratedRoot() / "MaterialGraph-current.hlsl", "old shader");
    project.Write(project.GeneratedRoot() / "MaterialGraph-current.hlsl.keiremeta", "stable metadata");
    project.Write(project.GeneratedRoot() / "MaterialGraph-stale.hlsl", "stale shader");
    project.Write(project.GeneratedRoot() / "MaterialGraph-stale.hlsl.keiremeta", "stale metadata");

    const std::array variants{Variant(project, "MaterialGraph-current"), Variant(project, "MaterialGraph-new")};
    const auto graph = Bytes("new graph");
    KeireEditor::PublishMaterialGraph({.ProjectRoot = project.Root,
                                       .SourceDirectory = "Assets",
                                       .GraphRelativePath = "Materials/Test.keirematerialgraph",
                                       .Asset = project.Asset,
                                       .Variants = variants,
                                       .GraphBytes = graph});

    CHECK(project.Read(project.GraphSource()) == "new graph");
    CHECK(project.Read(project.GeneratedRoot() / "MaterialGraph-current.hlsl") == "shader MaterialGraph-current");
    CHECK(project.Read(project.GeneratedRoot() / "MaterialGraph-current.hlsl.keiremeta") == "stable metadata");
    CHECK(project.Read(project.GeneratedRoot() / "MaterialGraph-new.keireshader") == "manifest MaterialGraph-new");
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "MaterialGraph-stale.hlsl"));
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "MaterialGraph-stale.hlsl.keiremeta"));
}

TEST_CASE("Material Graph publication restores generated variants when graph publication fails")
{
    TemporaryMaterialGraphProject project;
    std::filesystem::create_directories(project.GraphSource());
    project.Write(project.GeneratedRoot() / "MaterialGraph-original.hlsl", "original shader");
    project.Write(project.GeneratedRoot() / "MaterialGraph-original.hlsl.keiremeta", "original metadata");

    const std::array variants{Variant(project, "MaterialGraph-replacement")};
    const auto graph = Bytes("replacement graph");
    CHECK_THROWS(KeireEditor::PublishMaterialGraph({.ProjectRoot = project.Root,
                                                    .SourceDirectory = "Assets",
                                                    .GraphRelativePath = "Materials/Test.keirematerialgraph",
                                                    .Asset = project.Asset,
                                                    .Variants = variants,
                                                    .GraphBytes = graph}));

    CHECK(project.Read(project.GeneratedRoot() / "MaterialGraph-original.hlsl") == "original shader");
    CHECK(project.Read(project.GeneratedRoot() / "MaterialGraph-original.hlsl.keiremeta") == "original metadata");
    CHECK_FALSE(std::filesystem::exists(project.GeneratedRoot() / "MaterialGraph-replacement.hlsl"));
}
