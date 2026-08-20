#include "KeireInternal/Scripting/ManagedRuntimeWorld.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <limits>

namespace Keire::Detail
{
    namespace
    {
        struct NativeSceneLoadStatus
        {
            std::uint64_t SceneHigh = 0;
            std::uint64_t SceneLow = 0;
            float Progress = 0.0F;
            std::uint8_t Mode = 0;
            std::uint8_t State = 0;
        };
        static_assert(sizeof(NativeSceneLoadStatus) == 24);

        struct NativeAssetId
        {
            std::uint64_t High = 0;
            std::uint64_t Low = 0;
        };
        static_assert(sizeof(NativeAssetId) == 16);

        struct NativeRenderEnvironment
        {
            Color AmbientColor;
            float AmbientIntensity = 0.75F;
            float Exposure = 1.0F;
            std::uint64_t EnvironmentHigh = 0;
            std::uint64_t EnvironmentLow = 0;
            float EnvironmentRotationDegrees = 0.0F;
            float EnvironmentDiffuseIntensity = 1.0F;
            float EnvironmentSpecularIntensity = 1.0F;
            std::uint8_t SkyVisible = 1;
            float DirectionalShadowDistance = 100.0F;
            std::uint32_t DirectionalShadowCascadeCount = 4;
            std::uint32_t DirectionalShadowResolution = 2048;
            float DirectionalShadowSplitLambda = 0.65F;
        };
        static_assert(sizeof(NativeRenderEnvironment) == 72);

        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

