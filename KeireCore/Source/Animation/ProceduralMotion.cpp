#include "Keire/Animation/ProceduralMotion.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string_view InterpolationName(const CurveInterpolation interpolation) noexcept
        {
            switch (interpolation)
            {
            case CurveInterpolation::Constant:
                return "constant";
            case CurveInterpolation::Linear:
                return "linear";
            case CurveInterpolation::Cubic:
                return "cubic";
            }
            return "linear";
        }

        [[nodiscard]] CurveInterpolation ParseInterpolation(const std::string_view value)
        {
            if (value == "constant")
                return CurveInterpolation::Constant;
            if (value == "linear")
                return CurveInterpolation::Linear;
            if (value == "cubic")
                return CurveInterpolation::Cubic;
            throw std::invalid_argument("Procedural-motion curve interpolation is invalid.");
        }

        [[nodiscard]] Json EncodeCurve(const Curve1D& curve)
        {
            auto result = Json::array();
            for (const auto& key : curve.Keys())
            {
                result.push_back({{"time", key.Time},
                                  {"value", key.Value},
                                  {"inTangent", key.InTangent},
                                  {"outTangent", key.OutTangent},
                                  {"interpolation", InterpolationName(key.Interpolation)}});
            }
            return result;
        }

        [[nodiscard]] Curve1D DecodeCurve(const Json& value)
        {
            if (!value.is_array())
                throw std::invalid_argument("Procedural-motion curves must be arrays.");
            std::vector<CurveKey> keys;
            keys.reserve(value.size());
            for (const auto& encoded : value)
            {
                keys.push_back({encoded.at("time").get<float>(), encoded.at("value").get<float>(),
                                encoded.value("inTangent", 0.0F), encoded.value("outTangent", 0.0F),
                                ParseInterpolation(encoded.value("interpolation", "linear"))});
            }
            return Curve1D(std::move(keys));
        }

        void ValidateScalar(const float value, const float minimum, const float maximum, const char* name)
        {
            if (!std::isfinite(value) || value < minimum || value > maximum)
            {
                throw std::invalid_argument(std::string("Procedural-motion ") + name +
                                            " is non-finite or outside its supported range.");
            }
        }

        void ValidateCurve(const Curve1D& curve, const char* name)
        {
            if (curve.Keys().empty())
                throw std::invalid_argument(std::string("Procedural-motion ") + name + " curve is empty.");
            for (const auto& key : curve.Keys())
            {
                if (!std::isfinite(key.Time) || key.Time < 0.0F || key.Time > 1.0F || !std::isfinite(key.Value) ||
                    key.Value < -4.0F || key.Value > 4.0F || !std::isfinite(key.InTangent) ||
                    !std::isfinite(key.OutTangent))
                {
                    throw std::invalid_argument(std::string("Procedural-motion ") + name +
                                                " curve contains an invalid normalized key.");
                }
            }
        }

        template <typename T> [[nodiscard]] T Read(const Json& object, const char* key, const T fallback)
        {
            return object.contains(key) ? object.at(key).get<T>() : fallback;
        }
    } // namespace

    ProceduralMotionProfile ProceduralMotionProfile::GroundedArmored()
    {
        ProceduralMotionProfile result;
        result.StrideTravel = Curve1D({{0.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {0.5F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {1.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic}});
        result.FootLift = Curve1D({{0.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.25F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.5F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {1.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic}});
        result.FootRoll = Curve1D({{0.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.50F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.78F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.93F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {1.0F, 0.0F, 0.0F, 0.0F, CurveInterpolation::Cubic}});
        result.PelvisMotion = Curve1D({{0.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {0.25F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {0.5F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {0.75F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                       {1.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic}});
        result.AirborneTuck = Curve1D({{0.0F, 0.0F}, {0.35F, 1.0F}, {0.68F, 0.82F}, {1.0F, 0.25F}});
        result.LandingCompression = Curve1D({{0.0F, 1.0F}, {0.34F, 0.78F}, {1.0F, 0.0F}});
        result.ArmSwing = Curve1D({{0.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {0.5F, 1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic},
                                   {1.0F, -1.0F, 0.0F, 0.0F, CurveInterpolation::Cubic}});
        return result;
    }

    ProceduralMotionProfileAsset::ProceduralMotionProfileAsset(ProceduralMotionProfile profile)
        : m_Profile(std::move(profile))
    {
        ValidateProceduralMotionProfile(m_Profile);
    }

    std::size_t ProceduralMotionProfileAsset::ResidentBytes() const noexcept
    {
        const std::array curves{&m_Profile.StrideTravel, &m_Profile.FootLift,     &m_Profile.FootRoll,
                                &m_Profile.PelvisMotion, &m_Profile.AirborneTuck, &m_Profile.LandingCompression,
                                &m_Profile.ArmSwing};
        std::size_t result = sizeof(*this);
        for (const auto* curve : curves)
            result += curve->Keys().size() * sizeof(CurveKey);
        return result;
    }

    void ValidateProceduralLocomotionIntent(const ProceduralLocomotionIntent& intent)
    {
        if (!Math::IsFinite(intent.DesiredWorldVelocity) || !Math::IsFinite(intent.FacingWorldDirection) ||
            !Math::IsFinite(intent.LookWorldDirection) || !std::isfinite(intent.CrouchAmount) ||
            intent.CrouchAmount < 0.0F || intent.CrouchAmount > 1.0F || !std::isfinite(intent.RunBlend) ||
            intent.RunBlend < 0.0F || intent.RunBlend > 1.0F)
        {
            throw std::invalid_argument("Procedural locomotion intent must contain finite vectors and 0..1 blends.");
        }
    }

    void ValidateProceduralMotionProfile(const ProceduralMotionProfile& profile)
    {
        if (profile.SchemaVersion != 1)
            throw std::invalid_argument("Unsupported procedural-motion profile schema version.");
        ValidateScalar(profile.WalkSpeed, 0.01F, 100.0F, "walk speed");
        ValidateScalar(profile.SprintSpeed, profile.WalkSpeed, 100.0F, "sprint speed");
        ValidateScalar(profile.WalkCadence, 0.05F, 10.0F, "walk cadence");
        ValidateScalar(profile.SprintCadence, profile.WalkCadence, 12.0F, "sprint cadence");
        ValidateScalar(profile.StrideLengthRatio, 0.01F, 2.0F, "stride length");
        ValidateScalar(profile.LateralStrideRatio, 0.01F, 2.0F, "lateral stride");
        ValidateScalar(profile.BackwardStrideRatio, 0.01F, 2.0F, "backward stride");
        ValidateScalar(profile.FootSpacingRatio, 0.01F, 1.0F, "foot spacing");
        ValidateScalar(profile.MinimumMovementSpeed, 0.0F, 5.0F, "minimum movement speed");
        ValidateScalar(profile.StopSettleTime, 0.0F, 2.0F, "stop settle time");
        ValidateScalar(profile.TurnInPlaceThresholdDegrees, 0.0F, 180.0F, "turn threshold");
        ValidateScalar(profile.TurnStepDegrees, 1.0F, 180.0F, "turn step");
        ValidateScalar(profile.PelvisBobRatio, 0.0F, 0.25F, "pelvis bob");
        ValidateScalar(profile.PelvisSwayRatio, 0.0F, 0.25F, "pelvis sway");
        ValidateScalar(profile.CrouchDepthRatio, 0.0F, 0.6F, "crouch depth");
        ValidateScalar(profile.MaximumAccelerationLeanDegrees, 0.0F, 45.0F, "acceleration lean");
        ValidateScalar(profile.MaximumTurnLeanDegrees, 0.0F, 45.0F, "turn lean");
        ValidateScalar(profile.SpineCounterRotationDegrees, 0.0F, 45.0F, "spine counter rotation");
        ValidateScalar(profile.ArmRestDropDegrees, 0.0F, 90.0F, "arm rest drop");
        ValidateScalar(profile.ArmSwingDegrees, 0.0F, 90.0F, "arm swing");
        ValidateScalar(profile.ElbowBendDegrees, 0.0F, 150.0F, "elbow bend");
        ValidateScalar(profile.BreathingAmplitudeDegrees, 0.0F, 15.0F, "breathing amplitude");
        ValidateScalar(profile.BreathingFrequency, 0.0F, 5.0F, "breathing frequency");
        ValidateScalar(profile.ProbeHeightRatio, 0.01F, 2.0F, "probe height");
        ValidateScalar(profile.ProbeDistanceRatio, 0.01F, 3.0F, "probe distance");
        ValidateScalar(profile.SoleOffsetRatio, 0.0F, 0.25F, "sole offset");
        ValidateScalar(profile.MaximumPelvisAdjustmentRatio, 0.0F, 1.0F, "pelvis adjustment");
        ValidateScalar(profile.MaximumHorizontalPelvisAdjustmentRatio, 0.0F, 1.0F, "horizontal pelvis adjustment");
        ValidateScalar(profile.PlantDistanceRatio, 0.0F, 0.5F, "plant distance");
        ValidateScalar(profile.ReleaseDistanceRatio, profile.PlantDistanceRatio, 1.0F, "release distance");
        ValidateScalar(profile.StepClearanceRatio, 0.0F, 1.0F, "step clearance");
        ValidateScalar(profile.MaximumSlopeDegrees, 0.0F, 89.0F, "maximum slope");
        ValidateScalar(profile.MaximumAnkleSlopeDegrees, 0.0F, 89.0F, "ankle slope");
        ValidateScalar(profile.MinimumKneeBendDegrees, 0.0F, 90.0F, "minimum knee bend");
        ValidateScalar(profile.MaximumKneeBendDegrees, profile.MinimumKneeBendDegrees, 179.0F, "maximum knee bend");
        ValidateScalar(profile.TakeoffCompressionRatio, 0.0F, 0.5F, "takeoff compression");
        ValidateScalar(profile.AirborneTuckRatio, 0.0F, 0.6F, "airborne tuck");
        ValidateScalar(profile.FallingExtensionRatio, 0.0F, 0.5F, "falling extension");
        ValidateScalar(profile.PreLandingProbeTime, 0.0F, 2.0F, "pre-landing time");
        ValidateScalar(profile.LandingCompressionRatio, 0.0F, 0.6F, "landing compression");
        ValidateScalar(profile.LandingRecoveryTime, 0.01F, 2.0F, "landing recovery");
        ValidateScalar(profile.MaximumLandingSpeed, 0.1F, 100.0F, "maximum landing speed");
        ValidateScalar(profile.VelocityResponseTime, 0.0F, 2.0F, "velocity response");
        ValidateScalar(profile.FacingResponseTime, 0.0F, 2.0F, "facing response");
        ValidateScalar(profile.PoseResponseTime, 0.0F, 2.0F, "pose response");
        ValidateScalar(profile.GroundingResponseTime, 0.0F, 2.0F, "grounding response");
        ValidateCurve(profile.StrideTravel, "stride travel");
        ValidateCurve(profile.FootLift, "foot lift");
        ValidateCurve(profile.FootRoll, "foot roll");
        ValidateCurve(profile.PelvisMotion, "pelvis motion");
        ValidateCurve(profile.AirborneTuck, "airborne tuck");
        ValidateCurve(profile.LandingCompression, "landing compression");
        ValidateCurve(profile.ArmSwing, "arm swing");
    }

    std::string_view ProceduralMotionStateName(const ProceduralMotionState state) noexcept
    {
        switch (state)
        {
        case ProceduralMotionState::Idle:
            return "Idle";
        case ProceduralMotionState::Locomotion:
            return "Locomotion";
        case ProceduralMotionState::TurnInPlace:
            return "Turn In Place";
        case ProceduralMotionState::Takeoff:
            return "Takeoff";
        case ProceduralMotionState::Rising:
            return "Rising";
        case ProceduralMotionState::Falling:
            return "Falling";
        case ProceduralMotionState::Landing:
            return "Landing";
        }
        return "Idle";
    }

    std::vector<std::byte> ProceduralMotionProfileAsset::Encode(const ProceduralMotionProfile& profile)
    {
        ValidateProceduralMotionProfile(profile);
        Json root{{"schemaVersion", profile.SchemaVersion}};
        root["gait"] = {{"walkSpeed", profile.WalkSpeed},
                        {"sprintSpeed", profile.SprintSpeed},
                        {"walkCadence", profile.WalkCadence},
                        {"sprintCadence", profile.SprintCadence},
                        {"strideLengthRatio", profile.StrideLengthRatio},
                        {"lateralStrideRatio", profile.LateralStrideRatio},
                        {"backwardStrideRatio", profile.BackwardStrideRatio},
                        {"footSpacingRatio", profile.FootSpacingRatio},
                        {"minimumMovementSpeed", profile.MinimumMovementSpeed},
                        {"stopSettleTime", profile.StopSettleTime},
                        {"turnInPlaceThresholdDegrees", profile.TurnInPlaceThresholdDegrees},
                        {"turnStepDegrees", profile.TurnStepDegrees}};
        root["body"] = {{"pelvisBobRatio", profile.PelvisBobRatio},
                        {"pelvisSwayRatio", profile.PelvisSwayRatio},
                        {"crouchDepthRatio", profile.CrouchDepthRatio},
                        {"maximumAccelerationLeanDegrees", profile.MaximumAccelerationLeanDegrees},
                        {"maximumTurnLeanDegrees", profile.MaximumTurnLeanDegrees},
                        {"spineCounterRotationDegrees", profile.SpineCounterRotationDegrees},
                        {"armRestDropDegrees", profile.ArmRestDropDegrees},
                        {"armSwingDegrees", profile.ArmSwingDegrees},
                        {"elbowBendDegrees", profile.ElbowBendDegrees},
                        {"breathingAmplitudeDegrees", profile.BreathingAmplitudeDegrees},
                        {"breathingFrequency", profile.BreathingFrequency}};
        root["grounding"] = {{"probeHeightRatio", profile.ProbeHeightRatio},
                             {"probeDistanceRatio", profile.ProbeDistanceRatio},
                             {"soleOffsetRatio", profile.SoleOffsetRatio},
                             {"maximumPelvisAdjustmentRatio", profile.MaximumPelvisAdjustmentRatio},
                             {"maximumHorizontalPelvisAdjustmentRatio", profile.MaximumHorizontalPelvisAdjustmentRatio},
                             {"plantDistanceRatio", profile.PlantDistanceRatio},
                             {"releaseDistanceRatio", profile.ReleaseDistanceRatio},
                             {"stepClearanceRatio", profile.StepClearanceRatio},
                             {"maximumSlopeDegrees", profile.MaximumSlopeDegrees},
                             {"maximumAnkleSlopeDegrees", profile.MaximumAnkleSlopeDegrees},
                             {"minimumKneeBendDegrees", profile.MinimumKneeBendDegrees},
                             {"maximumKneeBendDegrees", profile.MaximumKneeBendDegrees},
                             {"collisionMask", profile.CollisionMask}};
        root["airborne"] = {{"takeoffCompressionRatio", profile.TakeoffCompressionRatio},
                            {"airborneTuckRatio", profile.AirborneTuckRatio},
                            {"fallingExtensionRatio", profile.FallingExtensionRatio},
                            {"preLandingProbeTime", profile.PreLandingProbeTime},
                            {"landingCompressionRatio", profile.LandingCompressionRatio},
                            {"landingRecoveryTime", profile.LandingRecoveryTime},
                            {"maximumLandingSpeed", profile.MaximumLandingSpeed}};
        root["response"] = {{"velocityResponseTime", profile.VelocityResponseTime},
                            {"facingResponseTime", profile.FacingResponseTime},
                            {"poseResponseTime", profile.PoseResponseTime},
                            {"groundingResponseTime", profile.GroundingResponseTime}};
        root["curves"] = {{"strideTravel", EncodeCurve(profile.StrideTravel)},
                          {"footLift", EncodeCurve(profile.FootLift)},
                          {"footRoll", EncodeCurve(profile.FootRoll)},
                          {"pelvisMotion", EncodeCurve(profile.PelvisMotion)},
                          {"airborneTuck", EncodeCurve(profile.AirborneTuck)},
                          {"landingCompression", EncodeCurve(profile.LandingCompression)},
                          {"armSwing", EncodeCurve(profile.ArmSwing)}};
        const auto text = root.dump(2);
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    Ref<ProceduralMotionProfileAsset> ProceduralMotionProfileAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto root = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                      reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        auto profile = ProceduralMotionProfile::GroundedArmored();
        profile.SchemaVersion = root.at("schemaVersion").get<std::uint32_t>();
        const auto& gait = root.at("gait");
        profile.WalkSpeed = Read(gait, "walkSpeed", profile.WalkSpeed);
        profile.SprintSpeed = Read(gait, "sprintSpeed", profile.SprintSpeed);
        profile.WalkCadence = Read(gait, "walkCadence", profile.WalkCadence);
        profile.SprintCadence = Read(gait, "sprintCadence", profile.SprintCadence);
        profile.StrideLengthRatio = Read(gait, "strideLengthRatio", profile.StrideLengthRatio);
        profile.LateralStrideRatio = Read(gait, "lateralStrideRatio", profile.LateralStrideRatio);
        profile.BackwardStrideRatio = Read(gait, "backwardStrideRatio", profile.BackwardStrideRatio);
        profile.FootSpacingRatio = Read(gait, "footSpacingRatio", profile.FootSpacingRatio);
        profile.MinimumMovementSpeed = Read(gait, "minimumMovementSpeed", profile.MinimumMovementSpeed);
        profile.StopSettleTime = Read(gait, "stopSettleTime", profile.StopSettleTime);
        profile.TurnInPlaceThresholdDegrees =
            Read(gait, "turnInPlaceThresholdDegrees", profile.TurnInPlaceThresholdDegrees);
        profile.TurnStepDegrees = Read(gait, "turnStepDegrees", profile.TurnStepDegrees);
        const auto& body = root.at("body");
        profile.PelvisBobRatio = Read(body, "pelvisBobRatio", profile.PelvisBobRatio);
        profile.PelvisSwayRatio = Read(body, "pelvisSwayRatio", profile.PelvisSwayRatio);
        profile.CrouchDepthRatio = Read(body, "crouchDepthRatio", profile.CrouchDepthRatio);
        profile.MaximumAccelerationLeanDegrees =
            Read(body, "maximumAccelerationLeanDegrees", profile.MaximumAccelerationLeanDegrees);
        profile.MaximumTurnLeanDegrees = Read(body, "maximumTurnLeanDegrees", profile.MaximumTurnLeanDegrees);
        profile.SpineCounterRotationDegrees =
            Read(body, "spineCounterRotationDegrees", profile.SpineCounterRotationDegrees);
        profile.ArmRestDropDegrees = Read(body, "armRestDropDegrees", profile.ArmRestDropDegrees);
        profile.ArmSwingDegrees = Read(body, "armSwingDegrees", profile.ArmSwingDegrees);
        profile.ElbowBendDegrees = Read(body, "elbowBendDegrees", profile.ElbowBendDegrees);
        profile.BreathingAmplitudeDegrees = Read(body, "breathingAmplitudeDegrees", profile.BreathingAmplitudeDegrees);
        profile.BreathingFrequency = Read(body, "breathingFrequency", profile.BreathingFrequency);
        const auto& grounding = root.at("grounding");
        profile.ProbeHeightRatio = Read(grounding, "probeHeightRatio", profile.ProbeHeightRatio);
        profile.ProbeDistanceRatio = Read(grounding, "probeDistanceRatio", profile.ProbeDistanceRatio);
        profile.SoleOffsetRatio = Read(grounding, "soleOffsetRatio", profile.SoleOffsetRatio);
        profile.MaximumPelvisAdjustmentRatio =
            Read(grounding, "maximumPelvisAdjustmentRatio", profile.MaximumPelvisAdjustmentRatio);
        profile.MaximumHorizontalPelvisAdjustmentRatio =
            Read(grounding, "maximumHorizontalPelvisAdjustmentRatio", profile.MaximumHorizontalPelvisAdjustmentRatio);
        profile.PlantDistanceRatio = Read(grounding, "plantDistanceRatio", profile.PlantDistanceRatio);
        profile.ReleaseDistanceRatio = Read(grounding, "releaseDistanceRatio", profile.ReleaseDistanceRatio);
        profile.StepClearanceRatio = Read(grounding, "stepClearanceRatio", profile.StepClearanceRatio);
        profile.MaximumSlopeDegrees = Read(grounding, "maximumSlopeDegrees", profile.MaximumSlopeDegrees);
        profile.MaximumAnkleSlopeDegrees =
            Read(grounding, "maximumAnkleSlopeDegrees", profile.MaximumAnkleSlopeDegrees);
        profile.MinimumKneeBendDegrees = Read(grounding, "minimumKneeBendDegrees", profile.MinimumKneeBendDegrees);
        profile.MaximumKneeBendDegrees = Read(grounding, "maximumKneeBendDegrees", profile.MaximumKneeBendDegrees);
        profile.CollisionMask = Read(grounding, "collisionMask", profile.CollisionMask);
        const auto& airborne = root.at("airborne");
        profile.TakeoffCompressionRatio = Read(airborne, "takeoffCompressionRatio", profile.TakeoffCompressionRatio);
        profile.AirborneTuckRatio = Read(airborne, "airborneTuckRatio", profile.AirborneTuckRatio);
        profile.FallingExtensionRatio = Read(airborne, "fallingExtensionRatio", profile.FallingExtensionRatio);
        profile.PreLandingProbeTime = Read(airborne, "preLandingProbeTime", profile.PreLandingProbeTime);
        profile.LandingCompressionRatio = Read(airborne, "landingCompressionRatio", profile.LandingCompressionRatio);
        profile.LandingRecoveryTime = Read(airborne, "landingRecoveryTime", profile.LandingRecoveryTime);
        profile.MaximumLandingSpeed = Read(airborne, "maximumLandingSpeed", profile.MaximumLandingSpeed);
        const auto& response = root.at("response");
        profile.VelocityResponseTime = Read(response, "velocityResponseTime", profile.VelocityResponseTime);
        profile.FacingResponseTime = Read(response, "facingResponseTime", profile.FacingResponseTime);
        profile.PoseResponseTime = Read(response, "poseResponseTime", profile.PoseResponseTime);
        profile.GroundingResponseTime = Read(response, "groundingResponseTime", profile.GroundingResponseTime);
        const auto& curves = root.at("curves");
        profile.StrideTravel = DecodeCurve(curves.at("strideTravel"));
        profile.FootLift = DecodeCurve(curves.at("footLift"));
        profile.FootRoll = DecodeCurve(curves.at("footRoll"));
        profile.PelvisMotion = DecodeCurve(curves.at("pelvisMotion"));
        profile.AirborneTuck = DecodeCurve(curves.at("airborneTuck"));
        profile.LandingCompression = DecodeCurve(curves.at("landingCompression"));
        profile.ArmSwing = DecodeCurve(curves.at("armSwing"));
        ValidateProceduralMotionProfile(profile);
        return CreateRef<ProceduralMotionProfileAsset>(std::move(profile));
    }

    AssetImporterRegistration CreateProceduralMotionProfileAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ProceduralMotionProfile";
        result.Version = 1;
        result.Type = ProceduralMotionProfileAsset::StaticType();
        result.Extensions = {".keiremotionprofile"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto decoded = ProceduralMotionProfileAsset::Decode(bytes);
            return AssetImportOutput{ProceduralMotionProfileAsset::Encode(decoded->Profile())};
        };
        return result;
    }

    AssetDecoderRegistration CreateProceduralMotionProfileAssetDecoder()
    {
        AssetDecoderRegistration result;
        result.Type = ProceduralMotionProfileAsset::StaticType();
        result.Fallback = CreateRef<ProceduralMotionProfileAsset>();
        result.Decode = [](const std::span<const std::byte> bytes) -> Ref<Asset>
        { return ProceduralMotionProfileAsset::Decode(bytes); };
        return result;
    }
} // namespace Keire
