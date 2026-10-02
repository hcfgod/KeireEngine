#include "Keire/Animation/Skinning.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    const std::filesystem::path SpiderFixtureRoot = "Build/Validation/AsterReachSpiderPose";

    bool IsOriginalSpiderLiveCapture()
    {
        // Only this immutable game-script capture has the documented infeasible targets and mesh witness.
        // A changed input must go through strict acceptance rather than inherit historical exceptions.
        const std::array<std::pair<std::string_view, std::string_view>, 6> identities{{
            {"live-targets.txt", "a74ca320492f2aace1995d229862cd4fe19c6b9fcb5ed913be4697b082fb6fe1"},
            {"live-scene.keirescene", "c4f6b0e99076d04c816ea21f1183167bef3fb6ae435487352ffd6edbfd8d7689"},
            {"StarterScene.keirescene", "c4f6b0e99076d04c816ea21f1183167bef3fb6ae435487352ffd6edbfd8d7689"},
            {"ActualSkeleton.keireskeleton", "55e8bd7ac319760f5100f99202292b8b6dc422a0019b027f6482123a54328c6a"},
            {"SpiderRest.keireanim", "7bc8314fa9ccf91a3019e3410b97c92e5328ede3f18467a61d748925bf13ea90"},
            {"WolfSpider.glb", "c1176ad1d553f12506b3549bb5f6015d10d25436cbd7b682b4847d679616f94d"},
        }};
        try
        {
            for (const auto& [name, digest] : identities)
            {
                const auto path = SpiderFixtureRoot / name;
                if (!std::filesystem::is_regular_file(path) ||
                    Keire::Detail::DigestToString(Keire::Detail::Sha256File(path)) != digest)
                    return false;
            }
            return true;
        }
        catch (const std::exception&)
        {
            // Classification cannot hide a read failure: the positive case still reads and validates the inputs.
            return false;
        }
    }

    std::vector<std::byte> ReadSpiderBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        REQUIRE(stream.is_open());
        REQUIRE(stream.tellg() > 0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return bytes;
    }

    float SpiderPoseDistance(const Keire::Vector3 a, const Keire::Vector3 b)
    {
        return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
    }

    struct SpiderLiveLeg
    {
        bool Active = false;
        bool Planted = false;
        Keire::Vector3 Foot;
        Keire::Vector3 Ankle;
        Keire::Vector3 LowerPole;
    };

    struct SpiderLiveFrame
    {
        bool CapturesLowerPole = false;
        std::uint64_t Tick = 0;
        float Delta = 0;
        Keire::Vector3 Position;
        Keire::Quaternion Rotation;
        Keire::Vector3 Scale;
        std::array<SpiderLiveLeg, 8> Legs;
    };

    std::vector<SpiderLiveFrame> ReadSpiderLiveFrames(std::istream& input)
    {
        const auto demand = [](bool valid)
        {
            if (!valid)
                throw std::runtime_error("Invalid spider live capture");
        };
        std::string token;
        std::size_t count = 0;
        demand(static_cast<bool>(input >> token >> count));
        demand((token == "AsterSpiderLiveTargets1" || token == "AsterSpiderLiveTargets2" ||
                token == "AsterSpiderLiveTargets3"));
        const bool startupContacts = token == "AsterSpiderLiveTargets3";
        const bool capturesLowerPole = token != "AsterSpiderLiveTargets1";
        demand(count > 0);
        demand(count <= 10000);
        std::vector<SpiderLiveFrame> frames;
        for (std::size_t index = 0; index < count; ++index)
        {
            SpiderLiveFrame frame;
            frame.CapturesLowerPole = capturesLowerPole;
            std::size_t captureIndex = count;
            demand(static_cast<bool>(input >> token >> captureIndex >> frame.Tick >> frame.Delta >> frame.Position.X >>
                                     frame.Position.Y >> frame.Position.Z >> frame.Rotation.X >> frame.Rotation.Y >>
                                     frame.Rotation.Z >> frame.Rotation.W >> frame.Scale.X >> frame.Scale.Y >>
                                     frame.Scale.Z));
            demand(token == "frame");
            demand(captureIndex == index);
            if (!frames.empty())
                demand(frame.Tick >= frames.back().Tick);
            demand(std::isfinite(frame.Delta));
            demand(frame.Delta > 0);
            demand(frame.Delta <= .1F);
            demand(Keire::Math::IsFinite(frame.Position));
            demand(frame.Scale == Keire::Vector3{40, 40, 40});
            const auto& q = frame.Rotation;
            const float norm = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
            demand(std::isfinite(norm));
            demand(norm == doctest::Approx(1).epsilon(.00001));
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                std::size_t actual = 99;
                int active = -1, planted = -1;
                auto& target = frame.Legs[leg];
                demand(static_cast<bool>(input >> token >> actual >> active >> planted >> target.Foot.X >>
                                         target.Foot.Y >> target.Foot.Z >> target.Ankle.X >> target.Ankle.Y >>
                                         target.Ankle.Z));
                if (capturesLowerPole)
                {
                    demand(static_cast<bool>(input >> target.LowerPole.X >> target.LowerPole.Y >> target.LowerPole.Z));
                    demand(Keire::Math::IsFinite(target.LowerPole));
                }
                demand(token == "leg");
                demand(actual == leg);
                demand((active == 0 || active == 1));
                demand((planted == 0 || planted == 1));

                demand(Keire::Math::IsFinite(target.Foot));
                demand(Keire::Math::IsFinite(target.Ankle));
                if (frame.Tick == 0 && !startupContacts)
                    demand(active == 0);
                target.Active = active == 1;
                target.Planted = planted == 1;
            }
            frames.push_back(frame);
        }
        std::size_t ended = 0;
        demand(static_cast<bool>(input >> token >> ended));
        demand(token == "end");
        demand(ended == count);
        demand(!static_cast<bool>(input >> token));
        demand(input.eof());
        return frames;
    }

    std::vector<SpiderLiveFrame> ReadSpiderLiveFrames()
    {
        std::ifstream input(SpiderFixtureRoot / "live-targets.txt");
        REQUIRE(input.is_open());
        return ReadSpiderLiveFrames(input);
    }

    struct SpiderBounds
    {
        Keire::Vector3 Minimum{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::infinity()};
        Keire::Vector3 Maximum{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()};
        void Add(Keire::Vector3 p)
        {
            Minimum = {std::min(Minimum.X, p.X), std::min(Minimum.Y, p.Y), std::min(Minimum.Z, p.Z)};
            Maximum = {std::max(Maximum.X, p.X), std::max(Maximum.Y, p.Y), std::max(Maximum.Z, p.Z)};
        }
        bool Disjoint(const SpiderBounds& other) const
        {
            // Arithmetic enclosure only: this never relaxes narrowphase clearance.
            const float magnitude =
                std::max({1.0F, std::abs(Minimum.X), std::abs(Minimum.Y), std::abs(Minimum.Z), std::abs(Maximum.X),
                          std::abs(Maximum.Y), std::abs(Maximum.Z), std::abs(other.Minimum.X),
                          std::abs(other.Minimum.Y), std::abs(other.Minimum.Z), std::abs(other.Maximum.X),
                          std::abs(other.Maximum.Y), std::abs(other.Maximum.Z)});
            const double pad = 64.0 * std::numeric_limits<float>::epsilon() * magnitude;
            return double(Maximum.X) + pad < other.Minimum.X || double(other.Maximum.X) + pad < Minimum.X ||
                   double(Maximum.Y) + pad < other.Minimum.Y || double(other.Maximum.Y) + pad < Minimum.Y ||
                   double(Maximum.Z) + pad < other.Minimum.Z || double(other.Maximum.Z) + pad < Minimum.Z;
        }
    };

    struct SpiderTerrainBox
    {
        std::string Identity;
        std::string Name;
        Keire::Matrix4 Inverse;
        Keire::Vector3 Center;
        Keire::Vector3 HalfExtent;
        Keire::Vector3 Scale;
        bool Active = true;

        SpiderBounds Bounds() const
        {
            SpiderBounds bounds;
            const auto world = Keire::Math::Inverse(Inverse);
            for (int x : {-1, 1})
                for (int y : {-1, 1})
                    for (int z : {-1, 1})
                        bounds.Add(Keire::Math::TransformPoint(
                            world,
                            {Center.X + x * HalfExtent.X, Center.Y + y * HalfExtent.Y, Center.Z + z * HalfExtent.Z}));
            return bounds;
        }

        double TriangleInterior(Keire::Vector3 first, Keire::Vector3 second, Keire::Vector3 third) const
        {
            if (!Active)
                return 0;
            using V = std::array<double, 3>;
            const auto local = [&](Keire::Vector3 p)
            {
                auto v = Keire::Math::TransformPoint(Inverse, p);
                return V{double(v.X) - Center.X, double(v.Y) - Center.Y, double(v.Z) - Center.Z};
            };
            const V a = local(first), b = local(second), c = local(third),
                    extent{HalfExtent.X, HalfExtent.Y, HalfExtent.Z};
            const auto sub = [](V a, V b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
            const auto cross = [](V a, V b)
            { return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; };
            const auto dot = [](V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
            const std::array<V, 3> edges{sub(b, a), sub(c, b), sub(a, c)};
            const auto normal = cross(edges[0], sub(c, a));
            if (dot(normal, normal) == 0)
                return 0; // Zero-area triangles have no interior surface.
            double margin = std::numeric_limits<double>::infinity();
            const auto overlap = [&](V axis)
            {
                const double square = dot(axis, axis);
                if (square == 0)
                    return true;
                const double radius =
                    extent[0] * std::abs(axis[0]) + extent[1] * std::abs(axis[1]) + extent[2] * std::abs(axis[2]);
                const double lo = std::min({dot(a, axis), dot(b, axis), dot(c, axis)}),
                             hi = std::max({dot(a, axis), dot(b, axis), dot(c, axis)});
                // Strict interior; exact boundary touch has zero margin and is not penetration.
                if (lo >= radius || hi <= -radius)
                    return false;
                margin = std::min(margin, std::min(radius - lo, hi + radius) / std::sqrt(square));
                return true;
            };
            const std::array<V, 3> axes{V{1, 0, 0}, V{0, 1, 0}, V{0, 0, 1}};
            for (const auto& axis : axes)
                if (!overlap(axis))
                    return 0;
            if (!overlap(normal))
                return 0;
            for (const auto& edge : edges)
                for (const auto& axis : axes)
                    if (!overlap(cross(edge, axis)))
                        return 0;
            return margin;
        }

        bool CapsuleMayIntersect(const Keire::Vector3 first, const Keire::Vector3 second, const float radius) const
        {
            if (!Active)
                return false;
            const auto a = Keire::Math::TransformPoint(Inverse, first);
            const auto b = Keire::Math::TransformPoint(Inverse, second);
            const std::array<float, 3> origin{a.X - Center.X, a.Y - Center.Y, a.Z - Center.Z};
            const std::array<float, 3> delta{b.X - a.X, b.Y - a.Y, b.Z - a.Z};
            const std::array<float, 3> extent{HalfExtent.X + radius / Scale.X, HalfExtent.Y + radius / Scale.Y,
                                              HalfExtent.Z + radius / Scale.Z};
            float begin = 0, end = 1;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (std::abs(delta[axis]) < 1e-12F)
                {
                    if (std::abs(origin[axis]) > extent[axis])
                        return false;
                    continue;
                }
                auto low = (-extent[axis] - origin[axis]) / delta[axis];
                auto high = (extent[axis] - origin[axis]) / delta[axis];
                if (low > high)
                    std::swap(low, high);
                begin = std::max(begin, low);
                end = std::min(end, high);
                if (begin > end)
                    return false;
            }
            return true; // Expanded AABB is a conservative capsule enclosure, not exact collision.
        }

        float SignedDistance(const Keire::Vector3 world) const
        {
            if (!Active)
                return std::numeric_limits<float>::infinity();
            const auto point = Keire::Math::TransformPoint(Inverse, world);
            const Keire::Vector3 d{(std::abs(point.X - Center.X) - HalfExtent.X) * Scale.X,
                                   (std::abs(point.Y - Center.Y) - HalfExtent.Y) * Scale.Y,
                                   (std::abs(point.Z - Center.Z) - HalfExtent.Z) * Scale.Z};
            const auto x = std::max(d.X, 0.0F), y = std::max(d.Y, 0.0F), z = std::max(d.Z, 0.0F);
            return std::sqrt(x * x + y * y + z * z) + std::min(std::max({d.X, d.Y, d.Z}), 0.0F);
        }
    };

    std::vector<SpiderTerrainBox> ReadSpiderTerrainBoxes(const bool complete = false)
    {
        auto definition =
            Keire::SceneAsset::Decode(ReadSpiderBytes(SpiderFixtureRoot / "live-scene.keirescene"))->Definition();
        // Retain hierarchy and actual collider serialization, without loading gameplay scripts or assets.
        for (auto& object : definition.Objects)
            std::erase_if(object.Components, [](const auto& component)
                          { return component.Type != Keire::ColliderComponent::StaticType(); });
        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), std::move(definition));
        std::vector<SpiderTerrainBox> boxes;
        for (const auto& entity : scene->Entities())
        {
            const auto id = entity.Id().ToString();
            if (!complete && id != "e9752120-5df4-504c-9aa0-74c04669d313" &&
                id != "b72bc53d-f782-5699-baa7-6b4af645d8f0")
                continue;
            const auto collider = entity.GetComponent<Keire::ColliderComponent>();
            if (complete && (!collider || !collider->Enabled() || !entity.ActiveInHierarchy() || collider->Trigger()))
                continue;
            REQUIRE(collider);
            REQUIRE(collider->Enabled());
            REQUIRE(entity.ActiveInHierarchy());
            REQUIRE_FALSE(collider->Trigger());
            REQUIRE(collider->Shape() == Keire::ColliderShape::Box);
            const auto matrix = entity.GetComponent<Keire::TransformComponent>()->WorldMatrix();
            Keire::Vector3 position, scale;
            Keire::Quaternion rotation;
            REQUIRE(Keire::Math::DecomposeTransform(matrix, position, rotation, scale));
            // These two authored terrain boxes are unrotated. Do not infer bounds from a screenshot.
            if (!complete)
            {
                REQUIRE(rotation.X == doctest::Approx(0));
                REQUIRE(rotation.Y == doctest::Approx(0));
                REQUIRE(rotation.Z == doctest::Approx(0));
            }
            boxes.push_back({id,
                             entity.Name(),
                             Keire::Math::Inverse(matrix),
                             collider->Center(),
                             collider->HalfExtent(),
                             {std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z)}});
        }
        scene->Close();
        REQUIRE(boxes.size() == (complete ? 35 : 2));
        return boxes;
    }

    std::vector<std::vector<SpiderTerrainBox>>
    ReadSpiderTerrainSnapshots(std::istream& input, const std::vector<SpiderTerrainBox>& inventory,
                               const std::vector<SpiderLiveFrame>& frames)
    {
        const auto demand = [](bool valid)
        {
            if (!valid)
                throw std::runtime_error("Invalid complete spider terrain sidecar");
        };
        std::string token;
        std::size_t count = 0, boxes = 0;
        demand(static_cast<bool>(input >> token >> count >> boxes));
        demand(token == "AsterSpiderTerrain1" && count > 0 && count <= 8192 && count == frames.size() && boxes > 0 &&
               boxes <= 64 && boxes == inventory.size());
        std::vector<SpiderTerrainBox> ordered;
        for (std::size_t index = 0; index < boxes; ++index)
        {
            std::size_t actual = boxes;
            std::string id;
            demand(static_cast<bool>(input >> token >> actual >> id));
            demand(token == "box" && actual == index);
            auto found =
                std::find_if(inventory.begin(), inventory.end(), [&](const auto& box) { return box.Identity == id; });
            demand(found != inventory.end());
            demand(std::none_of(ordered.begin(), ordered.end(), [&](const auto& box) { return box.Identity == id; }));
            ordered.push_back(*found);
        }
        std::vector<std::vector<SpiderTerrainBox>> result;
        result.reserve(count);
        for (std::size_t frame = 0; frame < count; ++frame)
        {
            std::size_t index = count, actualBoxes = 0;
            std::uint64_t tick = 0;
            demand(static_cast<bool>(input >> token >> index >> tick >> actualBoxes));
            demand(token == "frame" && index == frame && tick == frames[frame].Tick && actualBoxes == boxes);
            auto current = ordered;
            for (std::size_t box = 0; box < boxes; ++box)
            {
                std::size_t actual = boxes;
                int active = -1;
                Keire::Vector3 center, extent;
                Keire::Quaternion q;
                demand(static_cast<bool>(input >> token >> actual >> active >> center.X >> center.Y >> center.Z >>
                                         q.X >> q.Y >> q.Z >> q.W >> extent.X >> extent.Y >> extent.Z));
                const float norm = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
                demand(token == "obb" && actual == box && (active == 0 || active == 1) &&
                       Keire::Math::IsFinite(center) && Keire::Math::IsFinite(extent) && extent.X > 0 && extent.Y > 0 &&
                       extent.Z > 0 && std::isfinite(norm) && std::abs(norm - 1) < .00001F);
                current[box].Inverse = Keire::Math::Inverse(Keire::Math::ComposeTransform(center, q, {1, 1, 1}));
                current[box].Center = {};
                current[box].HalfExtent = extent;
                current[box].Scale = {1, 1, 1};
                current[box].Active = active == 1;
            }
            result.push_back(std::move(current));
        }
        std::size_t end = 0;
        demand(static_cast<bool>(input >> token >> end));
        demand(token == "end" && end == count);
        demand(!(input >> token) && input.eof());
        return result;
    }

    Keire::Vector3 SpiderNearestSegment(Keire::Vector3 point, Keire::Vector3 a, Keire::Vector3 b)
    {
        const Keire::Vector3 ab{b.X - a.X, b.Y - a.Y, b.Z - a.Z};
        const float size = ab.X * ab.X + ab.Y * ab.Y + ab.Z * ab.Z;
        const float t =
            size > 0 ? std::clamp(((point.X - a.X) * ab.X + (point.Y - a.Y) * ab.Y + (point.Z - a.Z) * ab.Z) / size,
                                  0.0F, 1.0F)
                     : 0;
        return {a.X + ab.X * t, a.Y + ab.Y * t, a.Z + ab.Z * t};
    }

    struct SpiderUpperEnvelope
    {
        std::vector<std::size_t> Vertices;
        float BindRadius = 0, MaximumRadius = 0, MaximumCapsule = 0;
        std::size_t Frames = 0;
        std::array<std::size_t, 64> MayIntersect{}, ActualVertexPenetration{};
    };

    struct SpiderPoseSession
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("spider-published-pose");
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::Ref<Keire::AssetSystem> Assets;
        Keire::Ref<Keire::Scene> Scene;
        Keire::Ref<Keire::SceneRuntimeSession> Session;

        ~SpiderPoseSession()
        {
            if (Session)
                Session->Stop();
            if (Scene)
                Scene->Close();
            if (Assets)
                Assets->Close();
            Database = {};
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        void Write(const std::string& name, const std::vector<std::byte>& bytes)
        {
            std::ofstream stream(Root / "Assets" / name, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }
    };
} // namespace

