#include <KeireHubTests/TestSupport.h>

#include <KeireHubRuntimeInternal/PackageArchiveOutput.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace KeireHub;

namespace
{
    [[nodiscard]] bool HasTemporarySibling(const std::filesystem::path& output)
    {
        const auto prefix = output.filename().string() + ".tmp-";
        for (const auto& entry : std::filesystem::directory_iterator(output.parent_path()))
        {
            if (entry.path().filename().string().starts_with(prefix))
                return true;
        }
        return false;
    }

    void WriteAndFinish(Detail::ExclusivePackageOutput& output, const std::string_view contents)
    {
        REQUIRE(output.Write(std::span<const char>(contents.data(), contents.size())));
        REQUIRE(output.Finish());
    }

    [[nodiscard]] std::string FailureDetails(const HubStatus& status)
    {
        return status ? std::string{} : status.Error().TechnicalDetails;
    }
} // namespace

TEST_CASE("Exclusive no-replace output rolls back a post-commit failure for a clean retry")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto output = temporary.Path() / "artifact.keirepackage";
    bool observedCommit = false;
    auto created = Detail::ExclusivePackageOutput::Create(output, "artifact",
                                                          {.FailAfterCommit = [&]
                                                           {
                                                               observedCommit =
                                                                   std::filesystem::is_regular_file(output);
                                                               return true;
                                                           }});
    REQUIRE(created);
    auto writer = std::move(created).Value();
    WriteAndFinish(*writer, "first-attempt");

    const auto failed = writer->Publish(output);
    REQUIRE_FALSE(failed);
    INFO(failed.Error().TechnicalDetails);
    CHECK(failed.Error().Code == HubErrorCode::IoWrite);
    CHECK(observedCommit);
    CHECK_FALSE(std::filesystem::exists(output));
    writer.reset();
    CHECK_FALSE(HasTemporarySibling(output));

    auto retried = Detail::ExclusivePackageOutput::Create(output, "artifact");
    REQUIRE(retried);
    auto retryWriter = std::move(retried).Value();
    WriteAndFinish(*retryWriter, "second-attempt");
    const auto retriedPublish = retryWriter->Publish(output);
    INFO(FailureDetails(retriedPublish));
    REQUIRE(retriedPublish);
    retryWriter.reset();
    CHECK(KeireHubTests::ReadText(output) == "second-attempt");
    CHECK_FALSE(HasTemporarySibling(output));
}

TEST_CASE("Exclusive replace output treats the atomic name change as its commit point")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto output = temporary.Path() / "journal.json";
    KeireHubTests::WriteText(output, "old");
    bool observedCommit = false;
    auto created = Detail::ExclusivePackageOutput::Create(output, "journal",
                                                          {.FailAfterCommit = [&]
                                                           {
                                                               observedCommit =
                                                                   std::filesystem::is_regular_file(output);
                                                               return true;
                                                           }});
    REQUIRE(created);
    auto writer = std::move(created).Value();
    WriteAndFinish(*writer, "new");

    const auto replaced = writer->Publish(output, true);
    INFO(FailureDetails(replaced));
    REQUIRE(replaced);
    CHECK(observedCommit);
    writer.reset();
    CHECK(KeireHubTests::ReadText(output) == "new");
    CHECK_FALSE(HasTemporarySibling(output));

    auto reconciled = Detail::ExclusivePackageOutput::Create(output, "journal");
    REQUIRE(reconciled);
    auto reconciliationWriter = std::move(reconciled).Value();
    WriteAndFinish(*reconciliationWriter, "reconciled");
    const auto reconciledPublish = reconciliationWriter->Publish(output, true);
    INFO(FailureDetails(reconciledPublish));
    REQUIRE(reconciledPublish);
    reconciliationWriter.reset();
    CHECK(KeireHubTests::ReadText(output) == "reconciled");
    CHECK_FALSE(HasTemporarySibling(output));
}

TEST_CASE("Exclusive no-replace output preserves an occupied destination and removes its temporary file")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto output = temporary.Path() / "artifact.keirepackage";
    KeireHubTests::WriteText(output, "existing");
    auto created = Detail::ExclusivePackageOutput::Create(output, "artifact");
    REQUIRE(created);
    auto writer = std::move(created).Value();
    WriteAndFinish(*writer, "replacement");

    const auto conflict = writer->Publish(output);
    REQUIRE_FALSE(conflict);
    CHECK(
        (conflict.Error().Code == HubErrorCode::DestinationConflict || conflict.Error().Code == HubErrorCode::IoWrite));
    writer.reset();
    CHECK(KeireHubTests::ReadText(output) == "existing");
    CHECK_FALSE(HasTemporarySibling(output));
}