        [[nodiscard]] int CopyText(const std::string_view text, std::uint8_t* destination, const int capacity) noexcept
        {
            if (capacity < 0 || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return -1;
            const auto size = static_cast<int>(text.size());
            if (!destination || capacity == 0)
                return size;
            if (capacity < size)
                return -1;
            if (size > 0)
                std::memcpy(destination, text.data(), text.size());
            return size;
        }

        [[nodiscard]] std::uint64_t BeginSceneLoad(const std::uint64_t high, const std::uint64_t low,
                                                   const std::uint8_t mode) noexcept
        {
            if (!ActiveServices || mode > static_cast<std::uint8_t>(SceneLoadMode::Additive))
                return 0;
            return ActiveServices->BeginManagedSceneLoad(AssetId(high, low), static_cast<SceneLoadMode>(mode));
        }

        [[nodiscard]] std::uint8_t GetSceneLoadStatus(const std::uint64_t operation,
                                                      NativeSceneLoadStatus* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto status = ActiveServices->ManagedSceneLoad(operation);
            if (!status)
                return 0;
            destination->SceneHigh = status->Scene.High();
            destination->SceneLow = status->Scene.Low();
            destination->Progress = std::clamp(status->Progress, 0.0F, 1.0F);
            destination->Mode = static_cast<std::uint8_t>(status->Mode);
            destination->State = static_cast<std::uint8_t>(status->State);
            return 1;
        }

        [[nodiscard]] int GetSceneLoadDiagnostic(const std::uint64_t operation, std::uint8_t* destination,
                                                 const int capacity) noexcept
        {
            if (!ActiveServices)
                return CopyText({}, destination, capacity);
            const auto status = ActiveServices->ManagedSceneLoad(operation);
            return CopyText(status ? status->Diagnostic : std::string_view{}, destination, capacity);
        }

        [[nodiscard]] std::uint8_t CancelSceneLoad(const std::uint64_t operation) noexcept
        {
            return ActiveServices && ActiveServices->CancelManagedSceneLoad(operation) ? 1 : 0;
        }

        [[nodiscard]] std::uint8_t GetActiveScene(NativeAssetId* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto scene = ActiveServices->ActiveManagedScene();
            destination->High = scene.High();
            destination->Low = scene.Low();
            return scene ? 1 : 0;
        }

        [[nodiscard]] int GetLoadedScenes(NativeAssetId* destination, const int capacity) noexcept
        {
            if (!ActiveServices || capacity < 0)
                return -1;
            try
            {
                const auto scenes = ActiveServices->LoadedManagedScenes();
                if (scenes.size() > 1024 || scenes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    return -1;
                const auto size = static_cast<int>(scenes.size());
                if (!destination || capacity == 0)
                    return size;
                if (capacity < size)
                    return -1;
                for (int index = 0; index < size; ++index)
                    destination[index] = {scenes[static_cast<std::size_t>(index)].High(),
                                          scenes[static_cast<std::size_t>(index)].Low()};
                return size;
            }
            catch (...)
            {
                return -1;
            }
        }

        [[nodiscard]] NativeRenderEnvironment ToNative(const RenderEnvironmentSettings& value) noexcept
        {
            return {.AmbientColor = value.AmbientColor,
                    .AmbientIntensity = value.AmbientIntensity,
                    .Exposure = value.Exposure,
                    .EnvironmentHigh = value.Environment.High(),
                    .EnvironmentLow = value.Environment.Low(),
                    .EnvironmentRotationDegrees = value.EnvironmentRotationDegrees,
                    .EnvironmentDiffuseIntensity = value.EnvironmentDiffuseIntensity,
                    .EnvironmentSpecularIntensity = value.EnvironmentSpecularIntensity,
                    .SkyVisible = value.SkyVisible ? std::uint8_t{1} : std::uint8_t{0},
                    .DirectionalShadowDistance = value.DirectionalShadowDistance,
                    .DirectionalShadowCascadeCount = value.DirectionalShadowCascadeCount,
                    .DirectionalShadowResolution = value.DirectionalShadowResolution,
                    .DirectionalShadowSplitLambda = value.DirectionalShadowSplitLambda};
        }

        [[nodiscard]] RenderEnvironmentSettings FromNative(const NativeRenderEnvironment& value) noexcept
        {
            return {.AmbientColor = value.AmbientColor,
                    .AmbientIntensity = value.AmbientIntensity,
                    .Exposure = value.Exposure,
                    .Environment = AssetId(value.EnvironmentHigh, value.EnvironmentLow),
                    .EnvironmentRotationDegrees = value.EnvironmentRotationDegrees,
                    .EnvironmentDiffuseIntensity = value.EnvironmentDiffuseIntensity,
                    .EnvironmentSpecularIntensity = value.EnvironmentSpecularIntensity,
                    .SkyVisible = value.SkyVisible != 0,
                    .DirectionalShadowDistance = value.DirectionalShadowDistance,
                    .DirectionalShadowCascadeCount = value.DirectionalShadowCascadeCount,
                    .DirectionalShadowResolution = value.DirectionalShadowResolution,
                    .DirectionalShadowSplitLambda = value.DirectionalShadowSplitLambda};
        }

        [[nodiscard]] std::uint8_t GetRenderEnvironment(NativeRenderEnvironment* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto value = ActiveServices->ManagedRenderEnvironment();
            if (!value)
                return 0;
            *destination = ToNative(*value);
            return 1;
        }

        [[nodiscard]] std::uint8_t SetRenderEnvironment(const NativeRenderEnvironment* value) noexcept
        {
            if (!ActiveServices || !value)
                return 0;
            return ActiveServices->SetManagedRenderEnvironment(FromNative(*value)) ? 1 : 0;
        }
    } // namespace

    ManagedRuntimeWorldScope::ManagedRuntimeWorldScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeWorldScope::~ManagedRuntimeWorldScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeWorld(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeWorld", "BeginSceneLoadIcall", reinterpret_cast<void*>(&BeginSceneLoad));
        assembly.AddInternalCall("Keire.NativeWorld", "GetSceneLoadStatusIcall",
                                 reinterpret_cast<void*>(&GetSceneLoadStatus));
        assembly.AddInternalCall("Keire.NativeWorld", "GetSceneLoadDiagnosticIcall",
                                 reinterpret_cast<void*>(&GetSceneLoadDiagnostic));
        assembly.AddInternalCall("Keire.NativeWorld", "CancelSceneLoadIcall",
                                 reinterpret_cast<void*>(&CancelSceneLoad));
        assembly.AddInternalCall("Keire.NativeWorld", "GetActiveSceneIcall", reinterpret_cast<void*>(&GetActiveScene));
        assembly.AddInternalCall("Keire.NativeWorld", "GetLoadedScenesIcall",
                                 reinterpret_cast<void*>(&GetLoadedScenes));
        assembly.AddInternalCall("Keire.NativeWorld", "GetRenderEnvironmentIcall",
                                 reinterpret_cast<void*>(&GetRenderEnvironment));
        assembly.AddInternalCall("Keire.NativeWorld", "SetRenderEnvironmentIcall",
                                 reinterpret_cast<void*>(&SetRenderEnvironment));
    }
} // namespace Keire::Detail
