#include "KeireClient/Editor/GraphNavigation.h"

#include <doctest/doctest.h>

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
}
