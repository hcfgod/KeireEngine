#include "Keire/Animation/RiggingSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr float Epsilon = 0.000001F;

        struct TemplateBone
        {
            RigBoneSemantic Semantic = RigBoneSemantic::None;
            std::string_view Name;
            std::int32_t Parent = -1;
            Vector3 NormalizedPosition;
        };

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] Vector3 Multiply(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                    left.X * right.Y - left.Y * right.X};
        }

        [[nodiscard]] float Length(const Vector3 value) noexcept { return std::sqrt(Dot(value, value)); }

        [[nodiscard]] Vector3 Normalize(const Vector3 value, const Vector3 fallback = {0.0F, 1.0F, 0.0F}) noexcept
        {
            const auto length = Length(value);
            return length > Epsilon ? Multiply(value, 1.0F / length) : fallback;
        }

        [[nodiscard]] Quaternion Multiply(const Quaternion left, const Quaternion right) noexcept
        {
            return {left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
                    left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
                    left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
                    left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z};
        }

        [[nodiscard]] Quaternion Conjugate(const Quaternion value) noexcept
        {
            return {-value.X, -value.Y, -value.Z, value.W};
        }

        [[nodiscard]] Quaternion Normalize(const Quaternion value) noexcept
        {
            const auto length =
                std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W);
            if (length <= Epsilon)
                return {};
            const auto inverse = 1.0F / length;
            return {value.X * inverse, value.Y * inverse, value.Z * inverse, value.W * inverse};
        }

        [[nodiscard]] Quaternion FromTo(const Vector3 source, const Vector3 destination) noexcept
        {
            const auto from = Normalize(source);
            const auto to = Normalize(destination);
            const auto cosine = std::clamp(Dot(from, to), -1.0F, 1.0F);
            if (cosine > 0.999999F)
                return {};
            if (cosine < -0.999999F)
            {
                auto axis = Cross(from, {1.0F, 0.0F, 0.0F});
                if (Length(axis) <= Epsilon)
                    axis = Cross(from, {0.0F, 1.0F, 0.0F});
                axis = Normalize(axis);
                return {axis.X, axis.Y, axis.Z, 0.0F};
            }
            const auto axis = Cross(from, to);
            return Normalize({axis.X, axis.Y, axis.Z, 1.0F + cosine});
        }

        [[nodiscard]] Quaternion Nlerp(const Quaternion left, const Quaternion right, const float amount) noexcept
        {
            const auto t = std::clamp(amount, 0.0F, 1.0F);
            const auto dot = left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
            const auto sign = dot < 0.0F ? -1.0F : 1.0F;
            return Normalize({left.X + (right.X * sign - left.X) * t, left.Y + (right.Y * sign - left.Y) * t,
                              left.Z + (right.Z * sign - left.Z) * t, left.W + (right.W * sign - left.W) * t});
        }

        [[nodiscard]] std::string_view SemanticName(const RigBoneSemantic semantic)
        {
            switch (semantic)
            {
            case RigBoneSemantic::None:
                return "none";
            case RigBoneSemantic::Root:
                return "root";
            case RigBoneSemantic::Pelvis:
                return "pelvis";
            case RigBoneSemantic::Spine:
                return "spine";
            case RigBoneSemantic::Chest:
                return "chest";
            case RigBoneSemantic::Neck:
                return "neck";
            case RigBoneSemantic::Head:
                return "head";
            case RigBoneSemantic::LeftShoulder:
                return "leftShoulder";
            case RigBoneSemantic::LeftUpperArm:
                return "leftUpperArm";
            case RigBoneSemantic::LeftLowerArm:
                return "leftLowerArm";
            case RigBoneSemantic::LeftHand:
                return "leftHand";
            case RigBoneSemantic::RightShoulder:
                return "rightShoulder";
            case RigBoneSemantic::RightUpperArm:
                return "rightUpperArm";
            case RigBoneSemantic::RightLowerArm:
                return "rightLowerArm";
            case RigBoneSemantic::RightHand:
                return "rightHand";
            case RigBoneSemantic::LeftUpperLeg:
                return "leftUpperLeg";
            case RigBoneSemantic::LeftLowerLeg:
                return "leftLowerLeg";
            case RigBoneSemantic::LeftFoot:
                return "leftFoot";
            case RigBoneSemantic::RightUpperLeg:
                return "rightUpperLeg";
            case RigBoneSemantic::RightLowerLeg:
                return "rightLowerLeg";
            case RigBoneSemantic::RightFoot:
                return "rightFoot";
            case RigBoneSemantic::LeftFrontUpperLeg:
                return "leftFrontUpperLeg";
            case RigBoneSemantic::LeftFrontLowerLeg:
                return "leftFrontLowerLeg";
            case RigBoneSemantic::LeftFrontFoot:
                return "leftFrontFoot";
            case RigBoneSemantic::RightFrontUpperLeg:
                return "rightFrontUpperLeg";
            case RigBoneSemantic::RightFrontLowerLeg:
                return "rightFrontLowerLeg";
            case RigBoneSemantic::RightFrontFoot:
                return "rightFrontFoot";
            case RigBoneSemantic::LeftRearUpperLeg:
                return "leftRearUpperLeg";
            case RigBoneSemantic::LeftRearLowerLeg:
                return "leftRearLowerLeg";
            case RigBoneSemantic::LeftRearFoot:
                return "leftRearFoot";
            case RigBoneSemantic::RightRearUpperLeg:
                return "rightRearUpperLeg";
            case RigBoneSemantic::RightRearLowerLeg:
                return "rightRearLowerLeg";
            case RigBoneSemantic::RightRearFoot:
                return "rightRearFoot";
            case RigBoneSemantic::TailBase:
                return "tailBase";
            case RigBoneSemantic::TailTip:
                return "tailTip";
            case RigBoneSemantic::LeftWingRoot:
                return "leftWingRoot";
            case RigBoneSemantic::LeftWingTip:
                return "leftWingTip";
            case RigBoneSemantic::RightWingRoot:
                return "rightWingRoot";
            case RigBoneSemantic::RightWingTip:
                return "rightWingTip";
            }
            throw std::invalid_argument("Unknown rig bone semantic.");
        }

        [[nodiscard]] RigBoneSemantic ParseSemantic(const std::string_view value)
        {
            for (std::uint16_t raw = static_cast<std::uint16_t>(RigBoneSemantic::None);
                 raw <= static_cast<std::uint16_t>(RigBoneSemantic::RightWingTip); ++raw)
            {
                const auto semantic = static_cast<RigBoneSemantic>(raw);
                if (SemanticName(semantic) == value)
                    return semantic;
            }
            throw std::invalid_argument("Unknown rig bone semantic: " + std::string(value));
        }

        [[nodiscard]] std::string_view ProfileName(const RigProfileType profile)
        {
            switch (profile)
            {
            case RigProfileType::Humanoid:
                return "humanoid";
            case RigProfileType::Biped:
                return "biped";
            case RigProfileType::Quadruped:
                return "quadruped";
            case RigProfileType::Custom:
                return "custom";
            }
            throw std::invalid_argument("Unknown rig profile.");
        }

        [[nodiscard]] RigProfileType ParseProfile(const std::string_view value)
        {
            if (value == "humanoid")
                return RigProfileType::Humanoid;
            if (value == "biped")
                return RigProfileType::Biped;
            if (value == "quadruped")
                return RigProfileType::Quadruped;
            if (value == "custom")
                return RigProfileType::Custom;
            throw std::invalid_argument("Unknown rig profile: " + std::string(value));
        }

        [[nodiscard]] Json EncodeVector(const Vector3 value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Json EncodeQuaternion(const Quaternion value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Vector3 DecodeVector(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::invalid_argument("Rig vectors must contain three finite numbers.");
            const Vector3 result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            if (!Math::IsFinite(result))
                throw std::invalid_argument("Rig vectors must contain finite values.");
            return result;
        }

        [[nodiscard]] Quaternion DecodeQuaternion(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("Rig quaternions must contain four finite numbers.");
            const Quaternion result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                    value[3].get<float>()};
            if (!Math::IsFinite(result))
                throw std::invalid_argument("Rig quaternions must contain finite values.");
            return Math::Normalize(result);
        }

        [[nodiscard]] std::vector<TemplateBone> HumanoidTemplate()
        {
            return {{RigBoneSemantic::Root, "Root", -1, {0.0F, 0.02F, 0.0F}},
                    {RigBoneSemantic::Pelvis, "Pelvis", 0, {0.0F, 0.50F, 0.0F}},
                    {RigBoneSemantic::Spine, "Spine", 1, {0.0F, 0.64F, 0.0F}},
                    {RigBoneSemantic::Chest, "Chest", 2, {0.0F, 0.78F, 0.0F}},
                    {RigBoneSemantic::Neck, "Neck", 3, {0.0F, 0.90F, 0.0F}},
                    {RigBoneSemantic::Head, "Head", 4, {0.0F, 0.98F, 0.0F}},
                    {RigBoneSemantic::LeftShoulder, "LeftShoulder", 3, {-0.18F, 0.82F, 0.0F}},
                    {RigBoneSemantic::LeftUpperArm, "LeftUpperArm", 6, {-0.34F, 0.79F, 0.0F}},
                    {RigBoneSemantic::LeftLowerArm, "LeftLowerArm", 7, {-0.54F, 0.75F, 0.0F}},
                    {RigBoneSemantic::LeftHand, "LeftHand", 8, {-0.70F, 0.72F, 0.0F}},
                    {RigBoneSemantic::RightShoulder, "RightShoulder", 3, {0.18F, 0.82F, 0.0F}},
                    {RigBoneSemantic::RightUpperArm, "RightUpperArm", 10, {0.34F, 0.79F, 0.0F}},
                    {RigBoneSemantic::RightLowerArm, "RightLowerArm", 11, {0.54F, 0.75F, 0.0F}},
                    {RigBoneSemantic::RightHand, "RightHand", 12, {0.70F, 0.72F, 0.0F}},
                    {RigBoneSemantic::LeftUpperLeg, "LeftUpperLeg", 1, {-0.10F, 0.47F, 0.0F}},
                    {RigBoneSemantic::LeftLowerLeg, "LeftLowerLeg", 14, {-0.10F, 0.24F, 0.01F}},
                    {RigBoneSemantic::LeftFoot, "LeftFoot", 15, {-0.10F, 0.04F, 0.09F}},
                    {RigBoneSemantic::RightUpperLeg, "RightUpperLeg", 1, {0.10F, 0.47F, 0.0F}},
                    {RigBoneSemantic::RightLowerLeg, "RightLowerLeg", 17, {0.10F, 0.24F, 0.01F}},
                    {RigBoneSemantic::RightFoot, "RightFoot", 18, {0.10F, 0.04F, 0.09F}}};
        }

        [[nodiscard]] std::vector<TemplateBone> QuadrupedTemplate()
        {
            return {{RigBoneSemantic::Root, "Root", -1, {0.0F, 0.48F, 0.0F}},
                    {RigBoneSemantic::Pelvis, "Pelvis", 0, {0.0F, 0.54F, -0.32F}},
                    {RigBoneSemantic::Spine, "Spine", 1, {0.0F, 0.56F, -0.05F}},
                    {RigBoneSemantic::Chest, "Chest", 2, {0.0F, 0.58F, 0.28F}},
                    {RigBoneSemantic::Neck, "Neck", 3, {0.0F, 0.68F, 0.47F}},
                    {RigBoneSemantic::Head, "Head", 4, {0.0F, 0.73F, 0.65F}},
                    {RigBoneSemantic::LeftFrontUpperLeg, "LeftFrontUpperLeg", 3, {-0.17F, 0.51F, 0.28F}},
                    {RigBoneSemantic::LeftFrontLowerLeg, "LeftFrontLowerLeg", 6, {-0.17F, 0.26F, 0.30F}},
                    {RigBoneSemantic::LeftFrontFoot, "LeftFrontFoot", 7, {-0.17F, 0.04F, 0.34F}},
                    {RigBoneSemantic::RightFrontUpperLeg, "RightFrontUpperLeg", 3, {0.17F, 0.51F, 0.28F}},
                    {RigBoneSemantic::RightFrontLowerLeg, "RightFrontLowerLeg", 9, {0.17F, 0.26F, 0.30F}},
                    {RigBoneSemantic::RightFrontFoot, "RightFrontFoot", 10, {0.17F, 0.04F, 0.34F}},
                    {RigBoneSemantic::LeftRearUpperLeg, "LeftRearUpperLeg", 1, {-0.17F, 0.49F, -0.30F}},
                    {RigBoneSemantic::LeftRearLowerLeg, "LeftRearLowerLeg", 12, {-0.17F, 0.25F, -0.27F}},
                    {RigBoneSemantic::LeftRearFoot, "LeftRearFoot", 13, {-0.17F, 0.04F, -0.22F}},
                    {RigBoneSemantic::RightRearUpperLeg, "RightRearUpperLeg", 1, {0.17F, 0.49F, -0.30F}},
                    {RigBoneSemantic::RightRearLowerLeg, "RightRearLowerLeg", 15, {0.17F, 0.25F, -0.27F}},
                    {RigBoneSemantic::RightRearFoot, "RightRearFoot", 16, {0.17F, 0.04F, -0.22F}},
                    {RigBoneSemantic::TailBase, "TailBase", 1, {0.0F, 0.59F, -0.45F}},
                    {RigBoneSemantic::TailTip, "TailTip", 18, {0.0F, 0.69F, -0.72F}}};
        }

        [[nodiscard]] std::string NormalizeBoneName(const std::string_view name)
        {
            std::string result;
            result.reserve(name.size());
            for (const auto character : name)
            {
                const auto value = static_cast<unsigned char>(character);
                if (std::isalnum(value) != 0)
                    result.push_back(static_cast<char>(std::tolower(value)));
            }
            return result;
        }

        [[nodiscard]] bool ContainsAny(const std::string_view value,
                                       const std::initializer_list<std::string_view> candidates) noexcept
        {
            return std::ranges::any_of(candidates, [value](const std::string_view candidate)
                                       { return value.find(candidate) != std::string_view::npos; });
        }

        [[nodiscard]] bool Contains(const std::string_view value, const std::string_view candidate) noexcept
        {
            return value.find(candidate) != std::string_view::npos;
        }

        enum class BoneSide : std::uint8_t
        {
            None,
            Left,
            Right
        };

        [[nodiscard]] BoneSide DetectBoneSide(const std::string_view name) noexcept
        {
            if (Contains(name, "left") || name.ends_with('l'))
                return BoneSide::Left;
            if (Contains(name, "right") || name.ends_with('r'))
                return BoneSide::Right;
            constexpr std::array<std::string_view, 16> prefixedBones{"upper", "lower", "arm",  "hand",  "leg",   "foot",
                                                                     "thigh", "calf",  "shin", "wrist", "front", "rear",
                                                                     "hind",  "wing",  "paw",  "hoof"};
            if (name.size() > 1 && (name.front() == 'l' || name.front() == 'r'))
            {
                const auto unprefixed = name.substr(1);
                if (std::ranges::any_of(prefixedBones,
                                        [unprefixed](const auto prefix) { return unprefixed.starts_with(prefix); }))
                {
                    return name.front() == 'l' ? BoneSide::Left : BoneSide::Right;
                }
            }
            return BoneSide::None;
        }

        [[nodiscard]] RigBoneSemantic ClassifyHumanoidBone(const std::string_view name,
                                                           const std::unordered_set<RigBoneSemantic>& assigned) noexcept
        {
            const auto side = DetectBoneSide(name);
            if (Contains(name, "tail"))
                return ContainsAny(name, {"tip", "end"}) ? RigBoneSemantic::TailTip : RigBoneSemantic::TailBase;
            if (Contains(name, "wing"))
            {
                const auto tip = ContainsAny(name, {"tip", "end"});
                if (side == BoneSide::Left)
                    return tip ? RigBoneSemantic::LeftWingTip : RigBoneSemantic::LeftWingRoot;
                if (side == BoneSide::Right)
                    return tip ? RigBoneSemantic::RightWingTip : RigBoneSemantic::RightWingRoot;
            }
            if (ContainsAny(name, {"pelvis", "hips", "hiproot"}))
                return RigBoneSemantic::Pelvis;
            if (Contains(name, "head") && !Contains(name, "end"))
                return RigBoneSemantic::Head;
            if (Contains(name, "neck"))
                return RigBoneSemantic::Neck;
            if (ContainsAny(name, {"chest", "upperchest"}))
                return RigBoneSemantic::Chest;
            if (Contains(name, "spine"))
            {
                if (!assigned.contains(RigBoneSemantic::Spine))
                    return RigBoneSemantic::Spine;
                if (!assigned.contains(RigBoneSemantic::Chest))
                    return RigBoneSemantic::Chest;
            }
            if (side != BoneSide::None && ContainsAny(name, {"clavicle", "shoulder"}))
                return side == BoneSide::Left ? RigBoneSemantic::LeftShoulder : RigBoneSemantic::RightShoulder;
            if (side != BoneSide::None && ContainsAny(name, {"hand", "wrist"}))
                return side == BoneSide::Left ? RigBoneSemantic::LeftHand : RigBoneSemantic::RightHand;
            if (side != BoneSide::None && ContainsAny(name, {"forearm", "lowerarm", "elbow"}))
                return side == BoneSide::Left ? RigBoneSemantic::LeftLowerArm : RigBoneSemantic::RightLowerArm;
            if (side != BoneSide::None && ContainsAny(name, {"upperarm", "uparm"}) &&
                !ContainsAny(name, {"forearm", "lowerarm"}))
            {
                return side == BoneSide::Left ? RigBoneSemantic::LeftUpperArm : RigBoneSemantic::RightUpperArm;
            }
            if (side != BoneSide::None && Contains(name, "arm") &&
                !ContainsAny(name, {"forearm", "lowerarm", "clavicle"}))
            {
                return side == BoneSide::Left ? RigBoneSemantic::LeftUpperArm : RigBoneSemantic::RightUpperArm;
            }
            if (side != BoneSide::None && ContainsAny(name, {"foot", "ankle"}) && !Contains(name, "toe"))
                return side == BoneSide::Left ? RigBoneSemantic::LeftFoot : RigBoneSemantic::RightFoot;
            if (side != BoneSide::None && ContainsAny(name, {"calf", "shin", "lowerleg"}))
                return side == BoneSide::Left ? RigBoneSemantic::LeftLowerLeg : RigBoneSemantic::RightLowerLeg;
            if (side != BoneSide::None && ContainsAny(name, {"thigh", "upperleg", "upleg"}))
                return side == BoneSide::Left ? RigBoneSemantic::LeftUpperLeg : RigBoneSemantic::RightUpperLeg;
            if (side != BoneSide::None && Contains(name, "leg") && !Contains(name, "upleg"))
                return side == BoneSide::Left ? RigBoneSemantic::LeftLowerLeg : RigBoneSemantic::RightLowerLeg;
            if (ContainsAny(name, {"root", "armature", "skeleton"}) && !ContainsAny(name, {"tailroot", "wingroot"}))
            {
                return RigBoneSemantic::Root;
            }
            return RigBoneSemantic::None;
        }

        [[nodiscard]] RigBoneSemantic
        ClassifyQuadrupedBone(const std::string_view name, const std::unordered_set<RigBoneSemantic>& assigned) noexcept
        {
            if (const auto common = ClassifyHumanoidBone(name, assigned);
                common == RigBoneSemantic::Root || common == RigBoneSemantic::Pelvis ||
                common == RigBoneSemantic::Spine || common == RigBoneSemantic::Chest ||
                common == RigBoneSemantic::Neck || common == RigBoneSemantic::Head ||
                common == RigBoneSemantic::TailBase || common == RigBoneSemantic::TailTip ||
                common == RigBoneSemantic::LeftWingRoot || common == RigBoneSemantic::LeftWingTip ||
                common == RigBoneSemantic::RightWingRoot || common == RigBoneSemantic::RightWingTip)
            {
                return common;
            }

            const auto side = DetectBoneSide(name);
            if (side == BoneSide::None)
                return RigBoneSemantic::None;
            const auto front = ContainsAny(name, {"front", "fore"});
            const auto rear = ContainsAny(name, {"rear", "hind", "back"});
            if (!front && !rear)
                return RigBoneSemantic::None;

            const auto foot = ContainsAny(name, {"foot", "paw", "hoof", "ankle"});
            const auto lower = ContainsAny(name, {"lower", "calf", "shin", "foreleg", "metacarp", "metatars"});
            if (front)
            {
                if (side == BoneSide::Left)
                    return foot    ? RigBoneSemantic::LeftFrontFoot
                           : lower ? RigBoneSemantic::LeftFrontLowerLeg
                                   : RigBoneSemantic::LeftFrontUpperLeg;
                return foot    ? RigBoneSemantic::RightFrontFoot
                       : lower ? RigBoneSemantic::RightFrontLowerLeg
                               : RigBoneSemantic::RightFrontUpperLeg;
            }
            if (side == BoneSide::Left)
                return foot    ? RigBoneSemantic::LeftRearFoot
                       : lower ? RigBoneSemantic::LeftRearLowerLeg
                               : RigBoneSemantic::LeftRearUpperLeg;
            return foot    ? RigBoneSemantic::RightRearFoot
                   : lower ? RigBoneSemantic::RightRearLowerLeg
                           : RigBoneSemantic::RightRearUpperLeg;
        }

        [[nodiscard]] Vector3 FitToBounds(const Vector3 normalized, const MeshBounds& bounds) noexcept
        {
            const auto center = Multiply(Add(bounds.Minimum, bounds.Maximum), 0.5F);
            const auto extent = Subtract(bounds.Maximum, bounds.Minimum);
            return {center.X + normalized.X * extent.X, bounds.Minimum.Y + normalized.Y * extent.Y,
                    center.Z + normalized.Z * extent.Z};
        }

        [[nodiscard]] float PointSegmentDistanceSquared(const Vector3 point, const Vector3 start,
                                                        const Vector3 end) noexcept
        {
            const auto segment = Subtract(end, start);
            const auto lengthSquared = Dot(segment, segment);
            const auto amount = lengthSquared > Epsilon
                                    ? std::clamp(Dot(Subtract(point, start), segment) / lengthSquared, 0.0F, 1.0F)
                                    : 0.0F;
            const auto delta = Subtract(point, Add(start, Multiply(segment, amount)));
            return Dot(delta, delta);
        }

        [[nodiscard]] std::vector<Matrix4> WorldMatrices(const SkeletonAsset& skeleton,
                                                         const std::span<const BoneTransform> localPose)
        {
            if (skeleton.Bones().size() != localPose.size())
                return {};
            std::vector<Matrix4> result(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                result[index] = Math::ComposeTransform(localPose[index].Translation, localPose[index].Rotation,
                                                       localPose[index].Scale);
                const auto parent = skeleton.Bones()[index].Parent;
                if (parent >= 0)
                    result[index] = Math::Multiply(result[static_cast<std::size_t>(parent)], result[index]);
            }
            return result;
        }

        [[nodiscard]] bool MatrixRotation(const Matrix4& matrix, Quaternion& rotation) noexcept
        {
            Vector3 position;
            Vector3 scale;
            return Math::DecomposeTransform(matrix, position, rotation, scale);
        }

        [[nodiscard]] bool SetBoneModelRotation(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                                                const std::uint32_t bone, const Quaternion modelRotation,
                                                const float weight)
        {
            const auto parent = skeleton.Bones()[bone].Parent;
            Quaternion parentRotation;
            if (parent >= 0)
            {
                const auto world = WorldMatrices(skeleton, localPose);
                if (!MatrixRotation(world[static_cast<std::size_t>(parent)], parentRotation))
                    return false;
            }
            const auto desiredLocal = parent >= 0
                                          ? Multiply(Conjugate(Normalize(parentRotation)), Normalize(modelRotation))
                                          : Normalize(modelRotation);
            localPose[bone].Rotation = Nlerp(localPose[bone].Rotation, desiredLocal, weight);
            return true;
        }

        [[nodiscard]] bool ApplyBoneModelRotationDelta(const SkeletonAsset& skeleton,
                                                       const std::span<BoneTransform> localPose,
                                                       const std::uint32_t bone, const Quaternion delta,
                                                       const float weight)
        {
            const auto world = WorldMatrices(skeleton, localPose);
            Quaternion currentRotation;
            if (!MatrixRotation(world[bone], currentRotation))
                return false;
            return SetBoneModelRotation(skeleton, localPose, bone,
                                        Multiply(Normalize(delta), Normalize(currentRotation)), weight);
        }

        [[nodiscard]] Vector3 ProjectOntoPlane(const Vector3 value, const Vector3 normal) noexcept
        {
            return Subtract(value, Multiply(normal, Dot(value, normal)));
        }
    } // namespace

    RigDefinitionAsset::RigDefinitionAsset(RigDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t RigDefinitionAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Bones.size() * sizeof(RigBoneDefinition) +
                             m_Definition.Chains.size() * sizeof(RigChainDefinition);
        for (const auto& bone : m_Definition.Bones)
            result += bone.Name.size();
        for (const auto& chain : m_Definition.Chains)
            result += chain.Name.size() + chain.Bones.size() * sizeof(RigBoneSemantic);
        return result;
    }

    std::string_view RigBoneSemanticName(const RigBoneSemantic semantic) noexcept { return SemanticName(semantic); }

    void ValidateRigDefinition(const RigDefinition& definition)
    {
        if (definition.SchemaVersion != 1)
            throw std::invalid_argument("Unsupported rig-definition schema version.");
        if (definition.MaximumInfluences != 4 && definition.MaximumInfluences != 8)
            throw std::invalid_argument("Rig definitions support exactly four or eight influences.");
        if (definition.Bones.empty() || definition.Bones.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::invalid_argument("Rig definitions require between one and 65535 bones.");

        std::unordered_set<std::string> names;
        std::unordered_set<RigBoneSemantic> semantics;
        for (std::size_t index = 0; index < definition.Bones.size(); ++index)
        {
            const auto& bone = definition.Bones[index];
            if (bone.Name.empty() || !names.emplace(bone.Name).second)
                throw std::invalid_argument("Rig bone names must be non-empty and unique.");
            if (bone.Semantic != RigBoneSemantic::None && !semantics.emplace(bone.Semantic).second)
                throw std::invalid_argument("Rig bone semantics must be unique.");
            if (bone.Parent >= static_cast<std::int32_t>(index))
                throw std::invalid_argument("Rig parents must precede their children.");
            if (!Math::IsFinite(bone.BindPose.Translation) || !Math::IsFinite(bone.BindPose.Rotation) ||
                !Math::IsFinite(bone.BindPose.Scale))
            {
                throw std::invalid_argument("Rig bind poses must be finite.");
            }
        }
        for (const auto& chain : definition.Chains)
        {
            if (chain.Name.empty() || chain.Bones.size() < 2)
                throw std::invalid_argument("Rig chains require a name and at least two bones.");
            for (const auto semantic : chain.Bones)
                if (!semantics.contains(semantic))
                    throw std::invalid_argument("Rig chains may only reference declared semantic bones.");
        }
    }

    RigDefinition InferRigDefinition(const SkeletonAsset& skeleton, const RigProfileType profile,
                                     const SkinningMethod skinning, const std::uint8_t maximumInfluences)
    {
        if (maximumInfluences != 4 && maximumInfluences != 8)
            throw std::invalid_argument("Rig inference supports exactly four or eight influences.");
        if (skeleton.Bones().empty())
            throw std::invalid_argument("Rig inference requires a non-empty skeleton.");

        RigDefinition result;
        result.Profile = profile;
        result.Skinning = skinning;
        result.MaximumInfluences = maximumInfluences;
        result.Bones.reserve(skeleton.Bones().size());
        std::unordered_set<RigBoneSemantic> assigned;
        for (const auto& bone : skeleton.Bones())
        {
            const auto normalized = NormalizeBoneName(bone.Name);
            auto semantic = profile == RigProfileType::Quadruped ? ClassifyQuadrupedBone(normalized, assigned)
                                                                 : ClassifyHumanoidBone(normalized, assigned);
            if (semantic != RigBoneSemantic::None && assigned.contains(semantic))
                semantic = RigBoneSemantic::None;
            if (semantic != RigBoneSemantic::None)
                assigned.insert(semantic);
            result.Bones.push_back(
                {semantic, bone.Name, bone.Parent, bone.BindPose, semantic != RigBoneSemantic::None});
        }

        if (!assigned.contains(RigBoneSemantic::Root))
        {
            const auto root =
                std::ranges::find_if(result.Bones, [](const RigBoneDefinition& bone)
                                     { return bone.Parent < 0 && bone.Semantic == RigBoneSemantic::None; });
            if (root != result.Bones.end())
            {
                root->Semantic = RigBoneSemantic::Root;
                root->Required = true;
                assigned.insert(RigBoneSemantic::Root);
            }
        }

        const auto addChain = [&result, &assigned](std::string name, const std::initializer_list<RigBoneSemantic> bones)
        {
            if (std::ranges::all_of(bones, [&assigned](const auto semantic) { return assigned.contains(semantic); }))
                result.Chains.push_back({std::move(name), {bones.begin(), bones.end()}});
        };
        addChain("Spine", {RigBoneSemantic::Pelvis, RigBoneSemantic::Spine, RigBoneSemantic::Chest,
                           RigBoneSemantic::Neck, RigBoneSemantic::Head});
        if (profile == RigProfileType::Quadruped)
        {
            addChain("Left Front Leg", {RigBoneSemantic::LeftFrontUpperLeg, RigBoneSemantic::LeftFrontLowerLeg,
                                        RigBoneSemantic::LeftFrontFoot});
            addChain("Right Front Leg", {RigBoneSemantic::RightFrontUpperLeg, RigBoneSemantic::RightFrontLowerLeg,
                                         RigBoneSemantic::RightFrontFoot});
            addChain("Left Rear Leg", {RigBoneSemantic::LeftRearUpperLeg, RigBoneSemantic::LeftRearLowerLeg,
                                       RigBoneSemantic::LeftRearFoot});
            addChain("Right Rear Leg", {RigBoneSemantic::RightRearUpperLeg, RigBoneSemantic::RightRearLowerLeg,
                                        RigBoneSemantic::RightRearFoot});
        }
        else
        {
            addChain("Left Arm", {RigBoneSemantic::LeftShoulder, RigBoneSemantic::LeftUpperArm,
                                  RigBoneSemantic::LeftLowerArm, RigBoneSemantic::LeftHand});
            addChain("Right Arm", {RigBoneSemantic::RightShoulder, RigBoneSemantic::RightUpperArm,
                                   RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand});
            addChain("Left Leg",
                     {RigBoneSemantic::LeftUpperLeg, RigBoneSemantic::LeftLowerLeg, RigBoneSemantic::LeftFoot});
            addChain("Right Leg",
                     {RigBoneSemantic::RightUpperLeg, RigBoneSemantic::RightLowerLeg, RigBoneSemantic::RightFoot});
        }
        addChain("Tail", {RigBoneSemantic::TailBase, RigBoneSemantic::TailTip});
        addChain("Left Wing", {RigBoneSemantic::LeftWingRoot, RigBoneSemantic::LeftWingTip});
        addChain("Right Wing", {RigBoneSemantic::RightWingRoot, RigBoneSemantic::RightWingTip});
        ValidateRigDefinition(result);
        return result;
    }

    std::vector<std::byte> RigDefinitionAsset::Encode(const RigDefinition& definition)
    {
        ValidateRigDefinition(definition);
        Json root{
            {"schemaVersion", definition.SchemaVersion},
            {"profile", ProfileName(definition.Profile)},
            {"skinning", definition.Skinning == SkinningMethod::DualQuaternion ? "dualQuaternion" : "linearBlend"},
            {"maximumInfluences", definition.MaximumInfluences}};
        root["bones"] = Json::array();
        for (const auto& bone : definition.Bones)
        {
            root["bones"].push_back({{"semantic", SemanticName(bone.Semantic)},
                                     {"name", bone.Name},
                                     {"parent", bone.Parent},
                                     {"position", EncodeVector(bone.BindPose.Translation)},
                                     {"rotation", EncodeQuaternion(bone.BindPose.Rotation)},
                                     {"scale", EncodeVector(bone.BindPose.Scale)},
                                     {"required", bone.Required}});
        }
        root["chains"] = Json::array();
        for (const auto& chain : definition.Chains)
        {
            Json bones = Json::array();
            for (const auto semantic : chain.Bones)
                bones.push_back(SemanticName(semantic));
            root["chains"].push_back({{"name", chain.Name}, {"bones", std::move(bones)}});
        }
        const auto text = root.dump(2);
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<RigDefinitionAsset> RigDefinitionAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto root = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                      reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        RigDefinition definition;
        definition.SchemaVersion = root.at("schemaVersion").get<std::uint32_t>();
        definition.Profile = ParseProfile(root.at("profile").get<std::string>());
        definition.Skinning = root.value("skinning", "linearBlend") == "dualQuaternion" ? SkinningMethod::DualQuaternion
                                                                                        : SkinningMethod::LinearBlend;
        const auto maximumInfluences = root.value("maximumInfluences", 4);
        if (maximumInfluences != 4 && maximumInfluences != 8)
            throw std::invalid_argument("Rig maximum influences must be 4 or 8.");
        definition.MaximumInfluences = static_cast<std::uint8_t>(maximumInfluences);
        for (const auto& encoded : root.at("bones"))
        {
            RigBoneDefinition bone;
            bone.Semantic = ParseSemantic(encoded.value("semantic", "none"));
            bone.Name = encoded.at("name").get<std::string>();
            bone.Parent = encoded.value("parent", -1);
            bone.BindPose.Translation = DecodeVector(encoded.at("position"));
            bone.BindPose.Rotation = DecodeQuaternion(encoded.at("rotation"));
            bone.BindPose.Scale = DecodeVector(encoded.at("scale"));
            bone.Required = encoded.value("required", true);
            definition.Bones.push_back(std::move(bone));
        }
        for (const auto& encoded : root.value("chains", Json::array()))
        {
            RigChainDefinition chain;
            chain.Name = encoded.at("name").get<std::string>();
            for (const auto& semantic : encoded.at("bones"))
                chain.Bones.push_back(ParseSemantic(semantic.get<std::string>()));
            definition.Chains.push_back(std::move(chain));
        }
        ValidateRigDefinition(definition);
        return CreateRef<RigDefinitionAsset>(std::move(definition));
    }

    AutoRigResult GenerateRig(const MeshAsset& mesh, const AutoRigRequest& request)
    {
        if (mesh.Vertices().empty())
            throw std::invalid_argument("Auto-rigging requires a non-empty mesh.");
        if (request.MaximumInfluences != 4 && request.MaximumInfluences != 8)
            throw std::invalid_argument("Auto-rigging supports exactly four or eight influences.");
        if (!Math::IsFinite(mesh.Bounds().Minimum) || !Math::IsFinite(mesh.Bounds().Maximum))
            throw std::invalid_argument("Auto-rigging requires finite mesh bounds.");
        const auto extent = Subtract(mesh.Bounds().Maximum, mesh.Bounds().Minimum);
        if (extent.X <= Epsilon || extent.Y <= Epsilon || extent.Z <= Epsilon)
            throw std::invalid_argument("Auto-rigging requires non-degenerate three-dimensional mesh bounds.");

        AutoRigResult result;
        result.Rig.Profile = request.Profile;
        result.Rig.Skinning = request.Skinning;
        result.Rig.MaximumInfluences = request.MaximumInfluences;
        std::vector<TemplateBone> templateBones;
        if (request.Profile == RigProfileType::Quadruped)
            templateBones = QuadrupedTemplate();
        else if (request.Profile == RigProfileType::Custom)
        {
            if (!request.CustomProfile)
                throw std::invalid_argument("Custom auto-rigging requires a custom rig profile.");
            ValidateRigDefinition(*request.CustomProfile);
            result.Rig = *request.CustomProfile;
        }
        else
            templateBones = HumanoidTemplate();

        std::unordered_map<RigBoneSemantic, Vector3> markerPositions;
        for (const auto& marker : request.Markers)
        {
            if (marker.Bone == RigBoneSemantic::None || !Math::IsFinite(marker.Position) ||
                !std::isfinite(marker.Confidence) || marker.Confidence < 0.0F || marker.Confidence > 1.0F)
            {
                throw std::invalid_argument("Auto-rig markers require a semantic, finite position, and confidence.");
            }
            markerPositions.insert_or_assign(marker.Bone, marker.Position);
        }

        std::vector<Vector3> worldPositions;
        if (!templateBones.empty())
        {
            worldPositions.reserve(templateBones.size());
            result.Rig.Bones.reserve(templateBones.size());
            for (const auto& bone : templateBones)
            {
                auto position = FitToBounds(bone.NormalizedPosition, mesh.Bounds());
                if (const auto marker = markerPositions.find(bone.Semantic); marker != markerPositions.end())
                    position = marker->second;
                worldPositions.push_back(position);
                const auto parentPosition =
                    bone.Parent >= 0 ? worldPositions[static_cast<std::size_t>(bone.Parent)] : Vector3{};
                result.Rig.Bones.push_back(
                    {bone.Semantic,
                     std::string(bone.Name),
                     bone.Parent,
                     {bone.Parent >= 0 ? Subtract(position, parentPosition) : position, {}, {1.0F, 1.0F, 1.0F}},
                     true});
            }
            if (request.Profile == RigProfileType::Quadruped)
            {
                result.Rig.Chains = {{"Spine",
                                      {RigBoneSemantic::Pelvis, RigBoneSemantic::Spine, RigBoneSemantic::Chest,
                                       RigBoneSemantic::Neck, RigBoneSemantic::Head}},
                                     {"Tail", {RigBoneSemantic::TailBase, RigBoneSemantic::TailTip}},
                                     {"Left Front Leg",
                                      {RigBoneSemantic::LeftFrontUpperLeg, RigBoneSemantic::LeftFrontLowerLeg,
                                       RigBoneSemantic::LeftFrontFoot}},
                                     {"Right Front Leg",
                                      {RigBoneSemantic::RightFrontUpperLeg, RigBoneSemantic::RightFrontLowerLeg,
                                       RigBoneSemantic::RightFrontFoot}},
                                     {"Left Rear Leg",
                                      {RigBoneSemantic::LeftRearUpperLeg, RigBoneSemantic::LeftRearLowerLeg,
                                       RigBoneSemantic::LeftRearFoot}},
                                     {"Right Rear Leg",
                                      {RigBoneSemantic::RightRearUpperLeg, RigBoneSemantic::RightRearLowerLeg,
                                       RigBoneSemantic::RightRearFoot}}};
            }
            else
            {
                result.Rig.Chains = {
                    {"Spine",
                     {RigBoneSemantic::Pelvis, RigBoneSemantic::Spine, RigBoneSemantic::Chest, RigBoneSemantic::Neck,
                      RigBoneSemantic::Head}},
                    {"Left Arm",
                     {RigBoneSemantic::LeftUpperArm, RigBoneSemantic::LeftLowerArm, RigBoneSemantic::LeftHand}},
                    {"Right Arm",
                     {RigBoneSemantic::RightUpperArm, RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand}},
                    {"Left Leg",
                     {RigBoneSemantic::LeftUpperLeg, RigBoneSemantic::LeftLowerLeg, RigBoneSemantic::LeftFoot}},
                    {"Right Leg",
                     {RigBoneSemantic::RightUpperLeg, RigBoneSemantic::RightLowerLeg, RigBoneSemantic::RightFoot}}};
            }
        }
        else
        {
            worldPositions.resize(result.Rig.Bones.size());
            for (std::size_t index = 0; index < result.Rig.Bones.size(); ++index)
            {
                const auto local = Math::ComposeTransform(result.Rig.Bones[index].BindPose.Translation,
                                                          result.Rig.Bones[index].BindPose.Rotation,
                                                          result.Rig.Bones[index].BindPose.Scale);
                auto world = local;
                if (result.Rig.Bones[index].Parent >= 0)
                {
                    const auto parent = static_cast<std::size_t>(result.Rig.Bones[index].Parent);
                    const auto parentWorld = Math::ComposeTransform(worldPositions[parent], {}, {1.0F, 1.0F, 1.0F});
                    world = Math::Multiply(parentWorld, local);
                }
                worldPositions[index] = Math::TransformPoint(world, {});
            }
        }

        ValidateRigDefinition(result.Rig);
        result.Skeleton.reserve(result.Rig.Bones.size());
        std::vector<Matrix4> worldMatrices(result.Rig.Bones.size());
        for (std::size_t index = 0; index < result.Rig.Bones.size(); ++index)
        {
            const auto& rigBone = result.Rig.Bones[index];
            auto world =
                Math::ComposeTransform(rigBone.BindPose.Translation, rigBone.BindPose.Rotation, rigBone.BindPose.Scale);
            if (rigBone.Parent >= 0)
                world = Math::Multiply(worldMatrices[static_cast<std::size_t>(rigBone.Parent)], world);
            worldMatrices[index] = world;
            result.Skeleton.push_back({rigBone.Name, rigBone.Parent, rigBone.BindPose, Math::Inverse(world)});
        }

        const auto diagonal = Length(extent);
        const auto radiusSquared = std::max(diagonal * diagonal * 0.0004F, Epsilon);
        result.Influences.reserve(mesh.Vertices().size());
        std::vector<std::pair<float, std::uint16_t>> scores(worldPositions.size());
        const auto scoreOrder = [](const auto& left, const auto& right)
        {
            if (left.first != right.first)
                return left.first > right.first;
            return left.second < right.second;
        };
        for (const auto& vertex : mesh.Vertices())
        {
            for (std::size_t bone = 0; bone < worldPositions.size(); ++bone)
            {
                const auto parent = result.Rig.Bones[bone].Parent;
                const auto start =
                    parent >= 0 ? worldPositions[static_cast<std::size_t>(parent)] : worldPositions[bone];
                const auto distanceSquared = PointSegmentDistanceSquared(vertex.Position, start, worldPositions[bone]);
                scores[bone] = {1.0F / (distanceSquared + radiusSquared), static_cast<std::uint16_t>(bone)};
            }
            SkinVertexInfluence8 influence;
            const auto influenceCount = std::min<std::size_t>(request.MaximumInfluences, scores.size());
            const auto influenceEnd = scores.begin() + static_cast<decltype(scores)::difference_type>(influenceCount);
            std::partial_sort(scores.begin(), influenceEnd, scores.end(), scoreOrder);
            influence.Count = static_cast<std::uint8_t>(influenceCount);
            float total = 0.0F;
            for (std::size_t index = 0; index < influence.Count; ++index)
            {
                influence.Bones[index] = scores[index].second;
                influence.Weights[index] = scores[index].first;
                total += scores[index].first;
            }
            for (std::size_t index = 0; index < influence.Count; ++index)
                influence.Weights[index] /= total;
            result.Influences.push_back(influence);
        }

        if (request.Markers.empty())
        {
            result.Diagnostics.push_back(
                {RigDiagnosticSeverity::Information, "KEIRERIG0001",
                 "The rig used deterministic profile fitting. Add semantic markers to refine unusual proportions."});
        }
        if (mesh.Indices().empty())
        {
            result.Diagnostics.push_back(
                {RigDiagnosticSeverity::Warning, "KEIRERIG0002",
                 "The mesh has no triangle topology; weight solving used geometric proximity only."});
        }
        return result;
    }

    AnimationRetargetDiagnostics DiagnoseAnimationRetargeting(const SkeletonAsset& sourceSkeleton,
                                                              const RigDefinition& sourceRig,
                                                              const AnimationClipAsset& sourceClip,
                                                              const SkeletonAsset& targetSkeleton,
                                                              const RigDefinition& targetRig)
    {
        ValidateRigDefinition(sourceRig);
        ValidateRigDefinition(targetRig);
        if (sourceSkeleton.Bones().size() != sourceRig.Bones.size() ||
            targetSkeleton.Bones().size() != targetRig.Bones.size())
        {
            throw std::invalid_argument("Retargeting requires rig definitions that match their skeletons.");
        }

        std::unordered_map<RigBoneSemantic, std::uint32_t> targetBySemantic;
        for (std::uint32_t index = 0; index < targetRig.Bones.size(); ++index)
            if (targetRig.Bones[index].Semantic != RigBoneSemantic::None)
                targetBySemantic.emplace(targetRig.Bones[index].Semantic, index);
        std::unordered_map<std::string_view, std::uint32_t> targetByName;
        targetByName.reserve(targetSkeleton.Bones().size());
        for (std::uint32_t index = 0; index < targetSkeleton.Bones().size(); ++index)
            targetByName.emplace(targetSkeleton.Bones()[index].Name, index);

        const auto nearlyEqual = [](const float left, const float right)
        {
            const auto scale = std::max({1.0F, std::abs(left), std::abs(right)});
            return std::abs(left - right) <= scale * 0.0001F;
        };
        const auto matchingVector = [&nearlyEqual](const Vector3 left, const Vector3 right)
        { return nearlyEqual(left.X, right.X) && nearlyEqual(left.Y, right.Y) && nearlyEqual(left.Z, right.Z); };
        const auto matchingRotation = [](const Quaternion left, const Quaternion right)
        {
            const auto normalizedLeft = Normalize(left);
            const auto normalizedRight = Normalize(right);
            return std::abs(normalizedLeft.X * normalizedRight.X + normalizedLeft.Y * normalizedRight.Y +
                            normalizedLeft.Z * normalizedRight.Z + normalizedLeft.W * normalizedRight.W) >= 0.9999F;
        };
        const auto matchingHierarchy =
            [&sourceSkeleton, &targetSkeleton](const std::uint32_t source, const std::uint32_t target)
        {
            const auto sourceParent = sourceSkeleton.Bones()[source].Parent;
            const auto targetParent = targetSkeleton.Bones()[target].Parent;
            if (sourceParent < 0 || targetParent < 0)
                return sourceParent < 0 && targetParent < 0;
            return sourceSkeleton.Bones()[static_cast<std::size_t>(sourceParent)].Name ==
                   targetSkeleton.Bones()[static_cast<std::size_t>(targetParent)].Name;
        };
        const auto isAssimpFbxRotationHelper = [](const std::string_view name) noexcept
        { return name.ends_with("_$AssimpFbx$_Rotation"); };

        AnimationRetargetDiagnostics result;
        result.SourceTrackCount = sourceClip.Tracks().size();
        result.Mappings.reserve(sourceClip.Tracks().size());
        std::unordered_map<std::uint32_t, std::size_t> acceptedTargets;
        for (const auto& sourceTrack : sourceClip.Tracks())
        {
            AnimationRetargetBoneMapping mapping;
            mapping.SourceBone = sourceTrack.Bone;
            if (sourceTrack.Bone >= sourceRig.Bones.size())
            {
                result.Messages.push_back({RigDiagnosticSeverity::Error, "KEIRERETARGET0001",
                                           "The clip references a source bone outside its rig."});
                result.Mappings.push_back(std::move(mapping));
                continue;
            }
            mapping.SourceName = sourceSkeleton.Bones()[sourceTrack.Bone].Name;
            mapping.Semantic = sourceRig.Bones[sourceTrack.Bone].Semantic;
            std::optional<std::uint32_t> targetIndex;
            bool exactNameMatch = false;
            if (const auto exact = targetByName.find(sourceSkeleton.Bones()[sourceTrack.Bone].Name);
                exact != targetByName.end())
            {
                targetIndex = exact->second;
                exactNameMatch = true;
            }
            else if (const auto semantic = targetBySemantic.find(sourceRig.Bones[sourceTrack.Bone].Semantic);
                     semantic != targetBySemantic.end())
            {
                targetIndex = semantic->second;
            }
            if (!targetIndex)
            {
                result.Messages.push_back({RigDiagnosticSeverity::Warning, "KEIRERETARGET0002",
                                           "No target bone matches source track '" + mapping.SourceName + "'.",
                                           mapping.Semantic});
                result.Mappings.push_back(std::move(mapping));
                continue;
            }
            const auto& sourceBind = sourceSkeleton.Bones()[sourceTrack.Bone].BindPose;
            const auto& targetBind = targetSkeleton.Bones()[*targetIndex].BindPose;
            const auto sourceLength = Length(sourceBind.Translation);
            const auto targetLength = Length(targetBind.Translation);
            const auto translationScale = sourceLength > Epsilon ? targetLength / sourceLength : 1.0F;
            const auto preserveAuthoredLocalTrack =
                exactNameMatch && matchingHierarchy(sourceTrack.Bone, *targetIndex) &&
                (isAssimpFbxRotationHelper(sourceSkeleton.Bones()[sourceTrack.Bone].Name) ||
                 (matchingVector(sourceBind.Translation, targetBind.Translation) &&
                  matchingRotation(sourceBind.Rotation, targetBind.Rotation) &&
                  matchingVector(sourceBind.Scale, targetBind.Scale)));
            mapping.TargetBone = *targetIndex;
            mapping.TargetName = targetSkeleton.Bones()[*targetIndex].Name;
            mapping.Match = exactNameMatch ? AnimationRetargetMatch::ExactName : AnimationRetargetMatch::Semantic;
            mapping.TranslationScale = translationScale;
            mapping.PreservesAuthoredLocalTrack = preserveAuthoredLocalTrack;
            for (const auto& key : sourceTrack.Keys)
            {
                const auto fallback = [](const float animated, const float source)
                {
                    if (std::abs(source) <= Epsilon)
                        return true;
                    const auto relative = animated / source;
                    return !std::isfinite(relative) || relative < 0.125F || relative > 8.0F;
                };
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.X, sourceBind.Scale.X) ? 1U : 0U;
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.Y, sourceBind.Scale.Y) ? 1U : 0U;
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.Z, sourceBind.Scale.Z) ? 1U : 0U;
            }

            if (const auto accepted = acceptedTargets.find(*targetIndex); accepted != acceptedTargets.end())
            {
                auto& previous = result.Mappings[accepted->second];
                if (mapping.Match == AnimationRetargetMatch::ExactName &&
                    previous.Match == AnimationRetargetMatch::Semantic)
                {
                    previous.TargetBone.reset();
                    previous.Match = AnimationRetargetMatch::TargetConflict;
                    accepted->second = result.Mappings.size();
                }
                else
                {
                    mapping.TargetBone.reset();
                    mapping.Match = AnimationRetargetMatch::TargetConflict;
                }
                result.Messages.push_back({RigDiagnosticSeverity::Warning, "KEIRERETARGET0003",
                                           "Multiple source tracks resolve to target bone '" +
                                               targetSkeleton.Bones()[*targetIndex].Name +
                                               "'; the exact-name mapping takes priority.",
                                           mapping.Semantic});
            }
            else
            {
                acceptedTargets.emplace(*targetIndex, result.Mappings.size());
            }
            result.Mappings.push_back(std::move(mapping));
        }

        for (const auto& mapping : result.Mappings)
        {
            if (!mapping.TargetBone)
                continue;
            ++result.MappedTrackCount;
            result.ExactNameMatchCount += mapping.Match == AnimationRetargetMatch::ExactName ? 1U : 0U;
            result.SemanticMatchCount += mapping.Match == AnimationRetargetMatch::Semantic ? 1U : 0U;
        }
        if (sourceClip.RootMotion())
        {
            const auto rootMapping = std::ranges::find(result.Mappings, 0U, &AnimationRetargetBoneMapping::SourceBone);
            result.RootMotionMapped = rootMapping != result.Mappings.end() && rootMapping->TargetBone == 0U;
            if (!result.RootMotionMapped)
                result.Messages.push_back(
                    {RigDiagnosticSeverity::Warning, "KEIRERETARGET0004",
                     "Root motion is enabled but the source root does not map to the target root."});
        }
        if (!result.Compatible())
            result.Messages.push_back({RigDiagnosticSeverity::Error, "KEIRERETARGET0005",
                                       "Retargeting found no compatible source animation tracks."});
        return result;
    }

    AnimationRetargetResult
    RetargetAnimationClipWithDiagnostics(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                                         const AnimationClipAsset& sourceClip, const AssetId targetSkeletonId,
                                         const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig)
    {
        auto diagnostics =
            DiagnoseAnimationRetargeting(sourceSkeleton, sourceRig, sourceClip, targetSkeleton, targetRig);
        if (!diagnostics.Compatible())
            throw std::invalid_argument("Retargeting found no compatible semantic bone tracks.");

        std::vector<AnimationTrack> tracks;
        tracks.reserve(diagnostics.MappedTrackCount);
        for (const auto& sourceTrack : sourceClip.Tracks())
        {
            const auto found =
                std::ranges::find(diagnostics.Mappings, sourceTrack.Bone, &AnimationRetargetBoneMapping::SourceBone);
            if (found == diagnostics.Mappings.end() || !found->TargetBone)
                continue;
            const auto& sourceBind = sourceSkeleton.Bones()[sourceTrack.Bone].BindPose;
            const auto& targetBind = targetSkeleton.Bones()[*found->TargetBone].BindPose;
            AnimationTrack track;
            track.Bone = *found->TargetBone;
            track.Keys.reserve(sourceTrack.Keys.size());
            for (const auto& key : sourceTrack.Keys)
            {
                auto value = key.Value;
                if (!found->PreservesAuthoredLocalTrack)
                {
                    value.Translation =
                        Add(targetBind.Translation,
                            Multiply(Subtract(value.Translation, sourceBind.Translation), found->TranslationScale));
                    const auto sourceRotation = Normalize(sourceBind.Rotation);
                    const auto targetRotation = Normalize(targetBind.Rotation);
                    const auto rotationDelta = Multiply(Conjugate(sourceRotation), Normalize(value.Rotation));
                    value.Rotation = Normalize(Multiply(targetRotation, rotationDelta));
                    const auto retargetScale = [](const float animated, const float source, const float target)
                    {
                        if (std::abs(source) <= Epsilon)
                            return target;
                        const auto relative = animated / source;
                        if (!std::isfinite(relative) || relative < 0.125F || relative > 8.0F)
                            return target;
                        return target * relative;
                    };
                    value.Scale = {retargetScale(value.Scale.X, sourceBind.Scale.X, targetBind.Scale.X),
                                   retargetScale(value.Scale.Y, sourceBind.Scale.Y, targetBind.Scale.Y),
                                   retargetScale(value.Scale.Z, sourceBind.Scale.Z, targetBind.Scale.Z)};
                }
                track.Keys.push_back({key.Time, value});
            }
            tracks.push_back(std::move(track));
        }
        AnimationRetargetResult result;
        result.Clip = CreateRef<AnimationClipAsset>(
            targetSkeletonId, sourceClip.Duration(), std::move(tracks),
            std::vector<AnimationEvent>(sourceClip.Events().begin(), sourceClip.Events().end()),
            sourceClip.RootMotion() && diagnostics.RootMotionMapped);
        result.Diagnostics = std::move(diagnostics);
        return result;
    }

    Ref<AnimationClipAsset> RetargetAnimationClip(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                                                  const AnimationClipAsset& sourceClip, const AssetId targetSkeletonId,
                                                  const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig)
    {
        return RetargetAnimationClipWithDiagnostics(sourceSkeleton, sourceRig, sourceClip, targetSkeletonId,
                                                    targetSkeleton, targetRig)
            .Clip;
    }

    bool SolveTwoBoneIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                        const TwoBoneIkRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Root >= localPose.size() ||
            request.Middle >= localPose.size() || request.End >= localPose.size() ||
            skeleton.Bones()[request.Middle].Parent != static_cast<std::int32_t>(request.Root) ||
            skeleton.Bones()[request.End].Parent != static_cast<std::int32_t>(request.Middle) ||
            !Math::IsFinite(request.Target) || !Math::IsFinite(request.Pole) || !std::isfinite(request.Weight) ||
            (request.EndRotation &&
             (!Math::IsFinite(*request.EndRotation) || Math::Length(*request.EndRotation) <= Epsilon)) ||
            !std::isfinite(request.EndRotationWeight))
        {
            return false;
        }
        const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
        const auto endRotationWeight = std::clamp(request.EndRotationWeight, 0.0F, 1.0F);
        if (weight <= 0.0F)
        {
            if (request.EndRotation && endRotationWeight > 0.0F)
                return SetBoneModelRotation(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight);
            return true;
        }

        auto world = WorldMatrices(skeleton, localPose);
        auto rootPosition = Math::TransformPoint(world[request.Root], {});
        auto middlePosition = Math::TransformPoint(world[request.Middle], {});
        auto endPosition = Math::TransformPoint(world[request.End], {});
        const auto upperLength = Length(Subtract(middlePosition, rootPosition));
        const auto lowerLength = Length(Subtract(endPosition, middlePosition));
        if (upperLength <= Epsilon || lowerLength <= Epsilon)
            return false;

        auto targetDelta = Subtract(request.Target, rootPosition);
        if (Length(targetDelta) <= Epsilon)
            targetDelta = Subtract(endPosition, rootPosition);
        const auto targetDistance = std::clamp(Length(targetDelta), std::abs(upperLength - lowerLength) + Epsilon,
                                               upperLength + lowerLength - Epsilon);
        const auto forward = Normalize(targetDelta);
        auto bendVector = ProjectOntoPlane(Subtract(request.Pole, rootPosition), forward);
        if (Length(bendVector) <= Epsilon)
            bendVector = ProjectOntoPlane(Subtract(middlePosition, rootPosition), forward);
        if (Length(bendVector) <= Epsilon)
        {
            const auto fallback = std::abs(forward.Y) < 0.95F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{0.0F, 0.0F, 1.0F};
            bendVector = ProjectOntoPlane(fallback, forward);
        }
        const auto bend = Normalize(bendVector);
        const auto projected =
            (upperLength * upperLength + targetDistance * targetDistance - lowerLength * lowerLength) /
            (2.0F * targetDistance);
        const auto height = std::sqrt(std::max(0.0F, upperLength * upperLength - projected * projected));
        const auto desiredMiddle = Add(rootPosition, Add(Multiply(forward, projected), Multiply(bend, height)));

        const auto rootDelta = FromTo(Subtract(middlePosition, rootPosition), Subtract(desiredMiddle, rootPosition));
        if (!ApplyBoneModelRotationDelta(skeleton, localPose, request.Root, rootDelta, weight))
            return false;

        world = WorldMatrices(skeleton, localPose);
        middlePosition = Math::TransformPoint(world[request.Middle], {});
        endPosition = Math::TransformPoint(world[request.End], {});
        const auto reachableTarget = Add(rootPosition, Multiply(forward, targetDistance));
        const auto middleDelta =
            FromTo(Subtract(endPosition, middlePosition), Subtract(reachableTarget, middlePosition));
        if (!ApplyBoneModelRotationDelta(skeleton, localPose, request.Middle, middleDelta, weight))
            return false;
        if (request.EndRotation && endRotationWeight > 0.0F &&
            !SetBoneModelRotation(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight))
        {
            return false;
        }
        return true;
    }

    bool SolveFabrikIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                       const FabrikIkRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Chain.size() < 2 || request.MaximumIterations == 0 ||
            request.MaximumIterations > 1024 || request.Tolerance <= 0.0F || !std::isfinite(request.Tolerance) ||
            !std::isfinite(request.Weight) || !Math::IsFinite(request.Target))
        {
            return false;
        }
        for (std::size_t index = 0; index < request.Chain.size(); ++index)
        {
            if (request.Chain[index] >= localPose.size())
                return false;
            if (index > 0 &&
                skeleton.Bones()[request.Chain[index]].Parent != static_cast<std::int32_t>(request.Chain[index - 1]))
                return false;
        }

        const auto world = WorldMatrices(skeleton, localPose);
        std::vector<Vector3> positions(request.Chain.size());
        std::vector<float> lengths(request.Chain.size() - 1);
        float totalLength = 0.0F;
        for (std::size_t index = 0; index < request.Chain.size(); ++index)
        {
            positions[index] = Math::TransformPoint(world[request.Chain[index]], {});
            if (index > 0)
            {
                lengths[index - 1] = Length(Subtract(positions[index], positions[index - 1]));
                if (lengths[index - 1] <= Epsilon)
                    return false;
                totalLength += lengths[index - 1];
            }
        }

        const auto root = positions.front();
        if (Length(Subtract(request.Target, root)) >= totalLength)
        {
            const auto direction = Normalize(Subtract(request.Target, root));
            for (std::size_t index = 1; index < positions.size(); ++index)
                positions[index] = Add(positions[index - 1], Multiply(direction, lengths[index - 1]));
        }
        else
        {
            for (std::uint32_t iteration = 0; iteration < request.MaximumIterations; ++iteration)
            {
                positions.back() = request.Target;
                for (std::size_t index = positions.size() - 1; index > 0; --index)
                {
                    const auto direction = Normalize(Subtract(positions[index - 1], positions[index]));
                    positions[index - 1] = Add(positions[index], Multiply(direction, lengths[index - 1]));
                }
                positions.front() = root;
                for (std::size_t index = 1; index < positions.size(); ++index)
                {
                    const auto direction = Normalize(Subtract(positions[index], positions[index - 1]));
                    positions[index] = Add(positions[index - 1], Multiply(direction, lengths[index - 1]));
                }
                if (Length(Subtract(positions.back(), request.Target)) <= request.Tolerance)
                    break;
            }
        }

        const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
        for (std::size_t index = 0; index + 1 < request.Chain.size(); ++index)
        {
            const auto bone = request.Chain[index];
            const auto currentWorld = WorldMatrices(skeleton, localPose);
            const auto current = Math::TransformPoint(currentWorld[bone], {});
            const auto child = Math::TransformPoint(currentWorld[request.Chain[index + 1]], {});
            const auto delta = FromTo(Subtract(child, current), Subtract(positions[index + 1], positions[index]));
            if (!ApplyBoneModelRotationDelta(skeleton, localPose, bone, delta, weight))
                return false;
        }
        return true;
    }

    std::optional<FootGroundingResult> SolveFootGrounding(const SkeletonAsset& skeleton,
                                                          const std::span<BoneTransform> localPose,
                                                          const FootGroundingRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Contacts.empty() || request.Contacts.size() > 16 ||
            (request.Pelvis && *request.Pelvis >= localPose.size()) || !std::isfinite(request.FootHeight) ||
            request.FootHeight < 0.0F || !std::isfinite(request.PelvisWeight) || request.PelvisWeight < 0.0F ||
            request.PelvisWeight > 1.0F || !std::isfinite(request.MaximumPelvisAdjustment) ||
            request.MaximumPelvisAdjustment < 0.0F)
            return std::nullopt;

        std::set<std::uint32_t> feet;
        for (const auto& contact : request.Contacts)
        {
            if (contact.UpperLeg >= localPose.size() || contact.LowerLeg >= localPose.size() ||
                contact.Foot >= localPose.size() ||
                skeleton.Bones()[contact.LowerLeg].Parent != static_cast<std::int32_t>(contact.UpperLeg) ||
                skeleton.Bones()[contact.Foot].Parent != static_cast<std::int32_t>(contact.LowerLeg) ||
                !feet.insert(contact.Foot).second || !Math::IsFinite(contact.Position) ||
                !Math::IsFinite(contact.Normal) || Length(contact.Normal) <= Epsilon || !Math::IsFinite(contact.Pole) ||
                !std::isfinite(contact.Weight) || contact.Weight < 0.0F || contact.Weight > 1.0F ||
                !std::isfinite(contact.RotationWeight) || contact.RotationWeight < 0.0F ||
                contact.RotationWeight > 1.0F)
                return std::nullopt;
        }

        std::vector<BoneTransform> working(localPose.begin(), localPose.end());
        FootGroundingResult result;
        if (request.Pelvis && request.PelvisWeight > 0.0F)
        {
            const auto world = WorldMatrices(skeleton, working);
            float requestedAdjustment = request.MaximumPelvisAdjustment;
            for (const auto& contact : request.Contacts)
            {
                const auto current = Math::TransformPoint(world[contact.Foot], {});
                const auto target = Add(contact.Position, Multiply(Normalize(contact.Normal), request.FootHeight));
                requestedAdjustment = std::min(requestedAdjustment, target.Y - current.Y);
            }
            result.PelvisAdjustment =
                std::clamp(requestedAdjustment, -request.MaximumPelvisAdjustment, request.MaximumPelvisAdjustment) *
                request.PelvisWeight;
            Vector3 localAdjustment{0.0F, result.PelvisAdjustment, 0.0F};
            const auto parent = skeleton.Bones()[*request.Pelvis].Parent;
            if (parent >= 0)
            {
                try
                {
                    localAdjustment = Math::TransformDirection(Math::Inverse(world[static_cast<std::size_t>(parent)]),
                                                               localAdjustment);
                }
                catch (const std::exception&)
                {
                    return std::nullopt;
                }
            }
            working[*request.Pelvis].Translation = Add(working[*request.Pelvis].Translation, localAdjustment);
        }

        for (const auto& contact : request.Contacts)
        {
            const auto normal = Normalize(contact.Normal);
            const auto target = Add(contact.Position, Multiply(normal, request.FootHeight));
            if (!SolveTwoBoneIk(
                    skeleton, working,
                    {contact.UpperLeg, contact.LowerLeg, contact.Foot, target, contact.Pole, contact.Weight}))
                return std::nullopt;
            const auto world = WorldMatrices(skeleton, working);
            const auto currentUp = Math::TransformDirection(world[contact.Foot], {0.0F, 1.0F, 0.0F});
            if (!ApplyBoneModelRotationDelta(skeleton, working, contact.Foot, FromTo(currentUp, normal),
                                             contact.Weight * contact.RotationWeight))
                return std::nullopt;
            const auto solvedWorld = WorldMatrices(skeleton, working);
            const auto solvedPosition = Math::TransformPoint(solvedWorld[contact.Foot], {});
            const auto positionError = Length(Subtract(solvedPosition, target));
            result.MaximumPositionError = std::max(result.MaximumPositionError, positionError);
            if (positionError > 0.01F)
                ++result.UnreachableFeet;
            ++result.SolvedFeet;
        }

        std::ranges::copy(working, localPose.begin());
        return result;
    }

    AssetImporterRegistration CreateRigDefinitionAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.RigDefinition";
        result.Version = 1;
        result.Type = RigDefinitionAsset::StaticType();
        result.Extensions = {".keirerig"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto decoded = RigDefinitionAsset::Decode(bytes);
            return AssetImportOutput{RigDefinitionAsset::Encode(decoded->Definition())};
        };
        return result;
    }

    AssetDecoderRegistration CreateRigDefinitionAssetDecoder()
    {
        AssetDecoderRegistration result;
        result.Type = RigDefinitionAsset::StaticType();
        result.Fallback = CreateRef<RigDefinitionAsset>();
        result.Decode = [](const std::span<const std::byte> bytes) -> Ref<Asset>
        { return RigDefinitionAsset::Decode(bytes); };
        return result;
    }
} // namespace Keire
