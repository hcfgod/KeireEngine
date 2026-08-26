#include "Keire/Animation/AnimationSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string LegacyLocalId(const std::string_view category, const std::string_view seed)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const char value : seed)
            {
                hash ^= static_cast<std::uint8_t>(value);
                hash *= 1099511628211ULL;
            }
            std::string result;
            result.reserve(8 + category.size() + 20);
            result.append("legacy-");
            result.append(category);
            result.push_back('-');
            result.append(std::to_string(hash));
            return result;
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
                for (auto& subgraph : layer.Subgraphs)
                    if (subgraph.Id.empty())
                        subgraph.Id = LegacyLocalId("subgraph", layer.Id + ':' + subgraph.Name);
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
                for (auto& subgraph : layer.Subgraphs)
                {
                    if (const auto entry =
                            std::ranges::find(layer.States, subgraph.EntryStateId, &AnimationStateDefinition::Name);
                        entry != layer.States.end() && entry->SubgraphId == subgraph.Id)
                        subgraph.EntryStateId = entry->Id;
                    if (subgraph.EntryStateId.empty())
                    {
                        const auto first =
                            std::ranges::find(layer.States, subgraph.Id, &AnimationStateDefinition::SubgraphId);
                        if (first != layer.States.end())
                            subgraph.EntryStateId = first->Id;
                    }
                }

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
            state.SubgraphId = encoded.value("subgraphId", std::string{});
            if (state.SubgraphId.size() > 512)
                throw std::invalid_argument("Animation state subgraph ID is invalid.");
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
                    {"subgraphId", state.SubgraphId},
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

        using WeightedBlendTreeChildren = std::vector<std::pair<const AnimationBlendTreeChild*, float>>;

    } // namespace

    void ValidateAnimationGraph(const AnimationGraphDefinition& source)
    {
        const auto definition = CanonicalizeAnimationGraph(source);
        if ((definition.SchemaVersion != 1 && definition.SchemaVersion != 2 && definition.SchemaVersion != 3) ||
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
                layer.DefaultWeight < 0.0F || layer.DefaultWeight > 1.0F || layer.States.size() > 4096 ||
                layer.Subgraphs.size() > 256)
                throw std::invalid_argument("Animation graph contains an invalid layer.");
            std::set<std::string, std::less<>> subgraphIds;
            std::set<std::string, std::less<>> subgraphNames;
            for (const auto& subgraph : layer.Subgraphs)
                if (subgraph.Id.empty() || subgraph.Id.size() > 512 || subgraph.Name.empty() ||
                    subgraph.Name.size() > 256 || !localIds.insert(subgraph.Id).second ||
                    !subgraphIds.insert(subgraph.Id).second || !subgraphNames.insert(subgraph.Name).second)
                    throw std::invalid_argument("Animation graph contains an invalid state-machine subgraph.");
            if (layer.States.empty())
            {
                if (!layer.EntryStateId.empty() || !layer.Subgraphs.empty())
                    throw std::invalid_argument("An empty animation graph layer cannot declare an entry state.");
                continue;
            }
            std::set<std::string, std::less<>> stateIds;
            std::set<std::string, std::less<>> stateNames;
            for (const auto& state : layer.States)
            {
                if (state.Id.empty() || state.Id.size() > 512 || state.Name.empty() || state.Name.size() > 256 ||
                    !localIds.insert(state.Id).second || !stateIds.insert(state.Id).second ||
                    !stateNames.insert(state.Name).second || !std::isfinite(state.Speed) || state.Speed == 0.0F ||
                    state.Motion.Type > AnimationMotionType::BlendTree2D || !Math::IsFinite(state.EditorPosition) ||
                    (!state.SubgraphId.empty() && !subgraphIds.contains(state.SubgraphId)))
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
            const auto rootEntry = std::ranges::find(layer.States, layer.EntryStateId, &AnimationStateDefinition::Id);
            if (rootEntry == layer.States.end() || !rootEntry->SubgraphId.empty())
                throw std::invalid_argument("Animation graph layer entry state is unavailable.");
            for (const auto& subgraph : layer.Subgraphs)
            {
                const auto entry =
                    std::ranges::find(layer.States, subgraph.EntryStateId, &AnimationStateDefinition::Id);
                const auto hasStates = std::ranges::any_of(layer.States, [&](const auto& state)
                                                           { return state.SubgraphId == subgraph.Id; });
                if ((!hasStates && !subgraph.EntryStateId.empty()) ||
                    (hasStates && (entry == layer.States.end() || entry->SubgraphId != subgraph.Id)))
                    throw std::invalid_argument("Animation state-machine subgraph entry state is unavailable.");
            }
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
            for (const auto& subgraph : layer.Subgraphs)
                result += sizeof(subgraph) + subgraph.Id.size() + subgraph.Name.size() + subgraph.EntryStateId.size();
            for (const auto& state : layer.States)
            {
                result += sizeof(state) + state.Id.size() + state.Name.size() + state.SubgraphId.size();
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
        definition.SchemaVersion = 3;
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
            Json subgraphs = Json::array();
            for (const auto& subgraph : layer.Subgraphs)
                subgraphs.push_back(
                    {{"id", subgraph.Id}, {"name", subgraph.Name}, {"entryStateId", subgraph.EntryStateId}});
            layers.push_back({{"id", layer.Id},
                              {"name", layer.Name},
                              {"mode", static_cast<std::uint8_t>(layer.Mode)},
                              {"defaultWeight", layer.DefaultWeight},
                              {"avatarMask", layer.AvatarMask ? layer.AvatarMask.ToString() : std::string{}},
                              {"entryStateId", layer.EntryStateId},
                              {"subgraphs", std::move(subgraphs)},
                              {"states", std::move(states)}});
        }
        const auto text =
            Json{{"schemaVersion", 3}, {"parameters", std::move(parameters)}, {"layers", std::move(layers)}}.dump(2) +
            '\n';
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<AnimationGraphAsset> AnimationGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                          reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        const auto schemaVersion = document.value("schemaVersion", 0U);
        if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3)
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
                layer.EntryStateId = encodedLayer.value("entryStateId", std::string{});
                if (layer.EntryStateId.size() > 512)
                    throw std::invalid_argument("Animation graph entry state ID is invalid.");
                for (const auto& encodedSubgraph : encodedLayer.value("subgraphs", Json::array()))
                {
                    AnimationStateMachineSubgraphDefinition subgraph;
                    subgraph.Id = DecodeLocalId(encodedSubgraph, "id");
                    subgraph.Name = encodedSubgraph.at("name").get<std::string>();
                    subgraph.EntryStateId = encodedSubgraph.value("entryStateId", std::string{});
                    if (subgraph.EntryStateId.size() > 512)
                        throw std::invalid_argument("Animation subgraph entry state ID is invalid.");
                    layer.Subgraphs.push_back(std::move(subgraph));
                }
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
} // namespace Keire