static void CheckSpiderPublishedPose(const bool insetStance, const bool helperTargets = false,
                                     const bool liveTargets = false, const bool authoredLowerPole = false,
                                     const unsigned exportUpperRest = 0, const bool exportPosedWitness = false,
                                     const bool completeTerrain = false, const bool originalCaptureDefects = false)
{
    if (originalCaptureDefects)
    {
        REQUIRE(liveTargets);
        REQUIRE_FALSE(authoredLowerPole);
        REQUIRE_FALSE(completeTerrain);
        REQUIRE(IsOriginalSpiderLiveCapture());
    }
    struct ExportedTarget
    {
        Keire::Vector3 Foot;
        Keire::Vector3 Ankle;
    };
    std::vector<ExportedTarget> exported;
    if (helperTargets)
    {
        std::ifstream input(SpiderFixtureRoot / "AnkleProposal" / "candidate-targets.txt");
        REQUIRE(input.is_open());
        std::string header;
        REQUIRE(static_cast<bool>(input >> header));
        REQUIRE(header == "AsterSpiderAnkleTargets1");
        for (int expected = 0; expected < 180 * 8; ++expected)
        {
            int frame = -1, leg = -1, accepted = 0;
            ExportedTarget target;
            REQUIRE(static_cast<bool>(input >> frame >> leg >> accepted >> target.Foot.X >> target.Foot.Y >>
                                      target.Foot.Z >> target.Ankle.X >> target.Ankle.Y >> target.Ankle.Z));
            REQUIRE(frame == expected / 8);
            REQUIRE(leg == expected % 8);
            REQUIRE(accepted == 1);
            REQUIRE(Keire::Math::IsFinite(target.Foot));
            REQUIRE(Keire::Math::IsFinite(target.Ankle));
            exported.push_back(target);
        }
        std::string extra;
        REQUIRE_FALSE(static_cast<bool>(input >> extra));
        REQUIRE(input.eof());
    }
    const auto liveFrames = liveTargets ? ReadSpiderLiveFrames() : std::vector<SpiderLiveFrame>{};
    if (originalCaptureDefects)
    {
        REQUIRE(liveFrames.size() == 223);
        REQUIRE_FALSE(liveFrames.front().CapturesLowerPole);
        REQUIRE(liveFrames[91].Tick == 923);
        REQUIRE(liveFrames[116].Tick == 1033);
        REQUIRE(liveFrames[161].Tick == 1474);
    }
    for (const auto& captured : liveFrames)
        REQUIRE((!captured.CapturesLowerPole || authoredLowerPole));
    auto terrain = liveTargets ? ReadSpiderTerrainBoxes(completeTerrain) : std::vector<SpiderTerrainBox>{};
    std::vector<std::vector<SpiderTerrainBox>> terrainFrames;
    if (completeTerrain)
    {
        REQUIRE(liveTargets);
        REQUIRE(authoredLowerPole);
        for (const auto& frame : liveFrames)
            REQUIRE(frame.CapturesLowerPole);
        std::ifstream input(SpiderFixtureRoot / "live-terrain.txt");
        REQUIRE(input.is_open());
        terrainFrames = ReadSpiderTerrainSnapshots(input, terrain, liveFrames);
    }
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.SourcePath = std::filesystem::absolute(SpiderFixtureRoot / "WolfSpider.glb");
    context.RelativePath = context.SourcePath.filename();
    context.ImportSettings["materialImport"] = std::string("none");
    context.ImportSettings["rigSource"] = std::string("embedded");
    context.ImportSettings["rigProfile"] = std::string("custom");
    context.ImportSettings["maximumInfluences"] = std::string("4");
    context.ImportSettings["skinningMethod"] = std::string("linearBlend");
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&](std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto imported =
        Keire::CreateMeshAssetImporter().ContextualImport(context, ReadSpiderBytes(context.SourcePath));
    const auto find = [&](Keire::AssetTypeId type)
    {
        const auto found = std::ranges::find(imported.SubAssets, type, &Keire::AssetGeneratedSubAsset::Type);
        REQUIRE(found != imported.SubAssets.end());
        return found->Bytes;
    };
    const auto skeleton = Keire::SkeletonAsset::Decode(find(Keire::SkeletonAsset::StaticType()));
    const auto skin = Keire::SkinnedMeshAsset::Decode(find(Keire::SkinnedMeshAsset::StaticType()));
    const auto mesh = Keire::MeshAsset::Decode(imported.Bytes);
    const auto clip = Keire::AnimationClipAsset::Decode(ReadSpiderBytes(SpiderFixtureRoot / "SpiderRest.keireanim"));
    // This cache payload is the clip's actual source skeleton, not an assumed fresh-import ordering.
    const auto actualSkeleton =
        Keire::SkeletonAsset::Decode(ReadSpiderBytes(SpiderFixtureRoot / "ActualSkeleton.keireskeleton"));
    REQUIRE(clip->Skeleton().ToString() == "871f5b63-6727-5c8b-8bbe-fea268d3e069");
    REQUIRE(actualSkeleton->Bones().size() == skeleton->Bones().size());
    for (std::size_t bone = 0; bone < skeleton->Bones().size(); ++bone)
    {
        REQUIRE(actualSkeleton->Bones()[bone].Name == skeleton->Bones()[bone].Name);
        REQUIRE(actualSkeleton->Bones()[bone].Parent == skeleton->Bones()[bone].Parent);
        REQUIRE(actualSkeleton->Bones()[bone].BindPose == skeleton->Bones()[bone].BindPose);
        REQUIRE(actualSkeleton->Bones()[bone].InverseBindPose == skeleton->Bones()[bone].InverseBindPose);
    }
    for (const auto& track : clip->Tracks())
        REQUIRE(track.Bone < skeleton->Bones().size());

    // Resolve the actual A/B/D/E/F/end anatomy by names and parent topology, never importer indices.
    std::vector<std::vector<std::uint32_t>> chains;
    for (std::uint32_t end = 0; end < skeleton->Bones().size(); ++end)
    {
        const auto& name = skeleton->Bones()[end].Name;
        if (!name.starts_with("LegSegmentF.") || name.find("_end_") == std::string::npos)
            continue;
        std::vector<std::uint32_t> chain;
        for (auto bone = static_cast<std::int32_t>(end); bone >= 0; bone = skeleton->Bones()[bone].Parent)
        {
            chain.push_back(static_cast<std::uint32_t>(bone));
            if (skeleton->Bones()[bone].Name.starts_with("LegSegmentA."))
                break;
        }
        std::ranges::reverse(chain);
        REQUIRE(chain.size() == 6);
        chains.push_back(std::move(chain));
    }
    std::ranges::sort(chains, [&](const auto& a, const auto& b)
                      { return skeleton->Bones()[a.front()].Name < skeleton->Bones()[b.front()].Name; });
    REQUIRE(chains.size() == 8);
    const auto sceneDefinition =
        Keire::SceneAsset::Decode(ReadSpiderBytes(SpiderFixtureRoot / "StarterScene.keirescene"))->Definition();
    const auto authoredSpider =
        std::ranges::find(sceneDefinition.Objects, std::string("Scout Spider"), &Keire::SceneObjectDefinition::Name);
    REQUIRE(authoredSpider != sceneDefinition.Objects.end());
    REQUIRE(authoredSpider->Transform.Scale == Keire::Vector3{40, 40, 40});

    SpiderPoseSession fixture;
    std::filesystem::create_directories(fixture.Root / "Assets");
    Keire::AssetDatabaseSpecification database;
    database.ProjectRoot = fixture.Root;
    for (const auto& [extension, type] : std::array{std::pair{".spiderskeleton", Keire::SkeletonAsset::StaticType()},
                                                    std::pair{".spiderclip", Keire::AnimationClipAsset::StaticType()},
                                                    std::pair{".spidergraph", Keire::AnimationGraphAsset::StaticType()},
                                                    std::pair{".spidermesh", Keire::MeshAsset::StaticType()},
                                                    std::pair{".spiderskin", Keire::SkinnedMeshAsset::StaticType()}})
    {
        Keire::AssetImporterRegistration registration;
        registration.Name = std::string("SpiderPose") + extension;
        registration.Type = type;
        registration.Extensions = {extension};
        registration.Import = [](std::span<const std::byte> bytes)
        { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
        database.Importers.push_back(std::move(registration));
    }
    fixture.Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(database));
    fixture.Write("Rig.spiderskeleton", Keire::SkeletonAsset::Encode(skeleton->Bones()));
    fixture.Write("Mesh.spidermesh", Keire::MeshAsset::Encode(mesh->Vertices(), mesh->Indices()));
    (void)fixture.Database->ImportAll();
    const auto rig = fixture.Database->Find("Rig.spiderskeleton");
    const auto geometry = fixture.Database->Find("Mesh.spidermesh");
    REQUIRE(rig);
    REQUIRE(geometry);
    fixture.Write("Skin.spiderskin",
                  Keire::SkinnedMeshAsset::Encode(geometry->Id, rig->Id, skin->Influences8(), skin->Method()));
    fixture.Write("Rest.spiderclip",
                  Keire::AnimationClipAsset::Encode(rig->Id, clip->Duration(), clip->Tracks(), {}, false));
    (void)fixture.Database->ImportAll();
    const auto rest = fixture.Database->Find("Rest.spiderclip");
    REQUIRE(rest);
    Keire::AnimationGraphDefinition graph;
    graph.EntryState = "Rest";
    graph.States = {{"Rest", rest->Id, 1.0F}};
    fixture.Write("Graph.spidergraph", Keire::AnimationGraphAsset::Encode(graph));
    const auto catalog = fixture.Database->ImportAll();
    Keire::AssetSystemSpecification assets;
    assets.Mode = Keire::AssetMode::Development;
    assets.DevelopmentCatalog = catalog.CatalogPath;
    assets.Decoders = {Keire::CreateSkeletonAssetDecoder(), Keire::CreateAnimationClipAssetDecoder(),
                       Keire::CreateAnimationGraphAssetDecoder(), Keire::CreateSkinnedMeshAssetDecoder(),
                       Keire::CreateMeshAssetDecoder()};
    fixture.Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assets));
    fixture.Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
    auto actor = fixture.Scene->CreateEntity("Scout Spider pose fixture");
    actor.GetComponent<Keire::TransformComponent>()->SetLocalScale(authoredSpider->Transform.Scale);
    // Normalize authored X/Z to a flat infinite fixture plane; preserve the authored root height.
    actor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, authoredSpider->Transform.Position.Y, 0});
    auto animator = actor.AddComponent<Keire::AnimatorComponent>();
    animator->SetSkeleton(rig->Id);
    animator->SetSkinnedMesh(fixture.Database->Find("Skin.spiderskin")->Id);
    animator->SetGraph(fixture.Database->Find("Graph.spidergraph")->Id);
    animator->SetApplyRootMotion(false);
    fixture.Session = Keire::CreateRef<Keire::SceneRuntimeSession>(fixture.Scene, fixture.Assets);
    fixture.Session->Play();
    actor = fixture.Session->RuntimeScene()->FindEntity(actor.Id());
    animator = actor.GetComponent<Keire::AnimatorComponent>();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!animator->RuntimeDebugSnapshot() || animator->SkinPalette().empty()) &&
           std::chrono::steady_clock::now() < deadline)
    {
        (void)fixture.Assets->PumpCompletions();
        fixture.Session->Update(0, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(animator->RuntimeDebugSnapshot());
    REQUIRE(animator->SkinPalette().size() == skeleton->Bones().size());
    animator->Play("Rest", {}, 0);
    fixture.Session->Update(0, 1);
    const auto reference = animator->RuntimeDebugSnapshot()->Pose;
    const auto transform = actor.GetComponent<Keire::TransformComponent>();
    auto world = transform->PresentationWorldMatrix();
    if (exportUpperRest)
    {
        const bool fullLeg = exportUpperRest == 2;
        const std::size_t jointCount = fullLeg ? 6 : 4;
        const auto output = SpiderFixtureRoot / (fullLeg ? "leg-skin-export-v2.json" : "upper-skin-export.json");
        REQUIRE_FALSE(std::filesystem::exists(output));
        std::ofstream file(output);
        REQUIRE(file.is_open());
        file << std::setprecision(9);
        const auto vector = [&](Keire::Vector3 v) { file << "[" << v.X << "," << v.Y << "," << v.Z << "]"; };
        const auto origin = Keire::Math::TransformPoint(world, {});
        const auto relative = [&](Keire::Vector3 v)
        { return Keire::Vector3{v.X - origin.X, v.Y - origin.Y, v.Z - origin.Z}; };
        std::vector<Keire::MeshVertex> restSkin(mesh->Vertices().size());
        Keire::SkinMeshCpu(mesh->Vertices(), skin->Influences8(), animator->SkinPalette(), skin->Method(), restSkin);
        std::size_t totalVertices = 0;
        float maxError = 0;
        file << "{\"schema\":" << exportUpperRest << ",\"scale\":40,\"prefixUpper\":4,\"legs\":[";
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            if (leg)
                file << ",";
            file << "{\"leg\":" << leg << ",\"joints\":[";
            for (std::size_t joint = 0; joint < jointCount; ++joint)
            {
                if (joint)
                    file << ",";
                vector(relative(Keire::Math::TransformPoint(world, reference[chains[leg][joint]].WorldPosition)));
            }
            auto basis = Keire::Math::ComposeTransform({}, {}, {1, 1, 1});
            std::vector<std::size_t> lineage;
            for (std::int32_t bone = static_cast<std::int32_t>(chains[leg][0]); bone >= 0;
                 bone = reference[bone].Parent)
                lineage.push_back(static_cast<std::size_t>(bone));
            for (auto it = lineage.rbegin(); it != lineage.rend(); ++it)
                basis = Keire::Math::Multiply(
                    basis, Keire::Math::ComposeTransform({}, reference[*it].LocalTransform.Rotation, {1, 1, 1}));
            file << "],\"basisX\":";
            vector(Keire::Math::TransformDirection(basis, {1, 0, 0}));
            file << ",\"basisZ\":";
            vector(Keire::Math::TransformDirection(basis, {0, 0, 1}));
            file << ",\"bones\":[";
            for (std::size_t j = 0; j < jointCount; ++j)
            {
                if (j)
                    file << ",";
                const auto index = chains[leg][j];
                auto rotation = Keire::Math::ComposeTransform({}, {}, {1, 1, 1});
                std::vector<std::size_t> ancestors;
                for (std::int32_t bone = static_cast<std::int32_t>(index); bone >= 0; bone = reference[bone].Parent)
                    ancestors.push_back(static_cast<std::size_t>(bone));
                for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
                    rotation = Keire::Math::Multiply(
                        rotation, Keire::Math::ComposeTransform({}, reference[*it].LocalTransform.Rotation, {1, 1, 1}));
                file << "{\"name\":\"" << skeleton->Bones()[index].Name << "\",\"index\":" << index << ",\"basisX\":";
                vector(Keire::Math::TransformDirection(rotation, {1, 0, 0}));
                file << ",\"basisZ\":";
                vector(Keire::Math::TransformDirection(rotation, {0, 0, 1}));
                file << "}";
            }
            file << "]";
            file << ",\"vertices\":[";
            bool first = true;
            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
            {
                const auto& influence = skin->Influences8()[vertex];
                bool classified = true;
                float total = 0;
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    REQUIRE(std::isfinite(influence.Weights[slot]));
                    REQUIRE(influence.Weights[slot] >= 0);
                    if (influence.Weights[slot] <= 0)
                        continue;
                    total += influence.Weights[slot];
                    classified &= std::find(chains[leg].begin(), chains[leg].begin() + jointCount,
                                            influence.Bones[slot]) != chains[leg].begin() + jointCount;
                }
                if (!classified || total <= 0)
                    continue;
                REQUIRE(std::isfinite(total));
                if (!first)
                    file << ",";
                first = false;
                ++totalVertices;
                const auto actual = relative(Keire::Math::TransformPoint(world, restSkin[vertex].Position));
                file << "{\"index\":" << vertex << ",\"actual\":";
                vector(actual);
                file << ",\"influences\":[";
                bool firstInfluence = true;
                Keire::Vector3 reconstructed{};
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    if (influence.Weights[slot] <= 0)
                        continue;
                    const auto bone = influence.Bones[slot];
                    const auto joint =
                        std::find(chains[leg].begin(), chains[leg].begin() + jointCount, bone) - chains[leg].begin();
                    const auto point = relative(
                        Keire::Math::TransformPoint(Keire::Math::Multiply(world, animator->SkinPalette()[bone]),
                                                    mesh->Vertices()[vertex].Position));
                    REQUIRE(Keire::Math::IsFinite(point));
                    const float weight = influence.Weights[slot] / total;
                    reconstructed.X += point.X * weight;
                    reconstructed.Y += point.Y * weight;
                    reconstructed.Z += point.Z * weight;
                    if (!firstInfluence)
                        file << ",";
                    firstInfluence = false;
                    file << "{\"joint\":" << joint << ",\"weight\":" << weight << ",\"point\":";
                    vector(point);
                    file << "}";
                }
                const float error = SpiderPoseDistance(actual, reconstructed);
                maxError = std::max(maxError, error);
                // Float palette transforms, normalized accumulation and actor scaling have independent rounding.
                const float magnitude = std::max({1.0F, std::abs(actual.X), std::abs(actual.Y), std::abs(actual.Z)});
                CHECK(error <= 32 * std::numeric_limits<float>::epsilon() * magnitude);
                file << "]}";
            }
            file << "]}";
        }
        REQUIRE(totalVertices == (fullLeg ? 3872 : 2852));
        file << "],\"vertices\":" << totalVertices << ",\"maximumRestReconstructionError\":" << maxError << "}";
        file.close();
        REQUIRE(file.good());
        MESSAGE("Spider upper Rest exported vertices=" << totalVertices << " maximumError=" << maxError);
        return;
    }

    std::array<Keire::Vector3, 8> neutral{};
    // Actual ScoutSpider.NeutralFoot centers, in L001..004/R001..004 order. Rest tips are not stance targets.
    const std::array<Keire::Vector3, 8> centers{{{-.0103904444F, .0027942480F, .0032302858F},
                                                 {-.0160271516F, .0019407120F, -.0048169364F},
                                                 {-.0152319351F, .0008578870F, -.0126015627F},
                                                 {-.0125467619F, .0029624149F, -.0212374057F},
                                                 {.0103904424F, .0027942442F, .0032302871F},
                                                 {.0160271443F, .0019407060F, -.0048169402F},
                                                 {.0152319417F, .0008578794F, -.0126015723F},
                                                 {.0125467479F, .0029624085F, -.0212374099F}}};
    std::array<std::vector<std::size_t>, 8> distal;
    for (std::size_t leg = 0; leg < 8; ++leg)
    {
        neutral[leg] = Keire::Math::TransformPoint(world, centers[leg]);
        neutral[leg].Y = .035F;
        if (insetStance)
        {
            // Solver isolation only: explicitly narrower feet, never represented as the game's neutral stance.
            const auto root = Keire::Math::TransformPoint(world, reference[chains[leg][0]].WorldPosition);
            const auto x = root.X - neutral[leg].X, z = root.Z - neutral[leg].Z;
            const auto distance = std::sqrt(x * x + z * z);
            REQUIRE(distance > .08F);
            neutral[leg].X += .08F * x / distance;
            neutral[leg].Z += .08F * z / distance;
        }
        for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
        {
            const auto& influence = skin->Influences8()[vertex];
            float weight = 0;
            for (std::size_t slot = 0; slot < influence.Count; ++slot)
                if (influence.Bones[slot] == chains[leg][4] || influence.Bones[slot] == chains[leg][5])
                    weight += influence.Weights[slot];
            if (weight >= .75F)
                distal[leg].push_back(vertex);
        }
        REQUIRE_FALSE(distal[leg].empty());
        MESSAGE("Spider distal subset leg=" << leg << " selected=" << distal[leg].size()
                                            << " excluded=" << mesh->Vertices().size() - distal[leg].size()
                                            << " total=" << mesh->Vertices().size() << " minimum distal weight=.75");
    }
    std::array<SpiderUpperEnvelope, 8> envelopes;
    std::vector<Keire::Vector3> bindOrigins;
    for (const auto& bone : skeleton->Bones())
        bindOrigins.push_back(Keire::Math::TransformPoint(Keire::Math::Inverse(bone.InverseBindPose), {}));
    if (liveTargets)
    {
        REQUIRE(skin->Method() == Keire::SkinningMethod::LinearBlend);
        for (std::size_t leg = 0; leg < 8; ++leg)
            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
            {
                const auto& influence = skin->Influences8()[vertex];
                bool classified = true;
                float total = 0, radius = 0;
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    const float weight = influence.Weights[slot];
                    if (weight <= 0)
                        continue;
                    const auto bone = influence.Bones[slot];
                    const auto found = std::find(chains[leg].begin(), chains[leg].begin() + 4, bone);
                    if (found == chains[leg].begin() + 4)
                    {
                        classified = false;
                        break;
                    }
                    const auto index = static_cast<std::size_t>(found - chains[leg].begin());
                    const auto nearest =
                        SpiderNearestSegment(mesh->Vertices()[vertex].Position, bindOrigins[bone],
                                             bindOrigins[chains[leg][std::min(index + 1, std::size_t{3})]]);
                    radius += weight * SpiderPoseDistance(mesh->Vertices()[vertex].Position, nearest) * 40;
                    total += weight;
                }
                if (classified && total > 0)
                {
                    envelopes[leg].Vertices.push_back(vertex);
                    envelopes[leg].BindRadius = std::max(envelopes[leg].BindRadius, radius / total);
                }
            }
    }
    std::array<std::vector<std::size_t>, 8> terminalVertices, deVertices;
    std::array<float, 8> deRestRadius{}, dePoseRadius{};
    std::array<std::array<Keire::Vector3, 4>, 8> worstUpperJoints;
    std::array<float, 8> terminalRestRadius{}, terminalPoseRadius{};
    if (authoredLowerPole)
    {
        std::vector<Keire::MeshVertex> restSkin(mesh->Vertices().size());
        Keire::SkinMeshCpu(mesh->Vertices(), skin->Influences8(), animator->SkinPalette(), skin->Method(), restSkin);
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            const auto d = chains[leg][2], e = chains[leg][3];
            const auto a = Keire::Math::TransformPoint(world, reference[d].WorldPosition);
            const auto b = Keire::Math::TransformPoint(world, reference[e].WorldPosition);
            for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
            {
                const auto& influence = skin->Influences8()[vertex];
                bool onlyDE = true;
                float total = 0;
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    if (influence.Weights[slot] <= 0)
                        continue;
                    total += influence.Weights[slot];
                    onlyDE &= influence.Bones[slot] == d || influence.Bones[slot] == e;
                }
                if (!onlyDE || total <= 0)
                    continue;
                deVertices[leg].push_back(vertex);
                const auto point = Keire::Math::TransformPoint(world, restSkin[vertex].Position);
                deRestRadius[leg] =
                    std::max(deRestRadius[leg], SpiderPoseDistance(point, SpiderNearestSegment(point, a, b)));
            }
        }
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            std::size_t classified = 0;
            float bindCapsuleRadius = 0, bindTipRadius = 0, restCapsuleRadius = 0, restTipRadius = 0;
            const auto f = chains[leg][4], tip = chains[leg][5];
            const auto restF = Keire::Math::TransformPoint(world, reference[f].WorldPosition);
            const auto restTip = Keire::Math::TransformPoint(world, reference[tip].WorldPosition);
            for (std::size_t vertex = 0; vertex < mesh->Vertices().size(); ++vertex)
            {
                const auto& influence = skin->Influences8()[vertex];
                bool terminalOnly = true;
                float total = 0;
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    if (influence.Weights[slot] <= 0)
                        continue;
                    terminalOnly &= influence.Bones[slot] == f || influence.Bones[slot] == tip;
                    total += influence.Weights[slot];
                }
                if (!terminalOnly || total <= 0)
                    continue;
                ++classified;
                terminalVertices[leg].push_back(vertex);
                const auto bind = mesh->Vertices()[vertex].Position;
                bindCapsuleRadius = std::max(
                    bindCapsuleRadius,
                    40 * SpiderPoseDistance(bind, SpiderNearestSegment(bind, bindOrigins[f], bindOrigins[tip])));
                bindTipRadius = std::max(bindTipRadius, 40 * SpiderPoseDistance(bind, bindOrigins[tip]));
                const auto point = Keire::Math::TransformPoint(world, restSkin[vertex].Position);
                restCapsuleRadius =
                    std::max(restCapsuleRadius, SpiderPoseDistance(point, SpiderNearestSegment(point, restF, restTip)));
                restTipRadius = std::max(restTipRadius, SpiderPoseDistance(point, restTip));
            }
            REQUIRE(classified > 0);
            terminalRestRadius[leg] = restTipRadius;
            MESSAGE(std::setprecision(9) << "[SpiderTerminalEnvelope] leg=" << leg << " classified=" << classified
                                         << " excluded=" << mesh->Vertices().size() - classified
                                         << " bindSegmentRadius=" << bindCapsuleRadius << " bindTipSphereRadius="
                                         << bindTipRadius << " restSegmentRadius=" << restCapsuleRadius
                                         << " restTipSphereRadius=" << restTipRadius
                                         << " scope=AllPositiveInfluencesFOrEnd scale=40");
        }
    }
    std::vector<Keire::MeshVertex> deformed(mesh->Vertices().size());
    float maximumAnkleError = 0, maximumTipError = 0, minimumClearance = 100;
    float fullMeshMinimum = 100;
    int worstMeshFrame = -1;
    std::size_t worstMeshVertex = 0, worstMeshBox = 0;
    Keire::Vector3 worstMeshPosition;
    SpiderTerrainBox worstMeshTerrain;
    float fullyActiveMinimum = 100;
    int fullyActiveWorstFrame = -1, distalWorstFrame = -1;
    std::size_t fullyActiveFrames = 0, fullyActivePenetratingFrames = 0, distalWorstLeg = 0, distalWorstVertex = 0,
                distalWorstBox = 0;
    Keire::Vector3 distalWorstPosition;
    std::array<Keire::Vector3, 3> distalWorstJoints;
    const auto pointText = [](Keire::Vector3 p)
    {
        std::ostringstream text;
        text.precision(9);
        text << "(" << p.X << "," << p.Y << "," << p.Z << ")";
        return text.str();
    };
    const auto weightText = [&](std::size_t vertex)
    {
        const auto& influence = skin->Influences8()[vertex];
        std::vector<std::pair<float, std::uint32_t>> weights;
        for (std::size_t slot = 0; slot < influence.Count; ++slot)
        {
            REQUIRE(influence.Bones[slot] < skeleton->Bones().size());
            weights.emplace_back(influence.Weights[slot], influence.Bones[slot]);
        }
        std::ranges::sort(weights, std::greater{});
        std::ostringstream text;
        text.precision(9);
        for (const auto& [weight, bone] : weights)
            text << bone << ":" << skeleton->Bones()[bone].Name << ":" << weight << ";";
        return text.str();
    };
    const int frameCount = liveTargets ? static_cast<int>(liveFrames.size()) : 180;
    if (authoredLowerPole)
        MESSAGE(
            "Spider experimental lower policy=AuthoredActorRelativeTwoBone endpoints=Unchanged frames=" << frameCount);
    std::size_t activeSamples = 0, plantedSamples = 0;
    std::size_t originalOverreachSamples = 0, originalExtendedSamples = 0, originalEndpointFailures = 0;
    std::vector<int> vertexLeg(mesh->Vertices().size(), -1);
    if (completeTerrain)
    {
        std::size_t classified = 0;
        for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
        {
            const auto& influence = skin->Influences8()[vertex];
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                bool belongs = true;
                float total = 0;
                for (std::size_t slot = 0; slot < influence.Count; ++slot)
                {
                    if (influence.Weights[slot] <= 0)
                        continue;
                    total += influence.Weights[slot];
                    belongs &=
                        std::find(chains[leg].begin(), chains[leg].end(), influence.Bones[slot]) != chains[leg].end();
                }
                if (belongs && total > 0)
                {
                    vertexLeg[vertex] = static_cast<int>(leg);
                    ++classified;
                    break;
                }
            }
        }
        REQUIRE(classified == 3872);
    }
    std::ofstream activeTriangleFile;
    std::size_t activeTriangleRecords = 0;
    if (completeTerrain)
    {
        const auto trianglePath = SpiderFixtureRoot / "leg-triangles-v1.json";
        REQUIRE_FALSE(std::filesystem::exists(trianglePath));
        std::ofstream triangles(trianglePath);
        REQUIRE(triangles.is_open());
        std::vector<std::size_t> localVertex(vertexLeg.size(), 0);
        std::array<std::size_t, 8> counts{};
        for (std::size_t vertex = 0; vertex < vertexLeg.size(); ++vertex)
            if (vertexLeg[vertex] >= 0)
                localVertex[vertex] = counts[vertexLeg[vertex]]++;
        std::size_t classified = 0, other = 0;
        triangles << "{\"schema\":1,\"legs\":[";
        for (int leg = 0; leg < 8; ++leg)
        {
            if (leg)
                triangles << ",";
            triangles << "{\"leg\":" << leg << ",\"triangles\":[";
            bool first = true;
            for (std::size_t index = 0; index < mesh->Indices().size(); index += 3)
            {
                REQUIRE(index + 2 < mesh->Indices().size());
                const auto a = mesh->Indices()[index], b = mesh->Indices()[index + 1], c = mesh->Indices()[index + 2];
                REQUIRE(a < vertexLeg.size());
                REQUIRE(b < vertexLeg.size());
                REQUIRE(c < vertexLeg.size());
                if (vertexLeg[a] != leg || vertexLeg[b] != leg || vertexLeg[c] != leg)
                    continue;
                if (!first)
                    triangles << ",";
                first = false;
                ++classified;
                triangles << "{\"index\":" << index / 3 << ",\"global\":[" << a << "," << b << "," << c
                          << "],\"local\":[" << localVertex[a] << "," << localVertex[b] << "," << localVertex[c]
                          << "]}";
            }
            triangles << "]}";
        }
        other = mesh->Indices().size() / 3 - classified;
        triangles << "],\"classifiedTriangles\":" << classified << ",\"mixedOrOtherTriangles\":" << other
                  << ",\"totalTriangles\":" << mesh->Indices().size() / 3 << "}";
        triangles.close();
        REQUIRE(triangles.good());
        const auto witnesses = SpiderFixtureRoot / "active-triangle-witnesses.json";
        REQUIRE_FALSE(std::filesystem::exists(witnesses));
        activeTriangleFile.open(witnesses);
        REQUIRE(activeTriangleFile.is_open());
        activeTriangleFile << std::setprecision(9) << "{\"schema\":1,\"limit\":128,\"records\":[";
    }
    std::uint64_t fullActiveTriangleIntersections = 0, inactiveTriangleIntersections = 0, nonLegVertexIntersections = 0,
                  activeLegVertexIntersections = 0, inactiveLegVertexIntersections = 0;
    float nonLegMinimum = 100, activeLegMinimum = 100;
    int nonLegWorstFrame = -1, activeLegWorstFrame = -1, activeLegWorstLeg = -1;
    std::size_t nonLegWorstVertex = 0, activeLegWorstVertex = 0;
    std::string nonLegWorstBox, activeLegWorstBox;
    std::vector<Keire::Vector3> worldVertices(mesh->Vertices().size());
    std::vector<bool> candidateBoxes;
    std::vector<SpiderBounds> boxBounds;
    std::uint64_t skippedBoxFrames = 0, checkedBoxFrames = 0, triangleChecks = 0, triangleIntersections = 0;
    int firstTriangleFrame = -1, worstTriangleFrame = -1;
    std::size_t firstTriangle = 0, worstTriangle = 0;
    std::string worstTriangleBox, firstTriangleBox;
    SpiderTerrainBox worstTriangleTerrain;
    std::array<Keire::Vector3, 3> worstTrianglePoints;
    double maximumTriangleMargin = 0;
    if (completeTerrain)
    {
        REQUIRE(mesh->Indices().size() % 3 == 0);
        for (const auto index : mesh->Indices())
            REQUIRE(index < worldVertices.size());
    }
    for (int frame = 0; frame < frameCount; ++frame)
    {
        if (completeTerrain)
            terrain = terrainFrames[frame];
        if (liveTargets)
        {
            const auto& captured = liveFrames[frame];
            transform->SetLocalPosition(captured.Position);
            transform->SetLocalRotation(captured.Rotation);
            transform->SetLocalScale(captured.Scale);
            world = transform->PresentationWorldMatrix();
            animator->ClearIk();
        }
        std::array<Keire::Vector3, 8> tips = neutral, ankles{};
        std::array<float, 8> upperDistances{}, upperReaches{};
        // One bounded swing, with seven genuine held targets. This is not the gameplay scheduler.
        if (frame >= 60 && frame < 120)
            tips[0].Y += .06F * std::sin((frame - 60) / 60.0F * 3.14159265F);
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            const auto& chain = chains[leg];
            const auto distalReach =
                40.0F * (SpiderPoseDistance(reference[chain[3]].WorldPosition, reference[chain[4]].WorldPosition) +
                         SpiderPoseDistance(reference[chain[4]].WorldPosition, reference[chain[5]].WorldPosition));
            const float x = -tips[leg].X, z = -.4F - tips[leg].Z, length = std::sqrt(x * x + z * z);
            // ScoutSpider.CapturePresentation uses the same -0.4m bodyward pivot and .045m inset.
            // The fixed fixture root has identity rotation; this is not an arbitrary world-space pivot.
            REQUIRE(length > 0);
            ankles[leg] = {tips[leg].X + x / length * .045F, tips[leg].Y + distalReach * .88F,
                           tips[leg].Z + z / length * .045F};
            if (helperTargets)
            {
                const auto& target = exported[static_cast<std::size_t>(frame) * 8 + leg];
                REQUIRE(SpiderPoseDistance(target.Foot, tips[leg]) < .000001F);
                ankles[leg] = target.Ankle;
            }
            if (liveTargets)
            {
                const auto& target = liveFrames[frame].Legs[leg];
                tips[leg] = target.Foot;
                ankles[leg] = target.Ankle;
                if (!target.Active)
                    continue;
                ++activeSamples;
                plantedSamples += target.Planted ? 1 : 0;
            }
            float upperReach = 0, longestUpper = 0;
            for (std::size_t segment = 0; segment < 3; ++segment)
            {
                const auto size = 40.0F * SpiderPoseDistance(reference[chain[segment]].WorldPosition,
                                                             reference[chain[segment + 1]].WorldPosition);
                upperReach += size;
                longestUpper = std::max(longestUpper, size);
            }
            const auto root = Keire::Math::TransformPoint(world, reference[chain[0]].WorldPosition);
            const auto upperDistance = SpiderPoseDistance(root, ankles[leg]);
            upperDistances[leg] = upperDistance;
            upperReaches[leg] = upperReach;
            CAPTURE(frame);
            CAPTURE(leg);
            CAPTURE(distalReach);
            CAPTURE(root.X);
            CAPTURE(root.Y);
            CAPTURE(root.Z);
            CAPTURE(ankles[leg].X);
            CAPTURE(ankles[leg].Y);
            CAPTURE(ankles[leg].Z);
            if (!insetStance && !helperTargets && !liveTargets && frame == 0 && leg == 1)
            {
                // Preserve the captured old-recipe defect as an expected invalid-input regression.
                // Do not submit this impossible goal or interpret it as a native solver failure.
                CHECK(upperDistance == doctest::Approx(.539317F).epsilon(.00001));
                CHECK(upperReach == doctest::Approx(.522867F).epsilon(.00001));
                CHECK(upperDistance > upperReach);
                return;
            }
            REQUIRE(std::isfinite(upperDistance));
            if (liveTargets)
            {
                // These exact goals were already submitted by the live runtime. Preserve the failure,
                // but continue replay to collect subsequent endpoint and mesh diagnostics.
                if (originalCaptureDefects && ((frame == 91 && leg == 1) || (frame == 161 && leg == 5)))
                {
                    ++originalOverreachSamples;
                    CHECK(upperDistance > upperReach);
                    CHECK(upperDistance == doctest::Approx(frame == 91 ? .526121F : .523065F).epsilon(.00001));
                    CHECK(upperReach == doctest::Approx(frame == 91 ? .522867F : .522859F).epsilon(.00001));
                }
                else
                    CHECK(upperDistance < upperReach);
                CHECK(upperDistance >= std::max(0.0F, 2 * longestUpper - upperReach));
            }
            else
            {
                REQUIRE(upperDistance < upperReach);
                REQUIRE(upperDistance >= std::max(0.0F, 2 * longestUpper - upperReach));
            }
            const auto lowerA =
                40.0F * SpiderPoseDistance(reference[chain[3]].WorldPosition, reference[chain[4]].WorldPosition);
            const auto lowerB =
                40.0F * SpiderPoseDistance(reference[chain[4]].WorldPosition, reference[chain[5]].WorldPosition);
            const auto lowerDistance = SpiderPoseDistance(ankles[leg], tips[leg]);
            REQUIRE(std::isfinite(lowerDistance));
            if (liveTargets)
            {
                CHECK(lowerDistance < lowerA + lowerB);
                CHECK(lowerDistance >= std::abs(lowerA - lowerB));
            }
            else
            {
                REQUIRE(lowerDistance < lowerA + lowerB);
                REQUIRE(lowerDistance >= std::abs(lowerA - lowerB));
            }
            std::vector<std::string> upper, lower;
            for (std::size_t i = 0; i < 4; ++i)
                upper.push_back(skeleton->Bones()[chain[i]].Name);
            for (std::size_t i = 3; i < 6; ++i)
                lower.push_back(skeleton->Bones()[chain[i]].Name);
            const auto goal = "QA spider leg " + std::to_string(leg + 1);
            animator->SetFabrikIk(goal + " upper", upper, ankles[leg], 1, 64, .00001F,
                                  Keire::AnimatorIkSpace::PresentationWorld);
            if (authoredLowerPole)
            {
                // The authored bend is transported with the actor, not chosen from world-axis seeds.
                // Keep both captured endpoint targets unchanged; only the lower chain's bend policy varies.
                const auto restE = Keire::Math::TransformPoint(world, reference[chain[3]].WorldPosition);
                const auto restF = Keire::Math::TransformPoint(world, reference[chain[4]].WorldPosition);
                const auto restTip = Keire::Math::TransformPoint(world, reference[chain[5]].WorldPosition);
                const auto onChord = SpiderNearestSegment(restF, restE, restTip);
                Keire::Vector3 pole{ankles[leg].X + restF.X - onChord.X, ankles[leg].Y + restF.Y - onChord.Y,
                                    ankles[leg].Z + restF.Z - onChord.Z};
                if (liveTargets && liveFrames[frame].CapturesLowerPole)
                    pole = liveFrames[frame].Legs[leg].LowerPole;
                animator->SetTwoBoneIk(goal + " lower", lower[0], lower[1], lower[2], tips[leg], pole, 1,
                                       Keire::AnimatorIkSpace::PresentationWorld);
            }
            else
            {
                animator->SetFabrikIk(goal + " lower", lower, tips[leg], 1, 64, .00001F,
                                      Keire::AnimatorIkSpace::PresentationWorld);
            }
        }
        const float delta = liveTargets ? liveFrames[frame].Delta : 1.0F / 60;
        fixture.Session->FixedUpdate(delta);
        fixture.Session->Update(delta, 1);
        REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Playing);
        CHECK(animator->RuntimeDiagnostic().empty());
        const auto active =
            liveTargets ? std::ranges::count_if(liveFrames[frame].Legs, [](const auto& leg) { return leg.Active; }) : 8;
        REQUIRE(animator->IkGoals().size() == static_cast<std::size_t>(active) * 2);
        const auto& pose = animator->RuntimeDebugSnapshot()->Pose;
        Keire::SkinMeshCpu(mesh->Vertices(), skin->Influences8(), animator->SkinPalette(), skin->Method(), deformed);
        if (completeTerrain)
        {
            SpiderBounds meshBounds;
            for (std::size_t vertex = 0; vertex < deformed.size(); ++vertex)
            {
                worldVertices[vertex] =
                    Keire::Math::TransformPoint(transform->PresentationWorldMatrix(), deformed[vertex].Position);
                REQUIRE(Keire::Math::IsFinite(worldVertices[vertex]));
                meshBounds.Add(worldVertices[vertex]);
            }
            candidateBoxes.assign(terrain.size(), false);
            boxBounds.clear();
            for (const auto& box : terrain)
                boxBounds.push_back(box.Bounds());
            for (std::size_t box = 0; box < terrain.size(); ++box)
            {
                candidateBoxes[box] = terrain[box].Active && !meshBounds.Disjoint(boxBounds[box]);
                if (candidateBoxes[box])
                    ++checkedBoxFrames;
                else
                    ++skippedBoxFrames;
            }
            for (std::size_t index = 0; index < mesh->Indices().size(); index += 3)
            {
                const auto a = worldVertices[mesh->Indices()[index]], b = worldVertices[mesh->Indices()[index + 1]],
                           c = worldVertices[mesh->Indices()[index + 2]];
                SpiderBounds triangleBounds;
                triangleBounds.Add(a);
                triangleBounds.Add(b);
                triangleBounds.Add(c);
                for (std::size_t box = 0; box < terrain.size(); ++box)
                {
                    if (!candidateBoxes[box] || triangleBounds.Disjoint(boxBounds[box]))
                        continue;
                    ++triangleChecks;
                    const double margin = terrain[box].TriangleInterior(a, b, c);
                    if (margin <= 0)
                        continue;
                    ++triangleIntersections;
                    const bool allActive = std::all_of(liveFrames[frame].Legs.begin(), liveFrames[frame].Legs.end(),
                                                       [](const auto& leg) { return leg.Active; });
                    if (allActive && activeTriangleRecords < 128)
                    {
                        if (activeTriangleRecords++)
                            activeTriangleFile << ",";
                        const auto vec = [&](Keire::Vector3 p)
                        { activeTriangleFile << "[" << p.X << "," << p.Y << "," << p.Z << "]"; };
                        const auto matrix = Keire::Math::Inverse(terrain[box].Inverse);
                        activeTriangleFile << "{\"frame\":" << frame << ",\"tick\":" << liveFrames[frame].Tick
                                           << ",\"triangle\":" << index / 3 << ",\"vertices\":[";
                        for (std::size_t k = 0; k < 3; ++k)
                        {
                            if (k)
                                activeTriangleFile << ",";
                            const auto vertex = mesh->Indices()[index + k];
                            activeTriangleFile << "{\"index\":" << vertex << ",\"leg\":" << vertexLeg[vertex]
                                               << ",\"world\":";
                            vec(worldVertices[vertex]);
                            activeTriangleFile << "}";
                        }
                        activeTriangleFile << "],\"active\":[";
                        for (std::size_t leg = 0; leg < 8; ++leg)
                        {
                            if (leg)
                                activeTriangleFile << ",";
                            activeTriangleFile << (liveFrames[frame].Legs[leg].Active ? "true" : "false");
                        }
                        activeTriangleFile << "],\"box\":\"" << terrain[box].Identity << "\",\"margin\":" << margin
                                           << ",\"center\":";
                        vec(Keire::Math::TransformPoint(matrix, terrain[box].Center));
                        activeTriangleFile << ",\"halfExtent\":";
                        vec(terrain[box].HalfExtent);
                        activeTriangleFile << ",\"axes\":[";
                        vec(Keire::Math::TransformDirection(matrix, {1, 0, 0}));
                        activeTriangleFile << ",";
                        vec(Keire::Math::TransformDirection(matrix, {0, 1, 0}));
                        activeTriangleFile << ",";
                        vec(Keire::Math::TransformDirection(matrix, {0, 0, 1}));
                        activeTriangleFile << "]}";
                    }
                    if (allActive)
                        ++fullActiveTriangleIntersections;
                    else
                        ++inactiveTriangleIntersections;
                    if (firstTriangleFrame < 0)
                    {
                        firstTriangleFrame = frame;
                        firstTriangle = index / 3;
                        firstTriangleBox = terrain[box].Identity;
                    }
                    if (margin > maximumTriangleMargin)
                    {
                        maximumTriangleMargin = margin;
                        worstTriangleFrame = frame;
                        worstTriangle = index / 3;
                        worstTriangleBox = terrain[box].Identity;
                        worstTriangleTerrain = terrain[box];
                        worstTrianglePoints = {a, b, c};
                    }
                }
            }
        }
        if (exportPosedWitness && (frame == 2493 || frame == 2050 || frame == 3500 || frame == 5000))
        {
            REQUIRE(liveTargets);
            REQUIRE(liveFrames[frame].CapturesLowerPole);
            const auto path = SpiderFixtureRoot / ("posed-witness-" + std::to_string(frame) + ".json");
            REQUIRE_FALSE(std::filesystem::exists(path));
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << std::setprecision(9);
            const auto vector = [&](Keire::Vector3 v) { out << "[" << v.X << "," << v.Y << "," << v.Z << "]"; };
            const auto& capture = liveFrames[frame];
            const auto presented = transform->PresentationWorldMatrix();
            out << "{\"schema\":1,\"frame\":" << frame << ",\"tick\":" << capture.Tick << ",\"actor\":";
            vector(capture.Position);
            out << ",\"rotation\":[" << capture.Rotation.X << "," << capture.Rotation.Y << "," << capture.Rotation.Z
                << "," << capture.Rotation.W << "],\"scale\":";
            vector(capture.Scale);
            out << ",\"legs\":[";
            std::size_t count = 0;
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                if (leg)
                    out << ",";
                out << "{\"leg\":" << leg << ",\"active\":" << (capture.Legs[leg].Active ? "true" : "false")
                    << ",\"weight\":1,\"ankle\":";
                vector(capture.Legs[leg].Ankle);
                out << ",\"foot\":";
                vector(capture.Legs[leg].Foot);
                out << ",\"pole\":";
                vector(capture.Legs[leg].LowerPole);
                out << ",\"joints\":[";
                for (std::size_t joint = 0; joint < 6; ++joint)
                {
                    if (joint)
                        out << ",";
                    vector(Keire::Math::TransformPoint(presented, pose[chains[leg][joint]].WorldPosition));
                }
                out << "],\"vertices\":[";
                bool first = true;
                for (std::size_t vertex = 0; vertex < skin->Influences8().size(); ++vertex)
                {
                    const auto& influence = skin->Influences8()[vertex];
                    bool classified = true;
                    float sum = 0;
                    for (std::size_t slot = 0; slot < influence.Count; ++slot)
                    {
                        if (influence.Weights[slot] <= 0)
                            continue;
                        sum += influence.Weights[slot];
                        classified &= std::find(chains[leg].begin(), chains[leg].end(), influence.Bones[slot]) !=
                                      chains[leg].end();
                    }
                    if (!classified || sum <= 0)
                        continue;
                    if (!first)
                        out << ",";
                    first = false;
                    ++count;
                    out << "{\"index\":" << vertex << ",\"actual\":";
                    vector(Keire::Math::TransformPoint(presented, deformed[vertex].Position));
                    out << "}";
                }
                out << "]}";
            }
            REQUIRE(count == 3872);
            out << "],\"vertices\":" << count << "}";
            out.close();
            REQUIRE(out.good());
        }

        if (authoredLowerPole)
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                const auto presented = transform->PresentationWorldMatrix();
                const auto a = Keire::Math::TransformPoint(presented, pose[chains[leg][2]].WorldPosition);
                const auto b = Keire::Math::TransformPoint(presented, pose[chains[leg][3]].WorldPosition);
                for (auto vertex : deVertices[leg])
                {
                    const auto point = Keire::Math::TransformPoint(presented, deformed[vertex].Position);
                    dePoseRadius[leg] =
                        std::max(dePoseRadius[leg], SpiderPoseDistance(point, SpiderNearestSegment(point, a, b)));
                }
            }

        if (authoredLowerPole)
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                if (liveTargets && !liveFrames[frame].Legs[leg].Active)
                    continue;
                const auto terminalWorld = transform->PresentationWorldMatrix();
                const auto tip = Keire::Math::TransformPoint(terminalWorld, pose[chains[leg][5]].WorldPosition);
                for (const auto vertex : terminalVertices[leg])
                    terminalPoseRadius[leg] = std::max(
                        terminalPoseRadius[leg],
                        SpiderPoseDistance(tip, Keire::Math::TransformPoint(terminalWorld, deformed[vertex].Position)));
            }

        if (liveTargets)
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                if (!liveFrames[frame].Legs[leg].Active)
                    continue;
                auto& envelope = envelopes[leg];
                ++envelope.Frames;
                const auto presented = transform->PresentationWorldMatrix();
                std::array<Keire::Vector3, 4> joints;
                for (std::size_t joint = 0; joint < 4; ++joint)
                    joints[joint] = Keire::Math::TransformPoint(presented, pose[chains[leg][joint]].WorldPosition);
                float length = 0;
                for (std::size_t joint = 0; joint < 3; ++joint)
                    length += SpiderPoseDistance(joints[joint], joints[joint + 1]);
                float radius = 0;
                for (auto vertex : envelope.Vertices)
                {
                    const auto& influence = skin->Influences8()[vertex];
                    float total = 0, bound = 0;
                    for (std::size_t slot = 0; slot < influence.Count; ++slot)
                    {
                        const auto weight = influence.Weights[slot];
                        if (weight <= 0)
                            continue;
                        const auto bone = influence.Bones[slot];
                        const auto index = static_cast<std::size_t>(
                            std::find(chains[leg].begin(), chains[leg].begin() + 4, bone) - chains[leg].begin());
                        const auto next = std::min(index + 1, std::size_t{3});
                        const auto nearest = SpiderNearestSegment(mesh->Vertices()[vertex].Position, bindOrigins[bone],
                                                                  bindOrigins[chains[leg][next]]);
                        const auto matrix = Keire::Math::Multiply(presented, animator->SkinPalette()[bone]);
                        const auto moved = Keire::Math::TransformPoint(matrix, mesh->Vertices()[vertex].Position);
                        const auto referencePoint = Keire::Math::TransformPoint(matrix, nearest);
                        // Account for any skin-palette endpoint mismatch instead of assuming rigid bind transport.
                        const float mismatch = std::max(
                            SpiderPoseDistance(Keire::Math::TransformPoint(matrix, bindOrigins[bone]), joints[index]),
                            SpiderPoseDistance(Keire::Math::TransformPoint(matrix, bindOrigins[chains[leg][next]]),
                                               joints[next]));
                        bound += weight * (SpiderPoseDistance(moved, referencePoint) + mismatch);
                        total += weight;
                    }
                    radius = std::max(radius, bound / total);
                }
                // Convex normalized LBS weights combine projected points inside the joint hull.
                // Each segment lies in the root/ankle ellipsoid; its focal capsule has minor radius b.
                const float distance = SpiderPoseDistance(joints[0], joints[3]);
                const float capsule = std::sqrt(std::max(0.0F, length * length - distance * distance)) * .5F + radius;
                envelope.MaximumRadius = std::max(envelope.MaximumRadius, radius);
                envelope.MaximumCapsule = std::max(envelope.MaximumCapsule, capsule);
                for (std::size_t box = 0; box < terrain.size(); ++box)
                {
                    if (completeTerrain && !candidateBoxes[box])
                        continue;
                    envelope.MayIntersect[box] +=
                        terrain[box].CapsuleMayIntersect(joints[0], joints[3], capsule) ? 1 : 0;
                    bool penetrating = false;
                    for (auto vertex : envelope.Vertices)
                        penetrating |= terrain[box].SignedDistance(
                                           Keire::Math::TransformPoint(presented, deformed[vertex].Position)) < 0;
                    envelope.ActualVertexPenetration[box] += penetrating ? 1 : 0;
                }
            }
        float frameMeshMinimum = 100;
        if (liveTargets)
            for (std::size_t vertex = 0; vertex < deformed.size(); ++vertex)
            {
                const auto point =
                    Keire::Math::TransformPoint(transform->PresentationWorldMatrix(), deformed[vertex].Position);
                REQUIRE(Keire::Math::IsFinite(point));
                for (std::size_t box = 0; box < terrain.size(); ++box)
                {
                    if (completeTerrain && !candidateBoxes[box])
                        continue;
                    const auto distance = terrain[box].SignedDistance(point);
                    if (completeTerrain)
                    {
                        const int classified = vertexLeg[vertex];
                        if (classified < 0)
                        {
                            if (distance < 0)
                                ++nonLegVertexIntersections;
                            if (distance < nonLegMinimum)
                            {
                                nonLegMinimum = distance;
                                nonLegWorstFrame = frame;
                                nonLegWorstVertex = vertex;
                                nonLegWorstBox = terrain[box].Identity;
                            }
                        }
                        else if (liveFrames[frame].Legs[classified].Active)
                        {
                            if (distance < 0)
                                ++activeLegVertexIntersections;
                            if (distance < activeLegMinimum)
                            {
                                activeLegMinimum = distance;
                                activeLegWorstFrame = frame;
                                activeLegWorstVertex = vertex;
                                activeLegWorstLeg = classified;
                                activeLegWorstBox = terrain[box].Identity;
                            }
                        }
                        else if (distance < 0)
                            ++inactiveLegVertexIntersections;
                    }
                    frameMeshMinimum = std::min(frameMeshMinimum, distance);
                    if (distance < fullMeshMinimum)
                    {
                        fullMeshMinimum = distance;
                        worstMeshFrame = frame;
                        worstMeshVertex = vertex;
                        worstMeshBox = box;
                        worstMeshPosition = point;
                        worstMeshTerrain = terrain[box];
                        if (authoredLowerPole)
                            for (std::size_t leg = 0; leg < 8; ++leg)
                                for (std::size_t joint = 0; joint < 4; ++joint)
                                    worstUpperJoints[leg][joint] = Keire::Math::TransformPoint(
                                        transform->PresentationWorldMatrix(), pose[chains[leg][joint]].WorldPosition);
                    }
                }
            }
        if (liveTargets && active == 8)
        {
            ++fullyActiveFrames;
            fullyActivePenetratingFrames += frameMeshMinimum < 0 ? 1 : 0;
            if (frameMeshMinimum < fullyActiveMinimum)
            {
                fullyActiveMinimum = frameMeshMinimum;
                fullyActiveWorstFrame = frame;
            }
        }
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            if (liveTargets && !liveFrames[frame].Legs[leg].Active)
                continue;
            CAPTURE(frame);
            CAPTURE(leg);
            const auto ankle =
                Keire::Math::TransformPoint(transform->PresentationWorldMatrix(), pose[chains[leg][3]].WorldPosition);
            const auto tip =
                Keire::Math::TransformPoint(transform->PresentationWorldMatrix(), pose[chains[leg][5]].WorldPosition);
            REQUIRE(Keire::Math::IsFinite(ankle));
            REQUIRE(Keire::Math::IsFinite(tip));
            for (std::size_t segment = 0; segment < 5; ++segment)
            {
                const auto a = chains[leg][segment], b = chains[leg][segment + 1];
                const auto actualLength = 40.0F * SpiderPoseDistance(pose[a].WorldPosition, pose[b].WorldPosition);
                const auto originalLength =
                    40.0F * SpiderPoseDistance(reference[a].WorldPosition, reference[b].WorldPosition);
                REQUIRE(std::isfinite(actualLength));
                CHECK(actualLength == doctest::Approx(originalLength).epsilon(.00001).scale(1));
            }
            const auto ankleError = SpiderPoseDistance(ankle, ankles[leg]),
                       tipError = SpiderPoseDistance(tip, tips[leg]);
            maximumAnkleError = std::max(maximumAnkleError, ankleError);
            maximumTipError = std::max(maximumTipError, tipError);
            if (originalCaptureDefects && ((frame == 91 && leg == 1) || (frame == 161 && leg == 5)))
            {
                ++originalExtendedSamples;
                const auto root = Keire::Math::TransformPoint(transform->PresentationWorldMatrix(),
                                                              pose[chains[leg][0]].WorldPosition);
                const auto fraction = upperReaches[leg] / upperDistances[leg];
                const Keire::Vector3 extended{root.X + (ankles[leg].X - root.X) * fraction,
                                              root.Y + (ankles[leg].Y - root.Y) * fraction,
                                              root.Z + (ankles[leg].Z - root.Z) * fraction};
                CHECK(SpiderPoseDistance(root, ankle) == doctest::Approx(upperReaches[leg]).epsilon(.00001).scale(1));
                CHECK(SpiderPoseDistance(ankle, extended) <= .00001F);
                CHECK(ankleError == doctest::Approx(upperDistances[leg] - upperReaches[leg]).epsilon(.00001).scale(1));
            }
            if (originalCaptureDefects && frame == 91 && leg == 1)
            {
                ++originalEndpointFailures;
                CHECK(ankleError > .002F);
                CHECK(ankleError == doctest::Approx(.00325408F).epsilon(.00001).scale(1));
            }
            else
                CHECK(ankleError <= .002F);
            CHECK(tipError <= .002F);
            for (auto vertex : distal[leg])
            {
                const auto position =
                    Keire::Math::TransformPoint(transform->PresentationWorldMatrix(), deformed[vertex].Position);
                REQUIRE(Keire::Math::IsFinite(position));
                float clearance = position.Y;
                if (liveTargets)
                {
                    clearance = 100;
                    for (std::size_t box = 0; box < terrain.size(); ++box)
                    {
                        if (completeTerrain && !candidateBoxes[box])
                            continue;
                        const float candidate = terrain[box].SignedDistance(position);
                        clearance = std::min(clearance, candidate);
                        if (candidate < minimumClearance)
                        {
                            minimumClearance = candidate;
                            distalWorstFrame = frame;
                            distalWorstLeg = leg;
                            distalWorstVertex = vertex;
                            distalWorstBox = box;
                            distalWorstPosition = position;
                            for (std::size_t joint = 0; joint < 3; ++joint)
                                distalWorstJoints[joint] = Keire::Math::TransformPoint(
                                    transform->PresentationWorldMatrix(), pose[chains[leg][joint + 3]].WorldPosition);
                        }
                    }
                }
                minimumClearance = std::min(minimumClearance, clearance);
                CHECK(clearance >= 0);
            }
        }
    }
    if (liveTargets)
    {
        std::size_t classified = 0;
        for (std::size_t leg = 0; leg < 8; ++leg)
        {
            const auto& envelope = envelopes[leg];
            classified += envelope.Vertices.size();
            for (std::size_t box = 0; box < terrain.size(); ++box)
                MESSAGE("[SpiderUpperEnvelope] leg="
                        << leg << " verticesAllWeights=" << envelope.Vertices.size() << " bindWeightedSegmentRadius="
                        << envelope.BindRadius << " maximumPoseEnvelope=" << envelope.MaximumRadius
                        << " maximumCapsule=" << envelope.MaximumCapsule << " frames=" << envelope.Frames
                        << " box=" << terrain[box].Identity << " mayIntersect=" << envelope.MayIntersect[box]
                        << " actualVertexPenetrationFrames=" << envelope.ActualVertexPenetration[box]
                        << " scope=resolvedPoseOnly-mayIntersectIsNotCollision");
        }
        MESSAGE("[SpiderUpperEnvelopeCoverage] classified="
                << classified << " unclassifiedMixedOrOther=" << mesh->Vertices().size() - classified << " total="
                << mesh->Vertices().size() << " scope=allPositiveWeightsInSingleUpperChain-LBS-only-noRuntimeGuard");
        REQUIRE(activeSamples > 0);
        if (completeTerrain)
        {
            activeTriangleFile << "],\"recordsWritten\":" << activeTriangleRecords
                               << ",\"totalActiveIntersections\":" << fullActiveTriangleIntersections << "}";
            activeTriangleFile.close();
            REQUIRE(activeTriangleFile.good());
        }
        REQUIRE(plantedSamples > 0);
        if (completeTerrain)
            MESSAGE("Complete presented box snapshot scope: all scene boxes; vertices only, no triangle interior or "
                    "continuous-path proof");
        MESSAGE("Spider live route frames="
                << frameCount << " active=" << activeSamples << " planted=" << plantedSamples
                << " fullMeshVertices=" << mesh->Vertices().size() << " minimum box signed distance=" << fullMeshMinimum
                << (completeTerrain ? " scope=exact captured poses versus complete presented scene boxes; "
                                    : " scope=captured poses versus floor/plinth only; ")
                << "no interpolation-pipeline/triangle-intersection/GPU/continuous-path proof");
        if (completeTerrain && worstMeshFrame < 0)
        {
            MESSAGE(
                "[SpiderTriangleTerrain] all active terrain boxes disjoint from skinned mesh bounds; checkedBoxFrames="
                << checkedBoxFrames << " skippedBoxFrames=" << skippedBoxFrames
                << " triangleChecks=" << triangleChecks);
            CHECK(triangleIntersections == 0);
            return;
        }
        REQUIRE(worstMeshFrame >= 0);
        const auto& box = worstMeshTerrain;
        MESSAGE("[SpiderMeshWorst] frame="
                << worstMeshFrame << " tick=" << liveFrames[worstMeshFrame].Tick << " vertex=" << worstMeshVertex
                << " box=" << box.Identity << " name=" << box.Name << " signedDistance=" << fullMeshMinimum
                << " world=" << pointText(worstMeshPosition) << " weightsDescending=" << weightText(worstMeshVertex));
        if (authoredLowerPole)
            for (std::size_t leg = 0; leg < 8; ++leg)
            {
                const auto& joints = worstUpperJoints[leg];
                const auto& captured = liveFrames[worstMeshFrame].Legs[leg];
                MESSAGE("[SpiderWorstUpperJoints] leg="
                        << leg << " A=" << pointText(joints[0]) << " B=" << pointText(joints[1])
                        << " D=" << pointText(joints[2]) << " E=" << pointText(joints[3])
                        << " signedA=" << box.SignedDistance(joints[0]) << " signedB=" << box.SignedDistance(joints[1])
                        << " signedD=" << box.SignedDistance(joints[2]) << " signedE=" << box.SignedDistance(joints[3])
                        << " vertexToDESegment="
                        << SpiderPoseDistance(worstMeshPosition,
                                              SpiderNearestSegment(worstMeshPosition, joints[2], joints[3]))
                        << " ankle=" << pointText(captured.Ankle) << " foot=" << pointText(captured.Foot)
                        << " witnessClassifiedDE="
                        << (std::find(deVertices[leg].begin(), deVertices[leg].end(), worstMeshVertex) !=
                            deVertices[leg].end()));
                MESSAGE("[SpiderDEEnvelope] leg=" << leg << " vertices=" << deVertices[leg].size() << " restRadius="
                                                  << deRestRadius[leg] << " maximumCapturedRadius=" << dePoseRadius[leg]
                                                  << " scope=AllPositiveWeightsDEOnly-MixedBDExcluded");
            }
        MESSAGE("[SpiderFullyActiveMesh] frames="
                << fullyActiveFrames << " penetratingFrames=" << fullyActivePenetratingFrames
                << " minimum=" << fullyActiveMinimum << " worstFrame=" << fullyActiveWorstFrame
                << " worstTick=" << (fullyActiveWorstFrame >= 0 ? liveFrames[fullyActiveWorstFrame].Tick : 0)
                << " scope=additional-breakdown-all-frame-failures-preserved");
        REQUIRE(distalWorstFrame >= 0);
        const auto& captured = liveFrames[distalWorstFrame];
        const auto& target = captured.Legs[distalWorstLeg];
        MESSAGE("[SpiderActiveDistalWorst] frame="
                << distalWorstFrame << " tick=" << captured.Tick << " leg=" << distalWorstLeg
                << " planted=" << target.Planted << " vertex=" << distalWorstVertex
                << " box=" << terrain[distalWorstBox].Identity << " signedDistance=" << minimumClearance
                << " vertexWorld=" << pointText(distalWorstPosition) << " actor=" << pointText(captured.Position)
                << " actorQuaternion=" << captured.Rotation.X << "," << captured.Rotation.Y << ","
                << captured.Rotation.Z << "," << captured.Rotation.W << " footTarget=" << pointText(target.Foot)
                << " ankleTarget=" << pointText(target.Ankle) << " lowerE=" << pointText(distalWorstJoints[0])
                << " lowerF=" << pointText(distalWorstJoints[1]) << " tip=" << pointText(distalWorstJoints[2])
                << " weightsDescending=" << weightText(distalWorstVertex));
        if (completeTerrain)
        {
            MESSAGE("[SpiderClassifiedTerrain] fullActiveTriangleIntersections="
                    << fullActiveTriangleIntersections << " inactiveTriangleIntersections="
                    << inactiveTriangleIntersections << " nonLegVertexIntersections=" << nonLegVertexIntersections
                    << " nonLegMinimum=" << nonLegMinimum << " nonLegWorstFrame=" << nonLegWorstFrame
                    << " nonLegWorstVertex=" << nonLegWorstVertex << " nonLegWorstBox=" << nonLegWorstBox
                    << " activeLegVertexIntersections=" << activeLegVertexIntersections
                    << " activeLegMinimum=" << activeLegMinimum << " activeLegWorstFrame=" << activeLegWorstFrame
                    << " activeLegWorstVertex=" << activeLegWorstVertex << " activeLegWorstLeg=" << activeLegWorstLeg
                    << " activeLegWorstBox=" << activeLegWorstBox
                    << " inactiveLegVertexIntersections=" << inactiveLegVertexIntersections
                    << " scope=3872sixBoneLegVertices-and1474OtherVertices-noRestAssumption-nearboxMinimum");
            MESSAGE("[SpiderTriangleTerrain] checkedBoxFrames="
                    << checkedBoxFrames << " skippedBoxFrames=" << skippedBoxFrames
                    << " triangleChecks=" << triangleChecks << " intersections=" << triangleIntersections
                    << " firstFrame=" << firstTriangleFrame << " firstTriangle=" << firstTriangle << " firstBox="
                    << firstTriangleBox << " worstFrame=" << worstTriangleFrame << " worstTriangle=" << worstTriangle
                    << " box=" << worstTriangleBox << " maximumSatInteriorMargin=" << maximumTriangleMargin
                    << " scope=sampledCPUtriangles-strictInterior-nearboxMinimumOnly-noContinuousMotionProof");
            if (worstTriangleFrame >= 0)
            {
                const auto matrix = Keire::Math::Inverse(worstTriangleTerrain.Inverse);
                MESSAGE("[SpiderTriangleWitness] frame="
                        << worstTriangleFrame << " tick=" << liveFrames[worstTriangleFrame].Tick << " triangle="
                        << worstTriangle << " box=" << worstTriangleBox << " a=" << pointText(worstTrianglePoints[0])
                        << " b=" << pointText(worstTrianglePoints[1]) << " c=" << pointText(worstTrianglePoints[2])
                        << " boxCenter=" << pointText(Keire::Math::TransformPoint(matrix, worstTriangleTerrain.Center))
                        << " boxHalfExtent=" << pointText(worstTriangleTerrain.HalfExtent)
                        << " boxAxisX=" << pointText(Keire::Math::TransformDirection(matrix, {1, 0, 0}))
                        << " boxAxisY=" << pointText(Keire::Math::TransformDirection(matrix, {0, 1, 0}))
                        << " boxAxisZ=" << pointText(Keire::Math::TransformDirection(matrix, {0, 0, 1})));
            }
            CHECK(triangleIntersections == 0);
        }
        if (originalCaptureDefects)
        {
            CHECK(originalOverreachSamples == 2);
            CHECK(originalExtendedSamples == 2);
            CHECK(originalEndpointFailures == 1);
            CHECK(activeSamples == 1776);
            CHECK(plantedSamples == 1120);
            CHECK(fullyActiveFrames == 222);
            CHECK(fullyActivePenetratingFrames == 5);
            CHECK(worstMeshFrame == 116);
            CHECK(worstMeshVertex == 4286);
            CHECK(box.Identity == "b72bc53d-f782-5699-baa7-6b4af645d8f0");
            CHECK(box.Name == "Beacon plinth 1");
            CHECK(fullMeshMinimum < 0);
            CHECK(fullMeshMinimum == doctest::Approx(-.0304913F).epsilon(.00001).scale(1));
            const auto& influence = skin->Influences8()[worstMeshVertex];
            REQUIRE(influence.Count == 1);
            REQUIRE(influence.Bones[0] < skeleton->Bones().size());
            CHECK(skeleton->Bones()[influence.Bones[0]].Name == "LegSegmentD.L.004_029");
            CHECK(influence.Weights[0] == 1);
            MESSAGE("Original schema1 game-script defects reproduced; this does not certify gameplay correctness.");
        }
        else
            CHECK(fullMeshMinimum >= 0);
    }
    if (authoredLowerPole)
        for (std::size_t leg = 0; leg < 8; ++leg)
            MESSAGE(std::setprecision(9) << "[SpiderTerminalPoseRadius] leg=" << leg << " rest="
                                         << terminalRestRadius[leg] << " maximumCaptured=" << terminalPoseRadius[leg]
                                         << " vertices=" << terminalVertices[leg].size());
    MESSAGE("Spider published ankle error=" << maximumAnkleError << " tip error=" << maximumTipError
                                            << " distal tested-geometry clearance=" << minimumClearance);
}

