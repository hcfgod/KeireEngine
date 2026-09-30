#include "KeireHubInternal/HubEditorManagementNotice.h"

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("background editor refresh completion does not request a notice")
{
    CHECK_FALSE(Detail::ShouldAnnounceSuccessfulEditorManagementCompletion(HubEditorManagementOperation::Refresh));
}

TEST_CASE("initiated editor management completions request notices")
{
    CHECK(Detail::ShouldAnnounceSuccessfulEditorManagementCompletion(HubEditorManagementOperation::Verify));
    CHECK(
        Detail::ShouldAnnounceSuccessfulEditorManagementCompletion(HubEditorManagementOperation::RefreshRegistration));
    CHECK(Detail::ShouldAnnounceSuccessfulEditorManagementCompletion(HubEditorManagementOperation::AuthorizeRepair));
    CHECK(Detail::ShouldAnnounceSuccessfulEditorManagementCompletion(HubEditorManagementOperation::AuthorizeRemoval));
}
