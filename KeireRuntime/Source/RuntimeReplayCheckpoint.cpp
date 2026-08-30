#include "KeireRuntimeInternal/RuntimeReplayCheckpoint.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireRuntime
{
    nlohmann::json EncodeAnimatorCheckpoint(const Keire::SceneAnimatorCheckpoint& animator)
    {
        nlohmann::json parameters = nlohmann::json::array();
        for (const auto& parameter : animator.State.Parameters)
            parameters.push_back({{"id", parameter.Id},
                                  {"type", static_cast<std::uint8_t>(parameter.Type)},
                                  {"float", parameter.FloatValue},
                                  {"integer", parameter.IntegerValue},
                                  {"boolean", parameter.BooleanValue}});
        nlohmann::json layers = nlohmann::json::array();
        for (const auto& layer : animator.State.Layers)
        {
            nlohmann::json encoded{{"id", layer.Id},
                                   {"state", layer.StateId},
                                   {"time", layer.Time},
                                   {"weight", layer.Weight},
                                   {"normalizedTime", layer.NormalizedTime}};
            if (layer.Transition)
            {
                const auto& transition = *layer.Transition;
                encoded["transition"] = {{"id", transition.Id},
                                         {"source", transition.SourceStateId},
                                         {"destination", transition.DestinationStateId},
                                         {"sourceTime", transition.SourceTime},
                                         {"destinationTime", transition.DestinationTime},
                                         {"elapsed", transition.Elapsed},
                                         {"duration", transition.Duration}};
            }
            layers.push_back(std::move(encoded));
        }
        const auto& root = animator.State.PreviousRoot;
        return {{"entity", animator.Entity.ToString()},
                {"playing", animator.State.Playing},
                {"hasPreviousRootRotation", animator.State.HasPreviousRootRotation},
                {"previousRoot",
                 {{"translation", {root.Translation.X, root.Translation.Y, root.Translation.Z}},
                  {"rotation", {root.Rotation.X, root.Rotation.Y, root.Rotation.Z, root.Rotation.W}},
                  {"scale", {root.Scale.X, root.Scale.Y, root.Scale.Z}}}},
                {"parameters", std::move(parameters)},
                {"layers", std::move(layers)}};
    }

    Keire::SceneAnimatorCheckpoint DecodeAnimatorCheckpoint(const nlohmann::json& encoded)
    {
        const auto vector3 = [](const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() != 3U)
                throw std::runtime_error("Animator replay checkpoint vector is malformed.");
            return Keire::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        };
        Keire::SceneAnimatorCheckpoint result;
        result.Entity = Keire::EntityId::Parse(encoded.at("entity").get<std::string>());
        result.State.Playing = encoded.at("playing").get<bool>();
        result.State.HasPreviousRootRotation = encoded.at("hasPreviousRootRotation").get<bool>();
        const auto& root = encoded.at("previousRoot");
        result.State.PreviousRoot.Translation = vector3(root.at("translation"));
        const auto& rotation = root.at("rotation");
        if (!rotation.is_array() || rotation.size() != 4U)
            throw std::runtime_error("Animator replay checkpoint rotation is malformed.");
        result.State.PreviousRoot.Rotation = {rotation[0].get<float>(), rotation[1].get<float>(),
                                              rotation[2].get<float>(), rotation[3].get<float>()};
        result.State.PreviousRoot.Scale = vector3(root.at("scale"));
        for (const auto& parameter : encoded.at("parameters"))
            result.State.Parameters.push_back(
                {parameter.at("id").get<std::string>(),
                 static_cast<Keire::AnimationParameterType>(parameter.at("type").get<std::uint8_t>()),
                 parameter.at("float").get<float>(), parameter.at("integer").get<std::int32_t>(),
                 parameter.at("boolean").get<bool>()});
        for (const auto& layer : encoded.at("layers"))
        {
            Keire::AnimatorCheckpointLayer decoded{
                layer.at("id").get<std::string>(), layer.at("state").get<std::string>(), layer.at("time").get<float>(),
                layer.at("weight").get<float>(), layer.at("normalizedTime").get<float>()};
            if (layer.contains("transition"))
            {
                const auto& transition = layer.at("transition");
                decoded.Transition = Keire::AnimatorCheckpointTransition{
                    transition.at("id").get<std::string>(),          transition.at("source").get<std::string>(),
                    transition.at("destination").get<std::string>(), transition.at("sourceTime").get<float>(),
                    transition.at("destinationTime").get<float>(),   transition.at("elapsed").get<float>(),
                    transition.at("duration").get<float>()};
            }
            result.State.Layers.push_back(std::move(decoded));
        }
        return result;
    }
} // namespace KeireRuntime