TEST_CASE("Spider old neutral recipe exposes an infeasible upper-chain target" *
          doctest::skip(!std::filesystem::is_regular_file(SpiderFixtureRoot / "ActualSkeleton.keireskeleton")))
{
    CheckSpiderPublishedPose(false);
}

TEST_CASE("Spider narrowed fixture stance publishes endpoints and distal mesh" *
          doctest::skip(!std::filesystem::is_regular_file(SpiderFixtureRoot / "ActualSkeleton.keireskeleton")))
{
    CheckSpiderPublishedPose(true);
}

TEST_CASE("Spider C# ankle proposal publishes endpoints and distal mesh" *
          doctest::skip(!std::filesystem::is_regular_file(SpiderFixtureRoot / "AnkleProposal" /
                                                          "candidate-targets.txt")))
{
    CheckSpiderPublishedPose(false, true);
}

TEST_CASE("Spider captured live route publishes endpoints and terrain mesh" *
          doctest::skip(!std::filesystem::is_regular_file(SpiderFixtureRoot / "live-targets.txt") ||
                        IsOriginalSpiderLiveCapture()))
{
    CheckSpiderPublishedPose(false, false, true);
}

TEST_CASE("Spider original live capture preserves unreachable goals and terrain penetration" *
          doctest::skip(!IsOriginalSpiderLiveCapture()))
{
    CheckSpiderPublishedPose(false, false, true, false, 0, false, false, true);
}

// Explicit experiment: select this exact test and pass --no-skip; never changes default capture validation.
TEST_CASE("Spider captured route experimental authored lower pole" * doctest::skip(true))
{
    REQUIRE(std::filesystem::is_regular_file(SpiderFixtureRoot / "live-targets.txt"));
    CheckSpiderPublishedPose(false, false, true, true);
}

TEST_CASE("Spider live capture parser preserves exact schema2 poles and schema1 compatibility")
{
    for (const bool poles : {false, true})
    {
        std::ostringstream text;
        text << (poles ? "AsterSpiderLiveTargets2" : "AsterSpiderLiveTargets1") << " 1\n";
        text << "frame 0 1 .016666667 0 0 0 0 0 0 1 40 40 40\n";
        for (int leg = 0; leg < 8; ++leg)
        {
            text << "leg " << leg << " 1 0 1 2 3 4 5 6";
            if (poles)
                text << " 7.125 -8.25 9.5";
            text << "\n";
        }
        text << "end 1\n";
        std::istringstream input(text.str());
        const auto frames = ReadSpiderLiveFrames(input);
        REQUIRE(frames.size() == 1);
        CHECK(frames[0].CapturesLowerPole == poles);
        for (const auto& leg : frames[0].Legs)
        {
            CHECK(leg.Foot == Keire::Vector3{1, 2, 3});
            CHECK(leg.Ankle == Keire::Vector3{4, 5, 6});
            if (poles)
                CHECK(leg.LowerPole == Keire::Vector3{7.125F, -8.25F, 9.5F});
        }
    }
}

TEST_CASE("Spider upper Rest skin contribution export" * doctest::skip(true))
{
    CheckSpiderPublishedPose(false, false, false, false, true);
}

TEST_CASE("Spider full leg Rest skin contribution export" * doctest::skip(true))
{
    CheckSpiderPublishedPose(false, false, false, false, 2);
}

TEST_CASE("Spider exact captured posed skin witnesses" * doctest::skip(true))
{
    CheckSpiderPublishedPose(false, false, true, true, 0, true);
}

TEST_CASE("Spider complete terrain sidecar parser rejects missing unknown and malformed geometry")
{
    SpiderTerrainBox box;
    box.Identity = "test-box";
    SpiderLiveFrame frame;
    frame.Tick = 7;
    const std::string valid = "AsterSpiderTerrain1 1 1 box 0 test-box frame 0 7 1 obb 0 1 0 0 0 0 0 0 1 1 2 3 end 1";
    std::istringstream input(valid);
    auto parsed = ReadSpiderTerrainSnapshots(input, {box}, {frame});
    CHECK(parsed[0][0].SignedDistance({0, 0, 0}) == -1);
    CHECK(parsed[0][0].SignedDistance({2, 0, 0}) == 1);
    for (const auto& bad :
         {std::string("AsterSpiderTerrain1 9000 1"), std::string("AsterSpiderTerrain1 1 1 box 0 unknown"),
          valid + " extra", valid.substr(0, valid.size() - 5)})
    {
        std::istringstream broken(bad);
        CHECK_THROWS(ReadSpiderTerrainSnapshots(broken, {box}, {frame}));
    }
    for (const std::string badObb : {"obb 0 1 0 0 0 0 0 0 1 0 2 3", // Degenerate extent.
                                     "obb 0 1 0 0 0 0 0 0 2 1 2 3", // Nonunit rotation.
                                     "obb 1 1 0 0 0 0 0 0 1 1 2 3", // Unknown index.
                                     "obb 0 2 0 0 0 0 0 0 1 1 2 3", // Invalid active flag.
                                     "obb 0 1 nan 0 0 0 0 0 1 1 2 3"})
    {
        std::istringstream broken("AsterSpiderTerrain1 1 1 box 0 test-box frame 0 7 1 " + badObb + " end 1");
        CHECK_THROWS(ReadSpiderTerrainSnapshots(broken, {box}, {frame}));
    }
    std::istringstream disabled("AsterSpiderTerrain1 1 1 box 0 test-box frame 0 7 1 obb 0 0 0 0 0 0 0 0 1 1 2 3 end 1");
    CHECK(std::isinf(ReadSpiderTerrainSnapshots(disabled, {box}, {frame})[0][0].SignedDistance({0, 0, 0})));
    auto wrongTick = frame;
    wrongTick.Tick = 8;
    std::istringstream mismatched(valid);
    CHECK_THROWS(ReadSpiderTerrainSnapshots(mismatched, {box}, {wrongTick}));
}
TEST_CASE("Spider captured posed mesh against complete terrain snapshots" * doctest::skip(true))
{
    CheckSpiderPublishedPose(false, false, true, true, 0, false, true);
}

