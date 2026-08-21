#pragma once

#include "Keire/Scripting/ScriptSystem.h"

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    class ManagedRuntimeAssetsScope final
    {
      public:
        explicit ManagedRuntimeAssetsScope(IScriptRuntimeServices* services) noexcept;
        ~ManagedRuntimeAssetsScope();

        ManagedRuntimeAssetsScope(const ManagedRuntimeAssetsScope&) = delete;
        ManagedRuntimeAssetsScope& operator=(const ManagedRuntimeAssetsScope&) = delete;

      private:
        IScriptRuntimeServices* m_Previous = nullptr;
    };

    void RegisterManagedRuntimeAssets(Coral::ManagedAssembly& assembly);
} // namespace Keire::Detail
