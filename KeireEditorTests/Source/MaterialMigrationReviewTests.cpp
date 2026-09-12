#include "KeireClient/Editor/MaterialMigrationReview.h"

#include <doctest/doctest.h>

#include <stdexcept>

namespace
{
    Keire::ShaderGraphMigrationReport PendingReview()
    {
        Keire::ShaderGraphMigrationReport report;
        report.ReviewFingerprint = "reviewed";
        report.Items.push_back(
            {.MaterialGraph = "Legacy.keirematerial", .Disposition = Keire::ShaderGraphMigrationDisposition::Migrate});
        return report;
    }
} // namespace

TEST_CASE("material migration review consumes approval on success and failure")
{
    KeireEditor::MaterialMigrationReview review;
    const auto inspect = [](const auto&) { return PendingReview(); };
    const auto root = std::filesystem::current_path();
    CHECK_FALSE(review.CanApply());
    review.Refresh(root, inspect);
    CHECK(review.CanApply());
    int publications = 0;
    const auto publish = [&](const auto& path, const auto& report)
    {
        ++publications;
        CHECK(path == std::filesystem::weakly_canonical(root));
        CHECK(report.ReviewFingerprint == "reviewed");
        return report;
    };
    CHECK(review.Apply(root, publish).PendingCount() == 1);
    CHECK_FALSE(review.CanApply());
    CHECK_THROWS_AS((void)review.Apply(root, publish), std::logic_error);
    CHECK(publications == 1);

    review.Refresh(root, inspect);
    CHECK_THROWS_AS((void)review.Apply(root / "DifferentProject", publish), std::logic_error);
    CHECK_FALSE(review.CanApply());
    CHECK(publications == 1);

    review.Refresh(root, inspect);
    CHECK_THROWS_AS((void)review.Apply(root, [](const auto&, const auto&) -> Keire::ShaderGraphMigrationReport
                                       { throw std::runtime_error("stale source or failed import"); }),
                    std::runtime_error);
    CHECK_FALSE(review.CanApply());

    review.Refresh(root, inspect);
    CHECK_THROWS_AS(review.Refresh(root, [](const auto&) -> Keire::ShaderGraphMigrationReport
                                   { throw std::runtime_error("read failure"); }),
                    std::runtime_error);
    CHECK_FALSE(review.Report().has_value());
}

TEST_CASE("material migration review rejects incomplete and conflicting reports")
{
    KeireEditor::MaterialMigrationReview review;
    for (const auto disposition :
         {Keire::ShaderGraphMigrationDisposition::Conflict, Keire::ShaderGraphMigrationDisposition::Invalid,
          Keire::ShaderGraphMigrationDisposition::AlreadyMigrated})
    {
        review.Refresh(".",
                       [disposition](const auto&)
                       {
                           auto report = PendingReview();
                           report.Items.front().Disposition = disposition;
                           return report;
                       });
        CHECK_FALSE(review.CanApply());
    }
    review.Refresh(".",
                   [](const auto&)
                   {
                       auto report = PendingReview();
                       report.ReviewFingerprint.clear();
                       return report;
                   });
    CHECK_FALSE(review.CanApply());
}