TEST_CASE("Spider triangle terrain SAT rejects interior crossings but preserves touching")
{
    SpiderTerrainBox box;
    box.Inverse = Keire::Math::ComposeTransform({}, {}, {1, 1, 1});
    box.HalfExtent = {1, 1, 1};
    box.Scale = {1, 1, 1};
    CHECK(box.TriangleInterior({-2, 0, -2}, {2, 0, -2}, {0, 0, 2}) > 0);
    CHECK(box.TriangleInterior({-2, 1, -2}, {2, 1, -2}, {0, 1, 2}) == 0);
    CHECK(box.TriangleInterior({-2, .999999F, -2}, {2, .999999F, -2}, {0, .999999F, 2}) > 0);
    CHECK(box.TriangleInterior({0, 0, 0}, {0, 0, 0}, {0, 0, 0}) == 0);
    CHECK(box.TriangleInterior({2, 2, 2}, {3, 2, 2}, {2, 3, 2}) == 0);
    SpiderBounds inside;
    inside.Add({0, 0, 0});
    CHECK_FALSE(inside.Disjoint(box.Bounds()));
    SpiderBounds touching;
    touching.Add({1, 0, 0});
    CHECK_FALSE(touching.Disjoint(box.Bounds()));
    SpiderBounds distant;
    distant.Add({4, 0, 0});
    CHECK(distant.Disjoint(box.Bounds()));
    const Keire::Quaternion yaw{0, .382683432F, 0, .923879533F};
    const auto world = Keire::Math::ComposeTransform({2, 0, 3}, yaw, {1, 1, 1});
    box.Inverse = Keire::Math::Inverse(world);
    const auto move = [&](Keire::Vector3 p) { return Keire::Math::TransformPoint(world, p); };
    CHECK(box.TriangleInterior(move({-2, 0, -2}), move({2, 0, -2}), move({0, 0, 2})) > 0);
    SpiderBounds rotated;
    rotated.Add(move({0, 0, 0}));
    CHECK_FALSE(rotated.Disjoint(box.Bounds()));
}

