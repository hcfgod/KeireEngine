#include "KeireClient/Editor/PostProcessPreviewSession.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

TEST_CASE("post processing live preview restores external source and survives a failed publication")
{
    using namespace Keire;
    const AssetId asset(2, 1);
    auto source = CreateRef<PostProcessProfileAsset>();
    Ref<PostProcessProfileAsset> published;
    bool failAfterPublication = false;
    KeireEditor::PostProcessPreviewSession session({.ReadSource = [&](AssetId) { return source; },
                                                    .Publish =
                                                        [&](AssetId target, const Ref<PostProcessProfileAsset>& profile)
                                                    {
                                                        CHECK(target == asset);
                                                        published = profile;
                                                        if (failAfterPublication)
                                                        {
                                                            failAfterPublication = false;
                                                            throw std::runtime_error("publication callback failed");
                                                        }
                                                        return true;
                                                    }});
    auto draft = source->Definition();
    draft.Parameters[0] = {true, 2.0};
    session.Preview(asset, draft);
    REQUIRE(published);
    CHECK(published->Definition() == draft);
    auto rejected = draft;
    rejected.Parameters[0].Value = 5.0;
    failAfterPublication = true;
    CHECK_THROWS_AS(session.Preview(asset, rejected), std::runtime_error);
    CHECK(published->Definition() == draft);
    auto external = draft;
    external.Parameters[0].Value = -2.0;
    source = CreateRef<PostProcessProfileAsset>(external);
    session.Stop();
    CHECK(published->Definition() == external);
    session.Stop();
    CHECK(published->Definition() == external);
}

TEST_CASE("post processing preview retains saved baseline when source disappears and restore must retry")
{
    using namespace Keire;
    const AssetId asset(2, 2);
    bool sourceMissing = false;
    bool rejectPublication = false;
    Ref<PostProcessProfileAsset> published;
    KeireEditor::PostProcessPreviewSession session({.ReadSource =
                                                        [&](AssetId)
                                                    {
                                                        if (sourceMissing)
                                                        {
                                                            throw std::runtime_error("source deleted");
                                                        }
                                                        return CreateRef<PostProcessProfileAsset>();
                                                    },
                                                    .Publish =
                                                        [&](AssetId, const Ref<PostProcessProfileAsset>& profile)
                                                    {
                                                        if (rejectPublication)
                                                        {
                                                            return false;
                                                        }
                                                        published = profile;
                                                        return true;
                                                    }});
    PostProcessProfileDefinition saved;
    saved.Parameters[0] = {true, 1.0};
    session.Preview(asset, saved);
    session.CommitSavedBaseline(asset, CreateRef<PostProcessProfileAsset>(saved));
    auto unsaved = saved;
    unsaved.Parameters[0].Value = 3.0;
    session.Preview(asset, unsaved);
    sourceMissing = true;
    rejectPublication = true;
    CHECK_THROWS_AS(session.Stop(), std::runtime_error);
    CHECK(published->Definition() == unsaved);
    rejectPublication = false;
    session.Stop();
    CHECK(published->Definition() == saved);
}

TEST_CASE("post processing preview restores the previous profile before switching assets")
{
    using namespace Keire;
    const AssetId first(2, 3);
    const AssetId second(2, 4);
    std::vector<AssetId> publications;
    KeireEditor::PostProcessPreviewSession session(
        {.ReadSource = [](AssetId) { return CreateRef<PostProcessProfileAsset>(); },
         .Publish =
             [&](AssetId asset, const Ref<PostProcessProfileAsset>&)
         {
             publications.push_back(asset);
             return true;
         }});
    session.Preview(first, {});
    session.Preview(second, {});
    session.Stop();
    CHECK(publications == std::vector<AssetId>{first, first, second, second});
    CHECK_THROWS_AS(session.Preview({}, {}), std::invalid_argument);
    CHECK(publications.size() == 4);
}
