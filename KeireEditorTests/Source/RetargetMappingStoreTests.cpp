#include "KeireClientInternal/Editor/RetargetMappingStore.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    struct MappingProject
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("Keire-Mapping-" + Keire::AssetId::Generate().ToString());
        Keire::AssetId Source = Keire::AssetId::Generate();
        Keire::AssetId Target = Keire::AssetId::Generate();
        Keire::SkeletonAsset SourceRig{{{"source hand", -1, {}, {}}}};
        Keire::SkeletonAsset TargetRig{{{"target hand", -1, {}, {}}}};
        std::vector<Keire::AnimationRetargetOverride> Mappings{{"source hand", "target hand"}};

        ~MappingProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };
} // namespace

TEST_CASE("Saved retarget mappings round trip across instances and support automatic-only mappings")
{
    MappingProject project;
    KeireEditor::Detail::SaveRetargetMapping(project.Root, project.Source, project.Target, project.SourceRig,
                                             project.TargetRig, project.Mappings);
    const auto loaded = KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target,
                                                                 project.SourceRig, project.TargetRig);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front().SourceBone == "source hand");
    CHECK(loaded.front().TargetBone == "target hand");
    KeireEditor::Detail::SaveRetargetMapping(project.Root, project.Source, project.Target, project.SourceRig,
                                             project.TargetRig, {});
    CHECK(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target, project.SourceRig,
                                                   project.TargetRig)
              .empty());
}

TEST_CASE("Saved retarget mapping rejects invalid replacements without damaging the previous save")
{
    MappingProject project;
    KeireEditor::Detail::SaveRetargetMapping(project.Root, project.Source, project.Target, project.SourceRig,
                                             project.TargetRig, project.Mappings);
    const auto path = KeireEditor::Detail::RetargetMappingPath(project.Root, project.Source, project.Target);
    const auto original = Keire::Detail::ReadTextFile(path, 1024 * 1024);
    for (const auto invalid :
         {std::vector<Keire::AnimationRetargetOverride>{{"source hand", "target hand"}, {"other", "target hand"}},
          std::vector<Keire::AnimationRetargetOverride>{{"", "target hand"}},
          std::vector<Keire::AnimationRetargetOverride>{{"removed source", "target hand"}},
          std::vector<Keire::AnimationRetargetOverride>{{"source hand", "removed target"}},
          std::vector<Keire::AnimationRetargetOverride>{{std::string(1025, 'a'), "target hand"}}})
    {
        CHECK_THROWS(KeireEditor::Detail::SaveRetargetMapping(project.Root, project.Source, project.Target,
                                                              project.SourceRig, project.TargetRig, invalid));
        CHECK(Keire::Detail::ReadTextFile(path, 1024 * 1024) == original);
    }
    CHECK_THROWS(KeireEditor::Detail::RetargetMappingPath({}, project.Source, project.Target));
    CHECK_THROWS(KeireEditor::Detail::RetargetMappingPath(project.Root, {}, project.Target));
}

TEST_CASE("Saved retarget mapping reports missing stale malformed and foreign data")
{
    MappingProject project;
    CHECK_THROWS_WITH(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target,
                                                               project.SourceRig, project.TargetRig),
                      "No mapping is saved for these skeletons. Edit the bone mappings, then Save Mapping.");
    KeireEditor::Detail::SaveRetargetMapping(project.Root, project.Source, project.Target, project.SourceRig,
                                             project.TargetRig, project.Mappings);
    Keire::SkeletonAsset renamed{{{"renamed", -1, {}, {}}}};
    CHECK_THROWS_WITH(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target, renamed,
                                                               project.TargetRig),
                      "Source bone no longer exists: source hand. Repair the bone mappings before saving or loading.");
    CHECK_THROWS_WITH(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target,
                                                               project.SourceRig, renamed),
                      "Target bone no longer exists: target hand. Repair the bone mappings before saving or loading.");
    const auto path = KeireEditor::Detail::RetargetMappingPath(project.Root, project.Source, project.Target);
    const auto original = Keire::Detail::ReadTextFile(path, 1024 * 1024);
    for (const auto& broken : {std::string("not json"), std::string("[]"), std::string("{}"),
                               std::string(1024 * 1024 + 1, ' '), std::string("{\"schemaVersion\":2,\"mappings\":[]}")})
    {
        Keire::Detail::WriteTextFileAtomically(path, broken);
        CHECK_THROWS(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target,
                                                              project.SourceRig, project.TargetRig));
    }
    auto foreign = original;
    foreign.replace(foreign.find(project.Source.ToString()), project.Source.ToString().size(),
                    Keire::AssetId::Generate().ToString());
    Keire::Detail::WriteTextFileAtomically(path, foreign);
    CHECK_THROWS_WITH(KeireEditor::Detail::LoadRetargetMapping(project.Root, project.Source, project.Target,
                                                               project.SourceRig, project.TargetRig),
                      "The mapping version or skeleton identities do not match this selection.");
}
