#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Component.h"
#include "Keire/Math/Curves.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class AnimatorPoseSource : std::uint8_t
    {
        AnimationGraph,
        ProceduralHumanoid
    };

    enum class ProceduralMotionQuality : std::uint8_t
    {
        Auto,
        High,
        Medium,
        Low
    };

    enum class ProceduralMotionState : std::uint8_t
    {
        Idle,
        Locomotion,
        TurnInPlace,
        Takeoff,
        Rising,
        Falling,
        Landing
    };

    enum class ProceduralMotionEventType : std::uint8_t
    {
        FootLift,
        FootPlant,
        Takeoff,
        Apex,
        Land,
        StateChanged
    };

    enum class ProceduralFootSide : std::uint8_t
    {
        None,
        Left,
        Right
    };

    struct ProceduralLocomotionIntent
    {
        Vector3 DesiredWorldVelocity;
        Vector3 FacingWorldDirection;
        Vector3 LookWorldDirection;
        float CrouchAmount = 0.0F;
        float RunBlend = 0.0F;
        bool JumpRequested = false;
    };

    struct ProceduralLocomotionState
    {
        ProceduralMotionState State = ProceduralMotionState::Idle;
        ProceduralMotionQuality Quality = ProceduralMotionQuality::High;
        Vector3 ActualWorldVelocity;
        Vector3 GroundNormal{0.0F, 1.0F, 0.0F};
        float GaitPhase = 0.0F;
        float Speed = 0.0F;
        float VerticalSpeed = 0.0F;
        float LandingIntensity = 0.0F;
        bool Grounded = false;
        bool LeftFootPlanted = false;
        bool RightFootPlanted = false;
    };

    struct ProceduralMotionEvent
    {
        ProceduralMotionEventType Type = ProceduralMotionEventType::StateChanged;
        ProceduralFootSide Foot = ProceduralFootSide::None;
        ProceduralMotionState State = ProceduralMotionState::Idle;
        float Phase = 0.0F;
        float Intensity = 0.0F;
        Vector3 ContactPosition;
        Vector3 ContactNormal{0.0F, 1.0F, 0.0F};
        EntityId Support;
        AssetId PhysicsMaterial;
    };

    struct ProceduralMotionProfile
    {
        std::uint32_t SchemaVersion = 1;

        float WalkSpeed = 2.8F;
        float SprintSpeed = 6.2F;
        float WalkCadence = 1.15F;
        float SprintCadence = 1.75F;
        float StrideLengthRatio = 0.82F;
        float LateralStrideRatio = 0.78F;
        float BackwardStrideRatio = 0.82F;
        float FootSpacingRatio = 0.20F;
        float MinimumMovementSpeed = 0.08F;
        float StopSettleTime = 0.18F;
        float TurnInPlaceThresholdDegrees = 22.0F;
        float TurnStepDegrees = 48.0F;

        float PelvisBobRatio = 0.018F;
        float PelvisSwayRatio = 0.014F;
        float CrouchDepthRatio = 0.18F;
        float MaximumAccelerationLeanDegrees = 8.0F;
        float MaximumTurnLeanDegrees = 6.0F;
        float SpineCounterRotationDegrees = 7.0F;
        float ArmRestDropDegrees = 72.0F;
        float ArmSwingDegrees = 23.0F;
        float ElbowBendDegrees = 18.0F;
        float BreathingAmplitudeDegrees = 1.2F;
        float BreathingFrequency = 0.24F;

        float ProbeHeightRatio = 0.28F;
        float ProbeDistanceRatio = 0.62F;
        float SoleOffsetRatio = 0.003F;
        float MaximumPelvisAdjustmentRatio = 0.32F;
        float MaximumHorizontalPelvisAdjustmentRatio = 0.18F;
        float PlantDistanceRatio = 0.055F;
        float ReleaseDistanceRatio = 0.14F;
        float StepClearanceRatio = 0.11F;
        float MaximumSlopeDegrees = 55.0F;
        float MaximumAnkleSlopeDegrees = 42.0F;
        float MinimumKneeBendDegrees = 4.0F;
        float MaximumKneeBendDegrees = 150.0F;
        std::uint32_t CollisionMask = ~0U;

        float TakeoffCompressionRatio = 0.10F;
        float AirborneTuckRatio = 0.12F;
        float FallingExtensionRatio = 0.08F;
        float PreLandingProbeTime = 0.24F;
        float LandingCompressionRatio = 0.16F;
        float LandingRecoveryTime = 0.28F;
        float MaximumLandingSpeed = 18.0F;

        float VelocityResponseTime = 0.09F;
        float FacingResponseTime = 0.07F;
        float PoseResponseTime = 0.06F;
        float GroundingResponseTime = 0.045F;

        Curve1D StrideTravel;
        Curve1D FootLift;
        Curve1D FootRoll;
        Curve1D PelvisMotion;
        Curve1D AirborneTuck;
        Curve1D LandingCompression;
        Curve1D ArmSwing;

        [[nodiscard]] static ProceduralMotionProfile GroundedArmored();
    };

    class KEIRE_API ProceduralMotionProfileAsset final : public Asset
    {
      public:
        explicit ProceduralMotionProfileAsset(
            ProceduralMotionProfile profile = ProceduralMotionProfile::GroundedArmored());

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b4549524550524fULL, 0x434d4f54494f4e01ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const ProceduralMotionProfile& Profile() const noexcept { return m_Profile; }

        [[nodiscard]] static Ref<ProceduralMotionProfileAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ProceduralMotionProfile& profile);

      private:
        ProceduralMotionProfile m_Profile;
    };

    KEIRE_API void ValidateProceduralLocomotionIntent(const ProceduralLocomotionIntent& intent);
    KEIRE_API void ValidateProceduralMotionProfile(const ProceduralMotionProfile& profile);
    [[nodiscard]] KEIRE_API std::string_view ProceduralMotionStateName(ProceduralMotionState state) noexcept;
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateProceduralMotionProfileAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateProceduralMotionProfileAssetDecoder();
} // namespace Keire
