#include "Keire/Animation/AnimationSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t PackedSkinInfluenceStride = 1U + 8U * 2U + 8U * 4U;
        constexpr std::size_t PackedSkinInfluenceBoundsStride = 4U + 2U + 6U * 4U;
        constexpr std::size_t MaximumSkinInfluenceBounds = 16ULL * 1024ULL * 1024ULL;

        static_assert(sizeof(float) == sizeof(std::uint32_t));

        void AppendUnsigned16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
        }

        void AppendUnsigned32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
        }

        [[nodiscard]] std::uint16_t ReadUnsigned16(const std::span<const std::uint8_t> bytes, std::size_t& cursor)
        {
            if (cursor + 2U > bytes.size())
                throw std::invalid_argument("Packed skinned mesh influence data is truncated.");
            const auto result = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[cursor]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[cursor + 1U]) << 8U));
            cursor += 2U;
            return result;
        }

        [[nodiscard]] std::uint32_t ReadUnsigned32(const std::span<const std::uint8_t> bytes, std::size_t& cursor)
        {
            if (cursor + 4U > bytes.size())
                throw std::invalid_argument("Packed skinned mesh influence data is truncated.");
            const auto result = static_cast<std::uint32_t>(bytes[cursor]) |
                                (static_cast<std::uint32_t>(bytes[cursor + 1U]) << 8U) |
                                (static_cast<std::uint32_t>(bytes[cursor + 2U]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[cursor + 3U]) << 24U);
            cursor += 4U;
            return result;
        }

        [[nodiscard]] float ReadPackedFloat(const std::span<const std::uint8_t> bytes, std::size_t& cursor)
        {
            return std::bit_cast<float>(ReadUnsigned32(bytes, cursor));
        }

        [[nodiscard]] Json EncodeVector(const Vector3 value) { return {value.X, value.Y, value.Z}; }
        [[nodiscard]] Json EncodeQuaternion(const Quaternion value) { return {value.X, value.Y, value.Z, value.W}; }

        [[nodiscard]] Vector3 DecodeVector(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::invalid_argument("Animation vector must contain three elements.");
            const Vector3 result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            if (!Math::IsFinite(result))
                throw std::invalid_argument("Animation vector contains a non-finite value.");
            return result;
        }

        [[nodiscard]] Quaternion DecodeQuaternion(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("Animation quaternion must contain four elements.");
            return Math::Normalize(
                {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()});
        }

        [[nodiscard]] Json EncodeTransform(const BoneTransform& value)
        {
            return {{"translation", EncodeVector(value.Translation)},
                    {"rotation", EncodeQuaternion(value.Rotation)},
                    {"scale", EncodeVector(value.Scale)}};
        }

        [[nodiscard]] BoneTransform DecodeTransform(const Json& value)
        {
            return {DecodeVector(value.at("translation")), DecodeQuaternion(value.at("rotation")),
                    DecodeVector(value.at("scale"))};
        }

        [[nodiscard]] BoneTransform Blend(const BoneTransform& first, const BoneTransform& second, const float alpha)
        {
            const auto vector = [alpha](const Vector3 left, const Vector3 right)
            {
                return Vector3{left.X + (right.X - left.X) * alpha, left.Y + (right.Y - left.Y) * alpha,
                               left.Z + (right.Z - left.Z) * alpha};
            };
            auto secondRotation = second.Rotation;
            const auto rotationDot = first.Rotation.X * secondRotation.X + first.Rotation.Y * secondRotation.Y +
                                     first.Rotation.Z * secondRotation.Z + first.Rotation.W * secondRotation.W;
            if (rotationDot < 0.0F)
                secondRotation = {-secondRotation.X, -secondRotation.Y, -secondRotation.Z, -secondRotation.W};
            auto rotation = Quaternion{first.Rotation.X + (secondRotation.X - first.Rotation.X) * alpha,
                                       first.Rotation.Y + (secondRotation.Y - first.Rotation.Y) * alpha,
                                       first.Rotation.Z + (secondRotation.Z - first.Rotation.Z) * alpha,
                                       first.Rotation.W + (secondRotation.W - first.Rotation.W) * alpha};
            return {vector(first.Translation, second.Translation), Math::Normalize(rotation),
                    vector(first.Scale, second.Scale)};
        }

        [[nodiscard]] Quaternion RotationDelta(const Quaternion previous, const Quaternion current)
        {
            const auto first = Math::Normalize(previous);
            const auto second = Math::Normalize(current);
            const Quaternion inverse{-first.X, -first.Y, -first.Z, first.W};
            auto result = Math::Normalize(
                {inverse.W * second.X + inverse.X * second.W + inverse.Y * second.Z - inverse.Z * second.Y,
                 inverse.W * second.Y - inverse.X * second.Z + inverse.Y * second.W + inverse.Z * second.X,
                 inverse.W * second.Z + inverse.X * second.Y - inverse.Y * second.X + inverse.Z * second.W,
                 inverse.W * second.W - inverse.X * second.X - inverse.Y * second.Y - inverse.Z * second.Z});
            if (result.W < 0.0F)
                result = {-result.X, -result.Y, -result.Z, -result.W};
            return result;
        }

        [[nodiscard]] BoneTransform SampleTrack(const AnimationTrack& track, const float time)
        {
            if (track.Keys.size() == 1 || time <= track.Keys.front().Time)
                return track.Keys.front().Value;
            if (time >= track.Keys.back().Time)
                return track.Keys.back().Value;
            const auto upper = std::ranges::upper_bound(track.Keys, time, {}, &AnimationKeyframe::Time);
            const auto& second = *upper;
            const auto& first = *(upper - 1);
            const auto alpha = (time - first.Time) / (second.Time - first.Time);
            return Blend(first.Value, second.Value, alpha);
        }

        [[nodiscard]] float VectorDistance(const Vector3 first, const Vector3 second) noexcept
        {
            const auto x = first.X - second.X;
            const auto y = first.Y - second.Y;
            const auto z = first.Z - second.Z;
            return std::sqrt(x * x + y * y + z * z);
        }

        [[nodiscard]] float RotationDistanceDegrees(const Quaternion first, const Quaternion second)
        {
            const auto left = Math::Normalize(first);
            const auto right = Math::Normalize(second);
            const auto dot = std::abs(left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W);
            return 2.0F * std::acos(std::clamp(dot, 0.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>;
        }

        [[nodiscard]] float CompressionErrorScore(const BoneTransform& actual, const BoneTransform& approximated,
                                                  const AnimationCompressionSettings& settings)
        {
            const auto ratio = [](const float error, const float tolerance) noexcept
            {
                if (tolerance > 0.0F)
                    return error / tolerance;
                return error > 0.0F ? std::numeric_limits<float>::infinity() : 0.0F;
            };
            return std::max(
                {ratio(VectorDistance(actual.Translation, approximated.Translation), settings.MaximumTranslationError),
                 ratio(RotationDistanceDegrees(actual.Rotation, approximated.Rotation),
                       settings.MaximumRotationErrorDegrees),
                 ratio(VectorDistance(actual.Scale, approximated.Scale), settings.MaximumScaleError)});
        }

        [[nodiscard]] AnimationTrack CompressTrack(const AnimationTrack& source,
                                                   const AnimationCompressionSettings& settings)
        {
            if (!settings.Enabled || source.Keys.size() <= 2)
                return source;

            std::vector<bool> retained(source.Keys.size());
            retained.front() = true;
            retained.back() = true;
            std::vector<std::pair<std::size_t, std::size_t>> pending{{0, source.Keys.size() - 1}};
            while (!pending.empty())
            {
                const auto [firstIndex, lastIndex] = pending.back();
                pending.pop_back();
                if (lastIndex <= firstIndex + 1)
                    continue;

                const auto& first = source.Keys[firstIndex];
                const auto& last = source.Keys[lastIndex];
                const auto span = last.Time - first.Time;
                float largestError = 1.0F;
                std::size_t largestIndex = lastIndex;
                for (auto index = firstIndex + 1; index < lastIndex; ++index)
                {
                    const auto alpha = span > 0.0F ? (source.Keys[index].Time - first.Time) / span : 0.0F;
                    const auto error = CompressionErrorScore(source.Keys[index].Value,
                                                             Blend(first.Value, last.Value, alpha), settings);
                    if (error > largestError)
                    {
                        largestError = error;
                        largestIndex = index;
                    }
                }
                if (largestIndex == lastIndex)
                    continue;
                retained[largestIndex] = true;
                pending.emplace_back(largestIndex, lastIndex);
                pending.emplace_back(firstIndex, largestIndex);
            }

            AnimationTrack result;
            result.Bone = source.Bone;
            result.Keys.reserve(source.Keys.size());
            for (std::size_t index = 0; index < source.Keys.size(); ++index)
                if (retained[index])
                    result.Keys.push_back(source.Keys[index]);
            return result;
        }

        [[nodiscard]] std::vector<AnimatorPoseBoneDebugState>
        BuildPoseDebugState(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose)
        {
            if (localPose.size() != skeleton.Bones().size())
                throw std::invalid_argument("Animator debug pose does not match its skeleton.");
            std::vector<Matrix4> world(localPose.size());
            std::vector<AnimatorPoseBoneDebugState> result;
            result.reserve(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                const auto& bone = skeleton.Bones()[index];
                const auto& transform = localPose[index];
                const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
                world[index] =
                    bone.Parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(bone.Parent)], local);
                result.push_back({bone.Name, bone.Parent, transform, Math::TransformPoint(world[index], {})});
            }
            return result;
        }

        [[nodiscard]] bool Compare(const float current, const AnimationTransitionCondition& condition) noexcept
        {
            switch (condition.Comparison)
            {
            case AnimationConditionComparison::Greater:
                return current > condition.Value;
            case AnimationConditionComparison::Less:
                return current < condition.Value;
            case AnimationConditionComparison::Equal:
                return current == condition.Value;
            case AnimationConditionComparison::NotEqual:
                return current != condition.Value;
            }
            return false;
        }

        void ValidateClip(const AssetId skeleton, const float duration, const std::span<const AnimationTrack> tracks,
                          const std::span<const AnimationEvent> events)
        {
            if (!skeleton || !std::isfinite(duration) || duration <= 0.0F || duration > 24.0F * 60.0F * 60.0F ||
                tracks.size() > 65536 || events.size() > 65536)
                throw std::invalid_argument("Animation clip header is invalid.");
            std::set<std::uint32_t> trackedBones;
            for (const auto& track : tracks)
            {
                if (track.Keys.empty() || track.Keys.size() > 4ULL * 1024ULL * 1024U ||
                    !trackedBones.insert(track.Bone).second)
                    throw std::invalid_argument("Animation clip contains an empty or duplicate bone track.");
                float previous = -1.0F;
                for (const auto& key : track.Keys)
                {
                    if (!std::isfinite(key.Time) || key.Time < 0.0F || key.Time > duration || key.Time <= previous ||
                        !Math::IsFinite(key.Value.Translation) || !Math::IsFinite(key.Value.Rotation) ||
                        !Math::IsFinite(key.Value.Scale))
                        throw std::invalid_argument("Animation clip contains an invalid or unordered key.");
                    (void)Math::Normalize(key.Value.Rotation);
                    previous = key.Time;
                }
            }
            float previous = -1.0F;
            for (const auto& event : events)
            {
                if (!std::isfinite(event.Time) || event.Time < 0.0F || event.Time > duration || event.Time < previous ||
                    event.Name.empty() || event.Name.size() > 256 || event.Payload.size() > 4096)
                    throw std::invalid_argument("Animation clip contains an invalid or unordered event.");
                previous = event.Time;
            }
        }
    } // namespace

    void ValidateSkeleton(const std::span<const SkeletonBone> bones)
    {
        if (bones.empty() || bones.size() > 4096)
            throw std::invalid_argument("Skeleton must contain 1..4096 bones.");
        std::set<std::string, std::less<>> names;
        for (std::size_t index = 0; index < bones.size(); ++index)
        {
            const auto& bone = bones[index];
            if (bone.Name.empty() || bone.Name.size() > 256 || !names.insert(bone.Name).second ||
                bone.Parent >= static_cast<std::int32_t>(index) || bone.Parent < -1 ||
                !Math::IsFinite(bone.BindPose.Translation) || !Math::IsFinite(bone.BindPose.Rotation) ||
                !Math::IsFinite(bone.BindPose.Scale) || !Math::IsFinite(bone.InverseBindPose))
                throw std::invalid_argument("Skeleton contains an invalid bone hierarchy or transform.");
            (void)Math::Normalize(bone.BindPose.Rotation);
        }
    }

    SkeletonAsset::SkeletonAsset(std::vector<SkeletonBone> bones) : m_Bones(std::move(bones))
    {
        if (!m_Bones.empty())
            ValidateSkeleton(m_Bones);
    }

    std::size_t SkeletonAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Bones.size() * sizeof(SkeletonBone);
        for (const auto& bone : m_Bones)
            result += bone.Name.size();
        return result;
    }

    std::vector<std::byte> SkeletonAsset::Encode(const std::span<const SkeletonBone> bones)
    {
        ValidateSkeleton(bones);
        Json encoded = Json::array();
        for (const auto& bone : bones)
            encoded.push_back({{"name", bone.Name},
                               {"parent", bone.Parent},
                               {"bindPose", EncodeTransform(bone.BindPose)},
                               {"inverseBindPose", bone.InverseBindPose.Elements}});
        const auto cbor = Json::to_cbor(Json{{"schemaVersion", 1}, {"bones", std::move(encoded)}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<SkeletonAsset> SkeletonAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        if (document.value("schemaVersion", 0) != 1 || !document.at("bones").is_array())
            throw std::invalid_argument("Skeleton asset schema is unsupported.");
        std::vector<SkeletonBone> bones;
        for (const auto& encoded : document.at("bones"))
        {
            SkeletonBone bone;
            bone.Name = encoded.at("name").get<std::string>();
            bone.Parent = encoded.at("parent").get<std::int32_t>();
            bone.BindPose = DecodeTransform(encoded.at("bindPose"));
            bone.InverseBindPose.Elements = encoded.at("inverseBindPose").get<std::array<float, 16>>();
            bones.push_back(std::move(bone));
        }
        return CreateRef<SkeletonAsset>(std::move(bones));
    }

    SkinnedMeshAsset::SkinnedMeshAsset(const AssetId mesh, const AssetId skeleton,
                                       std::vector<SkinVertexInfluence> influences)
        : m_Mesh(mesh), m_Skeleton(skeleton), m_Influences(std::move(influences))
    {
        if (!m_Mesh && !m_Skeleton && m_Influences.empty())
            return;
        m_Influences8.reserve(m_Influences.size());
        for (const auto& influence : m_Influences)
        {
            SkinVertexInfluence8 expanded;
            expanded.Count = 4;
            std::ranges::copy(influence.Bones, expanded.Bones.begin());
            std::ranges::copy(influence.Weights, expanded.Weights.begin());
            m_Influences8.push_back(expanded);
        }
        (void)Decode(Encode(m_Mesh, m_Skeleton, m_Influences8, m_Method));
    }

    SkinnedMeshAsset::SkinnedMeshAsset(const AssetId mesh, const AssetId skeleton,
                                       std::vector<SkinVertexInfluence8> influences, const SkinningMethod method)
        : m_Mesh(mesh), m_Skeleton(skeleton), m_Influences8(std::move(influences)), m_Method(method)
    {
        if (!m_Mesh && !m_Skeleton && m_Influences8.empty())
            return;
        (void)Decode(Encode(m_Mesh, m_Skeleton, m_Influences8, m_Method));
        m_MaximumInfluences = 0;
        m_Influences.reserve(m_Influences8.size());
        for (const auto& influence : m_Influences8)
        {
            m_MaximumInfluences = std::max(m_MaximumInfluences, influence.Count);
            SkinVertexInfluence legacy;
            float total = 0.0F;
            for (std::size_t index = 0; index < 4 && index < influence.Count; ++index)
            {
                legacy.Bones[index] = influence.Bones[index];
                legacy.Weights[index] = influence.Weights[index];
                total += legacy.Weights[index];
            }
            if (total > 0.0F)
                for (auto& weight : legacy.Weights)
                    weight /= total;
            m_Influences.push_back(legacy);
        }
    }

    std::size_t SkinnedMeshAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Influences.size() * sizeof(SkinVertexInfluence) +
               m_Influences8.size() * sizeof(SkinVertexInfluence8) +
               m_InfluenceBounds.size() * sizeof(SkinInfluenceBounds);
    }

    std::vector<std::byte> SkinnedMeshAsset::Encode(const AssetId mesh, const AssetId skeleton,
                                                    const std::span<const SkinVertexInfluence> influences)
    {
        std::vector<SkinVertexInfluence8> expanded;
        expanded.reserve(influences.size());
        for (const auto& influence : influences)
        {
            SkinVertexInfluence8 value;
            value.Count = 4;
            std::ranges::copy(influence.Bones, value.Bones.begin());
            std::ranges::copy(influence.Weights, value.Weights.begin());
            expanded.push_back(value);
        }
        return Encode(mesh, skeleton, expanded, SkinningMethod::LinearBlend);
    }

    std::vector<std::byte> SkinnedMeshAsset::Encode(const AssetId mesh, const AssetId skeleton,
                                                    const std::span<const SkinVertexInfluence8> influences,
                                                    const SkinningMethod method)
    {
        return Encode(mesh, skeleton, influences, method, 0, {});
    }

    std::vector<std::byte> SkinnedMeshAsset::Encode(const AssetId mesh, const AssetId skeleton,
                                                    const std::span<const SkinVertexInfluence8> influences,
                                                    const SkinningMethod method, const std::uint32_t submeshCount,
                                                    const std::span<const SkinInfluenceBounds> influenceBounds)
    {
        if (!mesh || !skeleton || influences.empty() || influences.size() > 64ULL * 1024ULL * 1024U)
            throw std::invalid_argument("Skinned mesh asset header is invalid.");
        std::vector<std::uint8_t> encoded;
        encoded.reserve(influences.size() * PackedSkinInfluenceStride);
        std::uint8_t maximumInfluences = 0;
        for (const auto& influence : influences)
        {
            if (influence.Count == 0 || influence.Count > 8)
                throw std::invalid_argument("Skinned mesh influence count is invalid.");
            float sum = 0.0F;
            for (std::size_t index = 0; index < influence.Count; ++index)
            {
                const auto weight = influence.Weights[index];
                if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
                    throw std::invalid_argument("Skinned mesh contains an invalid influence weight.");
                sum += weight;
            }
            if (std::abs(sum - 1.0F) > 0.001F)
                throw std::invalid_argument("Skinned mesh influence weights must be normalized.");
            maximumInfluences = std::max(maximumInfluences, influence.Count);
            encoded.push_back(influence.Count);
            for (std::size_t index = 0; index < influence.Bones.size(); ++index)
                AppendUnsigned16(encoded, index < influence.Count ? influence.Bones[index] : std::uint16_t{});
            for (std::size_t index = 0; index < influence.Weights.size(); ++index)
                AppendUnsigned32(
                    encoded, std::bit_cast<std::uint32_t>(index < influence.Count ? influence.Weights[index] : 0.0F));
        }

        const bool boundsComplete = submeshCount != 0;
        if (boundsComplete != !influenceBounds.empty() || influenceBounds.size() > MaximumSkinInfluenceBounds ||
            (boundsComplete && submeshCount > influenceBounds.size()))
            throw std::invalid_argument("Skinned mesh influence-bound completeness metadata is invalid.");
        std::vector<std::uint8_t> encodedBounds;
        encodedBounds.reserve(influenceBounds.size() * PackedSkinInfluenceBoundsStride);
        std::set<std::uint16_t> referencedBones;
        for (const auto& influence : influences)
            for (std::size_t index = 0; index < influence.Count; ++index)
                if (influence.Weights[index] > 0.0F)
                    referencedBones.insert(influence.Bones[index]);
        std::optional<std::pair<std::uint32_t, std::uint16_t>> previousBound;
        std::vector<bool> coveredSubmeshes(submeshCount);
        for (const auto& bounds : influenceBounds)
        {
            const auto key = std::pair{bounds.Submesh, bounds.Bone};
            if ((previousBound && *previousBound >= key) || bounds.Submesh >= submeshCount ||
                !referencedBones.contains(bounds.Bone) || !Math::IsFinite(bounds.Minimum) ||
                !Math::IsFinite(bounds.Maximum) || bounds.Minimum.X > bounds.Maximum.X ||
                bounds.Minimum.Y > bounds.Maximum.Y || bounds.Minimum.Z > bounds.Maximum.Z)
                throw std::invalid_argument("Skinned mesh contains an invalid influence bound.");
            previousBound = key;
            coveredSubmeshes[bounds.Submesh] = true;
            AppendUnsigned32(encodedBounds, bounds.Submesh);
            AppendUnsigned16(encodedBounds, bounds.Bone);
            for (const auto value : {bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z, bounds.Maximum.X,
                                     bounds.Maximum.Y, bounds.Maximum.Z})
                AppendUnsigned32(encodedBounds, std::bit_cast<std::uint32_t>(value));
        }
        if (boundsComplete && !std::ranges::all_of(coveredSubmeshes, [](const bool covered) { return covered; }))
            throw std::invalid_argument("Skinned mesh influence bounds do not cover every submesh.");

        const auto cbor = Json::to_cbor(Json{{"schemaVersion", 4},
                                             {"mesh", mesh.ToString()},
                                             {"skeleton", skeleton.ToString()},
                                             {"skinningMethod", static_cast<std::uint8_t>(method)},
                                             {"maximumInfluences", maximumInfluences},
                                             {"vertexCount", influences.size()},
                                             {"influenceStride", PackedSkinInfluenceStride},
                                             {"influences", Json::binary(std::move(encoded))},
                                             {"influenceBoundsComplete", boundsComplete},
                                             {"influenceBoundsSubmeshCount", submeshCount},
                                             {"influenceBoundsCount", influenceBounds.size()},
                                             {"influenceBoundsStride", PackedSkinInfluenceBoundsStride},
                                             {"influenceBounds", Json::binary(std::move(encodedBounds))}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<SkinnedMeshAsset> SkinnedMeshAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        const auto schemaVersion = document.value("schemaVersion", 0);
        if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3 && schemaVersion != 4)
            throw std::invalid_argument("Skinned mesh asset schema is unsupported.");
        const auto mesh = AssetId::Parse(document.at("mesh").get<std::string>());
        const auto skeleton = AssetId::Parse(document.at("skeleton").get<std::string>());
        const auto method = schemaVersion >= 2 ? static_cast<SkinningMethod>(document.value("skinningMethod", 0))
                                               : SkinningMethod::LinearBlend;
        if (method != SkinningMethod::LinearBlend && method != SkinningMethod::DualQuaternion)
            throw std::invalid_argument("Skinned mesh asset skinning method is invalid.");
        std::vector<SkinVertexInfluence8> influences;
        if (schemaVersion >= 3)
        {
            const auto vertexCount = document.value("vertexCount", std::size_t{0});
            const auto influenceStride = document.value("influenceStride", std::size_t{0});
            const auto& encoded = document.at("influences");
            if (vertexCount == 0 || vertexCount > 64ULL * 1024ULL * 1024U ||
                influenceStride != PackedSkinInfluenceStride || !encoded.is_binary())
                throw std::invalid_argument("Packed skinned mesh asset header is invalid.");
            const auto& packed = encoded.get_binary();
            if (packed.size() != vertexCount * PackedSkinInfluenceStride)
                throw std::invalid_argument("Packed skinned mesh influence data has an invalid size.");
            const std::span<const std::uint8_t> packedBytes{packed.data(), packed.size()};
            influences.resize(vertexCount);
            std::size_t cursor = 0;
            for (auto& influence : influences)
            {
                influence.Count = packedBytes[cursor++];
                for (auto& bone : influence.Bones)
                    bone = ReadUnsigned16(packedBytes, cursor);
                for (auto& weight : influence.Weights)
                    weight = ReadPackedFloat(packedBytes, cursor);
            }
        }
        else
        {
            for (const auto& encoded : document.at("influences"))
            {
                SkinVertexInfluence8 influence;
                if (schemaVersion == 1)
                {
                    influence.Count = 4;
                    const auto bones = encoded.at("bones").get<std::array<std::uint16_t, 4>>();
                    const auto weights = encoded.at("weights").get<std::array<float, 4>>();
                    std::ranges::copy(bones, influence.Bones.begin());
                    std::ranges::copy(weights, influence.Weights.begin());
                }
                else
                {
                    influence.Count = encoded.at("count").get<std::uint8_t>();
                    influence.Bones = encoded.at("bones").get<std::array<std::uint16_t, 8>>();
                    influence.Weights = encoded.at("weights").get<std::array<float, 8>>();
                }
                influences.push_back(influence);
            }
        }
        if (!mesh || !skeleton || influences.empty() || influences.size() > 64ULL * 1024ULL * 1024U)
            throw std::invalid_argument("Skinned mesh asset header is invalid.");
        for (const auto& influence : influences)
        {
            if (influence.Count == 0 || influence.Count > 8)
                throw std::invalid_argument("Skinned mesh influence count is invalid.");
            float sum = 0.0F;
            for (std::size_t index = 0; index < influence.Count; ++index)
            {
                const auto weight = influence.Weights[index];
                if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
                    throw std::invalid_argument("Skinned mesh contains an invalid influence weight.");
                sum += weight;
            }
            if (std::abs(sum - 1.0F) > 0.001F)
                throw std::invalid_argument("Skinned mesh influence weights must be normalized.");
        }

        bool influenceBoundsComplete = false;
        std::uint32_t influenceBoundsSubmeshCount = 0;
        std::vector<SkinInfluenceBounds> influenceBounds;
        if (schemaVersion == 4)
        {
            if (!document.at("influenceBoundsComplete").is_boolean())
                throw std::invalid_argument("Packed skinned mesh influence-bound completeness is invalid.");
            influenceBoundsComplete = document.at("influenceBoundsComplete").get<bool>();
            influenceBoundsSubmeshCount = document.at("influenceBoundsSubmeshCount").get<std::uint32_t>();
            const auto influenceBoundsCount = document.at("influenceBoundsCount").get<std::size_t>();
            const auto influenceBoundsStride = document.at("influenceBoundsStride").get<std::size_t>();
            const auto& encodedBounds = document.at("influenceBounds");
            if (influenceBoundsCount > MaximumSkinInfluenceBounds ||
                influenceBoundsStride != PackedSkinInfluenceBoundsStride || !encodedBounds.is_binary())
                throw std::invalid_argument("Packed skinned mesh influence-bound header is invalid.");
            const auto& packedBounds = encodedBounds.get_binary();
            if (packedBounds.size() != influenceBoundsCount * PackedSkinInfluenceBoundsStride)
                throw std::invalid_argument("Packed skinned mesh influence-bound data has an invalid size.");
            if (influenceBoundsComplete != (influenceBoundsSubmeshCount != 0 && influenceBoundsCount != 0) ||
                (!influenceBoundsComplete && (influenceBoundsSubmeshCount != 0 || influenceBoundsCount != 0)) ||
                (influenceBoundsComplete && influenceBoundsSubmeshCount > influenceBoundsCount))
                throw std::invalid_argument("Packed skinned mesh influence-bound completeness is inconsistent.");

            std::set<std::uint16_t> referencedBones;
            for (const auto& influence : influences)
                for (std::size_t index = 0; index < influence.Count; ++index)
                    if (influence.Weights[index] > 0.0F)
                        referencedBones.insert(influence.Bones[index]);
            std::vector<bool> coveredSubmeshes(influenceBoundsSubmeshCount);
            std::optional<std::pair<std::uint32_t, std::uint16_t>> previousBound;
            const std::span<const std::uint8_t> packedBoundsBytes{packedBounds.data(), packedBounds.size()};
            std::size_t boundsCursor = 0;
            influenceBounds.reserve(influenceBoundsCount);
            for (std::size_t index = 0; index < influenceBoundsCount; ++index)
            {
                SkinInfluenceBounds bounds;
                bounds.Submesh = ReadUnsigned32(packedBoundsBytes, boundsCursor);
                bounds.Bone = ReadUnsigned16(packedBoundsBytes, boundsCursor);
                bounds.Minimum = {ReadPackedFloat(packedBoundsBytes, boundsCursor),
                                  ReadPackedFloat(packedBoundsBytes, boundsCursor),
                                  ReadPackedFloat(packedBoundsBytes, boundsCursor)};
                bounds.Maximum = {ReadPackedFloat(packedBoundsBytes, boundsCursor),
                                  ReadPackedFloat(packedBoundsBytes, boundsCursor),
                                  ReadPackedFloat(packedBoundsBytes, boundsCursor)};
                const auto key = std::pair{bounds.Submesh, bounds.Bone};
                if ((previousBound && *previousBound >= key) || bounds.Submesh >= influenceBoundsSubmeshCount ||
                    !referencedBones.contains(bounds.Bone) || !Math::IsFinite(bounds.Minimum) ||
                    !Math::IsFinite(bounds.Maximum) || bounds.Minimum.X > bounds.Maximum.X ||
                    bounds.Minimum.Y > bounds.Maximum.Y || bounds.Minimum.Z > bounds.Maximum.Z)
                    throw std::invalid_argument("Packed skinned mesh contains an invalid influence bound.");
                previousBound = key;
                coveredSubmeshes[bounds.Submesh] = true;
                influenceBounds.push_back(bounds);
            }
            if (influenceBoundsComplete &&
                !std::ranges::all_of(coveredSubmeshes, [](const bool covered) { return covered; }))
                throw std::invalid_argument("Packed skinned mesh influence bounds do not cover every submesh.");
        }
        auto result = CreateRef<SkinnedMeshAsset>();
        result->m_Mesh = mesh;
        result->m_Skeleton = skeleton;
        result->m_Method = method;
        result->m_Influences8 = std::move(influences);
        result->m_InfluenceBoundsComplete = influenceBoundsComplete;
        result->m_InfluenceBoundsSubmeshCount = influenceBoundsSubmeshCount;
        result->m_InfluenceBounds = std::move(influenceBounds);
        result->m_MaximumInfluences = 0;
        result->m_Influences.reserve(result->m_Influences8.size());
        for (const auto& influence : result->m_Influences8)
        {
            result->m_MaximumInfluences = std::max(result->m_MaximumInfluences, influence.Count);
            SkinVertexInfluence legacy;
            float total = 0.0F;
            for (std::size_t index = 0; index < 4 && index < influence.Count; ++index)
            {
                legacy.Bones[index] = influence.Bones[index];
                legacy.Weights[index] = influence.Weights[index];
                total += legacy.Weights[index];
            }
            if (total > 0.0F)
                for (auto& weight : legacy.Weights)
                    weight /= total;
            result->m_Influences.push_back(legacy);
        }
        return result;
    }

    AnimationClipAsset::AnimationClipAsset(const AssetId skeleton, const float duration,
                                           std::vector<AnimationTrack> tracks, std::vector<AnimationEvent> events,
                                           const bool rootMotion)
        : m_Skeleton(skeleton), m_Duration(duration), m_Tracks(std::move(tracks)), m_Events(std::move(events)),
          m_RootMotion(rootMotion)
    {
        if (m_Skeleton)
            ValidateClip(m_Skeleton, m_Duration, m_Tracks, m_Events);
    }

    AnimationCompressionSettings AnimationCompressionSettingsForPreset(const AnimationCompressionPreset preset) noexcept
    {
        switch (preset)
        {
        case AnimationCompressionPreset::Disabled:
            return {false, 0.0F, 0.0F, 0.0F};
        case AnimationCompressionPreset::Light:
            return {true, 0.0001F, 0.05F, 0.0001F};
        case AnimationCompressionPreset::Balanced:
            return {true, 0.001F, 0.25F, 0.001F};
        case AnimationCompressionPreset::Aggressive:
            return {true, 0.01F, 1.0F, 0.01F};
        }
        return {false, 0.0F, 0.0F, 0.0F};
    }

    AnimationCompressionResult CompressAnimationTracks(const std::span<const AnimationTrack> tracks,
                                                       const AnimationCompressionSettings& settings)
    {
        if (!std::isfinite(settings.MaximumTranslationError) || settings.MaximumTranslationError < 0.0F ||
            !std::isfinite(settings.MaximumRotationErrorDegrees) || settings.MaximumRotationErrorDegrees < 0.0F ||
            settings.MaximumRotationErrorDegrees > 180.0F || !std::isfinite(settings.MaximumScaleError) ||
            settings.MaximumScaleError < 0.0F)
            throw std::invalid_argument("Animation compression settings are invalid.");
        if (tracks.size() > 65536)
            throw std::invalid_argument("Animation compression track count exceeds the supported limit.");

        AnimationCompressionResult result;
        result.Tracks.reserve(tracks.size());
        std::set<std::uint32_t> bones;
        for (const auto& source : tracks)
        {
            if (source.Keys.empty() || source.Keys.size() > 4ULL * 1024ULL * 1024U || !bones.insert(source.Bone).second)
                throw std::invalid_argument("Animation compression received an empty, oversized, or duplicate track.");
            float previousTime = -1.0F;
            for (const auto& key : source.Keys)
            {
                if (!std::isfinite(key.Time) || key.Time < 0.0F || key.Time <= previousTime ||
                    !Math::IsFinite(key.Value.Translation) || !Math::IsFinite(key.Value.Rotation) ||
                    !Math::IsFinite(key.Value.Scale))
                    throw std::invalid_argument("Animation compression received an invalid or unordered key.");
                (void)Math::Normalize(key.Value.Rotation);
                previousTime = key.Time;
            }
            result.Statistics.SourceKeyCount += source.Keys.size();
            auto compressed = CompressTrack(source, settings);
            result.Statistics.CompressedKeyCount += compressed.Keys.size();
            for (const auto& key : source.Keys)
            {
                const auto sampled = SampleTrack(compressed, key.Time);
                result.Statistics.MaximumTranslationError =
                    std::max(result.Statistics.MaximumTranslationError,
                             VectorDistance(key.Value.Translation, sampled.Translation));
                result.Statistics.MaximumRotationErrorDegrees =
                    std::max(result.Statistics.MaximumRotationErrorDegrees,
                             RotationDistanceDegrees(key.Value.Rotation, sampled.Rotation));
                result.Statistics.MaximumScaleError =
                    std::max(result.Statistics.MaximumScaleError, VectorDistance(key.Value.Scale, sampled.Scale));
            }
            result.Tracks.push_back(std::move(compressed));
        }
        return result;
    }

    std::size_t AnimationClipAsset::ResidentBytes() const noexcept
    {
        std::size_t result =
            sizeof(*this) + m_Tracks.size() * sizeof(AnimationTrack) + m_Events.size() * sizeof(AnimationEvent);
        for (const auto& track : m_Tracks)
            result += track.Keys.size() * sizeof(AnimationKeyframe);
        for (const auto& event : m_Events)
            result += event.Name.size() + event.Payload.size();
        return result;
    }

    std::vector<std::byte> AnimationClipAsset::Encode(const AssetId skeleton, const float duration,
                                                      const std::span<const AnimationTrack> tracks,
                                                      const std::span<const AnimationEvent> events,
                                                      const bool rootMotion)
    {
        ValidateClip(skeleton, duration, tracks, events);
        Json encodedTracks = Json::array();
        for (const auto& track : tracks)
        {
            Json keys = Json::array();
            for (const auto& key : track.Keys)
                keys.push_back({{"time", key.Time}, {"value", EncodeTransform(key.Value)}});
            encodedTracks.push_back({{"bone", track.Bone}, {"keys", std::move(keys)}});
        }
        Json encodedEvents = Json::array();
        for (const auto& event : events)
            encodedEvents.push_back({{"time", event.Time}, {"name", event.Name}, {"payload", event.Payload}});
        const auto cbor = Json::to_cbor(Json{{"schemaVersion", 1},
                                             {"skeleton", skeleton.ToString()},
                                             {"duration", duration},
                                             {"rootMotion", rootMotion},
                                             {"tracks", std::move(encodedTracks)},
                                             {"events", std::move(encodedEvents)}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<AnimationClipAsset> AnimationClipAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        if (document.value("schemaVersion", 0) != 1)
            throw std::invalid_argument("Animation clip asset schema is unsupported.");
        std::vector<AnimationTrack> tracks;
        for (const auto& encodedTrack : document.at("tracks"))
        {
            AnimationTrack track;
            track.Bone = encodedTrack.at("bone").get<std::uint32_t>();
            for (const auto& encodedKey : encodedTrack.at("keys"))
                track.Keys.push_back({encodedKey.at("time").get<float>(), DecodeTransform(encodedKey.at("value"))});
            tracks.push_back(std::move(track));
        }
        std::vector<AnimationEvent> events;
        for (const auto& event : document.at("events"))
            events.push_back({event.at("time").get<float>(), event.at("name").get<std::string>(),
                              event.value("payload", std::string{})});
        return CreateRef<AnimationClipAsset>(AssetId::Parse(document.at("skeleton").get<std::string>()),
                                             document.at("duration").get<float>(), std::move(tracks), std::move(events),
                                             document.value("rootMotion", false));
    }

    AnimationSourceAsset::AnimationSourceAsset(AnimationSourceDefinition definition)
        : m_Definition(std::move(definition))
    {
        if (m_Definition.SchemaVersion != 1)
            throw std::invalid_argument("Animation source asset schema is unsupported.");
        if (m_Definition.Takes.empty() && (m_Definition.Skeleton || m_Definition.Rig))
            throw std::invalid_argument("Animation source asset requires at least one animation take.");
        for (const auto& take : m_Definition.Takes)
        {
            if (!take.Clip || take.Name.empty() || !std::isfinite(take.Duration) || take.Duration <= 0.0F)
                throw std::invalid_argument("Animation source asset contains an invalid animation take.");
        }
    }

    std::size_t AnimationSourceAsset::ResidentBytes() const noexcept
    {
        std::size_t result =
            sizeof(AnimationSourceAsset) + m_Definition.Takes.capacity() * sizeof(AnimationTakeDescriptor);
        for (const auto& take : m_Definition.Takes)
            result += take.Name.capacity();
        return result;
    }

    std::vector<std::byte> AnimationSourceAsset::Encode(const AnimationSourceDefinition& definition)
    {
        const AnimationSourceAsset validated(definition);
        Json takes = Json::array();
        for (const auto& take : validated.Definition().Takes)
            takes.push_back({{"clip", take.Clip.ToString()}, {"name", take.Name}, {"duration", take.Duration}});
        const auto cbor = Json::to_cbor({{"schemaVersion", 1},
                                         {"skeleton", validated.Definition().Skeleton.ToString()},
                                         {"rig", validated.Definition().Rig.ToString()},
                                         {"takes", std::move(takes)}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<AnimationSourceAsset> AnimationSourceAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        AnimationSourceDefinition definition;
        definition.SchemaVersion = document.value("schemaVersion", 0U);
        definition.Skeleton = AssetId::Parse(document.at("skeleton").get<std::string>());
        definition.Rig = AssetId::Parse(document.at("rig").get<std::string>());
        for (const auto& take : document.at("takes"))
            definition.Takes.push_back({AssetId::Parse(take.at("clip").get<std::string>()),
                                        take.at("name").get<std::string>(), take.at("duration").get<float>()});
        return CreateRef<AnimationSourceAsset>(std::move(definition));
    }

    AssetDecoderRegistration CreateSkeletonAssetDecoder()
    {
        return {SkeletonAsset::StaticType(), CreateRef<SkeletonAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return SkeletonAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateSkinnedMeshAssetDecoder()
    {
        return {SkinnedMeshAsset::StaticType(), CreateRef<SkinnedMeshAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return SkinnedMeshAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateAnimationClipAssetDecoder()
    {
        return {AnimationClipAsset::StaticType(), CreateRef<AnimationClipAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AnimationClipAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateAnimationSourceAssetDecoder()
    {
        return {AnimationSourceAsset::StaticType(), CreateRef<AnimationSourceAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return AnimationSourceAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateAnimationGraphAssetDecoder()
    {
        return {AnimationGraphAsset::StaticType(), CreateRef<AnimationGraphAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return AnimationGraphAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateAvatarMaskAssetDecoder()
    {
        return {AvatarMaskAsset::StaticType(), CreateRef<AvatarMaskAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return AvatarMaskAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateAnimationClipAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AnimationClip";
        result.Version = 1;
        result.Type = AnimationClipAsset::StaticType();
        result.Extensions = {".keireanim"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            const auto clip = AnimationClipAsset::Decode(bytes);
            output.Bytes.assign(bytes.begin(), bytes.end());
            output.AssetDependencies.push_back(clip->Skeleton());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateAnimationGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AnimationGraph";
        result.Version = 2;
        result.Type = AnimationGraphAsset::StaticType();
        result.Extensions = {".keireanimgraph"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            const auto graph = AnimationGraphAsset::Decode(bytes);
            output.Bytes.assign(bytes.begin(), bytes.end());
            for (const auto& layer : graph->Definition().Layers)
            {
                if (layer.AvatarMask)
                    output.AssetDependencies.push_back(layer.AvatarMask);
                for (const auto& state : layer.States)
                {
                    if (state.Motion.Type == AnimationMotionType::Clip)
                        output.AssetDependencies.push_back(state.Motion.Clip);
                    else
                        for (const auto& child : state.Motion.Children)
                            output.AssetDependencies.push_back(child.Clip);
                }
            }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateAvatarMaskAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AvatarMask";
        result.Version = 1;
        result.Type = AvatarMaskAsset::StaticType();
        result.Extensions = {".keireavatarmask"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            const auto mask = AvatarMaskAsset::Decode(bytes);
            output.Bytes.assign(bytes.begin(), bytes.end());
            output.AssetDependencies.push_back(mask->Skeleton());
            return output;
        };
        return result;
    }
} // namespace Keire
