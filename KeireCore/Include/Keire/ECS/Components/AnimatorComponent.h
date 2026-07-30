#pragma once

#include "Keire/ECS/Component.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    struct AnimatorDebugSnapshot;

    enum class AnimatorCommandType : std::uint8_t
    {
        SetFloat,
        SetInteger,
        SetBoolean,
        SetTrigger,
        ResetTrigger,
        SetLayerWeight,
        Play,
        CrossFade,
        Stop
    };

    struct AnimatorCommand
    {
        std::uint64_t Sequence = 0;
        AnimatorCommandType Type = AnimatorCommandType::SetFloat;
        std::string Name;
        std::string Layer;
        float FloatValue = 0.0F;
        float SecondaryFloatValue = 0.0F;
        std::int32_t IntegerValue = 0;
        bool BooleanValue = false;
    };

    enum class AnimatorIkSpace : std::uint8_t
    {
        Model,
        World
    };

    enum class AnimatorIkSolver : std::uint8_t
    {
        TwoBone,
        Fabrik
    };

    struct AnimatorIkGoal
    {
        std::string Name;
        AnimatorIkSolver Solver = AnimatorIkSolver::TwoBone;
        AnimatorIkSpace Space = AnimatorIkSpace::World;
        std::vector<std::string> Bones;
        Vector3 Target;
        Vector3 Pole{0.0F, 0.0F, 1.0F};
        float Weight = 1.0F;
        std::uint32_t MaximumIterations = 12;
        float Tolerance = 0.001F;
    };

    class KEIRE_API AnimatorComponent final : public Component
    {
      public:
        AnimatorComponent();
        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245414e49ULL, 0x4d41544f52000001ULL));
        }

        [[nodiscard]] AssetId Graph() const noexcept { return m_Graph; }
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] AssetId SkinnedMesh() const noexcept { return m_SkinnedMesh; }
        [[nodiscard]] bool ApplyRootMotion() const noexcept { return m_ApplyRootMotion; }
        [[nodiscard]] float Speed() const noexcept { return m_Speed; }
        [[nodiscard]] bool Paused() const noexcept { return m_Paused; }
        [[nodiscard]] bool RuntimePlaying() const noexcept { return m_RuntimePlaying; }
        [[nodiscard]] std::string_view CurrentState() const noexcept { return m_CurrentState; }
        [[nodiscard]] float NormalizedTime() const noexcept { return m_NormalizedTime; }
        [[nodiscard]] std::span<const Matrix4> SkinPalette() const noexcept { return m_SkinPalette; }
        [[nodiscard]] std::string_view RuntimeDiagnostic() const noexcept { return m_RuntimeDiagnostic; }
        [[nodiscard]] std::shared_ptr<const AnimatorDebugSnapshot> RuntimeDebugSnapshot() const noexcept
        {
            return m_DebugSnapshot;
        }

        void SetGraph(AssetId graph);
        void SetSkeleton(AssetId skeleton);
        void SetSkinnedMesh(AssetId mesh);
        void SetApplyRootMotion(bool enabled);
        void SetSpeed(float speed);
        void SetPaused(bool paused) noexcept;
        void Play(std::string state, std::string layer = {}, float normalizedTime = 0.0F);
        void CrossFade(std::string state, float duration, std::string layer = {}, float normalizedTime = 0.0F);
        void Stop();
        void SetFloat(std::string parameter, float value);
        void SetInteger(std::string parameter, std::int32_t value);
        void SetBool(std::string parameter, bool value);
        void SetTrigger(std::string parameter);
        void ResetTrigger(std::string parameter);
        void SetLayerWeight(std::string layer, float value);
        void SetTwoBoneIk(std::string name, std::string root, std::string middle, std::string end, Vector3 target,
                          Vector3 pole = {0.0F, 0.0F, 1.0F}, float weight = 1.0F,
                          AnimatorIkSpace space = AnimatorIkSpace::World);
        void SetFabrikIk(std::string name, std::vector<std::string> chain, Vector3 target, float weight = 1.0F,
                         std::uint32_t maximumIterations = 12, float tolerance = 0.001F,
                         AnimatorIkSpace space = AnimatorIkSpace::World);
        [[nodiscard]] bool ClearIk(std::string_view name) noexcept;
        void ClearIk() noexcept;
        [[nodiscard]] std::span<const AnimatorIkGoal> IkGoals() const noexcept { return m_IkGoals; }
        [[nodiscard]] std::vector<AnimatorCommand> ConsumeRuntimeCommands();
        void SetRuntimePose(std::string state, float normalizedTime, bool playing,
                            std::span<const Matrix4> skinPalette);
        void SetRuntimeDebugSnapshot(std::shared_ptr<const AnimatorDebugSnapshot> snapshot) noexcept;
        void SetRuntimeDiagnostic(std::string diagnostic) noexcept;
        void ClearRuntimePose() noexcept;

      private:
        void QueueCommand(AnimatorCommand command);

        friend ComponentRegistration CreateAnimatorComponentRegistration();
        AssetId m_Graph;
        AssetId m_Skeleton;
        AssetId m_SkinnedMesh;
        bool m_ApplyRootMotion = true;
        float m_Speed = 1.0F;
        bool m_Paused = false;
        bool m_RuntimePlaying = false;
        std::string m_CurrentState;
        float m_NormalizedTime = 0.0F;
        std::vector<Matrix4> m_SkinPalette;
        std::vector<AnimatorCommand> m_RuntimeCommands;
        std::vector<AnimatorIkGoal> m_IkGoals;
        std::uint64_t m_NextRuntimeCommand = 1;
        std::shared_ptr<const AnimatorDebugSnapshot> m_DebugSnapshot;
        std::string m_RuntimeDiagnostic;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateAnimatorComponentRegistration();
} // namespace Keire
