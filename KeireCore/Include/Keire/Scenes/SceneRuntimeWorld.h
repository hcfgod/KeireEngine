#pragma once

#include "Keire/Api.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneSystem.h"
#include "Keire/StableHandle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Keire
{
    struct SceneHandleTag;
    using SceneHandle = StableHandle<SceneHandleTag>;

    enum class SceneQueryScope : std::uint8_t
    {
        Active,
        Loaded,
        Persistent,
        Specific
    };

    struct SceneRuntimeWorldSpecification
    {
        Ref<SceneSystem> Scenes;
        Ref<AssetSystem> Assets;
        Ref<AudioSystem> Audio;
        Ref<PhysicsSystem> Physics;
        AssetId DefaultMixer;
        bool DeterministicSimulation = false;
    };

    class KEIRE_API SceneRuntimeLoadOperation final : public RefCounted
    {
      public:
        class Impl;
        ~SceneRuntimeLoadOperation() override;

        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] SceneLoadMode Mode() const noexcept;
        [[nodiscard]] SceneLoadState State() const noexcept;
        [[nodiscard]] float Progress() const noexcept;
        [[nodiscard]] AssetDiagnostic Diagnostic() const;
        [[nodiscard]] SceneHandle Result() const noexcept;
        void Cancel() noexcept;

      private:
        friend class SceneRuntimeWorld;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit SceneRuntimeLoadOperation(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API SceneRuntimeWorld final : public RefCounted
    {
      public:
        using SceneValidator = bool (*)(const Ref<Scene>& scene);

        explicit SceneRuntimeWorld(SceneRuntimeWorldSpecification specification);
        ~SceneRuntimeWorld() override;

        SceneRuntimeWorld(const SceneRuntimeWorld&) = delete;
        SceneRuntimeWorld& operator=(const SceneRuntimeWorld&) = delete;

        [[nodiscard]] SceneHandle Adopt(Ref<SceneRuntimeSession> session);
        [[nodiscard]] Ref<SceneRuntimeLoadOperation> Load(AssetId scene, SceneLoadMode mode = SceneLoadMode::Single,
                                                          AssetPriority priority = AssetPriority::High);
        [[nodiscard]] bool Unload(SceneHandle scene);
        [[nodiscard]] bool SetActive(SceneHandle scene);
        [[nodiscard]] bool MakePersistent(Entity entity);

        void Process(SceneValidator validator = nullptr);
        void FixedUpdate(float deltaSeconds);
        void Update(float deltaSeconds, float interpolationAlpha = 1.0F);
        void SetDeterministicSimulation(bool enabled);
        void SetPresentationViewport(float width, float height, RuntimeUiInsets safeArea = {});

        [[nodiscard]] SceneHandle Active() const noexcept;
        [[nodiscard]] bool IsLoaded(SceneHandle scene) const noexcept;
        [[nodiscard]] bool IsPersistent(Entity entity) const noexcept;
        [[nodiscard]] AssetId Asset(SceneHandle scene) const noexcept;
        [[nodiscard]] Ref<Scene> Find(SceneHandle scene) const noexcept;
        [[nodiscard]] Ref<Scene> FindWorld(std::uint64_t world) const noexcept;
        [[nodiscard]] Ref<SceneRuntimeSession> Session(SceneHandle scene) const noexcept;
        [[nodiscard]] Ref<SceneRuntimeSession> SessionForWorld(std::uint64_t world) const noexcept;
        [[nodiscard]] Ref<SceneRuntimeSession> SessionForEntity(EntityId entity) const noexcept;
        [[nodiscard]] std::vector<SceneHandle> LoadedScenes() const;
        [[nodiscard]] std::vector<Ref<SceneRuntimeSession>> Sessions(bool includePersistent = true) const;
        [[nodiscard]] std::vector<Ref<Scene>> QueryScenes(SceneQueryScope scope, SceneHandle specific = {}) const;
        [[nodiscard]] std::vector<Entity> QueryName(std::string_view name, SceneQueryScope scope,
                                                    SceneHandle specific = {}, std::size_t maximum = 4096) const;
        [[nodiscard]] std::vector<Entity> QueryTag(std::string_view tag, SceneQueryScope scope,
                                                   SceneHandle specific = {}, std::size_t maximum = 4096) const;
        [[nodiscard]] std::vector<Entity> Query(ComponentTypeId component, SceneQueryScope scope,
                                                SceneHandle specific = {}, std::size_t maximum = 4096) const;

        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
