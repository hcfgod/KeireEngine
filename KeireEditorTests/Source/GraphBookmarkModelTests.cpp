#include "KeireClient/Editor/GraphNavigation.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

TEST_CASE("Graph bookmarks update by name and preserve deterministic insertion order")
{
    KeireEditor::GraphBookmarkSet bookmarks;
    bookmarks.Save("Overview", {{10.0F, 20.0F}, 0.75F});
    bookmarks.Save("Detail", {{-4.0F, 9.0F}, 1.5F});
    bookmarks.Save("Overview", {{30.0F, 40.0F}, 1.0F});

    REQUIRE(bookmarks.Bookmarks().size() == 2);
    CHECK(bookmarks.Bookmarks()[0].Name == "Overview");
    CHECK(bookmarks.Find("Overview") == KeireEditor::NodeGraphViewport{{30.0F, 40.0F}, 1.0F});
    CHECK_FALSE(bookmarks.Find("Missing"));
}

TEST_CASE("Graph bookmarks reject invalid state and enforce their workspace bound")
{
    KeireEditor::GraphBookmarkSet bookmarks;
    CHECK_THROWS_AS(bookmarks.Save({}, {}), std::invalid_argument);
    for (std::size_t index = 0; index < KeireEditor::GraphBookmarkSet::MaximumBookmarks; ++index)
        bookmarks.Save("Bookmark " + std::to_string(index + 1U), {});
    CHECK_THROWS_WITH_AS(bookmarks.Save("Overflow", {}), "Graph bookmark limit is 9.", std::invalid_argument);
    bookmarks.Save("Bookmark 1", {{1, 2}, 0.5F});
    CHECK(bookmarks.Find("Bookmark 1") == KeireEditor::NodeGraphViewport{{1, 2}, 0.5F});
    CHECK(bookmarks.NextName().empty());
    REQUIRE(bookmarks.Remove("Bookmark 3"));
    CHECK(bookmarks.NextName() == "Bookmark 3");
    CHECK_FALSE(bookmarks.Remove("missing"));
}

TEST_CASE("Graph bookmark renaming and validation preserve state when a name is rejected")
{
    KeireEditor::GraphBookmarkSet bookmarks;
    bookmarks.Save("Overview", {{10, 20}, 1});
    bookmarks.Save("Details", {{30, 40}, 2});
    CHECK_FALSE(bookmarks.Rename("missing", "Other"));
    CHECK_FALSE(bookmarks.Rename("Overview", "Overview"));
    CHECK_THROWS_AS((void)bookmarks.Rename("Overview", "Details"), std::invalid_argument);
    CHECK_THROWS_AS((void)bookmarks.Rename("Overview", "   "), std::invalid_argument);
    CHECK(bookmarks.Find("Overview") == KeireEditor::NodeGraphViewport{{10, 20}, 1});
    REQUIRE(bookmarks.Rename("Overview", "Whole graph"));
    CHECK_FALSE(bookmarks.Find("Overview"));
    REQUIRE(bookmarks.Bookmarks().size() == 2);
    CHECK(bookmarks.Bookmarks().front().Name == "Whole graph");
    CHECK(bookmarks.Find("Whole graph") == KeireEditor::NodeGraphViewport{{10, 20}, 1});
    for (const auto* invalid : {"", "   ", "Line\nBreak", "Hidden##id", "Other###id"})
    {
        CHECK_FALSE(KeireEditor::GraphBookmarkSet::ValidName(invalid));
        CHECK_THROWS_AS(bookmarks.Save(invalid, {}), std::invalid_argument);
    }
    CHECK(KeireEditor::GraphBookmarkSet::ValidName("Lighting / Close-up"));
    CHECK(KeireEditor::GraphBookmarkSet::ValidName(std::string(64, 'x')));
    CHECK_FALSE(KeireEditor::GraphBookmarkSet::ValidName(std::string(65, 'x')));
}
