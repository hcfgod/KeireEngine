#include "KeireHubInternal/HubCatalogEmptyState.h"

#include <doctest/doctest.h>

#include <ostream>

TEST_CASE("Editor catalog empty states distinguish failures from filters")
{
    using KeireHub::Detail::EditorCatalogEmptyMessage;

    CHECK(EditorCatalogEmptyMessage(false, false, false) ==
          "No verified editor releases are available for this platform.");
    CHECK(EditorCatalogEmptyMessage(false, false, true) ==
          "No verified editor release matches these filters. Clear the search or "
          "select All channels.");
    CHECK(EditorCatalogEmptyMessage(false, true, false) ==
          "Editor releases could not be loaded. Review the catalog error above. "
          "Installed editors and local projects remain available.");
    CHECK(EditorCatalogEmptyMessage(false, true, true) == EditorCatalogEmptyMessage(false, true, false));
}

TEST_CASE("Editor catalog refresh does not show stale errors or filter failures")
{
    using KeireHub::Detail::EditorCatalogEmptyMessage;

    CHECK(EditorCatalogEmptyMessage(true, false, false) == "Loading verified editor catalogs...");
    CHECK(EditorCatalogEmptyMessage(true, false, true) == "Loading verified editor catalogs...");
    CHECK(EditorCatalogEmptyMessage(true, true, false) == "Loading verified editor catalogs...");
    CHECK(EditorCatalogEmptyMessage(true, true, true) == "Loading verified editor catalogs...");
}
