#pragma once

#include "Keire/Api.h"
#include "Keire/ECS/Component.h"
#include "Keire/Ref.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedDataAsset.h"

#include <chrono>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class JobSystem;
    struct VfxParameterOverride;

    enum class ManagedLogLevel : std::uint8_t
    {
        Trace,
        Debug,
        Information,
        Warning,
        Error,
        Critical
    };

    enum class ManagedInputState : std::uint8_t
    {
        None = 0,
        Held = 1U << 0U,
        Pressed = 1U << 1U,
        Released = 1U << 2U
    };

    [[nodiscard]] constexpr ManagedInputState operator|(const ManagedInputState left,
                                                        const ManagedInputState right) noexcept
    {
        return static_cast<ManagedInputState>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    struct ManagedRaycastQuery
    {
        std::uint64_t World = 0;
        Vector3 Origin;
        Vector3 Direction{0.0F, 0.0F, -1.0F};
        float MaximumDistance = 1000.0F;
        std::uint32_t Mask = ~0U;
        AssetId IgnoredEntity;
        bool IncludeTriggers = false;
    };

    struct ManagedRaycastHit
    {
        AssetId Entity;
        Vector3 Point;
        Vector3 Normal;
        float Distance = 0.0F;
    };

    struct ManagedAudioPlayback
    {
        AssetId Entity;
        AssetId Clip;
        std::string Bus = "SFX";
        float Gain = 1.0F;
        float Pitch = 1.0F;
        std::uint32_t Priority = 128;
        bool Loop = false;
        bool Spatial = true;
        float MinimumDistance = 1.0F;
        float MaximumDistance = 100.0F;
        AssetId Mixer;
        AssetId BusId;
        Curve1D Attenuation = Curve1D::Constant(1.0F);
    };

    enum class ManagedAudioPlaybackState : std::uint8_t
    {
        Stopped,
        Playing,
        Paused
    };

    struct ManagedAudioSourceStatus
    {
        ManagedAudioPlaybackState State = ManagedAudioPlaybackState::Stopped;
        float PositionSeconds = 0.0F;
        float DurationSeconds = 0.0F;
    };

    class KEIRE_API IScriptRuntimeServices
    {
      public:
        virtual ~IScriptRuntimeServices() = default;

        virtual void WriteManagedLog(ManagedLogLevel level, std::string_view message) noexcept = 0;
        virtual void RecordManagedProfileSpan(std::string_view, double, double) noexcept {}
        virtual void SetManagedProfileCounter(std::string_view, double) noexcept {}
        [[nodiscard]] virtual float ManagedDeltaTime() const noexcept = 0;
        [[nodiscard]] virtual float ManagedFixedDeltaTime() const noexcept { return 1.0F / 60.0F; }
        [[nodiscard]] virtual float ManagedUnscaledDeltaTime() const noexcept { return ManagedDeltaTime(); }
        [[nodiscard]] virtual double ManagedElapsedTime() const noexcept { return 0.0; }
        [[nodiscard]] virtual Vector2 ReadManagedInput(std::string_view action) noexcept = 0;
        [[nodiscard]] virtual ManagedInputState ReadManagedInputState(std::string_view) noexcept
        {
            return ManagedInputState::None;
        }
        [[nodiscard]] virtual std::optional<ManagedRaycastHit> RaycastManaged(const ManagedRaycastQuery&) noexcept
        {
            return std::nullopt;
        }
        virtual void SetManagedCursorVisible(bool) noexcept {}
        virtual void SetManagedCursorLocked(bool) noexcept {}
        [[nodiscard]] virtual bool IsManagedCursorVisible() const noexcept { return true; }
        [[nodiscard]] virtual bool IsManagedCursorLocked() const noexcept { return false; }
        [[nodiscard]] virtual bool PlayManagedAudio(AssetId, AssetId, float) noexcept { return false; }
        [[nodiscard]] virtual bool PlayManagedAudio(const ManagedAudioPlayback& playback) noexcept
        {
            return PlayManagedAudio(playback.Entity, playback.Clip, playback.Gain);
        }
        [[nodiscard]] virtual bool StopManagedAudio(AssetId) noexcept { return false; }
        [[nodiscard]] virtual bool PauseManagedAudio(AssetId, bool) noexcept { return false; }
        [[nodiscard]] virtual bool SeekManagedAudio(AssetId, float) noexcept { return false; }
        [[nodiscard]] virtual ManagedAudioSourceStatus ManagedAudioStatus(AssetId) const noexcept { return {}; }
        [[nodiscard]] virtual bool PlayManagedVfx(AssetId, AssetId, bool) noexcept { return false; }
        [[nodiscard]] virtual bool StopManagedVfx(AssetId) noexcept { return false; }
        [[nodiscard]] virtual bool PauseManagedVfx(AssetId, bool) noexcept { return false; }
        [[nodiscard]] virtual bool IsManagedVfxAlive(AssetId) const noexcept { return false; }
        [[nodiscard]] virtual bool SendManagedVfxEvent(AssetId, std::string_view, std::uint32_t) noexcept
        {
            return false;
        }
        /// Atomically updates an exposed parameter on both the runtime entity component and its live VFX instance.
        [[nodiscard]] virtual bool SetManagedVfxParameter(AssetId, const VfxParameterOverride&) noexcept
        {
            return false;
        }
        [[nodiscard]] virtual bool SetManagedUiText(AssetId, std::string_view) noexcept { return false; }
        [[nodiscard]] virtual bool ConsumeManagedUiClick(AssetId) noexcept { return false; }
    };

    enum class ScriptMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    enum class ManagedReloadPolicy : std::uint8_t
    {
        Disabled,
        PreserveState
    };

    enum class ManagedExceptionPolicy : std::uint8_t
    {
        DisableInstance,
        Propagate
    };

    enum class ManagedSdkSelection : std::uint8_t
    {
        Bundled,
        SystemPath,
        Custom
    };

    struct ManagedSdkConfiguration
    {
        ManagedSdkSelection Selection = ManagedSdkSelection::Bundled;
        std::filesystem::path CustomExecutable;
    };

    struct ScriptSystemSpecification
    {
        ScriptMode Mode = ScriptMode::Disabled;
        std::filesystem::path ProjectRoot = ".";
        std::filesystem::path AssemblyDirectory = "Library/ScriptAssemblies";
        std::filesystem::path RuntimeHostDirectory;
        std::filesystem::path RuntimeRootDirectory;
        std::filesystem::path ManagedApiAssembly;
        ManagedSdkSelection SdkSelection = ManagedSdkSelection::Bundled;
        std::filesystem::path DotnetExecutable;
        std::size_t MaximumDiagnostics = 4096;
        ManagedReloadPolicy ReloadPolicy = ManagedReloadPolicy::PreserveState;
        ManagedExceptionPolicy ExceptionPolicy = ManagedExceptionPolicy::DisableInstance;
        std::uint32_t ManagedApiVersion = 1;
        std::size_t MaximumManagedDataAssets = 4096;
        std::size_t MaximumManagedDataLoads = 64;
        IScriptRuntimeServices* RuntimeServices = nullptr;
    };

    enum class ManagedBuildState : std::uint8_t
    {
        Idle,
        Generating,
        Compiling,
        Publishing,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class ManagedDiagnosticSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error
    };

    struct ManagedBuildDiagnostic
    {
        ManagedDiagnosticSeverity Severity = ManagedDiagnosticSeverity::Error;
        std::filesystem::path Source;
        std::uint32_t Line = 0;
        std::uint32_t Column = 0;
        std::string Code;
        std::string Message;
    };

    class KEIRE_API ManagedBuildOperationId final
    {
      public:
        constexpr ManagedBuildOperationId() noexcept = default;
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr auto operator<=>(const ManagedBuildOperationId&) const noexcept = default;

      private:
        friend class ScriptSystem;
        explicit constexpr ManagedBuildOperationId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    struct ManagedBuildRequest
    {
        std::vector<ManagedAssemblyGraphEntry> Assemblies;
        std::string Configuration = "Debug";
    };

    struct ManagedBuildStatus
    {
        ManagedBuildOperationId Operation;
        ManagedBuildState State = ManagedBuildState::Idle;
        std::vector<ManagedBuildDiagnostic> Diagnostics;
        std::filesystem::path ActiveAssemblyDirectory;
        std::filesystem::path ManagedApiAssembly;
        std::uint64_t Generation = 0;
        std::chrono::milliseconds Elapsed{};
        std::vector<std::string> ChangedAssemblies;
    };

    struct ManagedIdeWorkspace
    {
        std::filesystem::path Solution;
        std::vector<std::filesystem::path> Projects;
    };

    enum class ManagedReloadState : std::uint8_t
    {
        Idle,
        Preparing,
        Prepared,
        Active,
        Failed,
        Cancelled
    };

    struct ManagedSerializedField
    {
        std::string StableFieldId;
        std::string Value;
        std::string Name;
        std::string TypeName;
        auto operator<=>(const ManagedSerializedField&) const = default;
    };

    struct ManagedBehaviourState
    {
        std::uint64_t Instance = 0;
        std::string StableTypeId;
        std::vector<ManagedSerializedField> Fields;
        std::uint32_t StateVersion = 1;
        auto operator<=>(const ManagedBehaviourState&) const = default;
    };

    struct ManagedReloadRequest
    {
        std::vector<std::filesystem::path> Assemblies;
        std::filesystem::path ManagedApiAssembly;
        std::vector<ManagedBehaviourState> State;
    };

    struct ManagedReloadStatus
    {
        ManagedReloadState State = ManagedReloadState::Idle;
        std::uint64_t Generation = 0;
        std::vector<std::string> AvailableTypes;
        std::vector<ManagedBehaviourState> RetainedState;
        std::string Diagnostic;
    };

    enum class ManagedBehaviourCallback : std::uint8_t
    {
        Awake,
        Enable,
        Start,
        FixedUpdate,
        Update,
        LateUpdate,
        AnimationEvent,
        PhysicsContact,
        Disable,
        Destroy,
        BeforeReload,
        AfterReload,
        AnimatorIk,
        ProceduralMotionEvent
    };

    struct ManagedCallbackMetric
    {
        std::string TypeName;
        ManagedBehaviourCallback Callback = ManagedBehaviourCallback::Update;
        std::size_t InstanceCount = 0;
        std::uint64_t Invocations = 0;
        std::uint64_t SkippedInvocations = 0;
        double Milliseconds = 0.0;
        double MaximumMilliseconds = 0.0;
    };

    struct ManagedCallbackMetrics
    {
        std::vector<ManagedCallbackMetric> Entries;
        bool Truncated = false;
    };

    struct ManagedRuntimeMetrics
    {
        std::uint64_t Generation = 0;
        std::size_t ActiveInstances = 0;
        std::size_t FaultedInstances = 0;
        std::size_t Diagnostics = 0;
        std::uint64_t CallbackInvocations = 0;
        std::uint64_t SkippedCallbacks = 0;
        std::uint64_t ManagedInteropCalls = 0;
        double CallbackMilliseconds = 0.0;
        double MaximumCallbackMilliseconds = 0.0;
    };

    struct ManagedBehaviourTypeDescriptor
    {
        std::string FullName;
        std::string DisplayName;
        ComponentTypeId ComponentType;
        std::int32_t ExecutionOrder = 0;
        std::vector<ComponentTypeId> RequiredComponents;
    };

    struct ManagedAssetTypeDiagnostic
    {
        std::string TypeName;
        std::string Message;
        auto operator<=>(const ManagedAssetTypeDiagnostic&) const = default;
    };

    class KEIRE_API ManagedBehaviourInstanceId final
    {
      public:
        constexpr ManagedBehaviourInstanceId() noexcept = default;
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr auto operator<=>(const ManagedBehaviourInstanceId&) const noexcept = default;

      private:
        friend class ScriptSystem;
        explicit constexpr ManagedBehaviourInstanceId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    struct ManagedRuntimeDiagnostic
    {
        ManagedBehaviourInstanceId Instance;
        ManagedDiagnosticSeverity Severity = ManagedDiagnosticSeverity::Error;
        ManagedBehaviourCallback Callback = ManagedBehaviourCallback::Update;
        std::uint64_t Generation = 0;
        std::string TypeName;
        AssetId Entity;
        std::string Message;
    };

    struct ManagedBehaviourCheckpoint
    {
        std::string TypeName;
        ComponentTypeId ComponentType;
        std::uint64_t World = 0;
        AssetId Entity;
        std::string State;
        bool Enabled = true;
        bool Faulted = false;
    };

    class KEIRE_API ScriptSystem final : public RefCounted
    {
      public:
        explicit ScriptSystem(ScriptSystemSpecification specification = {}, Ref<JobSystem> jobs = {});
        ~ScriptSystem() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ManagedBuildOperationId StartBuild(ManagedBuildRequest request);
        [[nodiscard]] ManagedIdeWorkspace GenerateIdeWorkspace(const ManagedBuildRequest& request,
                                                               std::string_view solutionName);
        void CancelBuild(ManagedBuildOperationId operation);
        [[nodiscard]] bool WaitForBuild(ManagedBuildOperationId operation, std::chrono::milliseconds timeout) const;
        [[nodiscard]] ManagedBuildStatus BuildStatus() const;
        [[nodiscard]] ManagedSdkConfiguration SdkConfiguration() const;
        void ConfigureManagedSdk(ManagedSdkSelection selection, std::filesystem::path customExecutable = {});
        [[nodiscard]] bool RuntimeHostAvailable() const noexcept;
        [[nodiscard]] bool PrepareReload(ManagedReloadRequest request);
        void CommitReload();
        void CancelReload();
        [[nodiscard]] ManagedReloadStatus ReloadStatus() const;
        [[nodiscard]] std::vector<ManagedBehaviourTypeDescriptor> BehaviourTypes() const;
        [[nodiscard]] std::vector<ManagedAssetTypeDescriptor> ManagedAssetTypes() const;
        [[nodiscard]] std::vector<ManagedAssetTypeDiagnostic> ManagedAssetTypeDiagnostics() const;
        [[nodiscard]] ManagedBehaviourInstanceId CreateBehaviour(std::string typeName, std::uint64_t world,
                                                                 AssetId entity);
        void InvokeBehaviour(ManagedBehaviourInstanceId instance, ManagedBehaviourCallback callback,
                             float deltaSeconds = 0.0F);
        [[nodiscard]] bool DestroyBehaviour(ManagedBehaviourInstanceId instance);
        [[nodiscard]] std::vector<ManagedRuntimeDiagnostic> RuntimeDiagnostics() const;
        [[nodiscard]] ManagedRuntimeMetrics Metrics() const;
        [[nodiscard]] ManagedCallbackMetrics CallbackMetrics() const;
        [[nodiscard]] bool RetryBehaviour(ManagedBehaviourInstanceId instance);
        [[nodiscard]] bool SetBehaviourEnabled(ManagedBehaviourInstanceId instance, bool enabled);
        [[nodiscard]] std::vector<ManagedBehaviourCheckpoint> CaptureReplayCheckpoint();
        void RestoreReplayCheckpoint(std::span<const ManagedBehaviourCheckpoint> checkpoint);
        void InstallManagedComponents(const Ref<ComponentRegistry>& registry);
        void SetAssetSystem(Ref<AssetSystem> assets);
        void PumpManagedAssets();
        // The caller retains ownership. The service must outlive every script callback and must be cleared before the
        // owning layer or application begins detachment.
        void SetRuntimeServices(IScriptRuntimeServices* services);
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
