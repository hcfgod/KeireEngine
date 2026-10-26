#include "KeireClient/Editor/GraphNavigation.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class BookmarkDirectory final
    {
      public:
        BookmarkDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("Keire-GraphViews-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Path);
        }

        ~BookmarkDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    constexpr Keire::AssetId First{0x4752415048564945ULL, 1};
    constexpr Keire::AssetId Second{0x4752415048564945ULL, 2};

    [[nodiscard]] std::string Read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void Write(const std::filesystem::path& path, const std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        REQUIRE(output.good());
    }
} // namespace

TEST_CASE("Graph bookmark storage round trips names precision renames removals and transient reset")
{
    BookmarkDirectory directory;
    const auto root = directory.Path / std::filesystem::path(u8"Kéire 图形");
    KeireEditor::GraphBookmarkSet bookmarks;
    CHECK(bookmarks.Open(root, First));
    CHECK_FALSE(bookmarks.Open(root, First));
    const KeireEditor::NodeGraphViewport viewport{{0.123456791F, -9876.543F}, 0.31415927F};
    bookmarks.Save("Light \"detail\" / Kéire", viewport);
    REQUIRE(bookmarks.Diagnostic().empty());
    bookmarks.Clear();
    CHECK(bookmarks.Bookmarks().empty());
    REQUIRE(bookmarks.Open(root, First));
    CHECK(bookmarks.Diagnostic().empty());
    CHECK(bookmarks.Find("Light \"detail\" / Kéire") == viewport);
    REQUIRE(bookmarks.Rename("Light \"detail\" / Kéire", "Renamed"));

    KeireEditor::GraphBookmarkSet reopened;
    REQUIRE(reopened.Open(root, First));
    CHECK(reopened.Find("Renamed") == viewport);
    REQUIRE(reopened.Remove("Renamed"));
    reopened.Clear();
    REQUIRE(reopened.Open(root, First));
    CHECK(reopened.Bookmarks().empty());
    CHECK(reopened.Diagnostic().empty());
}

TEST_CASE("Graph bookmark storage isolates assets VFX systems and project graph directories")
{
    BookmarkDirectory directory;
    KeireEditor::GraphBookmarkSet bookmarks;
    REQUIRE(bookmarks.Open(directory.Path, First, First));
    bookmarks.Save("First system", {});
    REQUIRE(bookmarks.Open(directory.Path, First, Second));
    CHECK(bookmarks.Bookmarks().empty());
    bookmarks.Save("Second system", {{1, 2}, 1});
    REQUIRE(bookmarks.Open(directory.Path, Second, First));
    CHECK(bookmarks.Bookmarks().empty());
    bookmarks.Save("Other asset", {{3, 4}, 1});
    REQUIRE(bookmarks.Open(directory.Path, First, First));
    CHECK(bookmarks.Find("First system").has_value());
    CHECK_FALSE(bookmarks.Find("Other asset"));
    REQUIRE(bookmarks.Open(directory.Path / "OtherProject", First, First));
    CHECK(bookmarks.Bookmarks().empty());
    bookmarks.Save("Other project", {});
    REQUIRE(bookmarks.Open(directory.Path, First, Second));
    CHECK(bookmarks.Find("Second system") == KeireEditor::NodeGraphViewport{{1, 2}, 1});
}

TEST_CASE("Graph bookmark storage preserves malformed oversized and unsupported files")
{
    BookmarkDirectory directory;
    const auto path = directory.Path / (First.ToString() + ".views");
    const std::vector<std::string> invalid{"",
                                           "KEIRE_GRAPH_VIEWS 2 0\n",
                                           "KEIRE_GRAPH_VIEWS 1 10\n",
                                           "KEIRE_GRAPH_VIEWS 1 0\ntrailing",
                                           "KEIRE_GRAPH_VIEWS 1 1\n\"Bad zoom\" 0 0 0.1\n",
                                           "KEIRE_GRAPH_VIEWS 1 2\n\"Duplicate\" 0 0 1\n\"Duplicate\" 1 1 1\n",
                                           "KEIRE_GRAPH_VIEWS 1 1\n\"Nonfinite\" 1e500 0 1\n",
                                           "KEIRE_GRAPH_VIEWS 1 1\n\"Hidden##id\" 0 0 1\n",
                                           std::string(16U * 1024U + 1U, 'x')};
    for (const auto& text : invalid)
    {
        INFO("Input size: " << text.size());
        Write(path, text);
        KeireEditor::GraphBookmarkSet bookmarks;
        REQUIRE(bookmarks.Open(directory.Path, First));
        CHECK(bookmarks.Bookmarks().empty());
        CHECK_FALSE(bookmarks.Diagnostic().empty());
        bookmarks.Save("Session only", {});
        CHECK(bookmarks.Find("Session only").has_value());
        CHECK_FALSE(bookmarks.Diagnostic().empty());
        CHECK(Read(path) == text);
    }
}

TEST_CASE("Graph bookmark write failures retain session views and allow an explicit save retry")
{
    BookmarkDirectory directory;
    const auto root = directory.Path / "InitiallyMissing";
    KeireEditor::GraphBookmarkSet bookmarks;
    REQUIRE(bookmarks.Open(root, First));
    Write(root, "not a directory");
    bookmarks.Save("Retained", {{1, 2}, 0.5F});
    CHECK_FALSE(bookmarks.Diagnostic().empty());
    CHECK(bookmarks.Find("Retained") == KeireEditor::NodeGraphViewport{{1, 2}, 0.5F});
    CHECK(Read(root) == "not a directory");
    REQUIRE(std::filesystem::remove(root));
    bookmarks.Save("Retained", {{1, 2}, 0.5F});
    CHECK(bookmarks.Diagnostic().empty());
    KeireEditor::GraphBookmarkSet reopened;
    REQUIRE(reopened.Open(root, First));
    CHECK(reopened.Find("Retained") == KeireEditor::NodeGraphViewport{{1, 2}, 0.5F});
}
