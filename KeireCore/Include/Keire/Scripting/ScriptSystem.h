#pragma once

#include "Keire/Api.h"
#include "Keire/ECS/Component.h"
#include "Keire/Ref.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include <chrono>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class ManagedLogLevel : std::uint8_t
    {
        Trace,
        Debug,
        Information,
        Warning,
        Error,
        Critical
    };

    class KEIRE_API IScriptRuntimeServices
    {
      public:
        virtual ~IScriptRuntimeServices() = default;

        virtual void WriteManagedLog(ManagedLogLevel level, std::string_view message) noexcept = 0;
        [[nodiscard]] virtual float ManagedDeltaTime() const noexcept = 0;
        [[nodiscard]] virtual Vector2 ReadManagedInput(std::string_view action) noexcept = 0;
        virtual void SetManagedCursorVisible(bool) noexcept {}
        virtual void SetManagedCursorLocked(bool) noexcept {}
        [[nodiscard]] virtual bool IsManagedCursorVisible() const noexcept { return true; }
        [[nodiscard]] virtual bool IsManagedCursorLocked() const noexcept { return false; }
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

    struct ManagedBehaviourTypeDescriptor
    {
        std::string FullName;
        std::string DisplayName;
        ComponentTypeId ComponentType;
        std::int32_t ExecutionOrder = 0;
    };

    enum class ManagedBehaviourCallback : std::uint8_t
    {
        Awake,
        Enable,
        Start,
        FixedUpdate,
        Update,
        LateUpdate,
        Disable,
        Destroy,
        BeforeReload,
        AfterReload
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

    class KEIRE_API ScriptSystem final : public RefCounted
    {
      public:
        explicit ScriptSystem(ScriptSystemSpecification specification = {});
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
        [[nodiscard]] ManagedBehaviourInstanceId CreateBehaviour(std::string typeName, std::uint64_t world,
                                                                 AssetId entity);
        void InvokeBehaviour(ManagedBehaviourInstanceId instance, ManagedBehaviourCallback callback,
                             float deltaSeconds = 0.0F);
        [[nodiscard]] bool DestroyBehaviour(ManagedBehaviourInstanceId instance);
        [[nodiscard]] std::vector<ManagedRuntimeDiagnostic> RuntimeDiagnostics() const;
        [[nodiscard]] bool RetryBehaviour(ManagedBehaviourInstanceId instance);
        [[nodiscard]] bool SetBehaviourEnabled(ManagedBehaviourInstanceId instance, bool enabled);
        void InstallManagedComponents(Ref<ComponentRegistry> registry);
        void SetRuntimeServices(IScriptRuntimeServices* services);
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
