#include "Keire/Animation/AnimationSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <set>
#include <stdexcept>
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
        (void)Decode(Encode(m_Mesh, m_Skeleton, m_Influences));
    }

    std::size_t SkinnedMeshAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Influences.size() * sizeof(SkinVertexInfluence);
    }

    std::vector<std::byte> SkinnedMeshAsset::Encode(const AssetId mesh, const AssetId skeleton,
                                                    const std::span<const SkinVertexInfluence> influences)
    {
        if (!mesh || !skeleton || influences.empty() || influences.size() > 64U * 1024U * 1024U)
            throw std::invalid_argument("Skinned mesh asset header is invalid.");
        Json encoded = Json::array();
        for (const auto& influence : influences)
        {
            float sum = 0.0F;
            for (const auto weight : influence.Weights)
            {
                if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
                    throw std::invalid_argument("Skinned mesh contains an invalid influence weight.");
                sum += weight;
            }
            if (std::abs(sum - 1.0F) > 0.001F)
                throw std::invalid_argument("Skinned mesh influence weights must be normalized.");
            encoded.push_back({{"bones", influence.Bones}, {"weights", influence.Weights}});
        }
        const auto cbor = Json::to_cbor(Json{{"schemaVersion", 1},
                                             {"mesh", mesh.ToString()},
                                             {"skeleton", skeleton.ToString()},
                                             {"influences", std::move(encoded)}});
        return {reinterpret_cast<const std::byte*>(cbor.data()),
                reinterpret_cast<const std::byte*>(cbor.data() + cbor.size())};
    }

    Ref<SkinnedMeshAsset> SkinnedMeshAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));
        if (document.value("schemaVersion", 0) != 1)
            throw std::invalid_argument("Skinned mesh asset schema is unsupported.");
        const auto mesh = AssetId::Parse(document.at("mesh").get<std::string>());
        const auto skeleton = AssetId::Parse(document.at("skeleton").get<std::string>());
        std::vector<SkinVertexInfluence> influences;
        for (const auto& encoded : document.at("influences"))
            influences.push_back({encoded.at("bones").get<std::array<std::uint16_t, 4>>(),
                                  encoded.at("weights").get<std::array<float, 4>>()});
        if (!mesh || !skeleton || influences.empty())
            throw std::invalid_argument("Skinned mesh asset header is invalid.");
        for (const auto& influence : influences)
        {
            float sum = 0.0F;
            for (const auto weight : influence.Weights)
                if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
                    throw std::invalid_argument("Skinned mesh contains an invalid influence weight.");
                else
                    sum += weight;
            if (std::abs(sum - 1.0F) > 0.001F)
                throw std::invalid_argument("Skinned mesh influence weights must be normalized.");
        }
        auto result = CreateRef<SkinnedMeshAsset>();
        result->m_Mesh = mesh;
        result->m_Skeleton = skeleton;
        result->m_Influences = std::move(influences);
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

    void ValidateAnimationGraph(const AnimationGraphDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || definition.EntryState.empty() || definition.States.empty() ||
            definition.States.size() > 4096 || definition.Parameters.size() > 4096)
            throw std::invalid_argument("Animation graph header is invalid.");
        std::set<std::string, std::less<>> parameters;
        for (const auto& parameter : definition.Parameters)
            if (parameter.empty() || parameter.size() > 256 || !parameters.insert(parameter).second)
                throw std::invalid_argument("Animation graph contains an invalid or duplicate parameter.");
        std::set<std::string, std::less<>> states;
        for (const auto& state : definition.States)
            if (state.Name.empty() || state.Name.size() > 256 || !state.Clip || !std::isfinite(state.Speed) ||
                state.Speed == 0.0F || !states.insert(state.Name).second)
                throw std::invalid_argument("Animation graph contains an invalid or duplicate state.");
        if (!states.contains(definition.EntryState))
            throw std::invalid_argument("Animation graph entry state is unavailable.");
        for (const auto& state : definition.States)
            for (const auto& transition : state.Transitions)
            {
                if (!states.contains(transition.Destination) || transition.Destination == state.Name ||
                    !std::isfinite(transition.Duration) || transition.Duration < 0.0F ||
                    !std::isfinite(transition.ExitTime) || transition.ExitTime < 0.0F)
                    throw std::invalid_argument("Animation graph contains an invalid transition.");
                for (const auto& condition : transition.Conditions)
                    if (!parameters.contains(condition.Parameter) || !std::isfinite(condition.Value))
                        throw std::invalid_argument("Animation graph transition condition is invalid.");
            }
    }

    AnimationGraphAsset::AnimationGraphAsset(AnimationGraphDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.States.empty())
            ValidateAnimationGraph(m_Definition);
    }

    std::size_t AnimationGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& parameter : m_Definition.Parameters)
            result += parameter.size();
        for (const auto& state : m_Definition.States)
        {
            result += state.Name.size() + sizeof(state);
            for (const auto& transition : state.Transitions)
            {
                result += transition.Destination.size() + sizeof(transition);
                for (const auto& condition : transition.Conditions)
                    result += condition.Parameter.size() + sizeof(condition);
            }
        }
        return result;
    }

    std::vector<std::byte> AnimationGraphAsset::Encode(const AnimationGraphDefinition& definition)
    {
        ValidateAnimationGraph(definition);
        Json states = Json::array();
        for (const auto& state : definition.States)
        {
            Json transitions = Json::array();
            for (const auto& transition : state.Transitions)
            {
                Json conditions = Json::array();
                for (const auto& condition : transition.Conditions)
                    conditions.push_back({{"parameter", condition.Parameter},
                                          {"comparison", static_cast<std::uint8_t>(condition.Comparison)},
                                          {"value", condition.Value}});
                transitions.push_back({{"destination", transition.Destination},
                                       {"duration", transition.Duration},
                                       {"hasExitTime", transition.HasExitTime},
                                       {"exitTime", transition.ExitTime},
                                       {"conditions", std::move(conditions)}});
            }
            states.push_back({{"name", state.Name},
                              {"clip", state.Clip.ToString()},
                              {"speed", state.Speed},
                              {"loop", state.Loop},
                              {"transitions", std::move(transitions)}});
        }
        const Json document{{"schemaVersion", 1},
                            {"entryState", definition.EntryState},
                            {"parameters", definition.Parameters},
                            {"states", std::move(states)}};
        const auto text = document.dump(2) + '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<AnimationGraphAsset> AnimationGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                          reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        AnimationGraphDefinition definition;
        definition.SchemaVersion = document.value("schemaVersion", 0U);
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
        return CreateRef<AnimationGraphAsset>(std::move(definition));
    }

    AnimatorInstance::AnimatorInstance(Ref<SkeletonAsset> skeleton, Ref<AnimationGraphAsset> graph,
                                       ClipResolver resolver)
        : m_Skeleton(std::move(skeleton)), m_Graph(std::move(graph)), m_Resolver(std::move(resolver))
    {
        if (!m_Skeleton || !m_Graph || !m_Resolver)
            throw std::invalid_argument("Animator requires a skeleton, graph, and clip resolver.");
        ValidateAnimationGraph(m_Graph->Definition());
        Reset();
    }

    void AnimatorInstance::SetFloat(std::string parameter, const float value)
    {
        if (!std::isfinite(value) || !m_Parameters.contains(parameter))
            throw std::invalid_argument("Animator parameter is unavailable or non-finite.");
        m_Parameters[std::move(parameter)] = value;
    }

    float AnimatorInstance::Float(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end())
            throw std::invalid_argument("Animator parameter is unavailable.");
        return found->second;
    }

    AnimatorSample AnimatorInstance::Update(const float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || deltaSeconds > 10.0F)
            throw std::invalid_argument("Animator delta time is invalid.");
        const auto& definition = m_Graph->Definition();
        auto state = std::ranges::find(definition.States, m_State, &AnimationStateDefinition::Name);
        if (state == definition.States.end())
            throw std::logic_error("Animator current state is unavailable.");
        auto clip = m_Resolver(state->Clip);
        if (!clip || clip->Skeleton() == AssetId{})
            throw std::runtime_error("Animator could not resolve its current clip.");
        const auto previousTime = m_Time;
        const auto previousNormalized = clip->Duration() > 0.0F ? previousTime / clip->Duration() : 0.0F;
        for (const auto& transition : state->Transitions)
        {
            if (transition.HasExitTime && previousNormalized < transition.ExitTime)
                continue;
            if (!std::ranges::all_of(transition.Conditions, [this](const AnimationTransitionCondition& condition)
                                     { return Compare(m_Parameters.at(condition.Parameter), condition); }))
                continue;
            m_State = transition.Destination;
            m_Time = 0.0F;
            state = std::ranges::find(definition.States, m_State, &AnimationStateDefinition::Name);
            clip = m_Resolver(state->Clip);
            if (!clip)
                throw std::runtime_error("Animator could not resolve its transition clip.");
            break;
        }
        m_Time += deltaSeconds * state->Speed;
        bool wrapped = false;
        if (state->Loop && clip->Duration() > 0.0F && m_Time >= clip->Duration())
        {
            m_Time = std::fmod(m_Time, clip->Duration());
            wrapped = true;
        }
        else
        {
            m_Time = std::clamp(m_Time, 0.0F, clip->Duration());
        }

        AnimatorSample result;
        result.State = m_State;
        result.NormalizedTime = clip->Duration() > 0.0F ? m_Time / clip->Duration() : 0.0F;
        result.LocalPose.reserve(m_Skeleton->Bones().size());
        for (const auto& bone : m_Skeleton->Bones())
            result.LocalPose.push_back(bone.BindPose);
        for (const auto& track : clip->Tracks())
        {
            if (track.Bone >= result.LocalPose.size())
                throw std::runtime_error("Animation clip track exceeds the animator skeleton.");
            result.LocalPose[track.Bone] = SampleTrack(track, m_Time);
        }
        for (const auto& event : clip->Events())
            if ((!wrapped && event.Time > previousTime && event.Time <= m_Time) ||
                (wrapped && (event.Time > previousTime || event.Time <= m_Time)))
                result.Events.push_back(event);
        if (clip->RootMotion() && !result.LocalPose.empty())
        {
            const auto current = result.LocalPose.front();
            if (!wrapped)
                result.RootMotion = {current.Translation.X - m_PreviousRoot.Translation.X,
                                     current.Translation.Y - m_PreviousRoot.Translation.Y,
                                     current.Translation.Z - m_PreviousRoot.Translation.Z};
            m_PreviousRoot = current;
            result.LocalPose.front().Translation = {};
        }
        return result;
    }

    void AnimatorInstance::Reset()
    {
        m_State = m_Graph->Definition().EntryState;
        m_Time = 0.0F;
        m_PreviousRoot = {};
        m_Parameters.clear();
        for (const auto& parameter : m_Graph->Definition().Parameters)
            m_Parameters.emplace(parameter, 0.0F);
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

    AssetImporterRegistration CreateAnimationGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.AnimationGraph";
        result.Version = 1;
        result.Type = AnimationGraphAsset::StaticType();
        result.Extensions = {".keireanimgraph"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            const auto graph = AnimationGraphAsset::Decode(bytes);
            output.Bytes = AnimationGraphAsset::Encode(graph->Definition());
            for (const auto& state : graph->Definition().States)
                output.AssetDependencies.push_back(state.Clip);
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            return output;
        };
        return result;
    }
} // namespace Keire
