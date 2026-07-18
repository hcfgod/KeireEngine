#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Event.h"
#include "Keire/Scenes/Scene.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Keire
{
    enum class SceneMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    enum class SceneLoadMode : std::uint8_t
    {
        Single,
        Additive
    };

    enum class SceneLoadState : std::uint8_t
    {
        Queued,
        Loading,
        Ready,
        Failed,
        Cancelled
    };

    struct SceneSystemSpecification
    {
        SceneMode Mode = SceneMode::Disabled;
        std::size_t MaximumLoadedScenes = 64;
    };

    struct SceneLoadedEvent
    {
        AssetId Scene;
        SceneLoadMode Mode = SceneLoadMode::Single;
    };

    struct SceneUnloadedEvent
    {
        AssetId Scene;
    };

    struct SceneLoadFailedEvent
    {
        AssetId Scene;
        AssetDiagnostic Diagnostic;
    };

    struct ActiveSceneChangedEvent
    {
        AssetId Previous;
        AssetId Current;
    };

    class KEIRE_API SceneLoadOperation final : public RefCounted
    {
      public:
        class Impl;
        ~SceneLoadOperation() override;
        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] SceneLoadMode Mode() const noexcept;
        [[nodiscard]] SceneLoadState State() const noexcept;
        [[nodiscard]] AssetDiagnostic Diagnostic() const;
        [[nodiscard]] Ref<Scene> Result() const noexcept;
        void Cancel() noexcept;

      private:
        friend class SceneSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit SceneLoadOperation(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API SceneSystem final : public RefCounted
    {
      public:
        SceneSystem(SceneSystemSpecification specification, Ref<AssetSystem> assets, Ref<EventBus> events = {});
        ~SceneSystem() override;

        SceneSystem(const SceneSystem&) = delete;
        SceneSystem& operator=(const SceneSystem&) = delete;

        [[nodiscard]] Ref<SceneLoadOperation> Load(AssetId scene, SceneLoadMode mode = SceneLoadMode::Single,
                                                   AssetPriority priority = AssetPriority::High);
        [[nodiscard]] bool Unload(AssetId scene);
        [[nodiscard]] bool SetActive(AssetId scene);
        [[nodiscard]] Ref<Scene> Active() const noexcept;
        [[nodiscard]] Ref<Scene> Find(AssetId scene) const noexcept;
        [[nodiscard]] std::vector<Ref<Scene>> LoadedScenes() const;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        friend class Application;
        class Impl;
        void AdvanceFrame();
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
