#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Diagnostics/Diagnostic.h"
#include "Keire/ECS/Component.h"
#include "Keire/Project/ProjectUpgrade.h"
#include "Keire/Ref.h"
#include "Keire/Replay/ReplaySystem.h"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class Application;

    struct ModuleVersion
    {
        std::uint32_t Major = 0;
        std::uint32_t Minor = 0;
        std::uint32_t Patch = 0;

        [[nodiscard]] static ModuleVersion Parse(std::string_view value);
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] auto operator<=>(const ModuleVersion&) const noexcept = default;
    };

    struct ModuleVersionRange
    {
        std::optional<ModuleVersion> MinimumInclusive;
        std::optional<ModuleVersion> MaximumExclusive;

        [[nodiscard]] static ModuleVersionRange Parse(std::string_view value);
        [[nodiscard]] bool Contains(ModuleVersion version) const noexcept;
        [[nodiscard]] std::string ToString() const;
    };

    struct ModuleDependency
    {
        std::string Id;
        ModuleVersionRange Version;
    };

    enum class ModuleReplayState : std::uint8_t
    {
        Unspecified,
        Stateless,
        Stateful
    };

    struct ModuleDescriptor
    {
        std::string Id;
        std::string DisplayName;
        ModuleVersion Version;
        std::vector<ModuleDependency> Dependencies;
        bool SimulationAffecting = false;
        bool DeterministicReplay = true;
        // Simulation-affecting modules must choose Stateless or register module.<Id> for Stateful capture/restore.
        ModuleReplayState ReplayState = ModuleReplayState::Unspecified;
    };

    struct ModuleMemoryDomainRegistration
    {
        std::string Name;
        std::string Parent;
    };

    enum class ManagedBindingValueKind : std::uint8_t
    {
        Void,
        Boolean,
        SignedInteger,
        UnsignedInteger,
        FloatingPoint,
        Utf8String,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        StableId,
        EntityId,
        AssetId,
        ComponentTypeId,
        BoundedBuffer,
        StructuredResult
    };

    enum class ManagedBindingThreadAffinity : std::uint8_t
    {
        AnyThread,
        ManagedOwnerThread,
        MainThread
    };

    struct ManagedBindingParameterDescriptor
    {
        std::string Name;
        ManagedBindingValueKind Kind = ManagedBindingValueKind::Void;
        ManagedBindingValueKind ElementKind = ManagedBindingValueKind::Void;
        std::uint32_t MaximumElements = 0;
        auto operator<=>(const ManagedBindingParameterDescriptor&) const = default;
    };

    struct ManagedBindingMethodDescriptor
    {
        std::string StableId;
        std::uint32_t AbiVersion = 1;
        ManagedBindingThreadAffinity ThreadAffinity = ManagedBindingThreadAffinity::ManagedOwnerThread;
        std::vector<ManagedBindingParameterDescriptor> Parameters;
        ManagedBindingValueKind Result = ManagedBindingValueKind::Void;
        ManagedBindingValueKind StructuredResult = ManagedBindingValueKind::Void;
        auto operator<=>(const ManagedBindingMethodDescriptor&) const = default;
    };

    struct ManagedServiceDescriptor
    {
        std::string StableId;
        std::uint32_t AbiVersion = 1;
        std::vector<ManagedBindingMethodDescriptor> Methods;
        auto operator<=>(const ManagedServiceDescriptor&) const = default;
    };

    class KEIRE_API ModuleRegistrationContext final
    {
      public:
        ~ModuleRegistrationContext();

        ModuleRegistrationContext(const ModuleRegistrationContext&) = delete;
        ModuleRegistrationContext& operator=(const ModuleRegistrationContext&) = delete;

        void RegisterComponent(ComponentRegistration registration);
        void RegisterImporter(AssetImporterRegistration registration);
        void RegisterDecoder(AssetDecoderRegistration registration);
        void RegisterReplaySerializer(ReplaySerializerRegistration registration);
        void RegisterDiagnostic(DiagnosticDefinition definition);
        void RegisterProjectUpgrade(ProjectUpgradeStep step);
        void RegisterMemoryDomain(ModuleMemoryDomainRegistration domain);
        void RegisterManagedService(ManagedServiceDescriptor service);

      private:
        friend class ModuleRegistry;
        class Impl;
        explicit ModuleRegistrationContext(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API EngineModule : public RefCounted
    {
      public:
        ~EngineModule() override;

        [[nodiscard]] virtual ModuleDescriptor Descriptor() const = 0;
        virtual void Register(ModuleRegistrationContext& context) = 0;
        virtual void OnStart(Application&) {}
        virtual void OnStop() noexcept {}
    };

    struct ModuleRegistrySpecification
    {
        std::vector<Ref<EngineModule>> Modules;
    };

    class KEIRE_API ModuleRegistry final : public RefCounted
    {
      public:
        explicit ModuleRegistry(ModuleRegistrySpecification specification = {});
        ~ModuleRegistry() override;

        ModuleRegistry(const ModuleRegistry&) = delete;
        ModuleRegistry& operator=(const ModuleRegistry&) = delete;

        [[nodiscard]] std::vector<ModuleDescriptor> OrderedCatalog() const;
        [[nodiscard]] std::string Fingerprint() const;
        void ValidateRequired(std::span<const RequiredSourceModule> required) const;
        void ValidateCatalog(std::span<const RequiredSourceModule> catalog) const;

        [[nodiscard]] std::vector<ComponentRegistration> Components() const;
        [[nodiscard]] std::vector<AssetImporterRegistration> Importers() const;
        [[nodiscard]] std::vector<AssetDecoderRegistration> Decoders() const;
        [[nodiscard]] std::vector<ReplaySerializerRegistration> ReplaySerializers() const;
        [[nodiscard]] std::vector<DiagnosticDefinition> Diagnostics() const;
        [[nodiscard]] std::vector<ProjectUpgradeStep> ProjectUpgrades() const;
        [[nodiscard]] std::vector<ModuleMemoryDomainRegistration> MemoryDomains() const;
        [[nodiscard]] std::vector<ManagedServiceDescriptor> ManagedServices() const;

        void Start(Application& application);
        [[nodiscard]] bool IsStarted() const noexcept;
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
