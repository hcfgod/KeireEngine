#include "Keire/Animation/AnimationSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
            auto rotation = Quaternion{first.Rotation.X + (second.Rotation.X - first.Rotation.X) * alpha,
                                       first.Rotation.Y + (second.Rotation.Y - first.Rotation.Y) * alpha,
                                       first.Rotation.Z + (second.Rotation.Z - first.Rotation.Z) * alpha,
                                       first.Rotation.W + (second.Rotation.W - first.Rotation.W) * alpha};
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
                if (track.Keys.empty() || track.Keys.size() > 4U * 1024U * 1024U ||
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
               m_Influences8.size() * sizeof(SkinVertexInfluence8);
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
        if (!mesh || !skeleton || influences.empty() || influences.size() > 64U * 1024U * 1024U)
            throw std::invalid_argument("Skinned mesh asset header is invalid.");
        Json encoded = Json::array();
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
            encoded.push_back({{"count", influence.Count}, {"bones", influence.Bones}, {"weights", influence.Weights}});
        }
        const auto cbor = Json::to_cbor(Json{{"schemaVersion", 2},
                                             {"mesh", mesh.ToString()},
                                             {"skeleton", skeleton.ToString()},
                                             {"skinningMethod", static_cast<std::uint8_t>(method)},
                                             {"maximumInfluences", maximumInfluences},
                                             {"influences", std::move(encoded)}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<SkinnedMeshAsset> SkinnedMeshAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        const auto schemaVersion = document.value("schemaVersion", 0);
        if (schemaVersion != 1 && schemaVersion != 2)
            throw std::invalid_argument("Skinned mesh asset schema is unsupported.");
        const auto mesh = AssetId::Parse(document.at("mesh").get<std::string>());
        const auto skeleton = AssetId::Parse(document.at("skeleton").get<std::string>());
        const auto method = schemaVersion == 2 ? static_cast<SkinningMethod>(document.value("skinningMethod", 0))
                                               : SkinningMethod::LinearBlend;
        if (method != SkinningMethod::LinearBlend && method != SkinningMethod::DualQuaternion)
            throw std::invalid_argument("Skinned mesh asset skinning method is invalid.");
        std::vector<SkinVertexInfluence8> influences;
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
        if (!mesh || !skeleton || influences.empty())
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
        auto result = CreateRef<SkinnedMeshAsset>();
        result->m_Mesh = mesh;
        result->m_Skeleton = skeleton;
        result->m_Method = method;
        result->m_Influences8 = std::move(influences);
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

    namespace
    {
        [[nodiscard]] std::string LegacyLocalId(const std::string_view category, const std::string_view seed)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const char value : seed)
            {
                hash ^= static_cast<std::uint8_t>(value);
                hash *= 1099511628211ULL;
            }
            return "legacy-" + std::string(category) + '-' + std::to_string(hash);
        }

        [[nodiscard]] std::string DecodeLocalId(const Json& value, const std::string_view key)
        {
            auto result = value.at(std::string(key)).get<std::string>();
            if (result.empty() || result.size() > 512)
                throw std::invalid_argument("Animation graph stable local ID is invalid.");
            return result;
        }

        [[nodiscard]] const AnimationParameterDefinition* FindParameterById(const AnimationGraphDefinition& definition,
                                                                            const std::string_view id) noexcept
        {
            const auto found =
                std::ranges::find(definition.ParameterDefinitions, id, &AnimationParameterDefinition::Id);
            return found == definition.ParameterDefinitions.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const AnimationLayerDefinition* FindLayer(const AnimationGraphDefinition& definition,
                                                                const std::string_view id) noexcept
        {
            const auto found = std::ranges::find(definition.Layers, id, &AnimationLayerDefinition::Id);
            return found == definition.Layers.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const AnimationStateDefinition* FindState(const AnimationLayerDefinition& layer,
                                                                const std::string_view id) noexcept
        {
            const auto found = std::ranges::find(layer.States, id, &AnimationStateDefinition::Id);
            return found == layer.States.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] AnimationGraphDefinition CanonicalizeAnimationGraph(AnimationGraphDefinition definition)
        {
            if (definition.ParameterDefinitions.empty())
            {
                definition.ParameterDefinitions.reserve(definition.Parameters.size());
                for (const auto& name : definition.Parameters)
                    definition.ParameterDefinitions.push_back(
                        {LegacyLocalId("parameter", name), name, AnimationParameterType::Float});
            }
            for (auto& parameter : definition.ParameterDefinitions)
                if (parameter.Id.empty())
                    parameter.Id = LegacyLocalId("parameter", parameter.Name);

            if (definition.Layers.empty() && !definition.States.empty())
            {
                AnimationLayerDefinition base;
                base.Id = LegacyLocalId("layer", "Base");
                base.Name = "Base";
                base.EntryStateId = definition.EntryState;
                base.States = definition.States;
                definition.Layers.push_back(std::move(base));
            }

            for (std::size_t layerIndex = 0; layerIndex < definition.Layers.size(); ++layerIndex)
            {
                auto& layer = definition.Layers[layerIndex];
                if (layer.Id.empty())
                    layer.Id = LegacyLocalId("layer", layer.Name);
                for (auto& state : layer.States)
                {
                    if (state.Id.empty())
                        state.Id = LegacyLocalId("state", layer.Id + ':' + state.Name);
                    if (!state.Motion.Clip && state.Clip)
                        state.Motion.Clip = state.Clip;
                    if (!state.Clip && state.Motion.Type == AnimationMotionType::Clip)
                        state.Clip = state.Motion.Clip;
                    const auto canonicalParameterId = [&](std::string& id)
                    {
                        const auto parameter =
                            std::ranges::find(definition.ParameterDefinitions, id, &AnimationParameterDefinition::Name);
                        if (parameter != definition.ParameterDefinitions.end())
                            id = parameter->Id;
                    };
                    canonicalParameterId(state.Motion.ParameterX);
                    canonicalParameterId(state.Motion.ParameterY);
                    for (std::size_t childIndex = 0; childIndex < state.Motion.Children.size(); ++childIndex)
                    {
                        auto& child = state.Motion.Children[childIndex];
                        if (child.Id.empty())
                            child.Id = LegacyLocalId("blend-child", state.Id + ':' + std::to_string(childIndex));
                    }
                }
                if (layer.EntryStateId.empty() && layerIndex == 0)
                    layer.EntryStateId = definition.EntryState;
                if (const auto entry =
                        std::ranges::find(layer.States, layer.EntryStateId, &AnimationStateDefinition::Name);
                    entry != layer.States.end())
                    layer.EntryStateId = entry->Id;

                for (auto& state : layer.States)
                {
                    for (std::size_t transitionIndex = 0; transitionIndex < state.Transitions.size(); ++transitionIndex)
                    {
                        auto& transition = state.Transitions[transitionIndex];
                        if (transition.Id.empty())
                            transition.Id =
                                LegacyLocalId("transition", state.Id + ':' + std::to_string(transitionIndex));
                        if (transition.DestinationId.empty())
                        {
                            const auto destination = std::ranges::find(layer.States, transition.Destination,
                                                                       &AnimationStateDefinition::Name);
                            if (destination != layer.States.end())
                                transition.DestinationId = destination->Id;
                        }
                        if (transition.Destination.empty())
                        {
                            if (const auto* destination = FindState(layer, transition.DestinationId))
                                transition.Destination = destination->Name;
                        }
                        for (auto& condition : transition.Conditions)
                        {
                            if (condition.ParameterId.empty())
                            {
                                const auto parameter =
                                    std::ranges::find(definition.ParameterDefinitions, condition.Parameter,
                                                      &AnimationParameterDefinition::Name);
                                if (parameter != definition.ParameterDefinitions.end())
                                    condition.ParameterId = parameter->Id;
                            }
                            if (condition.Parameter.empty())
                            {
                                if (const auto* parameter = FindParameterById(definition, condition.ParameterId))
                                    condition.Parameter = parameter->Name;
                            }
                        }
                    }
                }
            }

            definition.Parameters.clear();
            definition.Parameters.reserve(definition.ParameterDefinitions.size());
            for (const auto& parameter : definition.ParameterDefinitions)
                definition.Parameters.push_back(parameter.Name);
            if (!definition.Layers.empty())
            {
                definition.States = definition.Layers.front().States;
                if (const auto* entry = FindState(definition.Layers.front(), definition.Layers.front().EntryStateId))
                    definition.EntryState = entry->Name;
            }
            return definition;
        }

        [[nodiscard]] Json EncodeMotion(const AnimationMotionDefinition& motion)
        {
            Json result{{"type", static_cast<std::uint8_t>(motion.Type)}};
            if (motion.Type == AnimationMotionType::Clip)
            {
                result["clip"] = motion.Clip.ToString();
                return result;
            }
            result["parameterX"] = motion.ParameterX;
            if (motion.Type == AnimationMotionType::BlendTree2D)
                result["parameterY"] = motion.ParameterY;
            Json children = Json::array();
            for (const auto& child : motion.Children)
                children.push_back({{"id", child.Id},
                                    {"clip", child.Clip.ToString()},
                                    {"threshold", child.Threshold},
                                    {"position", Json::array({child.Position.X, child.Position.Y})},
                                    {"speed", child.Speed}});
            result["children"] = std::move(children);
            return result;
        }

        [[nodiscard]] AnimationMotionDefinition DecodeMotion(const Json& encoded)
        {
            AnimationMotionDefinition result;
            result.Type = static_cast<AnimationMotionType>(encoded.at("type").get<std::uint8_t>());
            if (result.Type == AnimationMotionType::Clip)
            {
                result.Clip = AssetId::Parse(encoded.at("clip").get<std::string>());
                return result;
            }
            result.ParameterX = encoded.at("parameterX").get<std::string>();
            result.ParameterY = encoded.value("parameterY", std::string{});
            for (const auto& encodedChild : encoded.at("children"))
            {
                AnimationBlendTreeChild child;
                child.Id = DecodeLocalId(encodedChild, "id");
                child.Clip = AssetId::Parse(encodedChild.at("clip").get<std::string>());
                child.Threshold = encodedChild.value("threshold", 0.0F);
                const auto position = encodedChild.value("position", Json::array({0.0F, 0.0F}));
                if (!position.is_array() || position.size() != 2)
                    throw std::invalid_argument("Animation blend-tree position must contain two elements.");
                child.Position = {position[0].get<float>(), position[1].get<float>()};
                child.Speed = encodedChild.value("speed", 1.0F);
                result.Children.push_back(std::move(child));
            }
            return result;
        }

        [[nodiscard]] AnimationStateDefinition DecodeStateV2(const Json& encoded)
        {
            AnimationStateDefinition state;
            state.Id = DecodeLocalId(encoded, "id");
            state.Name = encoded.at("name").get<std::string>();
            state.Speed = encoded.value("speed", 1.0F);
            state.Loop = encoded.value("loop", true);
            const auto editorPosition = encoded.value("editorPosition", Json::array({0.0F, 0.0F}));
            if (!editorPosition.is_array() || editorPosition.size() != 2)
                throw std::invalid_argument("Animation state editor position must contain two elements.");
            state.EditorPosition = {editorPosition[0].get<float>(), editorPosition[1].get<float>()};
            state.Motion = DecodeMotion(encoded.at("motion"));
            if (state.Motion.Type == AnimationMotionType::Clip)
                state.Clip = state.Motion.Clip;
            for (const auto& encodedTransition : encoded.value("transitions", Json::array()))
            {
                AnimationTransition transition;
                transition.Id = DecodeLocalId(encodedTransition, "id");
                transition.DestinationId = DecodeLocalId(encodedTransition, "destinationId");
                transition.Duration = encodedTransition.value("duration", 0.1F);
                transition.HasExitTime = encodedTransition.value("hasExitTime", false);
                transition.ExitTime = encodedTransition.value("exitTime", 1.0F);
                for (const auto& encodedCondition : encodedTransition.value("conditions", Json::array()))
                {
                    AnimationTransitionCondition condition;
                    condition.ParameterId = DecodeLocalId(encodedCondition, "parameterId");
                    condition.Comparison = static_cast<AnimationConditionComparison>(
                        encodedCondition.at("comparison").get<std::uint8_t>());
                    condition.Value = encodedCondition.value("floatValue", 0.0F);
                    condition.IntegerValue = encodedCondition.value("integerValue", 0);
                    condition.BooleanValue = encodedCondition.value("booleanValue", true);
                    transition.Conditions.push_back(std::move(condition));
                }
                state.Transitions.push_back(std::move(transition));
            }
            return state;
        }

        [[nodiscard]] Json EncodeStateV2(const AnimationStateDefinition& state)
        {
            Json transitions = Json::array();
            for (const auto& transition : state.Transitions)
            {
                Json conditions = Json::array();
                for (const auto& condition : transition.Conditions)
                    conditions.push_back({{"parameterId", condition.ParameterId},
                                          {"comparison", static_cast<std::uint8_t>(condition.Comparison)},
                                          {"floatValue", condition.Value},
                                          {"integerValue", condition.IntegerValue},
                                          {"booleanValue", condition.BooleanValue}});
                transitions.push_back({{"id", transition.Id},
                                       {"destinationId", transition.DestinationId},
                                       {"duration", transition.Duration},
                                       {"hasExitTime", transition.HasExitTime},
                                       {"exitTime", transition.ExitTime},
                                       {"conditions", std::move(conditions)}});
            }
            return {{"id", state.Id},
                    {"name", state.Name},
                    {"speed", state.Speed},
                    {"loop", state.Loop},
                    {"editorPosition", Json::array({state.EditorPosition.X, state.EditorPosition.Y})},
                    {"motion", EncodeMotion(state.Motion)},
                    {"transitions", std::move(transitions)}};
        }

        struct WeightedClip
        {
            std::string Id;
            AssetId Asset;
            Ref<const AnimationClipAsset> Clip;
            float Weight = 0.0F;
            float Speed = 1.0F;
        };

        struct MotionEvaluation
        {
            std::vector<BoneTransform> Pose;
            std::vector<AnimatorBlendWeight> Weights;
            std::vector<AnimationEvent> Events;
            float Duration = 0.0F;
            bool RootMotion = false;
        };

        [[nodiscard]] std::vector<WeightedClip>
        ResolveMotion(const AnimationStateDefinition& state,
                      const std::function<float(std::string_view)>& floatParameter,
                      const AnimatorInstance::ClipResolver& resolver)
        {
            std::vector<std::pair<const AnimationBlendTreeChild*, float>> weightedChildren;
            if (state.Motion.Type == AnimationMotionType::BlendTree1D)
            {
                std::vector<const AnimationBlendTreeChild*> sorted;
                sorted.reserve(state.Motion.Children.size());
                for (const auto& child : state.Motion.Children)
                    sorted.push_back(std::addressof(child));
                std::ranges::sort(
                    sorted, [](const auto* left, const auto* right)
                    { return std::tie(left->Threshold, left->Id) < std::tie(right->Threshold, right->Id); });
                const auto value = floatParameter(state.Motion.ParameterX);
                if (value <= sorted.front()->Threshold)
                    weightedChildren.emplace_back(sorted.front(), 1.0F);
                else if (value >= sorted.back()->Threshold)
                    weightedChildren.emplace_back(sorted.back(), 1.0F);
                else
                {
                    const auto upper = std::ranges::upper_bound(
                        sorted, value, {}, [](const AnimationBlendTreeChild* child) { return child->Threshold; });
                    const auto* second = *upper;
                    const auto* first = *(upper - 1);
                    const auto alpha = (value - first->Threshold) / (second->Threshold - first->Threshold);
                    weightedChildren.emplace_back(first, 1.0F - alpha);
                    weightedChildren.emplace_back(second, alpha);
                }
            }
            else if (state.Motion.Type == AnimationMotionType::BlendTree2D)
            {
                const Vector2 value{floatParameter(state.Motion.ParameterX), floatParameter(state.Motion.ParameterY)};
                float total = 0.0F;
                std::vector<const AnimationBlendTreeChild*> sorted;
                sorted.reserve(state.Motion.Children.size());
                for (const auto& child : state.Motion.Children)
                    sorted.push_back(std::addressof(child));
                std::ranges::sort(sorted, {}, &AnimationBlendTreeChild::Id);
                for (const auto* child : sorted)
                {
                    const auto x = value.X - child->Position.X;
                    const auto y = value.Y - child->Position.Y;
                    const auto distanceSquared = x * x + y * y;
                    if (distanceSquared <= 1.0e-12F)
                    {
                        weightedChildren = {{child, 1.0F}};
                        total = 1.0F;
                        break;
                    }
                    const auto weight = 1.0F / distanceSquared;
                    weightedChildren.emplace_back(child, weight);
                    total += weight;
                }
                if (weightedChildren.size() > 1)
                    for (auto& [child, weight] : weightedChildren)
                        weight /= total;
            }

            std::vector<WeightedClip> result;
            if (state.Motion.Type == AnimationMotionType::Clip)
            {
                auto clip = resolver(state.Motion.Clip);
                if (!clip)
                    throw std::runtime_error("Animator could not resolve an animation clip.");
                result.push_back({"clip", state.Motion.Clip, std::move(clip), 1.0F, 1.0F});
                return result;
            }
            for (const auto& [child, weight] : weightedChildren)
            {
                auto clip = resolver(child->Clip);
                if (!clip)
                    throw std::runtime_error("Animator could not resolve a blend-tree clip.");
                result.push_back({child->Id, child->Clip, std::move(clip), weight, child->Speed});
            }
            return result;
        }

        [[nodiscard]] float MotionDuration(const std::span<const WeightedClip> clips) noexcept
        {
            float result = 0.0F;
            for (const auto& clip : clips)
                result += clip.Weight * clip.Clip->Duration() / std::abs(clip.Speed);
            return result;
        }

        [[nodiscard]] float AdvanceStateTime(float time, const float delta, const float duration, const bool loop,
                                             bool& wrapped) noexcept
        {
            time += delta;
            wrapped = false;
            if (!loop)
                return std::clamp(time, 0.0F, duration);
            if (time >= duration || time < 0.0F)
            {
                time = std::fmod(time, duration);
                if (time < 0.0F)
                    time += duration;
                wrapped = true;
            }
            return time;
        }

        [[nodiscard]] std::vector<BoneTransform> SampleClipPose(const AnimationClipAsset& clip, const float time,
                                                                const std::span<const SkeletonBone> bones)
        {
            std::vector<BoneTransform> result;
            result.reserve(bones.size());
            for (const auto& bone : bones)
                result.push_back(bone.BindPose);
            for (const auto& track : clip.Tracks())
            {
                if (track.Bone >= result.size())
                    throw std::runtime_error("Animation clip track exceeds the animator skeleton.");
                result[track.Bone] = SampleTrack(track, time);
            }
            return result;
        }

        [[nodiscard]] MotionEvaluation EvaluateMotion(const std::span<const WeightedClip> clips, const float time,
                                                      const float previousTime, const bool wrapped,
                                                      const std::span<const SkeletonBone> bones)
        {
            MotionEvaluation result;
            result.Duration = MotionDuration(clips);
            result.Pose.resize(bones.size());
            std::vector<Vector4> rotations(bones.size());
            for (std::size_t bone = 0; bone < bones.size(); ++bone)
            {
                result.Pose[bone].Translation = {};
                result.Pose[bone].Scale = {};
            }
            const auto normalized = result.Duration > 0.0F ? time / result.Duration : 0.0F;
            const auto previousNormalized = result.Duration > 0.0F ? previousTime / result.Duration : 0.0F;
            for (const auto& weighted : clips)
            {
                result.RootMotion = result.RootMotion || (weighted.Weight > 0.0F && weighted.Clip->RootMotion());
                const auto clipTime =
                    std::clamp(normalized * weighted.Clip->Duration(), 0.0F, weighted.Clip->Duration());
                const auto previousClipTime =
                    std::clamp(previousNormalized * weighted.Clip->Duration(), 0.0F, weighted.Clip->Duration());
                const auto pose = SampleClipPose(*weighted.Clip, clipTime, bones);
                for (std::size_t bone = 0; bone < pose.size(); ++bone)
                {
                    result.Pose[bone].Translation.X += pose[bone].Translation.X * weighted.Weight;
                    result.Pose[bone].Translation.Y += pose[bone].Translation.Y * weighted.Weight;
                    result.Pose[bone].Translation.Z += pose[bone].Translation.Z * weighted.Weight;
                    result.Pose[bone].Scale.X += pose[bone].Scale.X * weighted.Weight;
                    result.Pose[bone].Scale.Y += pose[bone].Scale.Y * weighted.Weight;
                    result.Pose[bone].Scale.Z += pose[bone].Scale.Z * weighted.Weight;
                    rotations[bone].X += pose[bone].Rotation.X * weighted.Weight;
                    rotations[bone].Y += pose[bone].Rotation.Y * weighted.Weight;
                    rotations[bone].Z += pose[bone].Rotation.Z * weighted.Weight;
                    rotations[bone].W += pose[bone].Rotation.W * weighted.Weight;
                }
                result.Weights.push_back({weighted.Id, weighted.Asset, weighted.Weight});
                if (weighted.Weight > 0.0F)
                {
                    for (const auto& event : weighted.Clip->Events())
                    {
                        if ((!wrapped && event.Time > previousClipTime && event.Time <= clipTime) ||
                            (wrapped && (event.Time > previousClipTime || event.Time <= clipTime)))
                        {
                            if (result.Events.size() < 64)
                                result.Events.push_back(event);
                        }
                    }
                }
            }
            for (std::size_t bone = 0; bone < result.Pose.size(); ++bone)
                result.Pose[bone].Rotation =
                    Math::Normalize({rotations[bone].X, rotations[bone].Y, rotations[bone].Z, rotations[bone].W});
            return result;
        }

        [[nodiscard]] bool CompareInteger(const std::int32_t current,
                                          const AnimationTransitionCondition& condition) noexcept
        {
            switch (condition.Comparison)
            {
            case AnimationConditionComparison::Greater:
                return current > condition.IntegerValue;
            case AnimationConditionComparison::Less:
                return current < condition.IntegerValue;
            case AnimationConditionComparison::Equal:
                return current == condition.IntegerValue;
            case AnimationConditionComparison::NotEqual:
                return current != condition.IntegerValue;
            }
            return false;
        }

        [[nodiscard]] bool CompareBoolean(const bool current, const AnimationTransitionCondition& condition) noexcept
        {
            if (condition.Comparison == AnimationConditionComparison::Equal)
                return current == condition.BooleanValue;
            if (condition.Comparison == AnimationConditionComparison::NotEqual)
                return current != condition.BooleanValue;
            return false;
        }
    } // namespace

    void ValidateAnimationGraph(const AnimationGraphDefinition& source)
    {
        const auto definition = CanonicalizeAnimationGraph(source);
        if ((definition.SchemaVersion != 1 && definition.SchemaVersion != 2) ||
            definition.ParameterDefinitions.size() > 4096 || definition.Layers.size() > 64)
            throw std::invalid_argument("Animation graph header is invalid.");

        std::set<std::string, std::less<>> localIds;
        std::set<std::string, std::less<>> parameterNames;
        for (const auto& parameter : definition.ParameterDefinitions)
        {
            if (parameter.Id.empty() || parameter.Id.size() > 512 || parameter.Name.empty() ||
                parameter.Name.size() > 256 || !localIds.insert(parameter.Id).second ||
                !parameterNames.insert(parameter.Name).second || !std::isfinite(parameter.FloatDefault) ||
                parameter.Type > AnimationParameterType::Trigger)
                throw std::invalid_argument("Animation graph contains an invalid parameter.");
        }

        // Empty controllers are valid authoring assets. A layer becomes runtime-valid once it owns an entry state.
        if (definition.Layers.empty())
            return;

        std::set<std::string, std::less<>> layerNames;
        for (const auto& layer : definition.Layers)
        {
            if (layer.Id.empty() || layer.Id.size() > 512 || layer.Name.empty() || layer.Name.size() > 256 ||
                !localIds.insert(layer.Id).second || !layerNames.insert(layer.Name).second ||
                layer.Mode > AnimationLayerMode::Additive || !std::isfinite(layer.DefaultWeight) ||
                layer.DefaultWeight < 0.0F || layer.DefaultWeight > 1.0F || layer.States.empty() ||
                layer.States.size() > 4096)
                throw std::invalid_argument("Animation graph contains an invalid layer.");
            std::set<std::string, std::less<>> stateIds;
            std::set<std::string, std::less<>> stateNames;
            for (const auto& state : layer.States)
            {
                if (state.Id.empty() || state.Id.size() > 512 || state.Name.empty() || state.Name.size() > 256 ||
                    !localIds.insert(state.Id).second || !stateIds.insert(state.Id).second ||
                    !stateNames.insert(state.Name).second || !std::isfinite(state.Speed) || state.Speed == 0.0F ||
                    state.Motion.Type > AnimationMotionType::BlendTree2D || !Math::IsFinite(state.EditorPosition))
                    throw std::invalid_argument("Animation graph contains an invalid state.");
                if (state.Motion.Type == AnimationMotionType::Clip)
                {
                    if (!state.Motion.Clip || !state.Motion.Children.empty())
                        throw std::invalid_argument("Animation graph clip motion is invalid.");
                }
                else
                {
                    const auto* parameterX = FindParameterById(definition, state.Motion.ParameterX);
                    const auto* parameterY = FindParameterById(definition, state.Motion.ParameterY);
                    if (!parameterX || parameterX->Type != AnimationParameterType::Float ||
                        state.Motion.Children.size() <
                            (state.Motion.Type == AnimationMotionType::BlendTree1D ? 2U : 3U) ||
                        state.Motion.Children.size() > 256 ||
                        (state.Motion.Type == AnimationMotionType::BlendTree2D &&
                         (!parameterY || parameterY->Type != AnimationParameterType::Float ||
                          parameterY->Id == parameterX->Id)))
                        throw std::invalid_argument("Animation graph blend-tree parameters are invalid.");
                    std::set<float> thresholds;
                    std::set<std::pair<float, float>> positions;
                    for (const auto& child : state.Motion.Children)
                    {
                        if (child.Id.empty() || child.Id.size() > 512 || !localIds.insert(child.Id).second ||
                            !child.Clip || !std::isfinite(child.Threshold) || !Math::IsFinite(child.Position) ||
                            !std::isfinite(child.Speed) || child.Speed == 0.0F ||
                            (state.Motion.Type == AnimationMotionType::BlendTree1D &&
                             !thresholds.insert(child.Threshold).second) ||
                            (state.Motion.Type == AnimationMotionType::BlendTree2D &&
                             !positions.emplace(child.Position.X, child.Position.Y).second))
                            throw std::invalid_argument("Animation graph contains an invalid blend-tree child.");
                    }
                }
            }
            if (!stateIds.contains(layer.EntryStateId))
                throw std::invalid_argument("Animation graph layer entry state is unavailable.");
            for (const auto& state : layer.States)
            {
                for (const auto& transition : state.Transitions)
                {
                    if (transition.Id.empty() || transition.Id.size() > 512 || !localIds.insert(transition.Id).second ||
                        !stateIds.contains(transition.DestinationId) || transition.DestinationId == state.Id ||
                        !std::isfinite(transition.Duration) || transition.Duration < 0.0F ||
                        !std::isfinite(transition.ExitTime) || transition.ExitTime < 0.0F ||
                        transition.Conditions.size() > 64)
                        throw std::invalid_argument("Animation graph contains an invalid transition.");
                    for (const auto& condition : transition.Conditions)
                    {
                        const auto* parameter = FindParameterById(definition, condition.ParameterId);
                        if (!parameter || condition.Comparison > AnimationConditionComparison::NotEqual ||
                            !std::isfinite(condition.Value) ||
                            ((parameter->Type == AnimationParameterType::Boolean ||
                              parameter->Type == AnimationParameterType::Trigger) &&
                             condition.Comparison != AnimationConditionComparison::Equal &&
                             condition.Comparison != AnimationConditionComparison::NotEqual))
                            throw std::invalid_argument("Animation graph transition condition is invalid.");
                    }
                }
            }
        }
    }

    AnimationGraphAsset::AnimationGraphAsset(AnimationGraphDefinition definition)
        : m_Definition(CanonicalizeAnimationGraph(std::move(definition)))
    {
        if (!m_Definition.Layers.empty())
            ValidateAnimationGraph(m_Definition);
    }

    std::size_t AnimationGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& parameter : m_Definition.ParameterDefinitions)
            result += sizeof(parameter) + parameter.Id.size() + parameter.Name.size();
        for (const auto& layer : m_Definition.Layers)
        {
            result += sizeof(layer) + layer.Id.size() + layer.Name.size() + layer.EntryStateId.size();
            for (const auto& state : layer.States)
            {
                result += sizeof(state) + state.Id.size() + state.Name.size();
                for (const auto& child : state.Motion.Children)
                    result += sizeof(child) + child.Id.size();
                for (const auto& transition : state.Transitions)
                {
                    result += sizeof(transition) + transition.Id.size() + transition.DestinationId.size();
                    for (const auto& condition : transition.Conditions)
                        result += sizeof(condition) + condition.ParameterId.size();
                }
            }
        }
        return result;
    }

    std::vector<std::byte> AnimationGraphAsset::Encode(const AnimationGraphDefinition& source)
    {
        auto definition = CanonicalizeAnimationGraph(source);
        definition.SchemaVersion = 2;
        ValidateAnimationGraph(definition);
        Json parameters = Json::array();
        for (const auto& parameter : definition.ParameterDefinitions)
            parameters.push_back({{"id", parameter.Id},
                                  {"name", parameter.Name},
                                  {"type", static_cast<std::uint8_t>(parameter.Type)},
                                  {"floatDefault", parameter.FloatDefault},
                                  {"integerDefault", parameter.IntegerDefault},
                                  {"booleanDefault", parameter.BooleanDefault}});
        Json layers = Json::array();
        for (const auto& layer : definition.Layers)
        {
            Json states = Json::array();
            for (const auto& state : layer.States)
                states.push_back(EncodeStateV2(state));
            layers.push_back({{"id", layer.Id},
                              {"name", layer.Name},
                              {"mode", static_cast<std::uint8_t>(layer.Mode)},
                              {"defaultWeight", layer.DefaultWeight},
                              {"avatarMask", layer.AvatarMask ? layer.AvatarMask.ToString() : std::string{}},
                              {"entryStateId", layer.EntryStateId},
                              {"states", std::move(states)}});
        }
        const auto text =
            Json{{"schemaVersion", 2}, {"parameters", std::move(parameters)}, {"layers", std::move(layers)}}.dump(2) +
            '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<AnimationGraphAsset> AnimationGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                          reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        const auto schemaVersion = document.value("schemaVersion", 0U);
        if (schemaVersion != 1 && schemaVersion != 2)
            throw std::invalid_argument("Animation graph asset schema is unsupported.");
        AnimationGraphDefinition definition;
        definition.SchemaVersion = schemaVersion;
        if (schemaVersion == 1)
        {
            definition.EntryState = document.at("entryState").get<std::string>();
            definition.Parameters = document.value("parameters", std::vector<std::string>{});
            for (const auto& encodedState : document.at("states"))
            {
                AnimationStateDefinition state;
                state.Name = encodedState.at("name").get<std::string>();
                state.Clip = AssetId::Parse(encodedState.at("clip").get<std::string>());
                state.Speed = encodedState.value("speed", 1.0F);
                state.Loop = encodedState.value("loop", true);
                for (const auto& encodedTransition : encodedState.value("transitions", Json::array()))
                {
                    AnimationTransition transition;
                    transition.Destination = encodedTransition.at("destination").get<std::string>();
                    transition.Duration = encodedTransition.value("duration", 0.1F);
                    transition.HasExitTime = encodedTransition.value("hasExitTime", false);
                    transition.ExitTime = encodedTransition.value("exitTime", 1.0F);
                    for (const auto& encodedCondition : encodedTransition.value("conditions", Json::array()))
                        transition.Conditions.push_back({encodedCondition.at("parameter").get<std::string>(),
                                                         static_cast<AnimationConditionComparison>(
                                                             encodedCondition.at("comparison").get<std::uint8_t>()),
                                                         encodedCondition.at("value").get<float>()});
                    state.Transitions.push_back(std::move(transition));
                }
                definition.States.push_back(std::move(state));
            }
        }
        else
        {
            for (const auto& encodedParameter : document.at("parameters"))
            {
                AnimationParameterDefinition parameter;
                parameter.Id = DecodeLocalId(encodedParameter, "id");
                parameter.Name = encodedParameter.at("name").get<std::string>();
                parameter.Type = static_cast<AnimationParameterType>(encodedParameter.at("type").get<std::uint8_t>());
                parameter.FloatDefault = encodedParameter.value("floatDefault", 0.0F);
                parameter.IntegerDefault = encodedParameter.value("integerDefault", 0);
                parameter.BooleanDefault = encodedParameter.value("booleanDefault", false);
                definition.ParameterDefinitions.push_back(std::move(parameter));
            }
            for (const auto& encodedLayer : document.at("layers"))
            {
                AnimationLayerDefinition layer;
                layer.Id = DecodeLocalId(encodedLayer, "id");
                layer.Name = encodedLayer.at("name").get<std::string>();
                layer.Mode = static_cast<AnimationLayerMode>(encodedLayer.at("mode").get<std::uint8_t>());
                layer.DefaultWeight = encodedLayer.value("defaultWeight", 1.0F);
                const auto mask = encodedLayer.value("avatarMask", std::string{});
                if (!mask.empty())
                    layer.AvatarMask = AssetId::Parse(mask);
                layer.EntryStateId = DecodeLocalId(encodedLayer, "entryStateId");
                for (const auto& encodedState : encodedLayer.at("states"))
                    layer.States.push_back(DecodeStateV2(encodedState));
                definition.Layers.push_back(std::move(layer));
            }
        }
        return CreateRef<AnimationGraphAsset>(std::move(definition));
    }

    void ValidateAvatarMask(const AssetId skeleton, const std::span<const AvatarMaskBoneWeight> bones)
    {
        if (!skeleton || bones.empty() || bones.size() > 4096)
            throw std::invalid_argument("Avatar mask header is invalid.");
        std::set<std::string, std::less<>> names;
        for (const auto& bone : bones)
            if (bone.Bone.empty() || bone.Bone.size() > 256 || !names.insert(bone.Bone).second ||
                !std::isfinite(bone.Weight) || bone.Weight < 0.0F || bone.Weight > 1.0F)
                throw std::invalid_argument("Avatar mask contains an invalid bone weight.");
    }

    AvatarMaskAsset::AvatarMaskAsset(const AssetId skeleton, std::vector<AvatarMaskBoneWeight> bones)
        : m_Skeleton(skeleton), m_Bones(std::move(bones))
    {
        if (m_Skeleton || !m_Bones.empty())
            ValidateAvatarMask(m_Skeleton, m_Bones);
    }

    std::size_t AvatarMaskAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Bones.size() * sizeof(AvatarMaskBoneWeight);
        for (const auto& bone : m_Bones)
            result += bone.Bone.size();
        return result;
    }

    float AvatarMaskAsset::Weight(const std::string_view bone) const noexcept
    {
        const auto found = std::ranges::find(m_Bones, bone, &AvatarMaskBoneWeight::Bone);
        return found == m_Bones.end() ? 0.0F : found->Weight;
    }

    std::vector<std::byte> AvatarMaskAsset::Encode(const AssetId skeleton,
                                                   const std::span<const AvatarMaskBoneWeight> bones)
    {
        ValidateAvatarMask(skeleton, bones);
        Json encodedBones = Json::array();
        for (const auto& bone : bones)
            encodedBones.push_back({{"bone", bone.Bone}, {"weight", bone.Weight}});
        const auto text =
            Json{{"schemaVersion", 1}, {"skeleton", skeleton.ToString()}, {"bones", std::move(encodedBones)}}.dump(2) +
            '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<AvatarMaskAsset> AvatarMaskAsset::Decode(const std::span<const std::byte> bytes)
    {
        const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                          reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        if (document.value("schemaVersion", 0U) != 1)
            throw std::invalid_argument("Avatar mask asset schema is unsupported.");
        std::vector<AvatarMaskBoneWeight> bones;
        for (const auto& encoded : document.at("bones"))
            bones.push_back({encoded.at("bone").get<std::string>(), encoded.at("weight").get<float>()});
        return CreateRef<AvatarMaskAsset>(AssetId::Parse(document.at("skeleton").get<std::string>()), std::move(bones));
    }

    AnimatorInstance::AnimatorInstance(Ref<const SkeletonAsset> skeleton, Ref<const AnimationGraphAsset> graph,
                                       ClipResolver resolver, AvatarMaskResolver maskResolver)
        : m_Skeleton(std::move(skeleton)), m_Graph(std::move(graph)), m_Resolver(std::move(resolver)),
          m_MaskResolver(std::move(maskResolver))
    {
        if (!m_Skeleton || !m_Graph || !m_Resolver)
            throw std::invalid_argument("Animator requires a skeleton, graph, and clip resolver.");
        ValidateAnimationGraph(m_Graph->Definition());
        Reset();
    }

    void AnimatorInstance::SetFloat(std::string parameter, const float value)
    {
        const auto found = m_Parameters.find(parameter);
        if (!std::isfinite(value) || found == m_Parameters.end() || found->second.Type != AnimationParameterType::Float)
            throw std::invalid_argument("Animator float parameter is unavailable or non-finite.");
        found->second.FloatValue = value;
        PublishDebugSnapshot();
    }

    float AnimatorInstance::Float(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Float)
            throw std::invalid_argument("Animator float parameter is unavailable.");
        return found->second.FloatValue;
    }

    void AnimatorInstance::SetInteger(std::string parameter, const std::int32_t value)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Integer)
            throw std::invalid_argument("Animator integer parameter is unavailable.");
        found->second.IntegerValue = value;
        PublishDebugSnapshot();
    }

    std::int32_t AnimatorInstance::Integer(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Integer)
            throw std::invalid_argument("Animator integer parameter is unavailable.");
        return found->second.IntegerValue;
    }

    void AnimatorInstance::SetBool(std::string parameter, const bool value)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Boolean)
            throw std::invalid_argument("Animator boolean parameter is unavailable.");
        found->second.BooleanValue = value;
        PublishDebugSnapshot();
    }

    bool AnimatorInstance::Bool(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Boolean)
            throw std::invalid_argument("Animator boolean parameter is unavailable.");
        return found->second.BooleanValue;
    }

    void AnimatorInstance::SetTrigger(std::string parameter)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        found->second.BooleanValue = true;
        PublishDebugSnapshot();
    }

    void AnimatorInstance::ResetTrigger(std::string parameter)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        found->second.BooleanValue = false;
        PublishDebugSnapshot();
    }

    bool AnimatorInstance::Trigger(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        return found->second.BooleanValue;
    }

    void AnimatorInstance::SetLayerWeight(std::string layer, const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Animator layer weight is invalid.");
        const auto& definition = m_Graph->Definition();
        const auto layerDefinition = std::ranges::find_if(definition.Layers, [&](const auto& item)
                                                          { return item.Id == layer || item.Name == layer; });
        if (layerDefinition == definition.Layers.end())
            throw std::invalid_argument("Animator layer is unavailable.");
        const auto runtime = std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id);
        runtime->Weight = value;
        PublishDebugSnapshot();
    }

    float AnimatorInstance::LayerWeight(const std::string_view layer) const
    {
        const auto& definition = m_Graph->Definition();
        const auto layerDefinition = std::ranges::find_if(definition.Layers, [&](const auto& item)
                                                          { return item.Id == layer || item.Name == layer; });
        if (layerDefinition == definition.Layers.end())
            throw std::invalid_argument("Animator layer is unavailable.");
        return std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id)->Weight;
    }

    AnimatorSample AnimatorInstance::Update(const float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || deltaSeconds > 10.0F)
            throw std::invalid_argument("Animator delta time is invalid.");
        const auto& definition = m_Graph->Definition();
        const auto floatParameter = [&](const std::string_view id)
        {
            const auto* parameter = FindParameterById(definition, id);
            if (!parameter || parameter->Type != AnimationParameterType::Float)
                throw std::logic_error("Animation blend tree references an unavailable float parameter.");
            return m_Parameters.at(parameter->Name).FloatValue;
        };
        const auto conditionMatches = [&](const AnimationTransitionCondition& condition)
        {
            const auto* parameter = FindParameterById(definition, condition.ParameterId);
            if (!parameter)
                throw std::logic_error("Animation transition references an unavailable parameter.");
            const auto& value = m_Parameters.at(parameter->Name);
            switch (parameter->Type)
            {
            case AnimationParameterType::Float:
                return Compare(value.FloatValue, condition);
            case AnimationParameterType::Integer:
                return CompareInteger(value.IntegerValue, condition);
            case AnimationParameterType::Boolean:
            case AnimationParameterType::Trigger:
                return CompareBoolean(value.BooleanValue, condition);
            }
            return false;
        };

        AnimatorSample result;
        result.LocalPose.reserve(m_Skeleton->Bones().size());
        for (const auto& bone : m_Skeleton->Bones())
            result.LocalPose.push_back(bone.BindPose);
        if (m_Layers.empty())
        {
            m_State.clear();
            m_Time = 0.0F;
            m_HasPreviousRootRotation = false;
            PublishDebugSnapshot();
            return result;
        }
        bool baseWrapped = false;
        bool baseRootMotion = false;
        std::vector<AnimationEvent> events;

        for (std::size_t layerIndex = 0; layerIndex < m_Layers.size(); ++layerIndex)
        {
            auto& runtime = m_Layers[layerIndex];
            const auto* layer = FindLayer(definition, runtime.Id);
            if (!layer)
                throw std::logic_error("Animator runtime layer is unavailable.");
            auto* state = FindState(*layer, runtime.StateId);
            if (!state)
                throw std::logic_error("Animator runtime state is unavailable.");

            if (!runtime.Transition)
            {
                for (const auto& transition : state->Transitions)
                {
                    if (transition.HasExitTime && runtime.NormalizedTime < transition.ExitTime)
                        continue;
                    if (!std::ranges::all_of(transition.Conditions, conditionMatches))
                        continue;
                    runtime.Transition = RuntimeTransition{state->Id, transition.DestinationId, runtime.Time, 0.0F,
                                                           0.0F,      transition.Duration,      transition.Id};
                    runtime.StateId = transition.DestinationId;
                    for (const auto& condition : transition.Conditions)
                    {
                        const auto* parameter = FindParameterById(definition, condition.ParameterId);
                        if (parameter && parameter->Type == AnimationParameterType::Trigger)
                            m_Parameters.at(parameter->Name).BooleanValue = false;
                    }
                    break;
                }
            }

            MotionEvaluation evaluated;
            if (runtime.Transition)
            {
                auto& transition = *runtime.Transition;
                const auto* source = FindState(*layer, transition.SourceStateId);
                const auto* destination = FindState(*layer, transition.DestinationStateId);
                if (!source || !destination)
                    throw std::logic_error("Animator transition state is unavailable.");
                const auto sourceClips = ResolveMotion(*source, floatParameter, m_Resolver);
                const auto destinationClips = ResolveMotion(*destination, floatParameter, m_Resolver);
                bool sourceWrapped = false;
                bool destinationWrapped = false;
                const auto previousSourceTime = transition.SourceTime;
                const auto previousDestinationTime = transition.DestinationTime;
                transition.SourceTime = AdvanceStateTime(transition.SourceTime, deltaSeconds * source->Speed,
                                                         MotionDuration(sourceClips), source->Loop, sourceWrapped);
                transition.DestinationTime =
                    AdvanceStateTime(transition.DestinationTime, deltaSeconds * destination->Speed,
                                     MotionDuration(destinationClips), destination->Loop, destinationWrapped);
                transition.Elapsed += deltaSeconds;
                const auto sourceEvaluation = EvaluateMotion(sourceClips, transition.SourceTime, previousSourceTime,
                                                             sourceWrapped, m_Skeleton->Bones());
                evaluated = EvaluateMotion(destinationClips, transition.DestinationTime, previousDestinationTime,
                                           destinationWrapped, m_Skeleton->Bones());
                const auto alpha = transition.Duration <= 0.0F
                                       ? 1.0F
                                       : std::clamp(transition.Elapsed / transition.Duration, 0.0F, 1.0F);
                for (std::size_t bone = 0; bone < evaluated.Pose.size(); ++bone)
                    evaluated.Pose[bone] = Blend(sourceEvaluation.Pose[bone], evaluated.Pose[bone], alpha);
                evaluated.RootMotion = evaluated.RootMotion || sourceEvaluation.RootMotion;
                runtime.Time = transition.DestinationTime;
                runtime.NormalizedTime = evaluated.Duration > 0.0F ? runtime.Time / evaluated.Duration : 0.0F;
                runtime.BlendWeights = evaluated.Weights;
                if (layerIndex == 0)
                {
                    baseWrapped = destinationWrapped;
                    baseRootMotion = evaluated.RootMotion;
                }
                if (alpha >= 1.0F)
                    runtime.Transition.reset();
            }
            else
            {
                const auto clips = ResolveMotion(*state, floatParameter, m_Resolver);
                const auto duration = MotionDuration(clips);
                const auto previousTime = runtime.Time;
                bool wrapped = false;
                runtime.Time =
                    AdvanceStateTime(runtime.Time, deltaSeconds * state->Speed, duration, state->Loop, wrapped);
                evaluated = EvaluateMotion(clips, runtime.Time, previousTime, wrapped, m_Skeleton->Bones());
                runtime.NormalizedTime = duration > 0.0F ? runtime.Time / duration : 0.0F;
                runtime.BlendWeights = evaluated.Weights;
                if (layerIndex == 0)
                {
                    baseWrapped = wrapped;
                    baseRootMotion = evaluated.RootMotion;
                }
            }

            float layerWeight = runtime.Weight;
            Ref<const AvatarMaskAsset> mask;
            if (layer->AvatarMask)
            {
                if (!m_MaskResolver || !(mask = m_MaskResolver(layer->AvatarMask)))
                    throw std::runtime_error("Animator could not resolve an avatar mask.");
            }
            for (std::size_t bone = 0; bone < result.LocalPose.size(); ++bone)
            {
                const auto weight = layerWeight * (mask ? mask->Weight(m_Skeleton->Bones()[bone].Name) : 1.0F);
                if (layerIndex == 0 || layer->Mode == AnimationLayerMode::Override)
                {
                    result.LocalPose[bone] = Blend(result.LocalPose[bone], evaluated.Pose[bone], weight);
                }
                else
                {
                    const auto& bind = m_Skeleton->Bones()[bone].BindPose;
                    auto& target = result.LocalPose[bone];
                    target.Translation.X += (evaluated.Pose[bone].Translation.X - bind.Translation.X) * weight;
                    target.Translation.Y += (evaluated.Pose[bone].Translation.Y - bind.Translation.Y) * weight;
                    target.Translation.Z += (evaluated.Pose[bone].Translation.Z - bind.Translation.Z) * weight;
                    target.Scale.X += (evaluated.Pose[bone].Scale.X - bind.Scale.X) * weight;
                    target.Scale.Y += (evaluated.Pose[bone].Scale.Y - bind.Scale.Y) * weight;
                    target.Scale.Z += (evaluated.Pose[bone].Scale.Z - bind.Scale.Z) * weight;
                    target.Rotation = Math::Normalize(
                        {target.Rotation.X + (evaluated.Pose[bone].Rotation.X - bind.Rotation.X) * weight,
                         target.Rotation.Y + (evaluated.Pose[bone].Rotation.Y - bind.Rotation.Y) * weight,
                         target.Rotation.Z + (evaluated.Pose[bone].Rotation.Z - bind.Rotation.Z) * weight,
                         target.Rotation.W + (evaluated.Pose[bone].Rotation.W - bind.Rotation.W) * weight});
                }
            }
            events.insert(events.end(), evaluated.Events.begin(), evaluated.Events.end());
            if (events.size() > 64)
                events.resize(64);
        }

        const auto& baseRuntime = m_Layers.front();
        const auto& baseLayer = definition.Layers.front();
        const auto* baseState = FindState(baseLayer, baseRuntime.StateId);
        result.State = baseState ? baseState->Name : std::string{};
        result.NormalizedTime = baseRuntime.NormalizedTime;
        result.Events = events;
        m_State = result.State;
        m_Time = baseRuntime.Time;
        if (baseRootMotion && !result.LocalPose.empty())
        {
            const auto current = result.LocalPose.front();
            if (!baseWrapped)
            {
                result.RootMotion = {current.Translation.X - m_PreviousRoot.Translation.X,
                                     current.Translation.Y - m_PreviousRoot.Translation.Y,
                                     current.Translation.Z - m_PreviousRoot.Translation.Z};
                if (m_HasPreviousRootRotation)
                    result.RootRotation = RotationDelta(m_PreviousRoot.Rotation, current.Rotation);
            }
            m_PreviousRoot = current;
            m_HasPreviousRootRotation = true;
            result.LocalPose.front().Translation = {};
            result.LocalPose.front().Rotation = {};
        }
        else
        {
            m_HasPreviousRootRotation = false;
        }
        PublishDebugSnapshot(result.RootMotion, result.RootRotation, result.Events);
        return result;
    }

    bool AnimatorInstance::Reload(Ref<const AnimationGraphAsset> graph)
    {
        if (!graph || graph->Definition().Layers.empty())
            return false;
        try
        {
            ValidateAnimationGraph(graph->Definition());
        }
        catch (const std::invalid_argument&)
        {
            return false;
        }

        const auto& replacement = graph->Definition();
        bool compatible = true;
        for (const auto& [name, parameter] : m_Parameters)
        {
            (void)name;
            const auto* candidate = FindParameterById(replacement, parameter.Id);
            if (!candidate || candidate->Type != parameter.Type)
            {
                compatible = false;
                break;
            }
        }
        if (compatible)
        {
            for (const auto& runtime : m_Layers)
            {
                const auto* layer = FindLayer(replacement, runtime.Id);
                if (!layer || !FindState(*layer, runtime.StateId))
                {
                    compatible = false;
                    break;
                }
                if (runtime.Transition)
                {
                    const auto* source = FindState(*layer, runtime.Transition->SourceStateId);
                    const auto* destination = FindState(*layer, runtime.Transition->DestinationStateId);
                    if (!source || !destination)
                    {
                        compatible = false;
                        break;
                    }
                    const auto transition =
                        std::ranges::find(source->Transitions, runtime.Transition->Id, &AnimationTransition::Id);
                    if (transition == source->Transitions.end() ||
                        transition->DestinationId != runtime.Transition->DestinationStateId)
                    {
                        compatible = false;
                        break;
                    }
                }
            }
        }

        if (!compatible)
        {
            m_Graph = std::move(graph);
            Reset();
            return false;
        }

        std::map<std::string, RuntimeParameter, std::less<>> parametersById;
        for (const auto& [name, parameter] : m_Parameters)
        {
            (void)name;
            parametersById.emplace(parameter.Id, parameter);
        }
        const auto previousLayers = std::move(m_Layers);
        m_Graph = std::move(graph);
        m_Parameters.clear();
        for (const auto& parameter : replacement.ParameterDefinitions)
        {
            const auto previous = parametersById.find(parameter.Id);
            if (previous != parametersById.end())
            {
                m_Parameters.emplace(parameter.Name, previous->second);
            }
            else
            {
                m_Parameters.emplace(parameter.Name,
                                     RuntimeParameter{parameter.Type, parameter.Id, parameter.FloatDefault,
                                                      parameter.IntegerDefault, parameter.BooleanDefault});
            }
        }
        m_Layers.clear();
        m_Layers.reserve(replacement.Layers.size());
        for (const auto& layer : replacement.Layers)
        {
            const auto previous = std::ranges::find(previousLayers, layer.Id, &RuntimeLayer::Id);
            if (previous != previousLayers.end())
            {
                auto runtime = *previous;
                runtime.BlendWeights.clear();
                m_Layers.push_back(std::move(runtime));
            }
            else
            {
                m_Layers.push_back({layer.Id, layer.EntryStateId, 0.0F, layer.DefaultWeight});
            }
        }
        const auto& base = m_Layers.front();
        const auto* baseState = FindState(replacement.Layers.front(), base.StateId);
        m_State = baseState ? baseState->Name : std::string{};
        m_Time = base.Time;
        m_HasPreviousRootRotation = false;
        PublishDebugSnapshot();
        return true;
    }

    void AnimatorInstance::PublishDebugSnapshot(const Vector3 rootMotion, const Quaternion rootRotation,
                                                const std::span<const AnimationEvent> events)
    {
        auto snapshot = std::make_shared<AnimatorDebugSnapshot>();
        snapshot->Revision = ++m_DebugRevision;
        snapshot->RootMotion = rootMotion;
        snapshot->RootRotation = rootRotation;
        m_RecentEvents.insert(m_RecentEvents.end(), events.begin(), events.end());
        if (m_RecentEvents.size() > 64)
            m_RecentEvents.erase(m_RecentEvents.begin(), m_RecentEvents.end() - 64);
        snapshot->RecentEvents = m_RecentEvents;
        const auto& definition = m_Graph->Definition();
        snapshot->Parameters.reserve(definition.ParameterDefinitions.size());
        for (const auto& parameter : definition.ParameterDefinitions)
        {
            const auto& value = m_Parameters.at(parameter.Name);
            snapshot->Parameters.push_back({parameter.Id, parameter.Name, parameter.Type, value.FloatValue,
                                            value.IntegerValue, value.BooleanValue});
        }
        snapshot->Layers.reserve(m_Layers.size());
        for (const auto& runtime : m_Layers)
        {
            const auto* layer = FindLayer(definition, runtime.Id);
            const auto* state = layer ? FindState(*layer, runtime.StateId) : nullptr;
            AnimatorLayerDebugState debug;
            debug.Id = runtime.Id;
            debug.Name = layer ? layer->Name : std::string{};
            debug.StateId = runtime.StateId;
            debug.State = state ? state->Name : std::string{};
            debug.NormalizedTime = runtime.NormalizedTime;
            debug.Weight = runtime.Weight;
            debug.BlendWeights = runtime.BlendWeights;
            if (runtime.Transition)
            {
                debug.InTransition = true;
                debug.SourceStateId = runtime.Transition->SourceStateId;
                debug.DestinationStateId = runtime.Transition->DestinationStateId;
                debug.TransitionProgress =
                    runtime.Transition->Duration <= 0.0F
                        ? 1.0F
                        : std::clamp(runtime.Transition->Elapsed / runtime.Transition->Duration, 0.0F, 1.0F);
            }
            snapshot->Layers.push_back(std::move(debug));
        }
        m_DebugSnapshot = std::move(snapshot);
    }

    void AnimatorInstance::Reset()
    {
        const auto& definition = m_Graph->Definition();
        m_Parameters.clear();
        for (const auto& parameter : definition.ParameterDefinitions)
            m_Parameters.emplace(parameter.Name, RuntimeParameter{parameter.Type, parameter.Id, parameter.FloatDefault,
                                                                  parameter.IntegerDefault, parameter.BooleanDefault});
        m_Layers.clear();
        m_Layers.reserve(definition.Layers.size());
        for (const auto& layer : definition.Layers)
            m_Layers.push_back({layer.Id, layer.EntryStateId, 0.0F, layer.DefaultWeight});
        if (definition.Layers.empty())
        {
            m_State.clear();
        }
        else
        {
            const auto* entry = FindState(definition.Layers.front(), definition.Layers.front().EntryStateId);
            m_State = entry ? entry->Name : std::string{};
        }
        m_Time = 0.0F;
        m_PreviousRoot = {};
        m_HasPreviousRootRotation = false;
        m_RecentEvents.clear();
        PublishDebugSnapshot();
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
