#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct SnapshotProject final
    {
        SnapshotProject() : Root(KeireTests::MakeTestDirectory("ShaderSnapshotImporter"))
        {
            std::filesystem::create_directories(Root);
        }

        ~SnapshotProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };

    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Shader snapshot compiler fixture is unavailable.");
        }
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("shader importer compiles retained dependency bytes despite later reader revisions")
{
    const auto repository = std::filesystem::current_path();
    const auto project = repository / "KeireHubContent/Templates/Payloads/Starter3D";
    const std::filesystem::path sourcePath = "Assets/Shaders/StarterUnlit.hlsl";
    const std::filesystem::path headerPath = "Assets/Shaders/snapshot-captured.hlsl";
    const auto source = ReadText(project / sourcePath) + "\n/*\n#include \"commented-missing.hlsl\"\n*/\n"
                                                         "#include \\\n\"snapshot-captured.hlsl\"\n";
    const std::string header = "#define KEIRE_SNAPSHOT_CAPTURED 1\n";
    const auto manifest = ReadText(project / "Assets/Shaders/DefaultUnlit.keireshader");
    std::map<std::filesystem::path, std::size_t> reads;
    SnapshotProject scratch;
    Keire::AssetImportContext context;
    context.ProjectRoot = scratch.Root;
    context.SourceRoot = scratch.Root / "Assets";
    context.RelativePath = "Shaders/DefaultUnlit.keireshader";
    context.SourcePath = context.SourceRoot / context.RelativePath;
    context.ReadProjectFile = [&](const std::filesystem::path& requested)
    {
        const auto path = requested.lexically_normal();
        if (++reads[path] > 1U)
        {
            return Bytes("#error dependency changed after initial capture\n");
        }
        if (path == sourcePath)
        {
            return Bytes(source);
        }
        if (path == headerPath)
        {
            return Bytes(header);
        }
        throw std::runtime_error("Unexpected shader compiler fixture dependency.");
    };
    Keire::ShaderImporterSpecification specification;
#if defined(_WIN32)
    specification.Compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
    specification.Compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
    specification.Formats = {Keire::ShaderBinaryFormat::SpirV};
    REQUIRE(std::filesystem::is_regular_file(specification.Compiler));
    const auto importer = Keire::CreateShaderAssetImporter(specification);
    const auto imported = importer.ContextualImport(context, Bytes(manifest));
    const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
    REQUIRE(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK_FALSE(shader->Variant(Keire::ShaderBinaryFormat::SpirV)->Vertex.empty());
    CHECK_FALSE(shader->Variant(Keire::ShaderBinaryFormat::SpirV)->Fragment.empty());
    CHECK(reads.at(sourcePath) == 1U);
    CHECK(reads.at(headerPath) == 1U);
    REQUIRE(imported.SourceDependencies.size() == 2U);
    const auto captured =
        std::ranges::find(imported.SourceDependencies, headerPath, &Keire::AssetSourceDependency::RelativePath);
    REQUIRE(captured != imported.SourceDependencies.end());
    CHECK(captured->Digest == Keire::Detail::DigestToString(Keire::Detail::Sha256(Bytes(header))));
    CHECK(importer.Version == 11U);
}