TEST_CASE("Spider schema3 permits actual tick zero contacts without weakening older schemas")
{
    const auto capture = [](int schema, bool active, std::string pole)
    {
        std::ostringstream text;
        text << "AsterSpiderLiveTargets" << schema << " 1 frame 0 0 .016666667 0 0 0 0 0 0 1 40 40 40 ";
        for (int leg = 0; leg < 8; ++leg)
            text << "leg " << leg << " " << (active ? 1 : 0) << " 0 1 2 3 4 5 6 " << pole << " ";
        text << "end 1";
        return text.str();
    };
    for (int schema : {2, 3})
    {
        std::istringstream active(capture(schema, true, "7 8 9"));
        if (schema == 2)
            CHECK_THROWS(ReadSpiderLiveFrames(active));
        else
        {
            const auto frames = ReadSpiderLiveFrames(active);
            REQUIRE(frames.size() == 1);
            CHECK(frames[0].CapturesLowerPole);
            CHECK(frames[0].Legs[0].Active);
        }
        std::istringstream inactive(capture(schema, false, "7 8 9"));
        CHECK_NOTHROW(ReadSpiderLiveFrames(inactive));
        std::istringstream nonfinite(capture(schema, false, "nan 8 9"));
        CHECK_THROWS(ReadSpiderLiveFrames(nonfinite));
        std::istringstream incomplete(capture(schema, false, "7 8"));
        CHECK_THROWS(ReadSpiderLiveFrames(incomplete));
    }
}
