#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire
{
    class SceneRuntimeSession;
}

namespace Keire::Detail
{
    [[nodiscard]] std::optional<ManagedRaycastHit> QueryManagedCapsule(const Ref<SceneRuntimeSession>& runtime,
                                                                       const ManagedCapsuleCastQuery& query) noexcept;
    [[nodiscard]] std::vector<AssetId> QueryManagedSphereOverlap(const Ref<SceneRuntimeSession>& runtime,
                                                                 const ManagedSphereOverlapQuery& query);

    class ManagedRuntimePhysicsScope final
    {
      public:
        explicit ManagedRuntimePhysicsScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimePhysicsScope();

        ManagedRuntimePhysicsScope(const ManagedRuntimePhysicsScope&) = delete;
        ManagedRuntimePhysicsScope& operator=(const ManagedRuntimePhysicsScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimePhysics(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail
